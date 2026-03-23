#!/usr/bin/env python3
"""
BNO055 9-Axis IMU Visualizer

Real-time 3D orientation visualization and sensor data plotting
for the GY-BNO055 sensor module.

Features:
- 3D orientation display using quaternions
- Real-time graphs for accelerometer, gyroscope, magnetometer
- Euler angle display
- Calibration status monitoring

Usage:
    python bno055_visualizer.py [--port PORT] [--baud BAUD]

Requirements:
    pip install pyserial numpy matplotlib pyqt6 pyopengl
"""

import sys
import json
import argparse
import time
from collections import deque
from threading import Thread, Lock, Event
import serial
import serial.tools.list_ports
import numpy as np

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QComboBox, QPushButton, QGroupBox, QGridLayout, QProgressBar,
    QTextEdit, QSplitter, QSizePolicy, QSlider, QScrollArea
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
    """Signals for serial communication events"""
    connected = pyqtSignal()
    disconnected = pyqtSignal()
    error = pyqtSignal(str)
    data_received = pyqtSignal(int)  # packet count
    raw_line = pyqtSignal(str)


class SensorData:
    """Thread-safe container for sensor data"""

    def __init__(self, history_size=500):
        self.lock = Lock()
        self.history_size = history_size

        # Current values
        self.timestamp = 0
        self.euler = {'h': 0.0, 'r': 0.0, 'p': 0.0}
        self.quat = {'w': 1.0, 'x': 0.0, 'y': 0.0, 'z': 0.0}
        self.accel = {'x': 0.0, 'y': 0.0, 'z': 0.0}
        self.gyro = {'x': 0.0, 'y': 0.0, 'z': 0.0}
        self.mag = {'x': 0.0, 'y': 0.0, 'z': 0.0}
        self.mag = {'x': 0.0, 'y': 0.0, 'z': 0.0}
        self.calibration = {'sys': 0, 'gyr': 0, 'acc': 0, 'mag': 0}
        self.grideye = [0.0] * 64

        # History for plotting
        self.time_history = deque(maxlen=history_size)
        self.accel_history = {'x': deque(maxlen=history_size),
                              'y': deque(maxlen=history_size),
                              'z': deque(maxlen=history_size)}
        self.gyro_history = {'x': deque(maxlen=history_size),
                             'y': deque(maxlen=history_size),
                             'z': deque(maxlen=history_size)}
        self.euler_history = {'h': deque(maxlen=history_size),
                              'r': deque(maxlen=history_size),
                              'p': deque(maxlen=history_size)}

        self.start_time = None
        self.packet_count = 0

    def update(self, data_dict):
        """Update sensor data from parsed JSON"""
        with self.lock:
            if self.start_time is None:
                self.start_time = data_dict.get('t', 0)

            self.timestamp = data_dict.get('t', 0)
            relative_time = (self.timestamp - self.start_time) / 1000.0  # seconds

            if 'euler' in data_dict:
                self.euler = data_dict['euler']
            if 'quat' in data_dict:
                self.quat = data_dict['quat']
            if 'accel' in data_dict:
                self.accel = data_dict['accel']
            if 'gyro' in data_dict:
                self.gyro = data_dict['gyro']
            if 'mag' in data_dict:
                self.mag = data_dict['mag']
            if 'cal' in data_dict:
                self.calibration = data_dict['cal']
            if 'grideye' in data_dict:
                self.grideye = data_dict['grideye']

            # Update history
            self.time_history.append(relative_time)
            for axis in ['x', 'y', 'z']:
                self.accel_history[axis].append(self.accel.get(axis, 0))
                self.gyro_history[axis].append(self.gyro.get(axis, 0))
            for axis in ['h', 'r', 'p']:
                self.euler_history[axis].append(self.euler.get(axis, 0))

            self.packet_count += 1

    def get_current(self):
        """Get current sensor values"""
        with self.lock:
            return {
                'euler': self.euler.copy(),
                'quat': self.quat.copy(),
                'accel': self.accel.copy(),
                'gyro': self.gyro.copy(),
                'mag': self.mag.copy(),
                'cal': self.calibration.copy(),
                'grideye': list(self.grideye),
                'packet_count': self.packet_count
            }

    def get_history(self):
        """Get history arrays for plotting"""
        with self.lock:
            return {
                'time': list(self.time_history),
                'accel': {k: list(v) for k, v in self.accel_history.items()},
                'gyro': {k: list(v) for k, v in self.gyro_history.items()},
                'euler': {k: list(v) for k, v in self.euler_history.items()}
            }

    def reset(self):
        """Reset all data"""
        with self.lock:
            self.start_time = None
            self.packet_count = 0
            self.time_history.clear()
            for d in [self.accel_history, self.gyro_history, self.euler_history]:
                for dq in d.values():
                    dq.clear()


