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

class BaroPlotter:
    def __init__(self, port, baudrate, max_points=100):
        self.ser = None
        self.port = port
        self.baudrate = baudrate
        self.max_points = max_points

        self.pressure = deque(maxlen=max_points)
        self.temperature = deque(maxlen=max_points)
        self.altitude = deque(maxlen=max_points)

        self.baro_ok = False
        self.current_press = 0.0
        self.current_temp = 0.0
        self.current_alt = 0.0

        self.sample_num = deque(maxlen=max_points)
        self.sample_count = 0

        # Single Figure with 1 row, 3 columns
        self.fig, self.axes = plt.subplots(1, 3, figsize=(16, 5))
        self.fig.suptitle('Barometer Data Dashboard', fontsize=14, fontweight='bold')

        self.ax_press = self.axes[0]
        self.ax_temp = self.axes[1]
        self.ax_alt = self.axes[2]

        self.open_serial()

    def open_serial(self):
        try:
            print(f"Connecting to {self.port} at {self.baudrate} baud...")
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"✓ Connected to {self.port}")
            print("Waiting for barometer data (format: PRESS=... TEMP=... ALT=... BARO_OK=...)\n")
            return True
        except Exception as e:
            print(f"✗ Error opening serial port: {e}\n")
            return False

    def parse_baro_data(self, line):
        pattern = r'PRESS=([-\d.]+)\s+TEMP=([-\d.]+)\s+ALT=([-\d.]+)\s+BARO_OK=(\d)'
        match = re.search(pattern, line)
        if match:
            try:
                return {
                    'pressure': float(match.group(1)),
                    'temperature': float(match.group(2)),
                    'altitude': float(match.group(3)),
                    'baro_ok': int(match.group(4))
                }
            except ValueError:
                return None
        return None

    def update(self, frame):
        if self.ser is None or not self.ser.is_open:
            return

        try:
            if self.ser.in_waiting:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if line and len(line) > 20:
                    data = self.parse_baro_data(line)
                    if data:
                        self.pressure.append(data['pressure'])
                        self.temperature.append(data['temperature'])
                        self.altitude.append(data['altitude'])
                        self.baro_ok = data['baro_ok']

                        self.current_press = data['pressure']
                        self.current_temp = data['temperature']
                        self.current_alt = data['altitude']

                        self.sample_num.append(self.sample_count)
                        self.sample_count += 1

                        print(f"✓ Baro Data #{self.sample_count}: Press={data['pressure']:.2f} hPa, Temp={data['temperature']:.2f}°C, Alt={data['altitude']:.2f}m, OK={data['baro_ok']}")
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
        self.ax_press.clear()
        self.ax_temp.clear()
        self.ax_alt.clear()

        # Add large status indicator
        baro_color = 'green' if self.baro_ok else 'red'
        self.fig.text(0.05, 0.75, '●', fontsize=60, color=baro_color, ha='center', va='center', weight='bold')
        self.fig.text(0.05, 0.60, 'BARO', fontsize=10, ha='center', va='center', weight='bold')

        # Current info box
        info_text = f'''Current Barometer:
Pressure: {self.current_press:.2f} hPa
Temperature: {self.current_temp:.2f} °C
Altitude: {self.current_alt:.2f} m
Status: {"✓ OK" if self.baro_ok else "✗ ERROR"}
Samples: {self.sample_count}'''

        self.fig.text(0.92, 0.75, info_text, fontsize=9, ha='right', va='top',
                     bbox=dict(boxstyle='round', facecolor='lightblue', alpha=0.5),
                     family='monospace')

        # ===== Pressure Plot =====
        self.ax_press.plot(sample_list, list(self.pressure), label='Pressure', color='blue', linewidth=2.5)
        self.ax_press.set_ylabel('Pressure (hPa)', fontsize=10)
        self.ax_press.set_title('Pressure Over Time', fontsize=11, fontweight='bold')
        self.ax_press.grid(True, alpha=0.3)
        self.ax_press.legend(loc='upper right', fontsize=9)
        self.add_status_indicator(self.ax_press, self.baro_ok)

        # ===== Temperature Plot =====
        self.ax_temp.plot(sample_list, list(self.temperature), label='Temperature', color='red', linewidth=2.5)
        self.ax_temp.set_ylabel('Temperature (°C)', fontsize=10)
        self.ax_temp.set_title('Temperature Over Time', fontsize=11, fontweight='bold')
        self.ax_temp.grid(True, alpha=0.3)
        self.ax_temp.legend(loc='upper right', fontsize=9)
        self.add_status_indicator(self.ax_temp, self.baro_ok)

        # ===== Altitude Plot =====
        self.ax_alt.plot(sample_list, list(self.altitude), label='Altitude', color='green', linewidth=2.5)
        self.ax_alt.set_ylabel('Altitude (m)', fontsize=10)
        self.ax_alt.set_title('Altitude Over Time', fontsize=11, fontweight='bold')
        self.ax_alt.grid(True, alpha=0.3)
        self.ax_alt.legend(loc='upper right', fontsize=9)
        self.add_status_indicator(self.ax_alt, self.baro_ok)

        plt.tight_layout(rect=[0, 0, 1, 0.96])

    def run(self):
        ani = FuncAnimation(self.fig, self.update, interval=50, cache_frame_data=False)
        plt.show()
        self.close()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("Serial connection closed")

if __name__ == '__main__':
    plotter = BaroPlotter(SERIAL_PORT, BAUD_RATE, MAX_POINTS)
    plotter.run()
