#!/usr/bin/env python3
"""
Tube v2 Visualizer & Diagnostics
Unified tool for BNO055 visualization, GridEye heatmap, and Motor control.
"""

import sys
import argparse
import time
import json
from collections import deque
from threading import Thread, Lock, Event
import serial
import serial.tools.list_ports
import numpy as np

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QComboBox, QPushButton, QGroupBox, QGridLayout, QProgressBar,
    QTextEdit, QScrollArea, QSizePolicy, QSlider, QDial
)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QObject
from PyQt6.QtGui import QFont, QColor

from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure
import matplotlib.pyplot as plt

try:
    from OpenGL.GL import *
    from OpenGL.GLU import *
    from PyQt6.QtOpenGLWidgets import QOpenGLWidget
    OPENGL_AVAILABLE = True
except ImportError:
    OPENGL_AVAILABLE = False
    print("Warning: PyOpenGL not available. 3D visualization disabled.")


class SerialSignals(QObject):
    connected = pyqtSignal()
    disconnected = pyqtSignal()
    error = pyqtSignal(str)
    data_received = pyqtSignal(int)
    raw_line = pyqtSignal(str)


class SensorData:
    def __init__(self, history_size=200):
        self.lock = Lock()
        self.history_size = history_size
        
        # Data Fields
        self.timestamp = 0
        self.vr = 0
        self.faults = {'y': 0, 'p': 0}
        self.euler = {'h': 0.0, 'r': 0.0, 'p': 0.0}
        self.quat = {'w': 1.0, 'x': 0.0, 'y': 0.0, 'z': 0.0}
        self.cal = {'sys': 0, 'gyr': 0, 'acc': 0, 'mag': 0}
        self.grideye = [0.0] * 64
        
        # History
        self.time_history = deque(maxlen=history_size)
        self.euler_history = {k: deque(maxlen=history_size) for k in ['h','r','p']}
        
        self.start_time = None
        self.packet_count = 0

    def update(self, data):
        with self.lock:
            if self.start_time is None: self.start_time = data.get('t', 0)
            
            # Basic fields
            self.timestamp = data.get('t', 0)
            if 'vr' in data: self.vr = data['vr']
            if 'faults' in data: self.faults = data['faults']
            if 'euler' in data: self.euler = data['euler']
            if 'quat' in data: self.quat = data['quat']
            if 'cal' in data: self.cal = data['cal']
            if 'grideye' in data: self.grideye = data['grideye']
            
            # History
            rel_t = (self.timestamp - self.start_time) / 1000.0
            self.time_history.append(rel_t)
            for k in ['h','r','p']:
                self.euler_history[k].append(self.euler.get(k, 0))
                
            self.packet_count += 1

    def get_current(self):
        with self.lock:
            return {
                'vr': self.vr,
                'faults': self.faults.copy(),
                'euler': self.euler.copy(),
                'quat': self.quat.copy(),
                'cal': self.cal.copy(),
                'grideye': list(self.grideye),
                'cnt': self.packet_count
            }
    
    def get_history(self):
        with self.lock:
            return {
                'time': list(self.time_history),
                'euler': {k: list(v) for k, v in self.euler_history.items()}
            }
            
    def reset(self):
        with self.lock:
            self.start_time = None
            self.packet_count = 0
            self.time_history.clear()
            for q in self.euler_history.values(): q.clear()


