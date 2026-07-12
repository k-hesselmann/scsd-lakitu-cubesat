#!/usr/bin/env python3
import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.patches import Circle
from collections import deque
import re

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

        self.imu_ok = False
        self.alt_ok = False

        self.sample_num = deque(maxlen=max_points)
        self.sample_count = 0

        # Single Figure with 3 rows, 2 columns
        self.fig, self.axes = plt.subplots(3, 2, figsize=(14, 10))
        self.fig.suptitle('IMU & Altimeter Data', fontsize=14, fontweight='bold')

        self.ax_accel = self.axes[0, 0]
        self.ax_gyro = self.axes[1, 0]
        self.ax_pressure = self.axes[0, 1]
        self.ax_temp = self.axes[1, 1]
        self.ax_alt = self.axes[2, 1]

        # Hide the bottom-left plot
        self.axes[2, 0].set_visible(False)

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
        pattern = r'AX=([-\d.]+)\s+AY=([-\d.]+)\s+AZ=([-\d.]+)\s*\|\s*GX=([-\d.]+)\s+GY=([-\d.]+)\s+GZ=([-\d.]+)\s*\|\s*P=([-\d.]+)\s+T=([-\d.]+)\s+A=([-\d.]+)\s*\|\s*IMU_OK=(\d)\s+ALT_OK=(\d)'
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
                    'altitude': float(match.group(9)),
                    'imu_ok': int(match.group(10)),
                    'alt_ok': int(match.group(11))
                }
            except ValueError as e:
                return None
        return None

    def update(self, frame):
        if self.ser is None or not self.ser.is_open:
            return

        try:
            if self.ser.in_waiting:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if line and len(line) > 20:
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
                        self.imu_ok = data['imu_ok']
                        self.alt_ok = data['alt_ok']
                        self.sample_num.append(self.sample_count)
                        self.sample_count += 1
        except Exception as e:
            pass

        self.plot_data()

    def add_status_indicator(self, ax, is_ok):
        """Add a colored circle indicator (green=OK, red=error)"""
        color = 'green' if is_ok else 'red'
        circle = Circle((0.95, 0.95), 0.03, transform=ax.transAxes,
                       color=color, zorder=10, ec='black', linewidth=2)
        ax.add_patch(circle)

    def plot_data(self):
        if len(self.sample_num) == 0:
            return

        sample_list = list(self.sample_num)

        # Clear all plots
        self.ax_accel.clear()
        self.ax_gyro.clear()
        self.ax_pressure.clear()
        self.ax_temp.clear()
        self.ax_alt.clear()

        # Add large status indicator circles in bottom left
        imu_color = 'green' if self.imu_ok else 'red'
        alt_color = 'green' if self.alt_ok else 'red'

        # Add text annotations with large colored circles (bottom left)
        self.fig.text(0.08, 0.08, '●', fontsize=50, color=imu_color, ha='center', va='center', weight='bold')
        self.fig.text(0.08, 0.02, 'IMU', fontsize=11, ha='center', va='center', weight='bold')

        self.fig.text(0.18, 0.08, '●', fontsize=50, color=alt_color, ha='center', va='center', weight='bold')
        self.fig.text(0.18, 0.02, 'ALT', fontsize=11, ha='center', va='center', weight='bold')

        # ===== LEFT COLUMN: IMU =====
        # Accelerometer
        self.ax_accel.plot(sample_list, list(self.accel_x), label='AX', color='red', linewidth=2)
        self.ax_accel.plot(sample_list, list(self.accel_y), label='AY', color='green', linewidth=2)
        self.ax_accel.plot(sample_list, list(self.accel_z), label='AZ', color='blue', linewidth=2)
        self.ax_accel.set_ylabel('Acceleration (g)', fontsize=10)
        self.ax_accel.set_title('Accelerometer', fontsize=11, fontweight='bold')
        self.ax_accel.legend(loc='upper right', fontsize=9)
        self.ax_accel.grid(True, alpha=0.3)
        self.ax_accel.set_ylim([-3, 3])
        self.add_status_indicator(self.ax_accel, self.imu_ok)

        # Gyroscope
        self.ax_gyro.plot(sample_list, list(self.gyro_x), label='GX', color='red', linewidth=2)
        self.ax_gyro.plot(sample_list, list(self.gyro_y), label='GY', color='green', linewidth=2)
        self.ax_gyro.plot(sample_list, list(self.gyro_z), label='GZ', color='blue', linewidth=2)
        self.ax_gyro.set_xlabel('Sample Number', fontsize=10)
        self.ax_gyro.set_ylabel('Angular Velocity (°/s)', fontsize=10)
        self.ax_gyro.set_title('Gyroscope', fontsize=11, fontweight='bold')
        self.ax_gyro.legend(loc='upper right', fontsize=9)
        self.ax_gyro.grid(True, alpha=0.3)
        self.ax_gyro.set_ylim([-500, 500])
        self.add_status_indicator(self.ax_gyro, self.imu_ok)

        # ===== RIGHT COLUMN: ALTIMETER =====
        # Pressure
        self.ax_pressure.plot(sample_list, list(self.pressure), label='Pressure', color='purple', linewidth=2.5)
        self.ax_pressure.set_ylabel('Pressure (hPa)', fontsize=10, color='purple')
        self.ax_pressure.set_title('Pressure', fontsize=11, fontweight='bold')
        self.ax_pressure.tick_params(axis='y', labelcolor='purple')
        self.ax_pressure.grid(True, alpha=0.3)
        self.ax_pressure.legend(loc='upper right', fontsize=9)
        self.add_status_indicator(self.ax_pressure, self.alt_ok)

        # Temperature
        self.ax_temp.plot(sample_list, list(self.temperature), label='Temperature', color='orange', linewidth=2.5)
        self.ax_temp.set_ylabel('Temperature (°C)', fontsize=10, color='orange')
        self.ax_temp.set_title('Temperature', fontsize=11, fontweight='bold')
        self.ax_temp.tick_params(axis='y', labelcolor='orange')
        self.ax_temp.grid(True, alpha=0.3)
        self.ax_temp.legend(loc='upper right', fontsize=9)
        self.add_status_indicator(self.ax_temp, self.alt_ok)

        # Altitude
        self.ax_alt.plot(sample_list, list(self.altitude), label='Altitude', color='brown', linewidth=2.5)
        self.ax_alt.set_xlabel('Sample Number', fontsize=10)
        self.ax_alt.set_ylabel('Altitude (m)', fontsize=10, color='brown')
        self.ax_alt.set_title('Altitude', fontsize=11, fontweight='bold')
        self.ax_alt.tick_params(axis='y', labelcolor='brown')
        self.ax_alt.grid(True, alpha=0.3)
        self.ax_alt.legend(loc='upper right', fontsize=9)
        self.add_status_indicator(self.ax_alt, self.alt_ok)

        plt.tight_layout()

    def run(self):
        ani = FuncAnimation(self.fig, self.update, interval=50, cache_frame_data=False)
        plt.show()
        self.close()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("Serial connection closed")

if __name__ == '__main__':
    plotter = IMUPlotter(SERIAL_PORT, BAUD_RATE, MAX_POINTS)
    plotter.run()
