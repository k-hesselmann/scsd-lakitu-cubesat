#!/usr/bin/env python3
import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from collections import deque
import re
import numpy as np

SERIAL_PORT = 'COM16'
BAUD_RATE = 115200
MAX_POINTS = 200

class IMUPlotter:
    def __init__(self, port, baudrate, max_points=100):
        self.ser = None
        self.port = port
        self.baudrate = baudrate
        self.max_points = max_points

        self.accel_x = deque(maxlen=max_points)
        self.accel_y = deque(maxlen=max_points)
        self.accel_z = deque(maxlen=max_points)
        self.gyro_x = deque(maxlen=max_points)
        self.gyro_y = deque(maxlen=max_points)
        self.gyro_z = deque(maxlen=max_points)

        self.pressure = deque(maxlen=max_points)
        self.temperature = deque(maxlen=max_points)
        self.altitude = deque(maxlen=max_points)

        self.sample_num = deque(maxlen=max_points)
        self.sample_count = 0

        # IMU Figure
        self.fig_imu, (self.ax1, self.ax2) = plt.subplots(2, 1, figsize=(12, 8))
        self.fig_imu.suptitle('IMU Data - Accelerometer & Gyroscope')

        # Altimeter Figure
        self.fig_alt = plt.figure(figsize=(12, 6))
        self.ax3 = self.fig_alt.add_subplot(111)
        self.fig_alt.suptitle('Altimeter Data - MS5607')

        self.open_serial()

    def open_serial(self):
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"Connected to {self.port} at {self.baudrate} baud")
            return True
        except Exception as e:
            print(f"Error opening serial port: {e}")
            return False

    def parse_imu_data(self, line):
        pattern = r'AX=([-\d.]+)\s+AY=([-\d.]+)\s+AZ=([-\d.]+)\s*\|\s*GX=([-\d.]+)\s+GY=([-\d.]+)\s+GZ=([-\d.]+)\s*\|\s*P=([-\d.]+)\s+T=([-\d.]+)\s+A=([-\d.]+)'
        match = re.search(pattern, line)
        if match:
            try:
                return {
                    'ax': float(match.group(1)),
                    'ay': float(match.group(2)),
                    'az': float(match.group(3)),
                    'gx': float(match.group(4)),
                    'gy': float(match.group(5)),
                    'gz': float(match.group(6)),
                    'pressure': float(match.group(7)),
                    'temperature': float(match.group(8)),
                    'altitude': float(match.group(9))
                }
            except ValueError as e:
                print(f"Parse error: {e}")
                return None
        return None

    def update(self, frame):
        if self.ser is None or not self.ser.is_open:
            return

        try:
            if self.ser.in_waiting:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if line and len(line) > 20:  # Filter out partial lines
                    data = self.parse_imu_data(line)
                    if data:
                        self.accel_x.append(data['ax'])
                        self.accel_y.append(data['ay'])
                        self.accel_z.append(data['az'])
                        self.gyro_x.append(data['gx'])
                        self.gyro_y.append(data['gy'])
                        self.gyro_z.append(data['gz'])
                        self.pressure.append(data['pressure'])
                        self.temperature.append(data['temperature'])
                        self.altitude.append(data['altitude'])
                        self.sample_num.append(self.sample_count)
                        self.sample_count += 1
        except Exception as e:
            print(f"Error: {e}")

        self.plot_imu()
        self.plot_altimeter()

    def plot_imu(self):
        self.ax1.clear()
        self.ax2.clear()

        if len(self.sample_num) > 0:
            sample_list = list(self.sample_num)

            # Plot accelerometer
            self.ax1.plot(sample_list, list(self.accel_x), label='AX', color='red', linewidth=2, marker='.')
            self.ax1.plot(sample_list, list(self.accel_y), label='AY', color='green', linewidth=2, marker='.')
            self.ax1.plot(sample_list, list(self.accel_z), label='AZ', color='blue', linewidth=2, marker='.')
            self.ax1.set_ylabel('Acceleration (g)', fontsize=10)
            self.ax1.set_title('Accelerometer', fontsize=12)
            self.ax1.legend(loc='upper left')
            self.ax1.grid(True, alpha=0.3)
            self.ax1.set_ylim([-3, 3])

            # Plot gyroscope
            self.ax2.plot(sample_list, list(self.gyro_x), label='GX', color='red', linewidth=2, marker='.')
            self.ax2.plot(sample_list, list(self.gyro_y), label='GY', color='green', linewidth=2, marker='.')
            self.ax2.plot(sample_list, list(self.gyro_z), label='GZ', color='blue', linewidth=2, marker='.')
            self.ax2.set_xlabel('Sample Number', fontsize=10)
            self.ax2.set_ylabel('Angular Velocity (°/s)', fontsize=10)
            self.ax2.set_title('Gyroscope', fontsize=12)
            self.ax2.legend(loc='upper left')
            self.ax2.grid(True, alpha=0.3)
            self.ax2.set_ylim([-500, 500])

    def plot_altimeter(self):
        self.ax3.clear()

        if len(self.sample_num) > 0:
            sample_list = list(self.sample_num)

            # Create dual-axis plot
            ax3_alt = self.ax3
            ax3_pres = ax3_alt.twinx()

            line1 = ax3_pres.plot(sample_list, list(self.pressure), label='Pressure', color='purple', linewidth=2, marker='.')
            line2 = ax3_pres.plot(sample_list, list(self.temperature), label='Temperature', color='orange', linewidth=2, marker='.')
            line3 = ax3_alt.plot(sample_list, list(self.altitude), label='Altitude', color='brown', linewidth=2.5, marker='o')

            ax3_pres.set_xlabel('Sample Number', fontsize=10)
            ax3_pres.set_ylabel('Pressure (hPa) / Temp (°C)', fontsize=10)
            ax3_alt.set_ylabel('Altitude (m)', fontsize=10, color='brown')
            ax3_alt.grid(True, alpha=0.3)

            ax3_pres.tick_params(axis='y', labelcolor='black')
            ax3_alt.tick_params(axis='y', labelcolor='brown')

            lines = line1 + line2 + line3
            labels = [l.get_label() for l in lines]
            ax3_pres.legend(lines, labels, loc='upper left', fontsize=10)

    def run(self):
        ani1 = FuncAnimation(self.fig_imu, self.update, interval=50, cache_frame_data=False)
        ani2 = FuncAnimation(self.fig_alt, lambda x: None, interval=50, cache_frame_data=False)

        plt.show()
        self.close()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("Serial connection closed")

if __name__ == '__main__':
    plotter = IMUPlotter(SERIAL_PORT, BAUD_RATE, MAX_POINTS)
    plotter.run()