class SerialReader(Thread):
    def __init__(self, port, baud, sensor_data, signals):
        super().__init__(daemon=True)
        self.port = port
        self.baud = baud
        self.sensor_data = sensor_data
        self.signals = signals
        self.stop_event = Event()
        self.serial = None
        self.connected = False

    def connect(self):
        try:
            if self.serial and self.serial.is_open: self.serial.close()
            self.serial = serial.Serial(
                port=self.port, baudrate=self.baud, timeout=0.1, write_timeout=1.0
            )
            # ESP32 Reset
            self.serial.dtr = False; self.serial.rts = False; time.sleep(0.1)
            self.serial.dtr = True; self.serial.rts = True; time.sleep(2.0)
            
            self.serial.reset_input_buffer()
            self.serial.reset_output_buffer()
            time.sleep(0.1)
            
            # Start stream
            self.serial.write(b'j')
            
            self.connected = True
            self.signals.connected.emit()
            return True
        except Exception as e:
            self.signals.error.emit(str(e))
            return False

    def disconnect(self):
        self.stop_event.set()
        if self.serial: self.serial.close()
        self.connected = False
        self.signals.disconnected.emit()

    def send_command(self, cmd):
        if self.serial and self.connected:
            try:
                self.serial.write(cmd.encode())
                return True
            except: pass
        return False

    def run(self):
        buffer = ""
        while not self.stop_event.is_set():
            if not self.serial or not self.connected: 
                time.sleep(0.1)
                continue
            try:
                if self.serial.in_waiting:
                    chunk = self.serial.read(self.serial.in_waiting)
                    buffer += chunk.decode(errors='ignore')
                    # Split json objects
                    buffer = buffer.replace('}{', '}\n{')
                    
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if not line: continue
                        
                        if line.startswith('{') and line.endswith('}'):
                            try:
                                data = json.loads(line)
                                self.sensor_data.update(data)
                                self.signals.data_received.emit(self.sensor_data.packet_count)
                            except:
                                self.signals.raw_line.emit(line)
                        else:
                            self.signals.raw_line.emit(line)
                else:
                    time.sleep(0.01)
            except Exception as e:
                self.signals.error.emit(str(e))
                self.connected = False
                break


# --- Widgets ---