class SerialReader(Thread):
    """Background thread for reading serial data"""

    def __init__(self, port, baud, sensor_data, signals):
        super().__init__(daemon=True)
        self.port = port
        self.baud = baud
        self.sensor_data = sensor_data
        self.signals = signals
        self.stop_event = Event()
        self.serial = None
        self.connected = False
        self.error_message = ""

    def connect(self):
        """Establish serial connection with proper ESP32-S3 USB CDC handling"""
        try:
            # Close existing connection if any
            if self.serial and self.serial.is_open:
                self.serial.close()
                time.sleep(0.1)

            # Open serial port with specific settings for ESP32-S3 USB CDC
            self.serial = serial.Serial(
                port=self.port,
                baudrate=self.baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.1,
                write_timeout=1.0,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False
            )

            # ESP32-S3 USB CDC specific: Set DTR and RTS
            # DTR=1, RTS=1 is usually required for native USB CDC
            # Note: This might cause board reset. We wait 2.0s to allow reboot.
            self.serial.dtr = True
            self.serial.rts = True
            time.sleep(2.0) # wait for potential reboot

            # Clear any pending data
            self.serial.reset_input_buffer()
            self.serial.reset_output_buffer()

            # Small delay for USB enumeration
            time.sleep(0.2)

            # Send 'j' command to ensure JSON mode
            try:
                self.serial.write(b'j\n')
                self.serial.flush()
            except Exception:
                pass  # Ignore if write fails initially

            self.connected = True
            self.error_message = ""
            self.signals.connected.emit()
            return True

        except serial.SerialException as e:
            self.connected = False
            self.error_message = f"Serial error: {e}"
            self.signals.error.emit(self.error_message)
            return False
        except Exception as e:
            self.connected = False
            self.error_message = f"Connection error: {e}"
            self.signals.error.emit(self.error_message)
            return False

    def disconnect(self):
        """Close serial connection"""
        self.stop_event.set()
        if self.serial and self.serial.is_open:
            try:
                self.serial.close()
            except Exception:
                pass
        self.connected = False
        self.signals.disconnected.emit()

    def send_command(self, cmd):
        """Send a command to the device"""
        if self.serial and self.serial.is_open:
            try:
                self.serial.write(f"{cmd}\n".encode())
                self.serial.flush()
                return True
            except Exception as e:
                self.signals.error.emit(f"Send error: {e}")
                return False
        return False

    def run(self):
        """Main reading loop"""
        buffer = ""
        last_packet_time = time.time()
        reconnect_attempts = 0

        while not self.stop_event.is_set():
            if not self.connected or not self.serial:
                time.sleep(0.1)
                continue

            try:
                # Check if port is still open
                if not self.serial.is_open:
                    raise serial.SerialException("Port closed unexpectedly")

                # Read available data
                if self.serial.in_waiting > 0:
                    chunk = self.serial.read(self.serial.in_waiting)
                    if chunk:
                        buffer += chunk.decode('utf-8', errors='ignore')
                        # Hack to handle concatenated JSON objects "}{" -> "}\n{"
                        buffer = buffer.replace('}{', '}\n{')
                        last_packet_time = time.time()
                        reconnect_attempts = 0

                        # Process complete lines
                        while '\n' in buffer:
                            line, buffer = buffer.split('\n', 1)
                            line = line.strip()

                            if line:
                                # Emit raw line for debugging
                                if len(line) > 200:
                                    print(f"RX (truncated): {line[:100]}...")
                                else:
                                    print(f"RX: {line}")
                                self.signals.raw_line.emit(line)

                                # Parse JSON data
                                if line.startswith('{') and line.endswith('}'):
                                    try:
                                        data = json.loads(line)
                                        self.sensor_data.update(data)
                                        self.signals.data_received.emit(
                                            self.sensor_data.packet_count
                                        )
                                    except json.JSONDecodeError as e:
                                        print(f"JSON Error: {e} | Line: {line}")
                                        self.signals.error.emit(f"JSON error: {e}")
                else:
                    # No data available, small sleep
                    time.sleep(0.01)

                    # Check for timeout (no data for 5 seconds)
                    if time.time() - last_packet_time > 5.0 and self.connected:
                        # Try to wake up the device
                        self.send_command('j')
                        last_packet_time = time.time()

            except serial.SerialException as e:
                self.signals.error.emit(f"Serial error: {e}")
                self.connected = False
                self.signals.disconnected.emit()

                # Try to reconnect
                if reconnect_attempts < 3:
                    time.sleep(1.0)
                    reconnect_attempts += 1
                    if self.connect():
                        continue
                break

            except Exception as e:
                self.signals.error.emit(f"Read error: {e}")
                time.sleep(0.1)


if OPENGL_AVAILABLE:
    class OrientationWidget(QOpenGLWidget):
        """3D OpenGL widget for orientation visualization"""

        def __init__(self, sensor_data, parent=None):
            super().__init__(parent)
            self.sensor_data = sensor_data
            self.setMinimumSize(300, 300)

        def initializeGL(self):
            glClearColor(0.1, 0.1, 0.15, 1.0)
            glEnable(GL_DEPTH_TEST)
            glEnable(GL_LIGHTING)
            glEnable(GL_LIGHT0)
            glEnable(GL_COLOR_MATERIAL)
            glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE)

            # Light position
            glLightfv(GL_LIGHT0, GL_POSITION, [5.0, 5.0, 5.0, 1.0])
            glLightfv(GL_LIGHT0, GL_AMBIENT, [0.3, 0.3, 0.3, 1.0])
            glLightfv(GL_LIGHT0, GL_DIFFUSE, [0.8, 0.8, 0.8, 1.0])

        def resizeGL(self, w, h):
            glViewport(0, 0, w, h)
            glMatrixMode(GL_PROJECTION)
            glLoadIdentity()
            gluPerspective(45.0, w / h if h else 1, 0.1, 100.0)
            glMatrixMode(GL_MODELVIEW)

        def paintGL(self):
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
            glLoadIdentity()
            gluLookAt(0, 0, 5, 0, 0, 0, 0, 1, 0)

            # Get quaternion from BNO055
            data = self.sensor_data.get_current()
            q = data['quat']

            # BNO055 uses ENU (East-North-Up) coordinate system
            # OpenGL uses standard right-hand coordinate system
            # Need to remap: BNO055(x,y,z,w) -> OpenGL
            qw, qx, qy, qz = q['w'], q['x'], q['y'], q['z']

            # Remap BNO055 quaternion to OpenGL coordinate system
            # BNO055: X=East, Y=North, Z=Up
            # OpenGL: X=Right, Y=Up, Z=Out (towards viewer)
            # Transform: swap Y and Z, negate new Z
            gl_qw = qw
            gl_qx = qx
            gl_qy = qz  # BNO055 Z (up) -> OpenGL Y (up)
            gl_qz = -qy  # BNO055 Y (north) -> OpenGL -Z (into screen)

            # Build rotation matrix from remapped quaternion
            rotation_matrix = [
                1 - 2*gl_qy*gl_qy - 2*gl_qz*gl_qz, 2*gl_qx*gl_qy - 2*gl_qz*gl_qw, 2*gl_qx*gl_qz + 2*gl_qy*gl_qw, 0,
                2*gl_qx*gl_qy + 2*gl_qz*gl_qw, 1 - 2*gl_qx*gl_qx - 2*gl_qz*gl_qz, 2*gl_qy*gl_qz - 2*gl_qx*gl_qw, 0,
                2*gl_qx*gl_qz - 2*gl_qy*gl_qw, 2*gl_qy*gl_qz + 2*gl_qx*gl_qw, 1 - 2*gl_qx*gl_qx - 2*gl_qy*gl_qy, 0,
                0, 0, 0, 1
            ]

            glMultMatrixf(rotation_matrix)

            # Draw coordinate frame
            self.draw_axes()

            # Draw board representation
            self.draw_board()

        def draw_axes(self):
            """Draw XYZ coordinate axes"""
            glDisable(GL_LIGHTING)
            glLineWidth(2.0)

            glBegin(GL_LINES)
            # X axis - red
            glColor3f(1, 0, 0)
            glVertex3f(0, 0, 0)
            glVertex3f(2, 0, 0)
            # Y axis - green
            glColor3f(0, 1, 0)
            glVertex3f(0, 0, 0)
            glVertex3f(0, 2, 0)
            # Z axis - blue
            glColor3f(0, 0, 1)
            glVertex3f(0, 0, 0)
            glVertex3f(0, 0, 2)
            glEnd()

            glEnable(GL_LIGHTING)

        def draw_board(self):
            """Draw a simple board representation"""
            glColor3f(0.2, 0.6, 0.8)

            # Main board body
            glBegin(GL_QUADS)
            # Top
            glNormal3f(0, 1, 0)
            glVertex3f(-0.8, 0.1, -0.5)
            glVertex3f(0.8, 0.1, -0.5)
            glVertex3f(0.8, 0.1, 0.5)
            glVertex3f(-0.8, 0.1, 0.5)
            # Bottom
            glNormal3f(0, -1, 0)
            glVertex3f(-0.8, -0.1, -0.5)
            glVertex3f(-0.8, -0.1, 0.5)
            glVertex3f(0.8, -0.1, 0.5)
            glVertex3f(0.8, -0.1, -0.5)
            # Front
            glNormal3f(0, 0, 1)
            glVertex3f(-0.8, -0.1, 0.5)
            glVertex3f(0.8, -0.1, 0.5)
            glVertex3f(0.8, 0.1, 0.5)
            glVertex3f(-0.8, 0.1, 0.5)
            # Back
            glNormal3f(0, 0, -1)
            glVertex3f(-0.8, -0.1, -0.5)
            glVertex3f(-0.8, 0.1, -0.5)
            glVertex3f(0.8, 0.1, -0.5)
            glVertex3f(0.8, -0.1, -0.5)
            # Left
            glNormal3f(-1, 0, 0)
            glVertex3f(-0.8, -0.1, -0.5)
            glVertex3f(-0.8, -0.1, 0.5)
            glVertex3f(-0.8, 0.1, 0.5)
            glVertex3f(-0.8, 0.1, -0.5)
            # Right
            glNormal3f(1, 0, 0)
            glVertex3f(0.8, -0.1, -0.5)
            glVertex3f(0.8, 0.1, -0.5)
            glVertex3f(0.8, 0.1, 0.5)
            glVertex3f(0.8, -0.1, 0.5)
            glEnd()

            # Draw direction indicator (front of board)
            glColor3f(1, 0.5, 0)
            glBegin(GL_TRIANGLES)
            glNormal3f(0, 1, 0)
            glVertex3f(0.6, 0.12, 0)
            glVertex3f(0.9, 0.12, -0.15)
            glVertex3f(0.9, 0.12, 0.15)
            glEnd()


