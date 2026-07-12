#!/usr/bin/env python3
import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from collections import deque
import re

SERIAL_PORT = 'COM16'
BAUD_RATE = 115200
MAX_POINTS = 100

accel_data = {'x': deque(maxlen=MAX_POINTS), 'y': deque(maxlen=MAX_POINTS), 'z': deque(maxlen=MAX_POINTS)}
gyro_data = {'x': deque(maxlen=MAX_POINTS), 'y': deque(maxlen=MAX_POINTS), 'z': deque(maxlen=MAX_POINTS)}
time_data = deque(maxlen=MAX_POINTS)

fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))
fig.suptitle('IMU Data - Accelerometer & Gyroscope')

ser = None

def open_serial():
    global ser
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print(f"Connected to {SERIAL_PORT} at {BAUD_RATE} baud")
        return True
    except Exception as e:
        print(f"Error opening serial port: {e}")
        return False

def parse_imu_data(line):
    pattern = r'AX=([-\d.]+)\s+AY=([-\d.]+)\s+AZ=([-\d.]+)\s*\|\s*GX=([-\d.]+)\s+GY=([-\d.]+)\s+GZ=([-\d.]+)\s*\|\s*Out=(\d)'
    match = re.search(pattern, line)
    if match:
        return {
            'ax': float(match.group(1)),
            'ay': float(match.group(2)),
            'az': float(match.group(3)),
            'gx': float(match.group(4)),
            'gy': float(match.group(5)),
            'gz': float(match.group(6)),
            'outdated': int(match.group(7))
        }
    return None

def update_plot(frame):
    if ser is None or not ser.is_open:
        return

    try:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                data = parse_imu_data(line)
                if data:
                    accel_data['x'].append(data['ax'])
                    accel_data['y'].append(data['ay'])
                    accel_data['z'].append(data['az'])
                    gyro_data['x'].append(data['gx'])
                    gyro_data['y'].append(data['gy'])
                    gyro_data['z'].append(data['gz'])
                    time_data.append(len(time_data))
    except Exception as e:
        print(f"Error reading serial: {e}")

    ax1.clear()
    ax2.clear()

    if len(time_data) > 0:
        time_list = list(time_data)

        ax1.plot(time_list, list(accel_data['x']), label='AX', color='red', linewidth=2)
        ax1.plot(time_list, list(accel_data['y']), label='AY', color='green', linewidth=2)
        ax1.plot(time_list, list(accel_data['z']), label='AZ', color='blue', linewidth=2)
        ax1.set_ylabel('Acceleration (g)')
        ax1.set_title('Accelerometer')
        ax1.legend(loc='upper left')
        ax1.grid(True, alpha=0.3)
        ax1.set_ylim([-2, 2])

        ax2.plot(time_list, list(gyro_data['x']), label='GX', color='red', linewidth=2)
        ax2.plot(time_list, list(gyro_data['y']), label='GY', color='green', linewidth=2)
        ax2.plot(time_list, list(gyro_data['z']), label='GZ', color='blue', linewidth=2)
        ax2.set_xlabel('Sample Number')
        ax2.set_ylabel('Angular Velocity (°/s)')
        ax2.set_title('Gyroscope')
        ax2.legend(loc='upper left')
        ax2.grid(True, alpha=0.3)
        ax2.set_ylim([-500, 500])

if __name__ == '__main__':
    if not open_serial():
        exit(1)

    ani = FuncAnimation(fig, update_plot, interval=50, cache_frame_data=False)
    plt.tight_layout()
    plt.show()

    if ser and ser.is_open:
        ser.close()