if OPENGL_AVAILABLE:
    class OrientationWidget(QOpenGLWidget):
        def __init__(self, sensor_data, parent=None):
            super().__init__(parent)
            self.sensor_data = sensor_data
            self.setMinimumSize(300, 300)

        def initializeGL(self):
            glClearColor(0.1, 0.1, 0.15, 1.0)
            glEnable(GL_DEPTH_TEST)
            glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnable(GL_COLOR_MATERIAL)
            glLightfv(GL_LIGHT0, GL_POSITION, [5.0, 5.0, 5.0, 1.0])

        def resizeGL(self, w, h):
            glViewport(0, 0, w, h)
            glMatrixMode(GL_PROJECTION); glLoadIdentity()
            gluPerspective(45.0, w/h if h else 1, 0.1, 100.0)
            glMatrixMode(GL_MODELVIEW)

        def paintGL(self):
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); glLoadIdentity()
            gluLookAt(0, 0, 5, 0, 0, 0, 0, 1, 0)
            
            q = self.sensor_data.get_current()['quat']
            # BNO ENU to OpenGL
            # X=East, Y=North, Z=Up
            # GL: X=Right, Y=Up, Z=Out
            # Swap Y/Z, invert new Z
            qw, qx, qy, qz = q['w'], q['x'], q['z'], -q['y']
            
            # Rotate
            mat = [
                1-2*qy*qy-2*qz*qz, 2*qx*qy-2*qz*qw,   2*qx*qz+2*qy*qw,   0,
                2*qx*qy+2*qz*qw,   1-2*qx*qx-2*qz*qz, 2*qy*qz-2*qx*qw,   0,
                2*qx*qz-2*qy*qw,   2*qy*qz+2*qx*qw,   1-2*qx*qx-2*qy*qy, 0,
                0,                 0,                 0,                 1
            ]
            glMultMatrixf(mat)
            self.draw_board()

        def draw_board(self):
            glColor3f(0.2, 0.6, 0.8)
            glScalef(1.5, 0.2, 1.0)
            self.draw_cube()
            
        def draw_cube(self):
            glBegin(GL_QUADS)
            # Front/Back/Left/Right/Top/Bottom... simplified
            glNormal3f(0,0,1); glVertex3f(-0.5,-0.5,0.5); glVertex3f(0.5,-0.5,0.5); glVertex3f(0.5,0.5,0.5); glVertex3f(-0.5,0.5,0.5)
            glNormal3f(0,0,-1); glVertex3f(-0.5,-0.5,-0.5); glVertex3f(-0.5,0.5,-0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(0.5,-0.5,-0.5)
            glNormal3f(0,1,0); glVertex3f(-0.5,0.5,-0.5); glVertex3f(-0.5,0.5,0.5); glVertex3f(0.5,0.5,0.5); glVertex3f(0.5,0.5,-0.5)
            glNormal3f(0,-1,0); glVertex3f(-0.5,-0.5,-0.5); glVertex3f(0.5,-0.5,-0.5); glVertex3f(0.5,-0.5,0.5); glVertex3f(-0.5,-0.5,0.5)
            glNormal3f(1,0,0); glVertex3f(0.5,-0.5,-0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(0.5,0.5,0.5); glVertex3f(0.5,-0.5,0.5)
            glNormal3f(-1,0,0); glVertex3f(-0.5,-0.5,-0.5); glVertex3f(-0.5,-0.5,0.5); glVertex3f(-0.5,0.5,0.5); glVertex3f(-0.5,0.5,-0.5)
            glEnd()


class HeatmapWidget(FigureCanvas):
    def __init__(self, title):
        self.fig = Figure(figsize=(4, 4), dpi=100)
        self.fig.patch.set_facecolor('#1a1a2e')
        super().__init__(self.fig)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_title(title, color='white')
        self.data = np.zeros((8, 8))
        self.im = self.ax.imshow(self.data, cmap='inferno', vmin=15, vmax=35)
        self.fig.colorbar(self.im, ax=self.ax)
        self.ax.tick_params(colors='white')
        self.fig.subplots_adjust(left=0.05, right=0.9, top=0.9, bottom=0.05)
        
    def update_heatmap(self, data):
        if not data or len(data)!=64: return
        mat = np.array(data).reshape(8, 8)
        self.im.set_data(mat)
        self.draw_idle()

class PlotWidget(FigureCanvas):
    def __init__(self, title, ylabel):
        self.fig = Figure(figsize=(5, 2), dpi=100)
        self.fig.patch.set_facecolor('#1a1a2e')
        super().__init__(self.fig)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_facecolor('#1a1a2e')
        self.ax.set_title(title, color='white', fontsize=10)
        self.ax.set_ylabel(ylabel, color='white', fontsize=8)
        self.ax.tick_params(colors='white', labelsize=8)
        for s in self.ax.spines.values(): s.set_color('#444')
        self.fig.subplots_adjust(bottom=0.2)

    def update_plot(self, t, data, labels):
        if not t: return
        self.ax.clear()
        self.ax.set_facecolor('#1a1a2e')
        self.ax.grid(color='#333')
        colors = ['#ff6b6b', '#4ecdc4', '#45b7d1']
        for i, (k, lbl) in enumerate(labels.items()):
            if k in data:
                self.ax.plot(t, list(data[k]), color=colors[i%3], label=lbl)
        self.ax.legend(facecolor='#222', labelcolor='white')
        self.draw_idle()


class MainWindow(QMainWindow):
    def __init__(self, port=None):
        super().__init__()
        self.setWindowTitle("Tube v2 Visualizer")
        self.setStyleSheet("background-color: #1a1a2e; color: white; QGroupBox { border: 1px solid #444; margin-top: 10px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; }")
        self.resize(1400, 900)
        
        self.sensor_data = SensorData()
        self.serial_reader = None
        self.serial_signals = SerialSignals()
        self.serial_signals.connected.connect(self.on_conn)
        self.serial_signals.disconnected.connect(self.on_disconn)
        self.serial_signals.raw_line.connect(self.on_raw)
        
        self.init_ui()
        
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_ui)
        self.timer.start(50) # 20Hz update
        
        if OPENGL_AVAILABLE:
            self.timer3d = QTimer()
            self.timer3d.timeout.connect(self.orientation.update)
            self.timer3d.start(100)

    def init_ui(self):
        root = QWidget()
        self.setCentralWidget(root)
        layout = QHBoxLayout(root)
        
        # --- Left Column: Controls & 3D ---
        left = QWidget()
        left_layout = QVBoxLayout(left)
        
        # Connection
        c_grp = QGroupBox("Connection")
        c_lay = QHBoxLayout(c_grp)
        self.ports = QComboBox()
        self.refresh_ports()
        c_lay.addWidget(self.ports)
        self.btn_refresh = QPushButton("R")
        self.btn_refresh.clicked.connect(self.refresh_ports)
        c_lay.addWidget(self.btn_refresh)
        self.btn_conn = QPushButton("Connect")
        self.btn_conn.clicked.connect(self.toggle_conn)
        c_lay.addWidget(self.btn_conn)
        left_layout.addWidget(c_grp)
        
        # Motor Control
        m_grp = QGroupBox("Motor Control")
        m_lay = QGridLayout(m_grp)
        
        m_lay.addWidget(QLabel("Pitch:"), 0, 0)
        self.sld_pitch = QSlider(Qt.Orientation.Horizontal)
        self.sld_pitch.setRange(90, 210)
        self.sld_pitch.setValue(150)
        self.sld_pitch.valueChanged.connect(self.on_pitch)
        m_lay.addWidget(self.sld_pitch, 0, 1)
        self.lbl_pitch = QLabel("0")
        m_lay.addWidget(self.lbl_pitch, 0, 2)
        
        m_lay.addWidget(QLabel("Yaw:"), 1, 0)
        self.sld_yaw = QSlider(Qt.Orientation.Horizontal)
        self.sld_yaw.setRange(-100, 100) # -20 to 20 rad/s
        self.sld_yaw.valueChanged.connect(self.on_yaw)
        m_lay.addWidget(self.sld_yaw, 1, 1)
        self.lbl_yaw = QLabel("0")
        m_lay.addWidget(self.lbl_yaw, 1, 2)
        
        btn_stop = QPushButton("STOP [0]")
        btn_stop.setStyleSheet("background-color: #d63031; color: white; font-weight: bold;")
        btn_stop.clicked.connect(lambda: self.send_raw('0'))
        m_lay.addWidget(btn_stop, 2, 0, 1, 3)
        
        btn_cal = QPushButton("Calibrate Pitch (Horizon)")
        btn_cal.setStyleSheet("background-color: #0984e3; color: white; font-weight: bold;")
        btn_cal.clicked.connect(lambda: self.send_raw('C'))
        m_lay.addWidget(btn_cal, 3, 0, 1, 3)
        
        left_layout.addWidget(m_grp)
        
        # 3D
        if OPENGL_AVAILABLE:
            self.orientation = OrientationWidget(self.sensor_data)
            left_layout.addWidget(self.orientation, stretch=2)
            
        # Log
        self.log = QTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumHeight(150)
        left_layout.addWidget(self.log)
        
        layout.addWidget(left, stretch=1)
        
        # --- Right Column: Data ---
        right = QWidget()
        right_layout = QVBoxLayout(right)
        
        # Indicators
        ind_lay = QHBoxLayout()
        # VR
        v_lay = QVBoxLayout()
        v_lay.addWidget(QLabel("VR Input"))
        self.bar_vr = QProgressBar()
        self.bar_vr.setRange(0, 4095)
        v_lay.addWidget(self.bar_vr)
        ind_lay.addLayout(v_lay)
        # Faults
        self.flt_y = QLabel("FLT Y")
        self.flt_y.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.flt_p = QLabel("FLT P")
        self.flt_p.setAlignment(Qt.AlignmentFlag.AlignCenter)
        ind_lay.addWidget(self.flt_y)
        ind_lay.addWidget(self.flt_p)
        right_layout.addLayout(ind_lay)
        
        # Status Text
        self.lbl_euler = QLabel("H:0.0 P:0.0 R:0.0")
        self.lbl_euler.setFont(QFont("Menlo", 14))
        right_layout.addWidget(self.lbl_euler)
        
        self.lbl_cal = QLabel("Cal: Sys:0 Gyr:0 Acc:0 Mag:0")
        right_layout.addWidget(self.lbl_cal)
        
        # Visuals
        vis_lay = QHBoxLayout()
        self.heatmap = HeatmapWidget("GridEye")
        vis_lay.addWidget(self.heatmap)
        right_layout.addLayout(vis_lay, stretch=2)
        
        # Charts
        self.plot_euler = PlotWidget("Euler Angles", "Deg")
        right_layout.addWidget(self.plot_euler, stretch=1)
        
        layout.addWidget(right, stretch=2)

    def refresh_ports(self):
        self.ports.clear()
        for p in serial.tools.list_ports.comports():
            self.ports.addItem(p.device)

    def toggle_conn(self):
        if self.serial_reader and self.serial_reader.connected:
            self.serial_reader.disconnect()
        else:
            p = self.ports.currentText()
            if not p: return
            self.serial_reader = SerialReader(p, 115200, self.sensor_data, self.serial_signals)
            self.serial_reader.start()
            if self.serial_reader.connect():
                self.btn_conn.setText("Disconnect")

    def on_conn(self):
        self.btn_conn.setText("Disconnect")
        self.btn_conn.setStyleSheet("background-color: #00b894; color: black;")
        
    def on_disconn(self):
        self.btn_conn.setText("Connect")
        self.btn_conn.setStyleSheet("")

    def send_raw(self, cmd):
        if self.serial_reader: self.serial_reader.send_command(cmd)

    def on_pitch(self, val):
        self.lbl_pitch.setText(str(val))
        if self.serial_reader:
            self.serial_reader.send_command(f"P{val},0\n") # Send as P<pitch>,<yaw> for Unit 2 parsing

    def on_yaw(self, val):
        # Slider -100 to 100 -> -20.0 to 20.0 rad/s
        rads = val / 5.0
        self.lbl_yaw.setText(f"{rads:.1f}")
        if self.serial_reader:
            self.serial_reader.send_command(f"Y{rads:.1f}\n")

    def on_raw(self, line):
        self.log.append(f"RX: {line}")
        sb = self.log.verticalScrollBar()
        sb.setValue(sb.maximum())

    def update_ui(self):
        curr = self.sensor_data.get_current()
        hist = self.sensor_data.get_history()
        
        # VR
        self.bar_vr.setValue(curr['vr'])
        
        # Faults
        style_ok = "background-color: #333; color: gray; border: 1px solid #555; padding: 10px;"
        style_bad = "background-color: #ff7675; color: white; border: 2px solid red; padding: 10px;"
        self.flt_y.setStyleSheet(style_bad if curr['faults']['y'] else style_ok)
        self.flt_p.setStyleSheet(style_bad if curr['faults']['p'] else style_ok)
        
        # Text
        e = curr['euler']
        self.lbl_euler.setText(f"H:{e['h']:.1f}  P:{e['p']:.1f}  R:{e['r']:.1f}")
        c = curr['cal']
        self.lbl_cal.setText(f"Cal: Sys:{c['sys']} Gyr:{c['gyr']} Acc:{c['acc']} Mag:{c['mag']}")
        
        # Vis
        self.heatmap.update_heatmap(curr['grideye'])
        self.plot_euler.update_plot(hist['time'], hist['euler'], {'h':'Head', 'p':'Pitch', 'r':'Roll'})


if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())
