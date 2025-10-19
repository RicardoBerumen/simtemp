# SimTemp Driver Design

## 1. Overview

The **SimTemp** driver emulates a temperature sensor that periodically generates synthetic temperature samples in kernel space and exposes them to user space through a character device (`/dev/simtemp`).  

It supports:
- Multiple operating **modes** (`normal`, `noisy`, `ramp`)
- **Threshold event** signaling
- **Sysfs attributes** for runtime configuration
- **Polling** (`poll()`) for non-blocking user notifications
- A simple **Python CLI** for reading and visualizing samples

This project demonstrates end-to-end kernel–user communication using standard Linux interfaces.

---

## 2. Architecture

### 2.1 Block Diagram

┌──────────────────────────┐
│ User Space (CLI) │
│ - main.py │
│ - poll() + read() loop │
│ - sysfs writes via echo │
└────────────┬─────────────┘
│
│ (read/poll/sysfs)
▼
┌──────────────────────────┐
│ SimTemp Kernel Module │
│ nxp_simtemp.ko │
│ │
│ ┌──────────────────────┐ │
│ │ Workqueue Timer │ │
│ │ - Generates sample │ │
│ │ - Pushes to kfifo │ │
│ └──────────────────────┘ │
│ │ │
│ ▼ │
│ ┌──────────────────────┐ │
│ │ Character Device │ │
│ │ - read() │ │
│ │ - poll() │ │
│ └──────────────────────┘ │
│ │
│ Sysfs Group │
│ ├── /mode │
│ ├── /threshold_mC │
│ └── /sampling_ms │
| └── /stats │ 
└──────────────────────────┘
│
▼
┌──────────────────────────┐
│ Device Tree / Platform │
│ - compatible = "nxp,simtemp" │
└──────────────────────────┘

### 2.2 Interactions
**Kernel Side**
- Timer or workqueue generates a new temperature sample periodically.
- A struct simptemp_sample is filled with **timestamp**, **temperature**, **mode**, **flags**.
- Sample is pushed into a kfifo.
- When a new sample arrives, the `SAMPLE_FLAG_NEW` is set.
- And the `wake_up_interruptible(&wq)` is called.
- If the temperature sampled surpasses the threshold, the `SAMPLE_FLAG_ALERT` is set, and serves as an alert event for the user.

**User side**
- The CLI opens /dev/simtemp and calls `select()`, `poll()` on the file descriptor.
- The process is blocked until the kernel signals ready with the corresponding flag.
- Once `poll()` indicates that data is available, the CLI reads the device to fetch the next sample.
- It unpacks and prints the fields of the structure.
- If `SAMPLE_FLAG_ALERT` is set, and alert message is printed, as well as the test is completed.

**Control Flow**
Sysfs attributes act as control knobs for the runtime configuration.
- `mode` : controls the behaviour of the sensor.
- `threshold_mC`: sets the threshold for the temperature.
- `sampling_ms`: controls how often the samples are generated.
The CLI writes to these files in `/sys/class/misc/simtemp`to configure the driver before starting the readings.
The GUI, on the other side, can write to these files on the fly, just using the configuration set and the button pressed.

**Signals**
**Signal**               - **Direction**              - **Purpose**
`wait_queue + poll()`    - Kernel -> User       - Notify CLI/GUI of new data

`kfifo`                  - Kernel -> User       - Shared buffer for sample data

`sysfs`                  - User -> Kernel       - Config and mode control

`flags`                  - Kernel -> User       - Encodes event state and alerts CLI/GUI

---

## 3. API Contract

### 3.1 Character Device (`/dev/simtemp`)

**Interface**: character device (misc)  
**Purpose**: data path for streaming samples  
**IOCTLs**: sampling_ms, threshold_mC, mode -> Not actively in use (prefered to use sysfs) 
**Read semantics**:
- Blocks until a new sample is available (unless O_NONBLOCK)
- Each `read()` returns one packed struct:

```c
struct simtemp_sample {
    __u64 timestamp_ns;   /* CLOCK_REALTIME */
    __s32 temp_mC;        /* milli-Celsius */
    __s16 mode_name;      /* 0=normal,1=noisy,2=ramp */
    __u32 flags;          /* bit0=NEW_SAMPLE, bit1=THRESHOLD_CROSSED */
} __attribute__((packed));
```

**Polling** `poll()` wakes when a new sample is inserted in the fifo.

