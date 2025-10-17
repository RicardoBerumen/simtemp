# SimTemp Driver Design

## Overview
SimTemp is a Linux misc device driver that simulates a temperature sensor.  
It periodically generates temperature samples and exposes them via a character device (`/dev/simtemp`).

The module supports configurable sampling rate, temperature threshold, and mode via sysfs entries.

---

## Block Diagram



  ┌───────────────────────┐
  │  Device Tree (DTS)    │
  │-----------------------│
  │ compatible="nxp,simtemp" │
  │ sampling-ms=100        │
  │ threshold-mC=45000     │
  └───────────┬───────────┘
              │
              ▼
  ┌───────────────────────┐
  │ Platform Driver Probe │
  │-----------------------│
  │ - Reads DT properties │
  │   (sampling_ms, threshold_mC) │
  │ - Registers miscdevice (/dev/simtemp) │
  │ - Creates sysfs entries │
  │ - Initializes timer & FIFO │
  └───────────┬───────────┘
              │
              ▼
  ┌─────────────────────────────┐
  │ Kernel Timer / Workqueue     │
  │-----------------------------│
  │ - Generates simulated samples│
  │ - Sets flags (NEW_SAMPLE / THRESHOLD_CROSSED) │
  │ - Updates FIFO               │
  │ - Updates stats (atomic counters) │
  │ - Wakes waitqueue            │
  └───────────┬───────────┘
              │
              ▼
  ┌─────────────────────────────┐
  │ Character Device /dev/simtemp │
  │-----------------------------│
  │ File ops: read(), poll(), ioctl() │
  │ - read(): blocking/nonblocking binary sample │
  │ - poll(): signals new sample / threshold alert │
  │ - ioctl(): atomic batch config (sampling_ms, threshold, mode) │
  └───────────┬───────────┘
              │
              ▼
  ┌─────────────────────────────┐
  │ Sysfs Entries (/sys/class/...) │
  │-------------------------------│
  │ - sampling_ms (RW)             │
  │ - threshold_mC (RW)            │
  │ - mode (RW)                    │
  │ - stats (RO: updates, alerts, errors) │
  └───────────┬───────────┘
              │
              ▼
  ┌─────────────────────────────┐
  │ User Space Application      │
  │-----------------------------│
  │ - CLI / Python /       │
  │ - read() samples             │
  │ - poll()/select()/epoll()    │
  │ - sysfs configure    │
  │ - GUI visualization │
  └─────────────────────────────┘


---

## Interaction Flow

1. **Driver Initialization**
   - Registers misc device `/dev/simtemp`.
   - Creates sysfs group for configuration.
   - Starts workqueue that generates samples every `sampling_ms`.

2. **Data Generation**
   - Workqueue callback generates temperature based on `mode`.
   - Each sample includes:
     ```c
     struct simtemp_sample {
         __u64 timestamp_ns;
         __s32 temp_mC;
         __s16 mode_name;
         __u32 flags; // bit0=NEW_SAMPLE, bit1=THRESHOLD_CROSSED
     } __attribute__((packed));
     ```
   - Pushed into a `kfifo`.
   - If threshold crossed → sets bit1 and wakes up readers.

3. **User Space Read**
   - Python script uses `poll()` on `/dev/simtemp`.
   - When data available → reads `sizeof(struct simtemp_sample)` bytes.
   - Unpacks fields and prints timestamp, temperature, mode, and alert.

4. **Configuration via Sysfs**
   - `sampling_ms`, `threshold_mC`, `mode` writable via `/sys/class/misc/simtemp/...`.
   - Example:
     ```bash
     echo 100 > /sys/class/misc/simtemp/sampling_ms
     echo noisy > /sys/class/misc/simtemp/mode
     ```

---

## Locking Model
| Mechanism | Used For | Reason |
|------------|-----------|--------|
| **spinlock_t** | Protects `kfifo` access | FIFO accessed from both workqueue and user read context; must not sleep. |
| **mutex** | Not used | Sysfs writes are serialized automatically; no long critical sections. |

---

## API Design Choices

| Feature | Chosen API | Rationale |
|----------|-------------|-----------|
| Sampling rate, threshold, mode | **sysfs** | Simple configuration, human-readable |
| Sample data | **/dev/simtemp** | Binary structured data suited for poll-based reads |
| Event notification | **poll()** | Efficient, standard mechanism for user-space waiting |

---

## Device Tree Integration

Example node:
```dts
simtemp@0 {
    compatible = "nxp,simtemp";
    sampling-ms = <200>;
    threshold-mC = <50000>;
};
