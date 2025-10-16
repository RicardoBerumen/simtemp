#!/usr/bin/env python3 

# CLI for temperature sensor simulator
# Features
# Configure sampling period and threshold
# Read from /dev/simtemp using poll/epoll
# Test mode 
# Code by: Ricardo Berumen

import os
import struct
import time
import select
from datetime import datetime
import argparse

# mode mapping
MODE_NAMES = {
    0: "normal",
    1: "noisy",
    2: "ramp"
}

''' -- Module for IOCTL configuration (using sysfs properties at the moment)
class SimtempConfig(struct.Struct):
    """Binary struct for ioctl config"""
    def __init__(self, sampling_ms, threshold_mC, mode):
        super().__init__('III')
        self.sampling_ms = sampling_ms
        self.threshold_mC = threshold_mC
        self.mode = mode
    def pack(self):
        return super().pack(self.sampling_ms, self.threshold_mC, self.mode)

# --- IOCTL helper macros and header ---
IOC_NRBITS = 8
IOC_TYPEBITS = 8
IOC_SIZEBITS = 14
IOC_DIRBITS = 2

IOC_NRSHIFT = 0
IOC_TYPESHIFT = IOC_NRSHIFT + IOC_NRBITS
IOC_SIZESHIFT = IOC_TYPESHIFT + IOC_TYPEBITS
IOC_DIRSHIFT = IOC_SIZESHIFT + IOC_SIZEBITS

IOC_NONE = 0
IOC_WRITE = 1
IOC_READ = 2

def _IOC(direction, type_, nr, size):
    return ((direction << IOC_DIRSHIFT) |
            (ord(type_) << IOC_TYPESHIFT) |
            (nr << IOC_NRSHIFT) |
            (size << IOC_SIZESHIFT))

def _IOW(type_, nr, data_type):
    return _IOC(IOC_WRITE, type_, nr, ctypes.sizeof(data_type))

# --- define same IOCTL as kernel
SIMTEMP_IOC_CONFIG = _IOW('s', 1, SimtempConfig)'''

SAMPLE_STRUCT = struct.Struct("<Q i h I")

DEV_PATH = "/dev/simtemp"

def read_sample(fd):
    data = fd.read(SAMPLE_STRUCT.size)
    if len(data) != SAMPLE_STRUCT.size:
        return None
    ts_ns, temp_mC, mode_name, flags = SAMPLE_STRUCT.unpack(data)
    ts = datetime.utcfromtimestamp(ts_ns/1e9).isoformat(timespec="milliseconds")+"Z"
    temp_c = temp_mC / 1000.0
    alert = 1 if flags & 2 else 0
    #print(f"{ts} temp={temp_c:.1f}C alert={alert} mode={mode_name}")
    mode_str = MODE_NAMES.get(mode_name, f"unknown({mode_name})")
    return ts, temp_c, alert, mode_str

def configure_sysfs(sampling_ms=None, threshold_mC=None, mode=0):
    if sampling_ms is not None:
        with open("/sys/class/misc/simtemp/sampling_ms","w") as f:
            f.write(f"{sampling_ms}\n")
    if threshold_mC is not None:
        with open("/sys/class/misc/simtemp/threshold_mC", "w") as f:
            f.write(f"{threshold_mC}\n")
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

def main(test_mode = False):
    try:
        f = os.open(DEV_PATH, os.O_RDONLY | os.O_NONBLOCK)
        fd = os.fdopen(f, 'rb')
    except FileNotFoundError:
        print(f"Device {DEV_PATH} not found")
        return

    if test_mode:
        print("Test mode: setting low threshold to verify alert")
        configure_sysfs(threshold_mC=100) #really low
        poll = select.poll()
        poll.register(fd, select.POLLIN | select.POLLPRI)
        start = time.time()
        triggered = False
        while time.time() - start < 2.0:
            events = poll.poll(2000)
            for fd_evt, flag in events:
                sample = read_sample(fd)
                if sample:
                    ts, temp_c, alert, mode = sample
                    print(f"{ts} temp={temp_c:.1f}C alert={alert} mode = {mode}")
                    if alert:
                        triggered = True
                        break
            if triggered:
                break
        fd.close()
        if not triggered:
            print("Alert not triggered! Test failed")
            exit(1)
        print("Alert triggered. Test passed")
        exit(0)
    
    #normal run: continuos read
    poll = select.poll()
    poll.register(fd, select.POLLIN | select.POLLPRI)
    try:
        while True:
            events = poll.poll()
            for fd_evt, flag in events:
                sample = read_sample(fd)
                if sample:
                    ts, temp_c, alert, mode = sample
                    print(f"{ts} temp={temp_c:.1f}C alert={alert}, mode={mode} ")
    except KeyboardInterrupt:
        print("Exiting")
        fd.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SimTemp CLI")
    parser.add_argument("--test", action="store_true", help="Run test mode")
    parser.add_argument("--sampling", type=int, help="Set sampling period (ms)")
    parser.add_argument("--threshold", type=int, help="Set alert threshold (mC)")
    parser.add_argument("--mode", type=str, help="Set temp mode")
    args = parser.parse_args()

    if args.sampling or args.threshold or args.mode:
        configure_sysfs(sampling_ms=args.sampling, threshold_mC=args.threshold, mode=args.mode)
    
    main(test_mode=args.test)