class PlotWidget(FigureCanvas):
    """Matplotlib widget for sensor data plotting"""

    def __init__(self, title, ylabel, parent=None):
        self.fig = Figure(figsize=(5, 2), dpi=100)
        self.fig.patch.set_facecolor('#1a1a2e')
        super().__init__(self.fig)

        self.ax = self.fig.add_subplot(111)
        self.ax.set_facecolor('#1a1a2e')
        
        # Store labels for redraw
        self.plot_title = title
        self.plot_ylabel = ylabel
        
        self.ax.set_title(self.plot_title, color='white', fontsize=10, pad=2)
        self.ax.set_ylabel(self.plot_ylabel, color='white', fontsize=8)
        self.ax.tick_params(colors='white', labelsize=8)
        for spine in self.ax.spines.values():
            spine.set_color('#444')

        self.lines = {}

    def update_plot(self, time_data, data_dict, labels):
        """Update plot with new data"""
        if not time_data:
            return

        self.ax.clear()
        self.ax.set_facecolor('#1a1a2e')
        self.ax.set_title(self.plot_title, color='white', fontsize=10, pad=2)
        self.ax.set_ylabel(self.plot_ylabel, color='white', fontsize=8)

        colors = {'x': '#ff6b6b', 'y': '#4ecdc4', 'z': '#45b7d1',
                  'h': '#ff6b6b', 'r': '#4ecdc4', 'p': '#45b7d1'}

        for key, values in data_dict.items():
            if values:
                label = labels.get(key, key)
                self.ax.plot(time_data, values, color=colors.get(key, 'white'),
                             label=label, linewidth=1)

        self.ax.legend(loc='upper right', fontsize=7, facecolor='#2a2a4e',
                       labelcolor='white', framealpha=0.8)
        self.ax.tick_params(colors='white', labelsize=8)
        for spine in self.ax.spines.values():
            spine.set_color('#444')
            
        for spine in self.ax.spines.values():
            spine.set_color('#444')

        # Use explicit margins closer to edges but ensuring labels fit
        # bottom=0.3 provides plenty of space for X labels
        self.fig.subplots_adjust(left=0.15, right=0.95, top=0.85, bottom=0.3)

        # Use draw_idle() for efficient non-blocking redraw
        self.draw_idle()


