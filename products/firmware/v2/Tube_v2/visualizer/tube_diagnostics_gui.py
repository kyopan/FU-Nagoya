#!/usr/bin/env python3
"""
Tube v2 Diagnostics GUI
For debugging Unit 1 (Sensor Hub) + Unit 2 (Motor Control)
based on bno055_visualizer.py
"""

import sys
import argparse
import time
import re
from collections import deque
from threading import Thread, Lock, Event
import serial
import serial.tools.list_ports
import numpy as np

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QComboBox, QPushButton, QGroupBox, QGridLayout, QProgressBar,
    QTextEdit, QScrollArea, QSizePolicy, QSlider
)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QObject
from PyQt6.QtGui import QFont, QColor

from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure
import matplotlib.pyplot as plt

# 3D is optional
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
        self.start_time = time.time()
        
        # State
        self.packet_count = 0
        self.vr = 0
        self.fault_y = False
        self.fault_p = False
        self.euler = {'h': 0.0, 'r': 0.0, 'p': 0.0}
        self.euler = {'h': 0.0, 'r': 0.0, 'p': 0.0}
        self.grideye = np.zeros((8, 8))
        self.centroid = (3.5, 3.5) # Default Center

        # History
        self.time_history = deque(maxlen=history_size)
        self.vr_history = deque(maxlen=history_size)
        self.euler_history = {'h': deque(maxlen=history_size),
                              'r': deque(maxlen=history_size),
                              'p': deque(maxlen=history_size)}

    def update_sensor_line(self, line):
        # Format: "VR:1234 | FLT_Y:0 FLT_P:1 | H:120.5 P:10.2 R:-0.5"
        try:
            with self.lock:
                # Regex parsing
                # VR
                m_vr = re.search(r'VR:(\d+)', line)
                if m_vr: self.vr = int(m_vr.group(1))

                # Faults
                m_fy = re.search(r'FLT_Y:(\d)', line)
                if m_fy: self.fault_y = (m_fy.group(1) == '1')
                
                m_fp = re.search(r'FLT_P:(\d)', line)
                if m_fp: self.fault_p = (m_fp.group(1) == '1')

                # Euler
                m_h = re.search(r'H:([-\d.]+)', line)
                if m_h: self.euler['h'] = float(m_h.group(1))
                
                m_p = re.search(r'P:([-\d.]+)', line)
                if m_p: self.euler['p'] = float(m_p.group(1))
                
                m_r = re.search(r'R:([-\d.]+)', line)
                if m_r: self.euler['r'] = float(m_r.group(1))

                # History
                t = time.time() - self.start_time
                self.time_history.append(t)
                self.vr_history.append(self.vr)
                self.euler_history['h'].append(self.euler['h'])
                self.euler_history['r'].append(self.euler['r'])
                self.euler_history['p'].append(self.euler['p'])

                self.packet_count += 1
                return True
        except Exception as e:
            print(f"Parse Error: {e}")
            return False

    def update_grideye(self, line):
        # Format: "Heatmap: 24.5 25.0 ..."
        try:
            parts = line.split(":", 1)
            if len(parts) > 1:
                nums = [float(x) for x in parts[1].split()]
                if len(nums) == 64:
                    with self.lock:
                        self.grideye = np.array(nums).reshape(8, 8)
                        # print(f"GridEye Updated: min={self.grideye.min():.1f} max={self.grideye.max():.1f}")
        except Exception:
            pass

    def get_current(self):
        with self.lock:
            return {
                'vr': self.vr,
                'fault_y': self.fault_y,
                'fault_p': self.fault_p,
                'euler': self.euler.copy(),
                'euler': self.euler.copy(),
                'grideye': self.grideye.copy(),
                'centroid': self.centroid
            }
    
    def get_history(self):
        with self.lock:
            return {
                'time': list(self.time_history),
                'vr': list(self.vr_history),
                'euler': {k: list(v) for k, v in self.euler_history.items()}
            }

    def reset(self):
        with self.lock:
            self.packet_count = 0
            self.time_history.clear()
            self.vr_history.clear()
            for q in self.euler_history.values(): q.clear()
            self.start_time = time.time()


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
        self.grideye_buffer = []
        self.reading_grideye = False

    def connect(self):
        try:
            if self.serial and self.serial.is_open: self.serial.close()
            self.serial = serial.Serial(self.port, self.baud, timeout=0.1)
            # ESP32 Reset trick
            self.serial.dtr = False
            self.serial.rts = False
            time.sleep(0.5)
            self.serial.reset_input_buffer()
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
        if self.connected and self.serial:
            try:
                # If command is single char, just send it
                # If longer, maybe ends with \n? But main.cpp uses Serial.read() char by char mostly
                # But for safety, send string
                self.serial.write(cmd.encode())
                return True
            except:
                pass
        return False

    def run(self):
        buffer = ""
        while not self.stop_event.is_set():
            if not self.serial or not self.connected: 
                time.sleep(0.1)
                continue
            try:
                if self.serial.in_waiting:
                    line = self.serial.readline().decode(errors='ignore').strip()
                    if not line: continue
                    
                    self.signals.raw_line.emit(line)

                    # Sensor Line
                    if line.startswith("VR:"):
                        if self.sensor_data.update_sensor_line(line):
                            self.signals.data_received.emit(self.sensor_data.packet_count)
                    
                    elif line.startswith("Centroid:"):
                        try:
                            parts = line.split()
                            cx = float(parts[1])
                            cy = float(parts[2])
                            with self.sensor_data.lock:
                                self.sensor_data.centroid = (cx, cy)
                        except:
                            pass

                    # GridEye logic (Consolidated Line)
                    elif line.startswith("Heatmap:"):
                        self.sensor_data.update_grideye(line)
                        
                    elif "Heatmap" in line and not line.startswith("Heatmap:"):
                        pass # Ignore old format

            except Exception as e:
                self.signals.error.emit(str(e))
                self.connected = False
                break


