# NXP SimTemp – Temperature Sensor Simulator

This project implements a **Linux kernel module** that simulates a temperature sensor driver,
exported as `/dev/simtemp`, and a **Python CLI tool** as well as a **Python GUI** that interacts with it.

The simulated device produces temperature samples between a certain range every *N* milliseconds using a
combination of `hrtimer` and `workqueue`. It exposes control knobs via **sysfs** and **ioctl**, as well as a **DT support** and allows
user-space polling via **select/poll/epoll**.

The CLI tool reads via poll/epoll the samplings of the device, and prints it with the format: `YY-MM-DDT00:00:00.000Z temp=45.0C alert=0`.
It can be used in normal mode, or in test mode, where it reads a couple of times with a low threshold set, and determines if the module is 
working properly or not.
It is able to set the sysfs knobs with the input arguments when launching the tool, where the sampling time, the temperature threshold and 
the mode can be set.

The GUI tool is similar to the CLI tool in a Graphic version, without the testing mode. It graphs the last 100 samples, and lights up red 
if the temperature threshold is passed, the bar showing the current temperature lights up red. And it collects the settings for the sysfs 
values, setting safety limits.

---

## Demo & Repository

- **Video demonstration:** (link)
- **GitHub repository:** [RicardoBerumen/simtemp](https://github.com/RicardoBerumen/simtemp)  

---

## Build & Installing Device


### Using the automated scripts that checks for headers and gives warnings
    ```bash
    chmod +x scripts/build.sh
    ./scripts/build.sh
    sudo insmod nxp_simtemp.ko
    ```

### Manually building Kernel Module
    ```bash
    cd kernel
    make
    sudo insmod nxp_simtemp.ko
    ```

## Running tools (CLI and GUI)
Once the device is mounted and `/dev/simtemp` is available, to run the tools you would follow these steps:

### CLI
```bash
    pip install user/cli/requirements.txt
    python3 user/cli/main.py --test #run test
    python3 user/cli/main.py #read normal samples
    python3 user/cli/main.py --sampling 1 --threshold 42000 --mode noisy #change sysfs configs
```

### CLI
```bash
    pip install user/cli/requirements.txt
    python3 user/gui/app.py
```

