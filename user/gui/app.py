#!/usr/bin/env python3

# GUI for temperature sensor simulator
# Features
# Controls sampling period, threshold and mode
# Read from /dev/simtemp using poll/epoll
# Live Plot of temperature
# Code by: Ricardo Berumen

import sys, os, struct, fcntl, threading, time, select
from PyQt5 import QtWidgets, QtCore
import pyqtgraph as pg
from datetime import datetime

DEV_PATH = '/dev/simtemp'
SAMPLE_STRUCT = struct.Struct('<Q i h I')
SIMTEMP_IOC_CONFIG = 0x40047301
IOCTL_STRUCT = struct.Struct('III')

# mode mapping
MODE_NAMES = {
    0: "normal",
    1: "noisy",
    2: "ramp"
}

class SimTempGUI(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("SimTemp Monitor")
        self.resize(800,400)

        # Layout
        layout = QtWidgets.QVBoxLayout(self)

        # Plot widget
        self.plot_widget = pg.PlotWidget(title="Temperature (°C)")
        self.plot_curve = self.plot_widget.plot([],[], pen='y')
        layout.addWidget(self.plot_widget)

        # Gauge layout
        gauge_layout = QtWidgets.QHBoxLayout()
        layout.addLayout(gauge_layout)

        self.temp_gauge = QtWidgets.QProgressBar()
        self.temp_gauge.setMinimum(0)
        self.temp_gauge.setMaximum(100000)  # mC max
        self.temp_gauge.setFormat("%v mC")
        self.temp_gauge.setTextVisible(True)
        gauge_layout.addWidget(QtWidgets.QLabel("Temperature Gauge:"))
        gauge_layout.addWidget(self.temp_gauge)

        # Alert label
        self.alert_label = QtWidgets.QLabel("Alert: OFF")
        self.alert_label.setStyleSheet("QLabel { color : green; font-weight: bold; }")
        gauge_layout.addWidget(self.alert_label)

        # Controls
        control_layout = QtWidgets.QHBoxLayout()
        layout.addLayout(control_layout)

        self.sampling_spin = QtWidgets.QSpinBox()
        self.sampling_spin.setRange(10,10000)
        self.sampling_spin.setValue(100)
        self.sampling_spin.setSuffix(" ms")
        control_layout.addWidget(QtWidgets.QLabel("Sampling:"))
        control_layout.addWidget(self.sampling_spin)

        self.threshold_spin = QtWidgets.QSpinBox()
        self.threshold_spin.setRange(0,100)
        self.threshold_spin.setValue(45)
        self.threshold_spin.setSuffix(" C")
        control_layout.addWidget(QtWidgets.QLabel("Threshold:"))
        control_layout.addWidget(self.threshold_spin)

        self.mode_combo = QtWidgets.QComboBox()
        self.mode_combo.addItems(["normal","noisy","ramp"])
        control_layout.addWidget(QtWidgets.QLabel("Mode:"))
        control_layout.addWidget(self.mode_combo)

        # Apply button
        self.apply_btn = QtWidgets.QPushButton("Apply")
        self.apply_btn.clicked.connect(self.apply_settings)
        control_layout.addWidget(self.apply_btn)

        # Data buffers
        self.timestamps = []
        self.temps = []

        # Open device
        try:
            self.f = os.open(DEV_PATH, os.O_RDONLY | os.O_NONBLOCK)
            self.fd = os.fdopen(self.f, 'rb')
        except FileNotFoundError:
            print(f"Device {DEV_PATH} not found")
            return

        # Start polling thread
        self.running = True
        self.thread = threading.Thread(target=self.poll_samples)
        self.thread.start()

        # Timer for GUI update
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.update_plot)
        self.timer.start(200)

        # Current threshold for gauge coloring
        self.current_threshold = self.threshold_spin.value()

    def poll_samples(self):
        poll = select.poll()
        poll.register(self.fd, select.POLLIN | select.POLLPRI)
        temp_mC = 0
        alert = 0
        while self.running:
            events = poll.poll()
            for fd_evt, flag in events:
                sample = self.read_sample()
                if sample:
                    ts_ns, temp_mC, flags, mode_str = sample
                    ts = datetime.utcfromtimestamp(ts_ns/1e9)
                    temp_c = temp_mC / 1000.0
                    alert = 1 if flags & 2 else 0
                    #print(f"{ts} temp={temp_c:.1f}C alert={alert} mode={mode_name}")
                    self.timestamps.append(ts)
                    self.temps.append(temp_c)
                    if len(self.temps) > 100:
                        self.temps.pop(0)
                        self.timestamps.pop(0)
            QtCore.QMetaObject.invokeMethod(self, "update_gui",
                QtCore.Qt.QueuedConnection, 
                QtCore.Q_ARG(int, temp_mC), QtCore.Q_ARG(bool, alert))
    def read_sample(self):
        data = self.fd.read(SAMPLE_STRUCT.size)
        if len(data) != SAMPLE_STRUCT.size:
            return None
        ts_ns, temp_mC, mode_name, flags = SAMPLE_STRUCT.unpack(data)
        ts = datetime.utcfromtimestamp(ts_ns/1e9).isoformat(timespec="milliseconds")+"Z"
        temp_c = temp_mC / 1000.0
        alert = 1 if flags & 2 else 0
        #print(f"{ts} temp={temp_c:.1f}C alert={alert} mode={mode_name}")
        mode_str = MODE_NAMES.get(mode_name, f"unknown({mode_name})")
        return ts_ns, temp_mC, flags, mode_str

    @QtCore.pyqtSlot(int,bool)
    def update_gui(self, temp_mC, alert):
        # Update gauge
        self.temp_gauge.setValue(temp_mC)
        # Color gauge red if above threshold
        if temp_mC >= self.current_threshold:
            self.temp_gauge.setStyleSheet("QProgressBar::chunk { background-color: red; }")
        else:
            self.temp_gauge.setStyleSheet("QProgressBar::chunk { background-color: green; }")
        # Update alert label
        if alert:
            self.alert_label.setText("Alert: ON")
            self.alert_label.setStyleSheet("QLabel { color : red; font-weight: bold; }")
        else:
            self.alert_label.setText("Alert: OFF")
            self.alert_label.setStyleSheet("QLabel { color : green; font-weight: bold; }")

    def update_plot(self):
        if self.timestamps:
            x = [t.timestamp() for t in self.timestamps]
            self.plot_curve.setData(x,self.temps)

    def apply_settings(self):
        sampling = self.sampling_spin.value()
        threshold = self.threshold_spin.value()*1000
        mode = self.mode_combo.currentIndex()
        if sampling is not None:
            with open("/sys/class/misc/simtemp/sampling_ms","w") as f:
                f.write(f"{sampling}\n")
        if threshold is not None:
            with open("/sys/class/misc/simtemp/threshold_mC", "w") as f:
                f.write(f"{threshold}\n")
        if mode is not None:
            # Accept either "normal"/"noisy"/"ramp" or numeric (0/1/2)
            if isinstance(mode, int):
                if mode not in MODE_NAMES:
                    raise ValueError(f"Invalid mode code {mode}")
                mode_str = MODE_NAMES[mode]
            else:
                mode_str = str(mode).lower()
                if mode_str not in MODE_NAMES.values():
                    raise ValueError(f"Invalid mode '{mode_str}'. Must be one of {list(MODE_NAMES.values())}")
            with open("/sys/class/misc/simtemp/mode", "w") as f:
                f.write(f"{mode_str}\n")
        self.current_threshold = threshold

    def closeEvent(self, event):
        self.running = False
        self.thread.join()
        self.fd.close()
        event.accept()

# -----------------------------
# Main
# -----------------------------
if __name__ == "__main__":
    app = QtWidgets.QApplication(sys.argv)
    gui = SimTempGUI()
    gui.show()
    sys.exit(app.exec_())
