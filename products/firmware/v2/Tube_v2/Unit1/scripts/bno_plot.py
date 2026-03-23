import serial
import serial.tools.list_ports
import json
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import threading
import time
import numpy as np
from collections import deque

# Configuration
BAUD_RATE = 115200
MAX_POINTS = 200

# Data containers
data_x = deque(maxlen=MAX_POINTS)
data_y = deque(maxlen=MAX_POINTS)
data_z = deque(maxlen=MAX_POINTS)
timestamps = deque(maxlen=MAX_POINTS)

# Statistics
stats_text = ""

running = True

def find_port():
    ports = list(serial.tools.list_ports.comports())
    # Filter for typical ESP32 chips (CP210x, CH340, generic USB Serial)
    candidates = [p.device for p in ports if 'usb' in p.device.lower()]
    if not candidates:
        return None
    # If multiple, list them and ask or pick first. For automation, pick first.
    print(f"Found ports: {candidates}")
    return candidates[0]

def serial_reader(port_name):
    global running, stats_text
    try:
        ser = serial.Serial(port_name, BAUD_RATE, timeout=1)
        time.sleep(2) # Wait for reset
        ser.write(b"j\n") # Enable JSON stream
        print("Sent 'j' to enable JSON stream.")
        
        while running:
            try:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line.startswith('{'):
                    try:
                        pkg = json.loads(line)
                        if 'mag_vec' in pkg:
                            mag = pkg['mag_vec']
                            data_x.append(mag['x'])
                            data_y.append(mag['y'])
                            data_z.append(mag['z'])
                            timestamps.append(len(timestamps))

                            # Calc stats
                            if len(data_x) > 10:
                                std_x = np.std(data_x)
                                std_y = np.std(data_y)
                                std_z = np.std(data_z)
                                stats_text = (f"Std Dev (last {len(data_x)}):\n"
                                              f"X: {std_x:.2f} uT\n"
                                              f"Y: {std_y:.2f} uT\n"
                                              f"Z: {std_z:.2f} uT")
                    except json.JSONDecodeError:
                        pass
            except Exception as e:
                print(f"Read error: {e}")
                
        ser.write(b"s\n") # Stop stream
        ser.close()
    except Exception as e:
        print(f"Serial connection failed: {e}")
        running = False

def update(frame):
    plt.cla()
    plt.plot(timestamps, data_x, label='Mag X', color='r')
    plt.plot(timestamps, data_y, label='Mag Y', color='g')
    plt.plot(timestamps, data_z, label='Mag Z', color='b')
    plt.legend(loc='upper left')
    plt.title("Real-time BNO085 Magnetometer Data")
    plt.xlabel("Samples")
    plt.ylabel("Field Strength (uT)")
    plt.grid(True)
    
    # Add stats text box
    props = dict(boxstyle='round', facecolor='wheat', alpha=0.5)
    plt.text(0.02, 0.05, stats_text, transform=plt.gca().transAxes, fontsize=10,
            verticalalignment='bottom', bbox=props)

if __name__ == "__main__":
    port = find_port()
    if not port:
        print("No USB serial port found. Please connect Unit 1.")
        # fallback for testing logic without serial
        # port = "TEST" 
    else:
        print(f"Connecting to {port}...")
        t = threading.Thread(target=serial_reader, args=(port,), daemon=True)
        t.start()
        
        ani = FuncAnimation(plt.gcf(), update, interval=100)
        plt.show()
        
        running = False
        t.join()