### 3.2 Control event selection
SimTemp uses **sysfs** for configuration and control, mainly due to sysfs providing a **standardized, discoverable, and user-friendly**
**interface** for device configuration. Each attribute (e.g., `/sys/class/misc/simtemp/sampling_ms`) maps cleanly to a kernel variable, 
allowing users and scripts to inspect or modify parameters using simple shell tools (`cat`, `echo`, etc.) without requiring custom binaries.
With it's main advantages being:
- Transparency, plain text files.
- Standardization in Linux model device conventions.
- Safety, clear access semantics.
- Ease of automation with shell scripts.

In summary, sysfs is a better decision in the beginning, due to its safety and ease of use in linux devices, although it can be eventually
swapped out once the device needs more complexity or upscaling.


### 3.3 Sysfs Interfaces
Located under `/sys/class/misc/simtemp/`.
**mode**         - RW - Select generation mode (`normal`, `noisy`, `ramp`).
**threshold_mC** - RW - Set alert threshold (in millicelsius).
**sampling_ms**  - RW - Set sampling interval (in milliseconds).
**stats**        - RO - Publishes count of updates, alerts, and errors.

### 3.4 Events and Flags
Flags inside each `simtemp_sample` indicate:
**0x01**         - SAMPLE_FLAG_NEW   - Set when new valid data.
**0x02**         - SAMPLE_FLAG_ALERT - Set when threshold_mC exceeded.

### 3.5 User-space Contact
The python CLI inside `main.py`:
**1.-** Opens `/dev/simtemp`.
**2.-** Uses `select.poll()` to wait for readeable events.
**3.-** Reads variable-size packets in case some of the data is separated.
**4.-** Unpacks the struct using `("<Q i h I")`.
**5.-** Sets sysfs knobs (if indicated).
**6.-** Prints the samples and their timestamp.

---

## 4. Threading and Locking Model

### 4.1 Workqueue Timer Thread
The driver uses a **delayed workqueue** to periodically generate temperature samples.
Each iteration:
1. Computes a new synthetic temperature value based on the current mode.
2. Pushes the sample into a kernel FIFO (`kfifo_in()`).
3. Wakes up any user-space readers waiting via `poll()`.


### 4.2 Concurrency Control

| Resource | Protection | Reason |
|-----------|-------------|--------|
| `kfifo` | `spinlock_t fifo_lock` | Accessed concurrently from the workqueue (producer) and the `read()` handler (consumer). |
| `mode`, `threshold_mC`, `sampling_ms` | `mutex` (`config_lock`) | Modified through sysfs, which can sleep — safe to use mutex. |
| `wait_queue_head_t read_queue` | Wait queue | Synchronizes blocking readers with the producer thread. |

### 4.3 Locking Rationale
- **Spinlocks** are chosen for the FIFO path to ensure minimal latency inside atomic or softirq context (short critical sections).  
- **Mutexes** are used for sysfs writes because they occur in process context and may block.  
- **Wait queues** efficiently block user threads until new samples are available, avoiding busy-waiting.

---

## 5. Device Tree (DT) Mapping

The driver supports both static and Device Tree–based instantiation.

### 5.1 DT Example Node
The structure of the DT used is:
```dts
simtemp0: simtemp@0 {
    compatible = "nxp,simtemp";
    sampling-ms = <200>;
    threshold-mC = <45000>;
    status = "okay";
};
```

### 5.2 DT -> Probe() Mapping
The mapping of the DT values inside the kernels and the defaults used in case DT entry was not found are as follows:
**DT Property**      - **Kernel Field**   - **Default**
`sampling-ms`        - `sampling_ms`      - `100 ms`
`threshold-mC`       - `threshold_mC`     - `45000 mC`


## 6. Scaling and Performance
At **10 kHz sampling**, several limitations emerge.
### 6.1 Subsystem Limitations
**Subsystem**  - **Limitation**              - **Effect**
`kfifo`        - User space cannot drain     - FIFO overflow, lost samples
                 fast enough
`workqueue`    - Latency / jitter from       - Missed or delayed samples
                 scheduler
`poll()`       - Frequent wakeups            - High context-switch overhead

`sysfs`        - Slow for high-rate writes   - Not critical, but lags

## 6.2 Mitigation Strategies
- Implement batch reads (`read()` multiple times per call)
- Expose an mmap ring buffer for near zero-copy access.
- Use RT scheduling or per-CPU worker for precise timing.
- Add backpressure counters for overflow detection.
- Increase FIFO depth cautiously to handle bursts.
- Use **ioctl** for control and configuration in the CLI/GUI space, and test more extensively the kernel implementation.