class HeatmapWidget(FigureCanvas):
    """Matplotlib widget for GridEye heatmap"""

    def __init__(self, title, parent=None):
        self.fig = Figure(figsize=(5, 5), dpi=100) # Square visual
        self.fig.patch.set_facecolor('#1a1a2e')
        super().__init__(self.fig)
        
        # Explicitly set parent if provided, though layout.addWidget usually handles it
        if parent:
            self.setParent(parent)

        self.ax = self.fig.add_subplot(111)
        self.ax.set_facecolor('#1a1a2e')
        self.ax.set_title(title, color='white', fontsize=10)
        self.ax.tick_params(colors='white', labelsize=8)
        
        # Initial empty heatmap
        self.data = np.zeros((8, 8))
        # Use 'nearest' for crisp pixels (8x8 is low res)
        # Use aspect='equal' to ensure it's a square
        self.im = self.ax.imshow(self.data, cmap='inferno', vmin=15, vmax=35, 
                                 interpolation='nearest', aspect='equal')
        
        self.cbar = self.fig.colorbar(self.im, ax=self.ax, fraction=0.046, pad=0.04)
        self.cbar.ax.yaxis.set_tick_params(color='white')
        plt.setp(plt.getp(self.cbar.ax.axes, 'yticklabels'), color='white')
        
        # Explicit margins for Heatmap
        # top=0.88 leaves room for Title
        # bottom=0.05 since we don't have X labels usually (or very small pixels)
        self.fig.subplots_adjust(left=0.05, right=0.9, top=0.88, bottom=0.05)
        
        
        # Policy: Expands to fill available space, but tries to keep square aspect inside matplotlib
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setMinimumSize(300, 300) # Force visible area
        self.updateGeometry()

    def update_heatmap(self, grideye_data):
        """Update heatmap with new 64-element list"""
        if not grideye_data or len(grideye_data) != 64:
            return

        try:
            # Convert to 8x8 matrix
            # Valid 8x8 GridEye data is row-major
            matrix = np.array(grideye_data, dtype=float).reshape(8, 8)
            
            # Simple rotation if needed (GridEye often mounted in various orientations)
            # matrix = np.rot90(matrix, 2) 

            # Auto-scaling logic
            raw_min = np.min(matrix)
            raw_max = np.max(matrix)
            
            # Robust scaling: Add margin
            vmin = raw_min - 0.5
            vmax = raw_max + 0.5
            
            # Ensure minimum contrast range (e.g., at least 2 degrees)
            if vmax - vmin < 2.0:
                mid = (vmax + vmin) / 2.0
                vmin = mid - 1.0
                vmax = mid + 1.0

            self.im.set_data(matrix)
            self.im.set_clim(vmin=vmin, vmax=vmax)
            
            # Efficient redraw
            self.draw_idle() 
            
        except Exception as e:
            print(f"Heatmap Error: {e}")


class CompassWidget(FigureCanvas):
    """Matplotlib widget for visual Compass (North Up)"""

    def __init__(self, title, parent=None):
        self.fig = Figure(figsize=(3, 3), dpi=100)
        self.fig.patch.set_facecolor('#1a1a2e')
        super().__init__(self.fig)
        if parent:
            self.setParent(parent)

        self.ax = self.fig.add_subplot(111, projection='polar')
        self.ax.set_facecolor('#1a1a2e')
        
        # Configure Compass Style
        self.ax.set_theta_zero_location('N') # 0 is North
        self.ax.set_theta_direction(-1)      # Clockwise
        
        self.ax.set_rlim(0, 1)
        self.ax.set_yticklabels([]) # Hide radius labels
        self.ax.set_xticklabels(['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'])
        self.ax.tick_params(colors='white', labelsize=9, pad=10)
        
        self.ax.grid(color='#444', linewidth=0.5)
        self.ax.spines['polar'].set_color('#444')
        
        self.ax.set_title(title, color='white', fontsize=10, pad=15)
        
        # Current Needle
        self.needle, = self.ax.plot([], [], color='#ff6b6b', linewidth=3)
        self.arrow = None # For fancy arrow if needed

        self.fig.subplots_adjust(left=0.1, right=0.9, top=0.85, bottom=0.1)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.updateGeometry()

    def update_compass(self, heading_deg):
        """Update compass needle. Heading 0=North, 90=East"""
        if heading_deg is None:
            return
            
        # Convert degrees to radians
        rad = np.radians(heading_deg)
        
        # Draw needle from center to edge
        self.needle.set_data([rad, rad], [0, 0.9])
        
        self.draw_idle()


