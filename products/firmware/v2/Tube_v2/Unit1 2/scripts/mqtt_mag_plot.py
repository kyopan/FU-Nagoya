
import paho.mqtt.client as mqtt
import json
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from collections import deque
import numpy as np
import threading
import sys
import re

# MQTT Configuration
MQTT_BROKER = "192.168.1.2" # Default to typical production nanoMQ address
MQTT_PORT = 1883
TOPIC_PATTERN = "fu/device/+/debug"

# Data containers
data_x = deque(maxlen=200)
data_y = deque(maxlen=200)
data_z = deque(maxlen=200)
timestamps = deque(maxlen=200)

stats_text = "Waiting for data..."

def on_connect(client, userdata, flags, rc):
    print(f"Connected to MQTT Broker with result code {rc}")
    client.subscribe(TOPIC_PATTERN)
    print(f"Subscribed to {TOPIC_PATTERN}")

def on_message(client, userdata, msg):
    global stats_text
    try:
        # Winch v2 debug topic payload is usually raw hex string representing the serial line
        # e.g. "4D2C31322E332C..." which decodes to "M,12.3,..."
        payload = msg.payload.decode('utf-8')
        
        # Try to treat as hex first
        try:
            decoded = bytes.fromhex(payload).decode('utf-8', errors='ignore')
        except ValueError:
            # Maybe it's not hex, just raw string?
            decoded = payload
            
        decoded = decoded.strip()
        
        # Look for "M,x,y,z" pattern
        # Since it's a stream, we might get partials or multiple lines.
        # Simple regex: M, float, float, float
        match = re.search(r"M,(-?\d+\.?\d*),(-?\d+\.?\d*),(-?\d+\.?\d*)", decoded)
        if match:
            x = float(match.group(1))
            y = float(match.group(2))
            z = float(match.group(3))
            
            data_x.append(x)
            data_y.append(y)
            data_z.append(z)
            timestamps.append(len(timestamps))
            
            # Print to console as well
            # print(f"MAG: {x}, {y}, {z}")

            if len(data_x) > 10:
                std_x = np.std(data_x)
                std_y = np.std(data_y)
                std_z = np.std(data_z)
                stats_text = (f"Std Dev (last {len(data_x)}):\n"
                              f"X: {std_x:.2f} uT\n"
                              f"Y: {std_y:.2f} uT\n"
                              f"Z: {std_z:.2f} uT")

    except Exception as e:
        print(f"Error processing message: {e}")

def mqtt_thread():
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        client.loop_forever()
    except Exception as e:
        print(f"MQTT Connection Failed: {e}")
        sys.exit(1)

def update(frame):
    plt.cla()
    plt.plot(timestamps, data_x, label='Mag X', color='r')
    plt.plot(timestamps, data_y, label='Mag Y', color='g')
    plt.plot(timestamps, data_z, label='Mag Z', color='b')
    plt.legend(loc='upper left')
    plt.title("Magnetometer Data via MQTT (Winch Relay)")
    plt.xlabel("Samples")
    plt.ylabel("Field Strength (uT)")
    plt.grid(True)
    
    props = dict(boxstyle='round', facecolor='wheat', alpha=0.5)
    plt.text(0.02, 0.05, stats_text, transform=plt.gca().transAxes, fontsize=10,
            verticalalignment='bottom', bbox=props)

if __name__ == "__main__":
    print("Starting MQTT Magnetometer Monitor...")
    print(f"Target Broker: {MQTT_BROKER}")
    
    t = threading.Thread(target=mqtt_thread, daemon=True)
    t.start()
    
    ani = FuncAnimation(plt.gcf(), update, interval=100)
    plt.show()
