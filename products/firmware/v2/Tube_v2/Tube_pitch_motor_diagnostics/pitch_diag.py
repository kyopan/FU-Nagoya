import sys
import time
import json
import serial
import serial.tools.list_ports
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                             QHBoxLayout, QLabel, QPushButton, QSlider, QTextEdit, QComboBox)
from PyQt6.QtCore import QTimer, Qt

class PitchDiag(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Pitch Motor Diagnostics")
        self.resize(600, 500)
        
        self.serial = None
        self.buffer = ""
        
        # UI
        root = QWidget()
        self.setCentralWidget(root)
        layout = QVBoxLayout(root)
        
        # Connection
        conn_lay = QHBoxLayout()
        self.cb_port = QComboBox()
        conn_lay.addWidget(self.cb_port)
        
        # Toggle Button
        self.btn_conn = QPushButton("Connect")
        self.btn_conn.clicked.connect(self.toggle_connection)
        self.btn_conn.setStyleSheet("background-color: green; color: white;")
        conn_lay.addWidget(self.btn_conn)

        self.btn_ref = QPushButton("scan")
        self.btn_ref.clicked.connect(self.scan_ports_only)
        self.btn_ref.setFixedWidth(50)
        conn_lay.addWidget(self.btn_ref)

        btn_j = QPushButton("Stream (j)")
        btn_j.clicked.connect(lambda: self.send("j"))
        conn_lay.addWidget(btn_j)
        
        self.lbl_status = QLabel("Disconnected")
        conn_lay.addWidget(self.lbl_status)
        layout.addLayout(conn_lay)
        
        # VR Display
        self.lbl_vr = QLabel("VR: ---")
        self.lbl_vr.setStyleSheet("font-size: 24px; font-weight: bold; color: yellow; background: #333; padding: 10px;")
        layout.addWidget(self.lbl_vr)
        
        # Pitch Control
        ctrl_grp = QWidget()
        ctrl_lay = QVBoxLayout(ctrl_grp)
        ctrl_lay.addWidget(QLabel("Pitch Control (-60 to +60)"))
        
        slider_lay = QHBoxLayout()
        self.slider = QSlider(Qt.Orientation.Horizontal)
        self.slider.setRange(-60, 60)
        self.slider.setValue(0)
        self.slider.valueChanged.connect(self.on_slider)
        slider_lay.addWidget(self.slider)
        self.lbl_val = QLabel("0")
        slider_lay.addWidget(self.lbl_val)
        ctrl_lay.addLayout(slider_lay)
        
        btn_lay = QHBoxLayout()
        btn_center = QPushButton("Center (0)")
        btn_center.clicked.connect(lambda: self.set_pitch(0))
        btn_lay.addWidget(btn_center)
        
        self.btn_cal = QPushButton("CALIBRATE (Horizon)")
        self.btn_cal.setStyleSheet("background-color: blue; color: white; font-weight: bold;")
        self.btn_cal.clicked.connect(self.calibrate)
        btn_lay.addWidget(self.btn_cal)
        
        btn_clear = QPushButton("Clear Cal")
        btn_clear.clicked.connect(self.clear_cal)
        btn_clear.setStyleSheet("background-color: #555; color: white;")
        btn_lay.addWidget(btn_clear)
        
        btn_stop = QPushButton("STOP (Vel 0)")
        btn_stop.setStyleSheet("background-color: red; color: white; font-weight: bold;")
        btn_stop.clicked.connect(self.stop_motor)
        btn_lay.addWidget(btn_stop)
        
        ctrl_lay.addLayout(btn_lay)
        layout.addWidget(ctrl_grp)
        
        # Log
        self.log = QTextEdit()
        self.log.setReadOnly(True)
        self.log.setStyleSheet("background-color: #111; color: #0f0; font-family: monospace;")
        layout.addWidget(self.log)
        
        # Timers
        self.timer_update = QTimer()
        self.timer_update.timeout.connect(self.update_serial)
        self.timer_update.start(20) # 50Hz Read

        self.timer_scan = QTimer()
        self.timer_scan.timeout.connect(self.auto_connect_loop)
        self.timer_scan.start(1000) # 1Hz Scan

        self.auto_reconnect = True # Start auto-connecting
        self.scan_ports_only()

    def toggle_connection(self):
        if self.serial:
            # Disconnect
            self.user_disconnect()
        else:
            # Connect
            self.auto_reconnect = True
            self.scan_ports_only() # Update list first
            # Force immediate attempt?
            self.auto_connect_loop()

    def scan_ports_only(self):
        current = [p.device for p in serial.tools.list_ports.comports()]
        self.cb_port.clear()
        self.cb_port.addItems(current)
        # Select usbmodem if available and nothing selected
        for i, name in enumerate(current):
            if "usbmodem" in name:
                self.cb_port.setCurrentIndex(i)
                break

    def auto_connect_loop(self):
        if not self.auto_reconnect:
            return

        # If already connected, check if alive
        if self.serial and self.serial.is_open:
            # Check physical presence
            current = [p.device for p in serial.tools.list_ports.comports()]
            if self.serial.port not in current:
                self.log.append(f"Physical Disconnect: {self.serial.port}")
                self.disconnect_serial(manual=False) # Keep auto-reconnect ON
            return

        # Not connected, try to connect
        # Prioritize selected port from combo box if available, otherwise usbmodem
        target = self.cb_port.currentText()
        if not target and self.cb_port.count() > 0:
            target = self.cb_port.itemText(0)
            
        current_safe = [p.device for p in serial.tools.list_ports.comports()] # Re-scan? Lightweight
        
        # If target in list, connect
        if target and target in current_safe:
             self.connect_serial(target)
        else:
            # Auto-find usbmodem
            for p in current_safe:
                if "usbmodem" in p:
                    self.connect_serial(p)
                    break

    def connect_serial(self, port):
        try:
            if self.serial: self.serial.close()
            self.serial = serial.Serial(port, 115200, timeout=0.1)
            self.update_ui_state(connected=True, port=port)
            
            # Reset DTR/RTS to restart ESP32
            self.serial.dtr = False; self.serial.rts = False
            time.sleep(0.1)
            self.serial.dtr = True; self.serial.rts = True
            time.sleep(2.0)
            
            # Start JSON stream
            self.serial.write(b'j')
            self.log.append(f"Connected to {port}. Sent 'j'.")
        except Exception as e:
            self.log.append(f"Connection Failed: {e}")
            self.disconnect_serial(manual=False) # Retry

    def user_disconnect(self):
        """Called by Disconnect button"""
        self.auto_reconnect = False
        self.disconnect_serial(manual=True)
        self.log.append("Manual Disconnect. Auto-reconnect Disabled.")

    def disconnect_serial(self, manual=False):
        """Internal disconnect (physical removal or error), keeps auto_reconnect state."""
        if self.serial:
            try:
                self.serial.close()
            except: pass
        self.serial = None
        self.update_ui_state(connected=False, manual=manual)

    def update_ui_state(self, connected, port="", manual=False):
        if connected:
            self.btn_conn.setText("Disconnect")
            self.btn_conn.setStyleSheet("background-color: red; color: white;")
            self.lbl_status.setText(f"Connected: {port}")
            self.lbl_status.setStyleSheet("color: #0f0; font-weight: bold;")
        else:
            self.btn_conn.setText("Connect Auto" if self.auto_reconnect else "Connect")
            self.btn_conn.setStyleSheet("background-color: green; color: white;")
            state = "Disconnected (Manual)" if manual else "Disconnected (Scanning...)"
            self.lbl_status.setText(state)
            self.lbl_status.setStyleSheet("color: #f00; font-weight: bold;")

    def update_serial(self):
        if not self.serial or not self.serial.is_open: return
        try:
            if self.serial.in_waiting:
                chunk = self.serial.read(self.serial.in_waiting).decode(errors='ignore')
                self.buffer += chunk
                while '\n' in self.buffer:
                    line, self.buffer = self.buffer.split('\n', 1)
                    line = line.strip()
                    # ... processing ...
                    if not line: continue
                    
                    if line.startswith('D:'):
                        try:
                            # D:Raw,Cal
                            parts = line[2:].split(',')
                            if len(parts) >= 2:
                                raw = float(parts[0])
                                cal = float(parts[1])
                                self.lbl_vr.setText(f"Deg: {cal:.1f} (Raw: {raw:.1f})")
                        except: pass
                    elif "CAL_OK_HORIZON" in line:
                         self.log.append("CALIBRATION SUCCESSFUL! (Horizon=0)")
                         self.btn_cal.setText("CALIBRATED! (OK)")
                         self.btn_cal.setStyleSheet("background-color: #00ff00; color: black; font-weight: bold;")
                         QTimer.singleShot(2000, lambda: self.reset_cal_btn())
                    elif "CAL_CLEARED" in line:
                         self.log.append("Calibration CLEARED.")
                         self.reset_cal_btn()
                    elif line.startswith('{'):
                        self.log.append(f"JSON: {line[:50]}...") # Log start of JSON
                        try:
                            data = json.loads(line)
                            if 'raw' in data:
                                self.lbl_vr.setText(f"Deg: {data['deg']:.1f} (Raw: {data['raw']:.2f})")
                            elif 'vr' in data: # Backward compat or if Unit 1
                                self.lbl_vr.setText(f"VR: {data['vr']}")
                        except Exception as e:
                            self.log.append(f"JSON Error: {e}")
                    else:
                        self.log.append(line)
                        sb = self.log.verticalScrollBar()
                        sb.setValue(sb.maximum())
        except Exception as e:
            self.log.append(f"Read Error: {e}")
            self.disconnect_serial()

    def on_slider(self, val):
        self.lbl_val.setText(str(val))
        self.send(f"POS,{val},0")

    def set_pitch(self, val):
        self.slider.setValue(val) 

    def reset_cal_btn(self):
        self.btn_cal.setText("CALIBRATE (Horizon)")
        self.btn_cal.setStyleSheet("background-color: blue; color: white; font-weight: bold;")

    def calibrate(self):
        self.send("SET_HORIZON")

    def clear_cal(self):
        self.send("CLEAR_CAL")

    def stop_motor(self):
        self.send("S") # Unit 1 S -> PV,0.0

    def send(self, cmd):
        if self.serial:
            self.serial.write(f"{cmd}\n".encode())
            self.log.append(f"TX: {cmd}")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = PitchDiag()
    win.show()
    sys.exit(app.exec())