class MainWindow(QMainWindow):
    """Main application window"""

    def __init__(self, port=None, baud=115200):
        super().__init__()
        self.setWindowTitle("BNO055 IMU Visualizer")
        self.setStyleSheet("""
            QMainWindow, QWidget {
                background-color: #1a1a2e;
                color: white;
            }
            QGroupBox {
                border: 1px solid #444;
                border-radius: 5px;
                margin-top: 10px;
                padding-top: 10px;
                font-weight: bold;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }
            QPushButton {
                background-color: #4a4a6a;
                border: none;
                padding: 8px 16px;
                border-radius: 4px;
            }
            QPushButton:hover {
                background-color: #5a5a7a;
            }
            QPushButton:pressed {
                background-color: #3a3a5a;
            }
            QPushButton:disabled {
                background-color: #2a2a3a;
                color: #666;
            }
            QComboBox {
                background-color: #2a2a4e;
                border: 1px solid #444;
                padding: 5px;
                border-radius: 4px;
            }
            QComboBox:disabled {
                background-color: #1a1a2e;
                color: #666;
            }
            QProgressBar {
                border: 1px solid #444;
                border-radius: 4px;
                text-align: center;
            }
            QProgressBar::chunk {
                background-color: #4ecdc4;
            }
            QTextEdit {
                background-color: #0a0a15;
                border: 1px solid #333;
                border-radius: 4px;
                font-family: monospace;
                font-size: 10px;
            }
            QLabel#status_connected {
                color: #4ecdc4;
                font-weight: bold;
            }
            QLabel#status_disconnected {
                color: #ff6b6b;
                font-weight: bold;
            }
            QSlider::groove:horizontal {
                border: 1px solid #444;
                height: 8px;
                background: #2a2a4e;
                border-radius: 4px;
            }
            QSlider::handle:horizontal {
                background: #4ecdc4;
                border: 1px solid #4ecdc4;
                width: 18px;
                margin: -5px 0;
                border-radius: 9px;
            }
            QSlider::handle:horizontal:hover {
                background: #5fd9cf;
            }
            QSlider::handle:horizontal:disabled {
                background: #333;
                border: 1px solid #333;
            }
            QSlider::sub-page:horizontal {
                background: #4ecdc4;
                border-radius: 4px;
            }
            QSlider::add-page:horizontal {
                background: #2a2a4e;
                border-radius: 4px;
            }
            QScrollArea {
                border: none;
                background-color: #1a1a2e;
            }
            QScrollBar:vertical {
                border: none;
                background: #1a1a2e;
                width: 10px;
                margin: 0px;
            }
            QScrollBar::handle:vertical {
                background: #4a4a6a;
                min-height: 20px;
                border-radius: 5px;
            }
            QScrollBar::handle:vertical:hover {
                background: #5a5a7a;
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
                border: none;
                background: none;
            }
            QScrollBar:horizontal {
                border: none;
                background: #1a1a2e;
                height: 10px;
                margin: 0px;
            }
            QScrollBar::handle:horizontal {
                background: #4a4a6a;
                min-width: 20px;
                border-radius: 5px;
            }
            QScrollBar::handle:horizontal:hover {
                background: #5a5a7a;
            }
            QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
                border: none;
                background: none;
            }
        """)

        self.sensor_data = SensorData()
        self.serial_reader = None
        self.serial_signals = SerialSignals()
        self.selected_port = port
        self.baud_rate = baud

        # Connect signals
        self.serial_signals.connected.connect(self.on_connected)
        self.serial_signals.disconnected.connect(self.on_disconnected)
        self.serial_signals.error.connect(self.on_error)
        self.serial_signals.data_received.connect(self.on_data_received)
        self.serial_signals.raw_line.connect(self.on_raw_line)

        self.setup_ui()
        self.setup_timer()

        # Auto-connect if port specified
        if port:
            QTimer.singleShot(500, self.auto_connect)

    def setup_ui(self):
        """Initialize UI components"""
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # ROOT LAYOUT: Horizontal to fix "vertical overflow" issue
        main_layout = QHBoxLayout(central_widget)

        # --- LEFT COLUMN (Controls + 3D) with Scroll ---
        left_col_widget = QWidget()
        left_col_layout = QVBoxLayout(left_col_widget)

        # Wrap left column in scroll area
        left_scroll = QScrollArea()
        left_scroll.setWidget(left_col_widget)
        left_scroll.setWidgetResizable(True)
        left_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        left_scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        main_layout.addWidget(left_scroll, stretch=1)

        # Connection controls
        conn_group = QGroupBox("Connection")
        conn_layout = QVBoxLayout(conn_group)

        # Port selection row
        port_row = QHBoxLayout()
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(150)
        self.refresh_ports()
        port_row.addWidget(self.port_combo, stretch=1)

        self.refresh_btn = QPushButton("🔄")
        self.refresh_btn.setFixedWidth(40)
        self.refresh_btn.clicked.connect(self.refresh_ports)
        port_row.addWidget(self.refresh_btn)
        conn_layout.addLayout(port_row)

        # Connect button and status
        btn_row = QHBoxLayout()
        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self.toggle_connection)
        btn_row.addWidget(self.connect_btn)

        self.status_label = QLabel("Disconnected")
        self.status_label.setObjectName("status_disconnected")
        btn_row.addWidget(self.status_label)
        conn_layout.addLayout(btn_row)
        
        # Packet counter
        stats_row = QHBoxLayout()
        stats_row.addWidget(QLabel("Pkts:"))
        self.packet_label = QLabel("0")
        self.packet_label.setFont(QFont("Menlo", 10)) 
        stats_row.addWidget(self.packet_label)
        conn_layout.addLayout(stats_row)
        
        left_col_layout.addWidget(conn_group)

        # Control Buttons
        ctrl_group = QGroupBox("Controls")
        ctrl_layout = QGridLayout(ctrl_group)
        self.bno_btn = QPushButton("BNO Stream")
        self.bno_btn.setCheckable(True)
        self.bno_btn.clicked.connect(lambda: self.toggle_stream('B', self.bno_btn))
        ctrl_layout.addWidget(self.bno_btn, 0, 0)

        self.grideye_btn = QPushButton("GridEye Stream")
        self.grideye_btn.setCheckable(True)
        self.grideye_btn.clicked.connect(lambda: self.toggle_stream('G', self.grideye_btn))
        ctrl_layout.addWidget(self.grideye_btn, 0, 1)
        
        left_col_layout.addWidget(ctrl_group)
        
        # 3D visualization (in left column)
        if OPENGL_AVAILABLE:
            orient_group = QGroupBox("3D Orientation")
            orient_layout = QVBoxLayout(orient_group)
            self.orientation_widget = OrientationWidget(self.sensor_data)
            # Minimum size for 3D
            self.orientation_widget.setMinimumHeight(200)
            orient_layout.addWidget(self.orientation_widget)
            left_col_layout.addWidget(orient_group)

        # --- RIGHT COLUMN (Heatmap + Charts) ---
        right_col_widget = QWidget()
        right_col_layout = QVBoxLayout(right_col_widget)
        main_layout.addWidget(right_col_widget, stretch=2)

        # Visualization Row (Heatmap + Compass)
        viz_upper_group = QGroupBox("Visualization")
        viz_layout = QHBoxLayout(viz_upper_group)
        
        # Heatmap (Left)
        # self.heatmap_widget is already defined class
        self.heatmap_widget = HeatmapWidget("Thermal Array")
        viz_layout.addWidget(self.heatmap_widget, stretch=1)
        
        # Compass (Right)
        self.compass_widget = CompassWidget("Heading (North Up)")
        viz_layout.addWidget(self.compass_widget, stretch=1)
        
        # LED Widget (Far Right)
        self.led_widget = LEDWidget()
        viz_layout.addWidget(self.led_widget, stretch=1)
        
        right_col_layout.addWidget(viz_upper_group)

        # Data Labels (Euler) - Put under heatmap or charts
        euler_group = QGroupBox("Euler Angles")
        euler_layout = QGridLayout(euler_group)
        self.euler_labels = {}
        for i, (name, label) in enumerate([('h', 'HEAD'), ('r', 'ROLL'), ('p', 'PTCH')]):
            euler_layout.addWidget(QLabel(f"{label}:"), 0, i*2)
            self.euler_labels[name] = QLabel("0.00°")
            self.euler_labels[name].setFont(QFont("Menlo", 12))
            euler_layout.addWidget(self.euler_labels[name], 0, i*2+1)
        right_col_layout.addWidget(euler_group)

        # 2D Charts (Removed for now, not defined)
        # chart_group = QGroupBox("Sensor Graphs")
        # chart_layout = QVBoxLayout(chart_group)
        # self.chart_widget = ChartWidget(self.sensor_data)
        # chart_layout.addWidget(self.chart_widget)
        # right_col_layout.addWidget(chart_group)

        # Term/Log (Bottom of Left?) -> Maybe separate tab or bottom full width?
        # Let's put Log in Left column bottom
        log_group = QGroupBox("Log")
        log_layout = QVBoxLayout(log_group)
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setMaximumHeight(100)
        log_layout.addWidget(self.log_text)
        left_col_layout.addWidget(log_group)
        # Calibration status (Right Column)
        cal_group = QGroupBox("Calibration")
        cal_layout = QGridLayout(cal_group)
        self.cal_bars = {}
        for i, name in enumerate(['sys', 'gyr', 'acc', 'mag']):
            label = {'sys': 'System', 'gyr': 'Gyro', 'acc': 'Accel', 'mag': 'Mag'}[name]
            cal_layout.addWidget(QLabel(f"{label}:"), 0, i*2)
            bar = QProgressBar()
            bar.setRange(0, 3)
            bar.setValue(0)
            bar.setTextVisible(True)
            bar.setFormat("%v/3")
            bar.setFixedWidth(40) # Compact
            self.cal_bars[name] = bar
            cal_layout.addWidget(bar, 0, i*2+1)
        right_col_layout.addWidget(cal_group)

        # LED Control Group
        led_group = QGroupBox("LED Control")
        led_layout = QGridLayout(led_group)
        
        # L0: Off
        self.btn_l0 = QPushButton("OFF")
        self.btn_l0.setStyleSheet("background-color: #333; color: white;")
        self.btn_l0.clicked.connect(lambda: self.send_command('L0'))
        led_layout.addWidget(self.btn_l0, 0, 0)
        
        # L1: White
        self.btn_l1 = QPushButton("GR Front")
        self.btn_l1.setStyleSheet("background-color: #ddd; color: black;")
        self.btn_l1.clicked.connect(lambda: self.send_command('L1'))
        led_layout.addWidget(self.btn_l1, 0, 1)
        
        # L2: Red
        self.btn_l2 = QPushButton("GR Back")
        self.btn_l2.setStyleSheet("background-color: #800; color: white;")
        self.btn_l2.clicked.connect(lambda: self.send_command('L2'))
        led_layout.addWidget(self.btn_l2, 1, 0)
        
        # L3: Green
        self.btn_l3 = QPushButton("CR Front")
        self.btn_l3.setStyleSheet("background-color: #080; color: white;")
        self.btn_l3.clicked.connect(lambda: self.send_command('L3'))
        led_layout.addWidget(self.btn_l3, 1, 1)

        # L4: Blue
        self.btn_l4 = QPushButton("CR Back")
        self.btn_l4.setStyleSheet("background-color: #008; color: white;")
        self.btn_l4.clicked.connect(lambda: self.send_command('L4'))
        led_layout.addWidget(self.btn_l4, 2, 0)
        
        # L5: Rainbow
        self.btn_l5 = QPushButton("Rainbow")
        self.btn_l5.setStyleSheet("background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 red, stop:0.2 orange, stop:0.4 yellow, stop:0.6 green, stop:0.8 blue, stop:1 purple); color: black;")
        self.btn_l5.clicked.connect(lambda: self.send_command('L5'))
        led_layout.addWidget(self.btn_l5, 2, 1)
        
        left_col_layout.addWidget(led_group)

        # Motor Control Group
        motor_group = QGroupBox("Motor Control")
        motor_layout = QVBoxLayout(motor_group)

        # Pitch Angle Slider (-60° to 60°)
        pitch_row = QHBoxLayout()
        pitch_row.addWidget(QLabel("Pitch:"))
        self.pitch_value_label = QLabel("0°")
        self.pitch_value_label.setFont(QFont("Menlo", 12))
        self.pitch_value_label.setFixedWidth(50)
        pitch_row.addWidget(self.pitch_value_label)
        motor_layout.addLayout(pitch_row)

        self.pitch_slider = QSlider(Qt.Orientation.Horizontal)
        self.pitch_slider.setMinimum(-60)
        self.pitch_slider.setMaximum(60)
        self.pitch_slider.setValue(0)
        self.pitch_slider.setTickPosition(QSlider.TickPosition.TicksBelow)
        self.pitch_slider.setTickInterval(10)
        self.pitch_slider.valueChanged.connect(self.on_pitch_changed)
        motor_layout.addWidget(self.pitch_slider)

        # Yaw Speed Slider (-100% to 100%)
        yaw_row = QHBoxLayout()
        yaw_row.addWidget(QLabel("Yaw Speed:"))
        self.yaw_value_label = QLabel("0%")
        self.yaw_value_label.setFont(QFont("Menlo", 12))
        self.yaw_value_label.setFixedWidth(60)
        yaw_row.addWidget(self.yaw_value_label)
        motor_layout.addLayout(yaw_row)

        self.yaw_slider = QSlider(Qt.Orientation.Horizontal)
        self.yaw_slider.setMinimum(-100)
        self.yaw_slider.setMaximum(100)
        self.yaw_slider.setValue(0)
        self.yaw_slider.setTickPosition(QSlider.TickPosition.TicksBelow)
        self.yaw_slider.setTickInterval(20)
        self.yaw_slider.valueChanged.connect(self.on_yaw_speed_changed)
        motor_layout.addWidget(self.yaw_slider)

        # Zero Buttons
        zero_row = QHBoxLayout()
        pitch_zero_btn = QPushButton("Pitch 0°")
        pitch_zero_btn.clicked.connect(lambda: self.pitch_slider.setValue(0))
        zero_row.addWidget(pitch_zero_btn)

        yaw_zero_btn = QPushButton("Yaw STOP")
        yaw_zero_btn.clicked.connect(lambda: self.yaw_slider.setValue(0))
        zero_row.addWidget(yaw_zero_btn)
        motor_layout.addLayout(zero_row)

        left_col_layout.addWidget(motor_group)

        # Command buttons (Left Column)
        cmd_group = QGroupBox("Legacy Cmds")
        cmd_layout = QHBoxLayout(cmd_group)

        self.json_btn = QPushButton("JSON")
        self.json_btn.clicked.connect(lambda: self.send_command('j'))
        cmd_layout.addWidget(self.json_btn)

        self.human_btn = QPushButton("Human")
        self.human_btn.clicked.connect(lambda: self.send_command('h'))
        cmd_layout.addWidget(self.human_btn)
        
        self.reset_btn = QPushButton("Reset")
        self.reset_btn.clicked.connect(lambda: self.send_command('r'))
        cmd_layout.addWidget(self.reset_btn)
        
        left_col_layout.addWidget(cmd_group)
        
        # Debug console (Left Column Bottom)
        debug_group = QGroupBox("Debug Console")
        debug_layout = QVBoxLayout(debug_group)
        self.debug_console = QTextEdit()
        self.debug_console.setReadOnly(True)
        self.debug_console.setMaximumHeight(100)
        debug_layout.addWidget(self.debug_console)

        clear_btn = QPushButton("Clear")
        clear_btn.clicked.connect(self.debug_console.clear)
        debug_layout.addWidget(clear_btn)
        
        left_col_layout.addWidget(debug_group)
        
        # Add stretch to fill bottom of left column
        # left_col_layout.addStretch() # Debug console at bottom is better

        # Charts (Right Column Bottom)
        charts_group = QGroupBox("Graphs")
        charts_layout = QVBoxLayout(charts_group)
        
        # Accelerometer plot
        self.accel_plot = PlotWidget("Accel", "m/s²")
        self.accel_plot.setMinimumHeight(150)
        charts_layout.addWidget(self.accel_plot)

        # Gyroscope plot
        self.gyro_plot = PlotWidget("Gyro", "rad/s")
        self.gyro_plot.setMinimumHeight(150)
        charts_layout.addWidget(self.gyro_plot)
        
        # Euler angles plot
        self.euler_plot = PlotWidget("Euler", "deg")
        self.euler_plot.setMinimumHeight(150)
        charts_layout.addWidget(self.euler_plot)
        
        right_col_layout.addWidget(charts_group)

        # Set minimum size and initial size (resizable)
        self.setMinimumSize(1000, 600)  # Minimum window size
        self.resize(1400, 900)  # Initial size (user can resize)

        # Update command button states
        self.update_command_buttons(False)

    def setup_timer(self):
        """Setup update timer"""
        # Main UI update: 10Hz (reduced from 20Hz for performance)
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_display)
        self.timer.start(100)  # 10 Hz update

        # 3D Orientation update: 5Hz (separate timer for heavy OpenGL rendering)
        if OPENGL_AVAILABLE and hasattr(self, 'orientation_widget'):
            self.timer_3d = QTimer()
            self.timer_3d.timeout.connect(self.update_3d)
            self.timer_3d.start(200)  # 5 Hz update

    def refresh_ports(self):
        """Refresh available serial ports"""
        current_port = self.port_combo.currentData()
        self.port_combo.clear()

        ports = serial.tools.list_ports.comports()
        for port in ports:
            # Filter for likely ESP32 devices
            desc = port.description or ""
            self.port_combo.addItem(f"{port.device} - {desc}", port.device)

        # Try to re-select previous port or specified port
        target_port = self.selected_port or current_port
        if target_port:
            for i in range(self.port_combo.count()):
                if self.port_combo.itemData(i) == target_port:
                    self.port_combo.setCurrentIndex(i)
                    break

        # Auto-select usbmodem if only one ESP32 device
        if self.port_combo.count() == 0:
            self.log_debug("No serial ports found")
        else:
            self.log_debug(f"Found {self.port_combo.count()} port(s)")

    def auto_connect(self):
        """Auto-connect to specified port"""
        if self.selected_port and not (self.serial_reader and self.serial_reader.connected):
            self.toggle_connection()

    def toggle_connection(self):
        """Connect/disconnect serial"""
        if self.serial_reader and self.serial_reader.connected:
            # Disconnect
            self.log_debug("Disconnecting...")
            self.serial_reader.disconnect()
        else:
            # Connect
            port = self.port_combo.currentData()
            if port:
                self.log_debug(f"Connecting to {port}...")
                self.sensor_data.reset()

                self.serial_reader = SerialReader(
                    port, self.baud_rate, self.sensor_data, self.serial_signals
                )

                if self.serial_reader.connect():
                    self.serial_reader.start()
                else:
                    self.log_debug(f"Connection failed: {self.serial_reader.error_message}")
            else:
                self.log_debug("No port selected")

    def send_command(self, cmd):
        """Send command to device"""
        if self.serial_reader and self.serial_reader.connected:
            if self.serial_reader.send_command(cmd):
                self.log_debug(f"Sent: {cmd}")
            else:
                self.log_debug(f"Failed to send: {cmd}")
        else:
            self.log_debug("Not connected")

    def toggle_stream(self, cmd, btn):
        """Toggle stream command"""
        if self.serial_reader and self.serial_reader.connected:
            self.send_command(cmd)
            self.log_debug(f"Toggled stream: {cmd}")
        else:
            self.log_debug("Not connected, cannot toggle stream")
            btn.setChecked(False) # Reset if not connected

    def on_pitch_changed(self, value):
        """Handle pitch slider change"""
        self.pitch_value_label.setText(f"{value}°")
        # Send P[angle] command to Unit 1
        cmd = f"P{value}"
        self.send_command(cmd)

    def on_yaw_speed_changed(self, value):
        """Handle yaw speed slider change (-100% to 100%)"""
        self.yaw_value_label.setText(f"{value}%")

        # Convert percentage to rad/s
        # -100% = -20 rad/s (CCW max)
        # 0% = 0 rad/s (stop)
        # +100% = +20 rad/s (CW max)
        max_speed = 20.0  # rad/s
        speed_rad_s = (value / 100.0) * max_speed

        # Send Y[vel] command to Unit 2 (via Unit 1)
        cmd = f"Y{speed_rad_s:.2f}"
        self.send_command(cmd)

    def update_command_buttons(self, enabled):
        """Enable/disable command buttons"""
        self.json_btn.setEnabled(enabled)
        self.human_btn.setEnabled(enabled)
        self.reset_btn.setEnabled(enabled)
        self.bno_btn.setEnabled(enabled)
        self.grideye_btn.setEnabled(enabled)

        # Motor control sliders
        self.pitch_slider.setEnabled(enabled)
        self.yaw_slider.setEnabled(enabled)

    def on_connected(self):
        """Handle connection event"""
        self.connect_btn.setText("Disconnect")
        self.status_label.setText("Connected")
        self.status_label.setObjectName("status_connected")
        self.status_label.setStyleSheet("color: #4ecdc4; font-weight: bold;")
        self.port_combo.setEnabled(False)
        self.refresh_btn.setEnabled(False)
        self.update_command_buttons(True)
        self.log_debug("Connected successfully")

    def on_disconnected(self):
        """Handle disconnection event"""
        self.connect_btn.setText("Connect")
        self.status_label.setText("Disconnected")
        self.status_label.setObjectName("status_disconnected")
        self.status_label.setStyleSheet("color: #ff6b6b; font-weight: bold;")
        self.port_combo.setEnabled(True)
        self.refresh_btn.setEnabled(True)
        self.update_command_buttons(False)
        self.log_debug("Disconnected")

    def on_error(self, message):
        """Handle error event"""
        self.log_debug(f"ERROR: {message}")

    def on_data_received(self, count):
        """Handle data received event"""
        self.packet_label.setText(str(count))

    def on_raw_line(self, line):
        """Handle raw line for debugging"""
        if hasattr(self, 'led_widget'):
            if line.startswith('LED:'):
                self.led_widget.update_status(line)
                
        # Only show non-JSON lines or errors
        if not line.startswith('{'):
            self.log_debug(f"RX: {line[:80]}")

    def log_debug(self, message):
        """Add message to debug console"""
        # Check if debug_console exists (may be called before UI setup)
        if not hasattr(self, 'debug_console') or self.debug_console is None:
            print(f"[DEBUG] {message}")
            return
        timestamp = time.strftime("%H:%M:%S")
        self.debug_console.append(f"[{timestamp}] {message}")
        # Auto-scroll to bottom
        scrollbar = self.debug_console.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())

    def update_3d(self):
        """Update 3D orientation (low frequency for performance)"""
        if OPENGL_AVAILABLE and hasattr(self, 'orientation_widget'):
            self.orientation_widget.update()

    def update_display(self):
        """Update all display elements"""
        data = self.sensor_data.get_current()
        history = self.sensor_data.get_history()

        # 3D view is updated by separate timer (5Hz) for performance

        # Update Heatmap (only if data exists)
        grideye_data = data.get('grideye')
        if grideye_data is not None and hasattr(self, 'heatmap_widget'):
            self.heatmap_widget.update_heatmap(grideye_data)

        # Update Compass
        if hasattr(self, 'compass_widget'):
            heading = data['euler'].get('h')
            if heading is not None:
                self.compass_widget.update_compass(heading)

        # Update LED Widget (Rainbow animation if active)
        if hasattr(self, 'led_widget'):
            self.led_widget.animate()

        # Update Euler angles
        euler = data['euler']
        self.euler_labels['h'].setText(f"{euler.get('h', 0):.2f}°")
        self.euler_labels['r'].setText(f"{euler.get('r', 0):.2f}°")
        self.euler_labels['p'].setText(f"{euler.get('p', 0):.2f}°")

        # Update calibration bars
        cal = data['cal']
        for name, bar in self.cal_bars.items():
            bar.setValue(cal.get(name, 0))

        # Update plots (reduce frequency by skipping every other frame)
        self._plot_frame_counter = getattr(self, '_plot_frame_counter', 0) + 1
        if self._plot_frame_counter % 2 == 0:  # Update plots at 5Hz (every other frame)
            time_data = history['time']
            self.accel_plot.update_plot(time_data, history['accel'],
                                        {'x': 'X', 'y': 'Y', 'z': 'Z'})
            self.gyro_plot.update_plot(time_data, history['gyro'],
                                       {'x': 'X', 'y': 'Y', 'z': 'Z'})
            self.euler_plot.update_plot(time_data, history['euler'],
                                        {'h': 'Heading', 'r': 'Roll', 'p': 'Pitch'})

    def closeEvent(self, event):
        """Clean up on close"""
        # Stop timers
        if hasattr(self, 'timer'):
            self.timer.stop()
        if hasattr(self, 'timer_3d'):
            self.timer_3d.stop()

        # Disconnect serial
        if self.serial_reader:
            self.serial_reader.disconnect()

        event.accept()


