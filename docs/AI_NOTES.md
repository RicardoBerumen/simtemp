# SimTemp Driver AI Notes 

## 1. Overview

For the **SimTemp** driver, LLMs, from now on refered as AI (specifically ChatGPT), were used to help solve problems and implement new features on pre-existing code, trying to create different conversations for different problems, to prevent the AI from crossing information or assuming information that it didn't know.
In the following points, the structure used will be **Problem**, then **Prompt**, then **Validation**.  

---

## 2. Confirming Poll/Epoll usage.
I had implemented the poll() function in the kernel, and wanted to test its functionality, and check if I needed to make any fixes.

---

### 2.1. Prompt 1
**i'm working on a linux module in C to simulate a temperature sensor and I want to add a threshold in the poll/epoll I think I've added it with a flag, but I can't see the change, do you think you could help me with a quick python code to check if it works?**

#### Validation
I put the code suggested in a file in user/cli and adjusted it's settings to fit with my project specifications and specific names. I still didn't get any readings by poll, so I tried to cross - check if my code was correct with the following prompt. 

---

### 2.2. Prompt 2
**my code is supposed to - Wake on **new sample** availability. - Wake when **threshold is crossed** (separate event bit)**

#### Validation
As this was to double check if my code was correct, I saw some suggestions in the poll implementation and some typos that needed fixing. After correcting the kernel code, I used the python code previously generated, and started getting readings, although with some mistakes.

---

### 2.2. Prompt 3
**this is the output of the python file Threshold crossed event! New sample event: 43 New sample event: ⨱ New sample event: <#O Threshold crossed event! New sample event: f0$ Threshold crossed event! New sample event: W| New sample event: k0 New sample event: U6" New sample event: ŞU< New sample event: OB New sample event: <Hw Threshold crossed event! New sample event: 9N Threshold crossed event! New sample event: !T Threshold crossed event!**

#### Validation
The suggestion was to unpack the data using struct, so I implemented the struct and unpacking to the python polling code and started reading from the kernel with the expected output. I also added a pr_info with the temperature in the kernel, to check if the kernel and python were printing the same value.

---

## 3. Adapting the module to simulate a driver (using probe).
Although most of the functionalities were implemented, I still lacked the driver and probe aspect of the kernel module, so I needed some help understanding the needed changes to the codes architecture.

---

### 3.1. Prompt 4
**hi, i'm developing a linux module to simulate a temperature sensor driver, right now the features are sysfs controls for threshold and sampling time, poll/epoll with wake up feature for new event and threshold passed, as well as the use of hrtimer and kfifo A feature I'm missing is the registration of a platform driver, bind using DT "compatible = "nxp,simtemp"" What should I change to add it? Considering I'm simulating it in my computer without a DT**

#### Validation
Cross - examinating the code provided by the LLM and mine, as well as adding pr_info() messages in the kernel module, so that the steps and values could be checked in `dmesg -w` when mounting the device with `ìnsmod`. The main challenge was implementing `probe()` instead of the normal `init`, so once the module correctly mounted and executed, it was validated.

---

### 3.2. Prompt 5
**it seems to work, how can I check if it's working correctly?**

#### Validation
The LLM provided shell commands and a python script that read the kernel module driver and displayed the info needed to confirm the module functionalities were working.

---

### 3.3. Prompt 6
**my dtsi need to implement this simtemp0: simtemp@0 { compatible = "nxp, simtemp"; sampling-ms = <100>; threshold-mC = <45000>; status = "okay"; }; How should i do it?**

#### Validation
Just double checked with my code and the specification, adding the dedicated .dtsi and `pr_info()` to see the flow of the kernel, priting if it used the DT values or the default ones.

---

## 4. Adding IOCTL for atomic configuration.
Once most of the functionalities were correctly finished, the optional IOCTL config was added, so I needed guidance in the exact implementation.

---

### 4.1. Prompt 7
**if i were to add ioctl for atomic/batch config to a linux kernel module for a temperature sensor sim driver, how would I do it?**

#### Validation
Updated the python test code to send batch ioctl updates, and used `pr_info()` to print the values received.

---

### 4.2. Prompt 8
**i got this error [IOCTL] Setting batch config: sampling_ms=300, threshold_mC=46000, mode=1 Traceback (most recent call last): File "/home/student/simtemp/user/cli/main.py", line 86, in <module> main() File "/home/student/simtemp/user/cli/main.py", line 72, in main ioctl_set_config(fd, sampling_ms=sampling_ms, threshold_mC=threshold_mC, mode=mode) File "/home/student/simtemp/user/cli/main.py", line 31, in ioctl_set_config fcntl.ioctl(fd, SIMTEMP_IOC_SET_CONFIG, buf) OSError: [Errno 25] Inappropriate ioctl for device**

#### Validation
The LLM came to the conclusion that the error came from calling the incorrect ioctl config, so it added a `pr_info("simtemp: ioctl called, cmd=0x%x\n", cmd)` to debug using `dmesg` if the python code was providing the right configuration, which it did not, but it confirmed that the device was correctly mounted and reading ioctl.

---

### 4.3. Prompt 9
**what if instead of ioc_set and get, we just used #define SIMTEMP_IOC_MAGIC 's' #define SIMTEMP_IOC_CONFIG _IOW(SIMTEMP_IOC_MAGIC, 1, struct simtemp_config) in the c file?**

#### Validation
After some research, I wanted to see if the LLM could help debugging this new approach to solve the problem. In this case, it added a case to confirm whether the ioctl received matched the correct one, so it prints the unknown ioctl cmd and returns an error if the incorrect ioctl is called. So by reading `dmesg` and modifying the python code to define the appropriate constants as: 
```python
SIMTEMP_IOC_MAGIC = ord('s')
SIMTEMP_IOC_CONFIG = (1 << (8 + 8 + 14)) | (SIMTEMP_IOC_MAGIC << 8) | 1  
```