class PlotWidget(FigureCanvas):
    def __init__(self, title, ylabel, parent=None):
        self.fig = Figure(figsize=(5, 2), dpi=100)
        self.fig.patch.set_facecolor('#1a1a2e')
        super().__init__(self.fig)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_facecolor('#1a1a2e')
        self.ax.set_title(title, color='white', fontsize=10)
        self.ax.set_ylabel(ylabel, color='white', fontsize=8)
        self.ax.tick_params(colors='white', labelsize=8)
        self.fig.subplots_adjust(left=0.15, right=0.95, top=0.85, bottom=0.2)
        for spine in self.ax.spines.values(): spine.set_color('#444')

    def update_plot(self, time_data, data_dict, labels=None):
        if not time_data: return
        self.ax.clear()
        self.ax.set_facecolor('#1a1a2e')
        colors = ['#ff6b6b', '#4ecdc4', '#45b7d1', '#feca57']
        
        # Handle dict or single list
        if isinstance(data_dict, dict):
            for i, (key, val) in enumerate(data_dict.items()):
                label = labels.get(key, key) if labels else key
                # Ensure length matches
                min_len = min(len(time_data), len(val))
                self.ax.plot(list(time_data)[-min_len:], list(val)[-min_len:], 
                             color=colors[i%4], label=label)
        else:
            # Single list
            min_len = min(len(time_data), len(data_dict))
            self.ax.plot(list(time_data)[-min_len:], list(data_dict)[-min_len:], 
                         color=colors[0], label=labels)

        self.ax.legend(loc='upper right', fontsize=7, facecolor='#2a2a4e', labelcolor='white')
        self.ax.tick_params(colors='white')
        for spine in self.ax.spines.values(): spine.set_color('#444')
        self.draw_idle()