class LEDWidget(QWidget):
    """Widget to visualize LED status"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(300, 100)
        self.setStyleSheet("background-color: #1a1a2e; border: 1px solid #444; border-radius: 5px;")
        
        layout = QHBoxLayout(self)
        
        # 4 Zones
        self.zones = {}
        # Name: (Label, DefaultColor)
        configs = [
            ('GR_F', 'GR Front', '#333'),
            ('GR_B', 'GR Back', '#333'),
            ('CR_F', 'CR Front', '#333'),
            ('CR_B', 'CR Back', '#333')
        ]
        
        for key, name, color in configs:
            lbl = QLabel(name)
            lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            lbl.setStyleSheet(f"background-color: {color}; color: white; border: 1px solid #666; border-radius: 3px; padding: 10px;")
            layout.addWidget(lbl)
            self.zones[key] = lbl
            
        self.rainbow_mode = False
        self.rainbow_hue = 0
        
    def update_status(self, message):
        """Update based on 'LED: ...' message"""
        msg = message.strip()
        self.rainbow_mode = False
        
        # Reset all to off first
        for key in self.zones:
            self.zones[key].setStyleSheet("background-color: #333; color: #888; border: 1px solid #666; border-radius: 3px; padding: 10px;")
            
        if "All Off" in msg:
            pass # Already reset
        elif "GR Front" in msg:
            self.zones['GR_F'].setStyleSheet("background-color: white; color: black; border: 2px solid white; border-radius: 3px; padding: 10px;")
        elif "GR Back" in msg:
            self.zones['GR_B'].setStyleSheet("background-color: red; color: white; border: 2px solid red; border-radius: 3px; padding: 10px;")
        elif "CR Front" in msg:
            self.zones['CR_F'].setStyleSheet("background-color: green; color: white; border: 2px solid green; border-radius: 3px; padding: 10px;")
        elif "CR Back" in msg:
            self.zones['CR_B'].setStyleSheet("background-color: blue; color: white; border: 2px solid blue; border-radius: 3px; padding: 10px;")
        elif "Rainbow" in msg:
            self.rainbow_mode = True
            
    def animate(self):
        """Animate rainbow if active"""
        if not self.rainbow_mode:
            return
            
        self.rainbow_hue = (self.rainbow_hue + 10) % 360
        color = QColor.fromHsl(self.rainbow_hue, 200, 127).name()
        
        style = f"background-color: {color}; color: black; border: 2px solid white; border-radius: 3px; padding: 10px;"
        for key in self.zones:
            self.zones[key].setStyleSheet(style)


def main():
    parser = argparse.ArgumentParser(description='BNO055 IMU Visualizer')
    parser.add_argument('--port', '-p', help='Serial port (e.g., /dev/cu.usbmodem2101)')
    parser.add_argument('--baud', '-b', type=int, default=115200, help='Baud rate')
    parser.add_argument('--auto', '-a', action='store_true', help='Auto-connect on start')
    args = parser.parse_args()

    # Auto-detect ESP32 port if not specified
    if not args.port and args.auto:
        ports = serial.tools.list_ports.comports()
        for port in ports:
            if 'usbmodem' in port.device or 'USB' in (port.description or ''):
                args.port = port.device
                print(f"Auto-detected port: {args.port}")
                break

    app = QApplication(sys.argv)
    window = MainWindow(port=args.port, baud=args.baud)
    window.show()
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
