# TESTPLAN.md — SimTemp Project

## 1. Overview
This document describes the verification and validation steps for the **SimTemp kernel module** and **userspace CLI & GUI**.  
The goal is to ensure correct operation, stability, and compliance with the design requirements across configuration, data reporting, and alerting functions.

---

## 2. Objectives
- Verify proper device creation and sysfs interface behavior.  
- Confirm that data samples (timestamp, temperature, mode, flags) are correctly transferred to user space.  
- Validate threshold alert generation and event propagation.  
- Ensure robustness under rapid sampling and mode switching.  
- Confirm user CLI scripts correctly read, configure, and respond to device events.
- Confirm user GUI correctly reads, graphs, and configures the device.

---

## 3. Test Environment
| Component | Description |
|------------|-------------|
| **Kernel** | Linux 5.x+ (tested on Ubuntu 22.04) |
| **Driver** | simtemp.ko loaded via `insmod` |
| **Device Node** | `/dev/simtemp` (misc device) |
| **Sysfs Path** | `/sys/class/misc/simtemp/` |
| **CLI Scripts** | Located in `/user/cli/` |
| **GUI App** | Located in `/user/guii/` |
| **Tools** | `hexdump`, `dmesg`, `cat`, `echo`, Python 3.10+ |

---

## 4. Test Cases

### 4.1. Module Load/Unload
**Steps**
1. Run `sudo insmod simtemp.ko`
2. Verify with `ls /dev/simtemp`
3. Check `dmesg | tail`
4. Unload with `sudo rmmod simtemp`

**Expected Result**
- Device `/dev/simtemp` created.
- Sysfs entries exist under `/sys/class/misc/simtemp/`.
- Clean removal without warnings or use-after-free errors.

---

### 4.2. Sysfs Interface
**Parameters tested:** `sampling_ms`, `threshold_mC`, `mode`

**Steps**
```bash
cat /sys/class/misc/simtemp/sampling_ms 
echo 200 > /sys/class/misc/simtemp/sampling_ms
cat /sys/class/misc/simtemp/mode
echo noisy > /sys/class/misc/simtemp/mode
```
**Expected Result**
- Parameters update without kernel errors.
- Invalid writes (example: `echo invalid > mode`) return EINVAL.


---

### 4.3. Sampling and Data Path

**Steps**
1. Run `sudo ./user/cli/main.py`
2. Observe printed samples (timestamp, tempreature, mode, flags)

**Expected Result**
- Continuous valid data samples.
- Temperature varies according to selected mode.
- No hangs or struct misalignment issues.

---

### 4.4. Alert / Threshold Test

**Steps**
1. Run CLI alert test (sets low threshold and reads for alert)
```bash
sudo ./user/cli/main.py --test
```

**Expected Result**
- Alert triggered once temperature exceeds threshold.
- CLI reports "Alert triggered".
- Corresponding flags bit set (`SAMPLE_FLAG_ALERT`)

---

### 4.5. Mode Switching

**Steps**
1. Run in terminal:
```bash
echo normal > /sys/class/misc/simtemp/mode
sleep 2
echo noisy > /sys/class/misc/simtemp/mode
sleep 2
echo ramp > /sys/class/misc/simtemp/mode
```
2. (Optional) Test with CLI:
```bash
sudo ./user/cli/main.py --mode noisy
sudo ./user/cli/main.py --mode ramp
```

**Expected Result**
- Temperature output pattern changes for each mode.
- No missed samples or kernel warnings.

---

### 4.6. Frequency test

**Steps**
1. Set high sampling rate:
```bash
echo 10 > /sys/class/misc/simtemp/sampling_ms
```
2. Use CLI reader and dmesg for 60 seconds.
3. (Optional) Test directly with CLI:
```bash
sudo ./user/cli/main.py --sampling 10
```

**Expected Result**
- Samples continue without dropped reads or buffer overruns.
- No "kfifo full" warnings or deadlocks in dmesg.

---