class HeatmapWidget(FigureCanvas):
    def __init__(self, title):
        self.fig = Figure(figsize=(4, 4), dpi=100)
        self.fig.patch.set_facecolor('#1a1a2e')
        super().__init__(self.fig)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_title(title, color='white')
        self.data = np.zeros((8, 8))
        self.im = self.ax.imshow(self.data, cmap='inferno', vmin=15, vmax=35)
        self.marker, = self.ax.plot([], [], 'rx', markersize=10, markeredgewidth=2) # Centroid Marker
        self.fig.colorbar(self.im, ax=self.ax)
        self.ax.tick_params(colors='white')
            
    def update_heatmap(self, data, centroid=None):
        if data.shape != (8, 8):
            return # Invalid shape
        self.im.set_data(data)
        # Dynamic range or fixed? Fixed for now 20-40C usually good for body
        self.im.set_clim(vmin=15, vmax=35)
        if centroid:
            # GridEye coords: x (0-7), y (0-7).
            # imshow display: x=col, y=row. Usually centroid is (x, y).
            self.marker.set_data([centroid[0]], [centroid[1]])
        else:
            self.marker.set_data([], [])
        self.draw_idle()


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Tube v2 Diagnostics")
        self.setStyleSheet("background-color: #1a1a2e; color: white;")
        self.resize(1200, 800)

        self.sensor_data = SensorData()
        self.serial_reader = None
        self.signals = SerialSignals()
        self.signals.connected.connect(self.on_connected)
        self.signals.disconnected.connect(self.on_disconnected)
        self.signals.raw_line.connect(self.on_log)
        self.signals.error.connect(lambda e: self.log(f"ERR: {e}"))

        self.init_ui()
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_ui)
        self.timer.start(100)

    def init_ui(self):
        main_wid = QWidget()
        self.setCentralWidget(main_wid)
        layout = QHBoxLayout(main_wid)

        # Left Column: Controls
        left_layout = QVBoxLayout()
        # Connection
        conn_grp = QGroupBox("Connection")
        conn_layout = QHBoxLayout(conn_grp)
        self.port_combo = QComboBox()
        self.refresh_ports()
        conn_layout.addWidget(self.port_combo)
        self.refresh_btn = QPushButton("R")
        self.refresh_btn.setFixedWidth(30)
        self.refresh_btn.clicked.connect(self.refresh_ports)
        conn_layout.addWidget(self.refresh_btn)
        self.conn_btn = QPushButton("Connect")
        self.conn_btn.clicked.connect(self.toggle_connect)
        conn_layout.addWidget(self.conn_btn)
        left_layout.addWidget(conn_grp)

        # Basic Commands
        sys_grp = QGroupBox("System")
        sys_layout = QHBoxLayout(sys_grp)
        self.btn_sensor = QPushButton("Toggle Sensor [s]")
        self.btn_sensor.clicked.connect(lambda: self.send('s'))
        sys_layout.addWidget(self.btn_sensor)
        self.btn_grid = QPushButton("Toggle Heatmap [g]")
        self.btn_grid.clicked.connect(lambda: self.send('g'))
        sys_layout.addWidget(self.btn_grid)
        left_layout.addWidget(sys_grp)

        # Motor Controls
        motor_grp = QGroupBox("Motor Test")
        motor_layout = QGridLayout(motor_grp)
        
        self.btn_stop = QPushButton("EMERGENCY STOP [0]")
        self.btn_stop.setStyleSheet("background-color: red; color: white; font-weight: bold; height: 40px;")
        self.btn_stop.clicked.connect(lambda: self.send('0'))
        motor_layout.addWidget(self.btn_stop, 0, 0, 1, 2)

        motor_layout.addWidget(QLabel("Pitch:"), 1, 0)
        btn_p_up = QPushButton("UP (+30) [P]")
        btn_p_up.clicked.connect(lambda: self.send('P'))
        motor_layout.addWidget(btn_p_up, 1, 1)
        btn_p_dn = QPushButton("DOWN (-30) [p]")
        btn_p_dn.clicked.connect(lambda: self.send('p'))
        motor_layout.addWidget(btn_p_dn, 2, 1)

        motor_layout.addWidget(QLabel("Yaw:"), 3, 0)
        btn_y_cw = QPushButton("CW (+10 Vel) [Y]")
        btn_y_cw.clicked.connect(lambda: self.send('Y'))
        motor_layout.addWidget(btn_y_cw, 3, 1)
        btn_y_ccw = QPushButton("CCW (-10 Vel) [y]")
        btn_y_ccw.clicked.connect(lambda: self.send('y'))
        motor_layout.addWidget(btn_y_ccw, 4, 1)
        
        left_layout.addWidget(motor_grp)

        # Log
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        left_layout.addWidget(self.log_text)
        
        layout.addLayout(left_layout, stretch=1)

        # Right Column: Visuals
        right_layout = QVBoxLayout()
        
        # Top: Indicators
        ind_layout = QHBoxLayout()
        
        # VR Meter
        vr_layout = QVBoxLayout()
        vr_layout.addWidget(QLabel("VR (Pitch Input)"))
        self.vr_bar = QProgressBar()
        self.vr_bar.setRange(0, 4095)
        vr_layout.addWidget(self.vr_bar)
        ind_layout.addLayout(vr_layout)
        
        # Faults
        self.lbl_fy = QLabel("FAULT Y")
        self.lbl_fy.setStyleSheet("border: 1px solid gray; padding: 10px; color: gray;")
        self.lbl_fy.setAlignment(Qt.AlignmentFlag.AlignCenter)
        ind_layout.addWidget(self.lbl_fy)
        
        self.lbl_fp = QLabel("FAULT P")
        self.lbl_fp.setStyleSheet("border: 1px solid gray; padding: 10px; color: gray;")
        self.lbl_fp.setAlignment(Qt.AlignmentFlag.AlignCenter)
        ind_layout.addWidget(self.lbl_fp)
        
        right_layout.addLayout(ind_layout)

        # Heatmap
        self.heatmap = HeatmapWidget("GridEye")
        right_layout.addWidget(self.heatmap, stretch=2)

        # Graphs
        self.euler_plot = PlotWidget("Euler Angles (BNO055)", "Deg")
        right_layout.addWidget(self.euler_plot, stretch=1)
        
        layout.addLayout(right_layout, stretch=2)

    def refresh_ports(self):
        self.port_combo.clear()
        ports = sorted([p.device for p in serial.tools.list_ports.comports()])
        # Sort priority: usbmodem > usbserial > manual
        ports.sort(key=lambda p: (not ("usbmodem" in p), not ("usbserial" in p), p))
        
        self.port_combo.addItems(ports)
        if ports:
            self.port_combo.setCurrentIndex(0)

    def toggle_connect(self):
        if self.serial_reader and self.serial_reader.connected:
            self.serial_reader.disconnect()
        else:
            p = self.port_combo.currentText()
            if not p: return
            self.serial_reader = SerialReader(p, 115200, self.sensor_data, self.signals)
            self.serial_reader.start()
            if self.serial_reader.connect():
                self.conn_btn.setText("Disconnect")
                self.log("Connected.")

    def on_connected(self):
        self.conn_btn.setText("Disconnect")
        self.conn_btn.setStyleSheet("background-color: #4ecdc4; color: black;")
        # Auto-Enable GridEye Stream
        QTimer.singleShot(200, lambda: self.send('g'))

    def on_disconnected(self):
        self.conn_btn.setText("Connect")
        self.conn_btn.setStyleSheet("")
        self.log("Disconnected.")

    def send(self, cmd):
        if self.serial_reader:
            self.serial_reader.send_command(cmd + '\n')
            self.log(f">> TX: {cmd}")

    def log(self, msg):
        if not msg.startswith("VR:"): # Filter sensor data
            self.log_text.append(msg)
            # Auto scroll
            sb = self.log_text.verticalScrollBar()
            sb.setValue(sb.maximum())
    
    def on_log(self, line):
        # Filtering happens inside serial reader logic usually, but here we just log everything NOT sensor data
        if not line.startswith("VR:") and not line.startswith("Heatmap") and not re.match(r'^[\d\s.]+$', line):
            self.log(f"RX: {line}")

    def update_ui(self):
        data = self.sensor_data.get_current()
        hist = self.sensor_data.get_history()
        
        # VR
        self.vr_bar.setValue(data['vr'])
        
        # Faults
        if data['fault_y']: self.lbl_fy.setStyleSheet("background-color: red; color: white; padding: 10px;")
        else: self.lbl_fy.setStyleSheet("border: 1px solid gray; padding: 10px; color: gray;")
            
        if data['fault_p']: self.lbl_fp.setStyleSheet("background-color: red; color: white; padding: 10px;")
        else: self.lbl_fp.setStyleSheet("border: 1px solid gray; padding: 10px; color: gray;")

        # Heatmap
        # Heatmap
        self.heatmap.update_heatmap(data['grideye'], data['centroid'])

        # Plot
        self.euler_plot.update_plot(hist['time'], hist['euler'], {'h':'Heading', 'p':'Pitch', 'r':'Roll'})


if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())
