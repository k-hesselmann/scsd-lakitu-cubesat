#!/usr/bin/env python3
import serial
import serial.tools.list_ports
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.patches import Circle
from collections import deque
import re

SERIAL_PORT = 'COM16'
BAUD_RATE = 115200
MAX_POINTS = 200

class GPSPlotter:
    def __init__(self, port, baudrate, max_points=100):
        self.ser = None
        self.port = port
        self.baudrate = baudrate
        self.max_points = max_points
        self.no_data_count = 0
        self.parse_error_count = 0

        self.latitude = deque(maxlen=max_points)
        self.longitude = deque(maxlen=max_points)
        self.altitude = deque(maxlen=max_points)
        self.speed = deque(maxlen=max_points)
        self.satellites = deque(maxlen=max_points)
        self.fix_type = deque(maxlen=max_points)

        self.gps_ok = False
        self.current_lat = 0.0
        self.current_lon = 0.0
        self.current_alt = 0.0
        self.current_speed = 0.0
        self.current_sat = 0
        self.current_fix = 0
        self.last_raw_line = ""

        self.sample_num = deque(maxlen=max_points)
        self.sample_count = 0

        # Single Figure with 2 rows, 2 columns
        self.fig, self.axes = plt.subplots(2, 2, figsize=(14, 10))
        self.fig.suptitle('GPS Data Dashboard', fontsize=14, fontweight='bold')

        self.ax_lat = self.axes[0, 0]
        self.ax_lon = self.axes[0, 1]
        self.ax_alt = self.axes[1, 0]
        self.ax_speed = self.axes[1, 1]

        self.open_serial()

    def list_serial_ports(self):
        """List all available serial ports"""
        print("\n=== Available Serial Ports ===")
        ports = serial.tools.list_ports.comports()
        if not ports:
            print("No serial ports found!")
        else:
            for port in ports:
                print(f"  {port.device}: {port.description}")
        print("==============================\n")

    def open_serial(self):
        self.list_serial_ports()
        try:
            print(f"Attempting to connect to {self.port} at {self.baudrate} baud...")
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"✓ Connected to {self.port}")
            print("Waiting for GPS data (format: LAT=... LON=... ALT=... SPEED=... SAT=... FIX=... GPS_OK=...)\n")
            return True
        except Exception as e:
            print(f"✗ Error opening serial port: {e}")
            print(f"  Make sure {self.port} is correct. Check 'Available Serial Ports' above.\n")
            return False

    def parse_gps_data(self, line):
        pattern = r'LAT=([-\d.]+)\s+LON=([-\d.]+)\s+ALT=([-\d.]+)\s+SPEED=([-\d.]+)\s+SAT=(\d+)\s+FIX=(\d+)\s+GPS_OK=(\d)'
        match = re.search(pattern, line)
        if match:
            try:
                return {
                    'latitude': float(match.group(1)),
                    'longitude': float(match.group(2)),
                    'altitude': float(match.group(3)),
                    'speed': float(match.group(4)),
                    'satellites': int(match.group(5)),
                    'fix_type': int(match.group(6)),
                    'gps_ok': int(match.group(7))
                }
            except ValueError as e:
                return None
        return None

    def update(self, frame):
        if self.ser is None or not self.ser.is_open:
            self.no_data_count += 1
            if self.no_data_count % 20 == 0:  # Print every 20 frames
                print(f"⚠ Serial port not open!")
            return

        try:
            if self.ser.in_waiting:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    self.last_raw_line = line
                    self.no_data_count = 0

                    if len(line) > 20:
                        data = self.parse_gps_data(line)
                        if data:
                            self.latitude.append(data['latitude'])
                            self.longitude.append(data['longitude'])
                            self.altitude.append(data['altitude'])
                            self.speed.append(data['speed'])
                            self.satellites.append(data['satellites'])
                            self.fix_type.append(data['fix_type'])
                            self.gps_ok = data['gps_ok']

                            self.current_lat = data['latitude']
                            self.current_lon = data['longitude']
                            self.current_alt = data['altitude']
                            self.current_speed = data['speed']
                            self.current_sat = data['satellites']
                            self.current_fix = data['fix_type']

                            self.sample_num.append(self.sample_count)
                            self.sample_count += 1

                            print(f"✓ GPS Data #{self.sample_count}: Lat={data['latitude']:.6f}, Lon={data['longitude']:.6f}, Sats={data['satellites']}, Fix={data['fix_type']}, OK={data['gps_ok']}")
                            self.parse_error_count = 0
                        else:
                            self.parse_error_count += 1
                            if self.parse_error_count == 1:
                                print(f"⚠ Parse error on line: {line[:100]}")
                                print(f"  Expected format: LAT=X.X LON=X.X ALT=X.X SPEED=X.X SAT=X FIX=X GPS_OK=X")
            else:
                self.no_data_count += 1
                if self.no_data_count == 200:  # ~10 seconds without data
                    print(f"⚠ No data received for 10 seconds on {self.port}")
                    print(f"  - Is the firmware running?")
                    print(f"  - Is GPS debug enabled (CDH_Debug_PrintGPS)?")
                    print(f"  - Check the correct COM port\n")
                    self.no_data_count = 0

        except Exception as e:
            print(f"✗ Serial read error: {e}")

        self.plot_data()

    def add_status_indicator(self, ax, is_ok):
        """Add a colored circle indicator (green=OK, red=error)"""
        color = 'green' if is_ok else 'red'
        circle = Circle((0.95, 0.95), 0.03, transform=ax.transAxes,
                       color=color, zorder=10, ec='black', linewidth=2)
        ax.add_patch(circle)

    def plot_data(self):
        if len(self.sample_num) == 0:
            # Show waiting message
            self.ax_lat.clear()
            self.ax_lon.clear()
            self.ax_alt.clear()
            self.ax_speed.clear()

            self.ax_lat.text(0.5, 0.5, 'WAITING FOR GPS DATA...',
                            ha='center', va='center', fontsize=14,
                            transform=self.ax_lat.transAxes, color='red', weight='bold')
            self.ax_lat.set_title('Latitude', fontsize=11, fontweight='bold')

            self.ax_lon.text(0.5, 0.5, f'Last raw line:\n{self.last_raw_line[:60]}...',
                            ha='center', va='center', fontsize=10,
                            transform=self.ax_lon.transAxes, family='monospace')
            self.ax_lon.set_title('Longitude', fontsize=11, fontweight='bold')

            plt.tight_layout()
            return

        sample_list = list(self.sample_num)

        # Clear all plots
        self.ax_lat.clear()
        self.ax_lon.clear()
        self.ax_alt.clear()
        self.ax_speed.clear()

        # Add large status indicator and info in figure
        gps_color = 'green' if self.gps_ok else 'red'

        # Status indicator and current values
        self.fig.text(0.12, 0.48, '●', fontsize=60, color=gps_color, ha='center', va='center', weight='bold')
        self.fig.text(0.12, 0.40, 'GPS', fontsize=11, ha='center', va='center', weight='bold')

        # Current GPS Info Box (top right)
        info_text = f'''Current GPS Info:
Lat: {self.current_lat:.6f}°
Lon: {self.current_lon:.6f}°
Alt: {self.current_alt:.2f} m
Speed: {self.current_speed:.2f} m/s
Satellites: {self.current_sat}
Fix Type: {self.current_fix} (0=None, 1=DR, 2=2D, 3=3D)
Status: {"✓ OK" if self.gps_ok else "✗ NO FIX"}
Samples: {self.sample_count}'''

        self.fig.text(0.72, 0.75, info_text, fontsize=9, ha='left', va='top',
                     bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5),
                     family='monospace')

        # ===== Latitude Plot =====
        self.ax_lat.plot(sample_list, list(self.latitude), label='Latitude', color='blue', linewidth=2.5)
        self.ax_lat.set_ylabel('Latitude (°)', fontsize=10)
        self.ax_lat.set_title('Latitude Over Time', fontsize=11, fontweight='bold')
        self.ax_lat.grid(True, alpha=0.3)
        self.ax_lat.legend(loc='upper right', fontsize=9)
        self.add_status_indicator(self.ax_lat, self.gps_ok)

        # ===== Longitude Plot =====
        self.ax_lon.plot(sample_list, list(self.longitude), label='Longitude', color='green', linewidth=2.5)
        self.ax_lon.set_ylabel('Longitude (°)', fontsize=10)
        self.ax_lon.set_title('Longitude Over Time', fontsize=11, fontweight='bold')
        self.ax_lon.grid(True, alpha=0.3)
        self.ax_lon.legend(loc='upper right', fontsize=9)
        self.add_status_indicator(self.ax_lon, self.gps_ok)

        # ===== Altitude Plot =====
        self.ax_alt.plot(sample_list, list(self.altitude), label='Altitude', color='orange', linewidth=2.5)
        self.ax_alt.set_xlabel('Sample Number', fontsize=10)
        self.ax_alt.set_ylabel('Altitude (m)', fontsize=10)
        self.ax_alt.set_title('Altitude Over Time', fontsize=11, fontweight='bold')
        self.ax_alt.grid(True, alpha=0.3)
        self.ax_alt.legend(loc='upper right', fontsize=9)
        self.add_status_indicator(self.ax_alt, self.gps_ok)

        # ===== Speed Plot =====
        self.ax_speed.plot(sample_list, list(self.speed), label='Speed', color='red', linewidth=2.5)
        self.ax_speed.set_xlabel('Sample Number', fontsize=10)
        self.ax_speed.set_ylabel('Speed (m/s)', fontsize=10)
        self.ax_speed.set_title('Speed Over Time', fontsize=11, fontweight='bold')
        self.ax_speed.grid(True, alpha=0.3)
        self.ax_speed.legend(loc='upper right', fontsize=9)
        self.add_status_indicator(self.ax_speed, self.gps_ok)

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
    plotter = GPSPlotter(SERIAL_PORT, BAUD_RATE, MAX_POINTS)
    plotter.run()