---

### 4.4. Prompt 10
**got [ 1670.371611] simtemp: ioctl called, cmd=0x40007301 [ 1670.371617] simtemp: unknown ioctl cmd=0x40007301**

#### Validation
After this last prompt, the LLM provided the appropriate ioctl code configuration needed, which found the core problem and allowed the ioctl to be triggered. To test this, a separate python script was created, to read the sensor output, and the output matched the sampling time and threshold set with ioctl, as well as the `dmesg` message printing the correct values sent.

---

## 5. Fixing padding problem after adding mode to the data structure.
So for easier debugging, and possible future implementations, I added __s16 mode_name to the data structure of simtemp, and the readings sometimes had a 2 byte disalignement, which needed a quick fix in the padding.

---

### 5.1. Prompt 11
**hi, i'm working on the cli for a temperature sensor simulator, and i have some problems, specifically with the struct.Struct, since i get the error that it is expecting 20 bytes instead of 18, that is specified by the data structure in my kernel, this is the data structure struct simtemp_sample { __u64 timestamp_ns; __s32 temp_mC; __s16 mode_name; __u32 flags; /* bit0=NEW_SAMPLE, bit1=THRESHOLD_CROSSED */ } __attribute__((packed));**

#### Validation
The LLM said the data structure was correct, and provided a quick python code to test the new struct, although it provided a struct of 20 bytes `struct.Struct('<Q i h 2x I')`, so this needed some fixing to return to the correct struct.

---

### 5.2. Prompt 12
**after some good prints, i get Warning: incomplete sample (18 bytes) Warning: incomplete sample (18 bytes) Warning: incomplete sample (18 bytes) Warning: incomplete sample (18 bytes) Warning: incomplete sample (18 bytes) Warning: incomplete sample (18 bytes) Warning: incomplete sample (18 bytes) Warning: incomplete sample (18 bytes) Warning: incomplete sample (18 bytes) Warning: incomplete sample (18 bytes)**

#### Validation
After this, it was understood that sometimes the compiler padded the struct, and sometimes only the 18 bytes of the struct were provided. After learning this, I implemented a python script provided by the LLM with an 18 bytes struct and a way to read the complete buffer data, so that the CLI could work both on x86 systems (usually provide some form of padding by the compiler) and other systems(provides no padding).
After the corrections suggested in the output of the LLM, the CLI could read the samples consistently, preventing any mistakes in the data stream.

---

## 6. Changing permissions for sysfs configuration.
Once the CLI was completed, I realized it had to be run by sudo to be able to write to the sysfs configuration of the kernel. To prevent changing each one of the configs manually each time the device was mounted, the best possible way was to implement it in the kernel.

---

### 6.1. Prompt 13
**hi, i built a kernel module in ubuntu 22 to simulate a temperature sensor with sysfs configuration, but I can't write to the sysfs configs without sudo, is there a way I can fix that?**

#### Validation
It suggested to use `sysfs_chmod_file` after creating the group in the kernel, as the less invasive option. After implementing it, using `ls /path/to/sysfs -lah` to read permission, it checked out, the permissions were changed. Running the CLI without the power of sudo, also allowed the code to write to the control knobs without any problems.

---

## 7. Fixing the design.md
I had some problems with the design's block diagram, and asked the LLM to help me.

### 7.1. Prompt 14
**Can you help me put this in the correct format for an .md? I'm seeing it all scrambled ┌──────────────────────────┐ │ User Space (CLI) │ │ - main.py │ │ - poll() + read() loop │ │ - sysfs writes via echo │ └────────────┬─────────────┘ │ │ (read/poll/sysfs) ▼ ┌──────────────────────────┐ │ SimTemp Kernel Module │ │ nxp_simtemp.ko │ │ │ │ ┌──────────────────────┐ │ │ │ Workqueue Timer │ │ │ │ - Generates sample │ │ │ │ - Pushes to kfifo │ │ │ └──────────────────────┘ │ │ │ │ │ ▼ │ │ ┌──────────────────────┐ │ │ │ Character Device │ │ │ │ - read() │ │ │ │ - poll() │ │ │ └──────────────────────┘ │ │ │ │ Sysfs Group │ │ ├── /mode │ │ ├── /threshold_mC │ │ └── /sampling_ms │ | └── /stats │ └──────────────────────────┘ │ ▼ ┌──────────────────────────┐ │ Device Tree / Platform │ │ - compatible = "nxp,simtemp" │ └──────────────────────────┘**

#### Validation
It provided some code in **mermaid** for a better looking diagram, and I implemented it in the file. When I tried to visualize it to validate the output, I saw an error, mainly because github's render doesn't support HTML tags, something I learned after some research.

---

### 7.2. Prompt 15
**html tags aren't supported by github, i still get Unable to render rich display Parse error on line 2: ...ph USER [User Space (CLI)] CLI_m -----------------------^ Expecting 'SQE', 'DOUBLECIRCLEEND', 'PE', '-)', 'STADIUMEND', 'SUBROUTINEEND', 'PIPE', 'CYLINDEREND', 'DIAMOND_STOP', 'TAGEND', 'TRAPEND', 'INVTRAPEND', 'UNICODE_TEXT', 'TEXT', 'TAGSTART', got 'PS' For more information, see https://docs.github.com/get-started/writing-on-github/working-with-advanced-formatting/creating-diagrams#creating-mermaid-diagrams**


#### Validation
Since it was just a visualization error, once the fixes were made by the LLM, I pasted it in the file and the visualization was able to render the block correctly.

---

