# OeMga.me : oemga-core

<p align="center">
<img src="[https://raw.githubusercontent.com/OeMga-me/oemga-core/main/logo.png](https://www.google.com/search?q=https://raw.githubusercontent.com/OeMga-me/oemga-core/main/logo.png)" alt="OeMga.me Logo" width="200">
</p>

<p align="center">
<a href="LICENSE"><img src="[https://img.shields.io/badge/License-Apache%202.0-blue.svg](https://img.shields.io/badge/License-Apache%202.0-blue.svg)" alt="License"></a>
<a href="[https://www.zephyrproject.org/](https://www.zephyrproject.org/)"><img src="[https://img.shields.io/badge/Zephyr-RTOS-black.svg](https://img.shields.io/badge/Zephyr-RTOS-black.svg)" alt="Zephyr RTOS"></a>
<a href="[https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/)"><img src="[https://img.shields.io/badge/Nordic-nRF%20Connect%20SDK-00A9CE.svg](https://img.shields.io/badge/Nordic-nRF%20Connect%20SDK-00A9CE.svg)" alt="nRF Connect SDK"></a>
</p>

<p align="center">
<strong>The open-source, on-device bio-interaction platform.</strong>
</p>

## Technical Overview

**oemga-core** is the central firmware repository for the OeMga platform, targeting the **oemga_alpha** (nRF54L15) hardware. This repository facilitates the development of wearable bio-sensors, including EMG, EEG, and fNIRS devices.

By utilizing a Docker-based toolchain, this project ensures that all contributors build with the exact same compiler and SDK versions, eliminating environment-related inconsistencies.

## Workspace Architecture

This project utilizes the **Zephyr West Workspace** structure. To maintain repository integrity, dependencies are initialized in the parent directory.

```text
.
├── nrf/             # Nordic Semiconductor SDK (Managed by West)
├── zephyr/          # Zephyr RTOS (Managed by West)
├── oemga-core/      # Current Repository
│   ├── build/       # Local build artifacts
│   ├── west.yml     # Project manifest
│   └── oemga        # Unified build and setup wrapper
└── modules/         # External hardware abstraction layers

```

## Setup and Installation

### 1. Prerequisites

Ensure you have the following installed on your host machine:

* **Docker Desktop** (or Docker Engine)
* **Git**
* **nRF Command Line Tools** (Optional, recommended for advanced flashing)

### 2. Initialize the Workspace

Clone the repository and run the setup script to pull the necessary SDKs and Docker images.

```bash
git clone https://github.com/OeMga-me/oemga-core.git
cd oemga-core
./oemga setup

```
## Development Workflow

### Building Firmware

The build process occurs within a Docker container pinned to the nRF Connect SDK version.

```bash
./oemga build oemga_alpha

```

### Flashing Hardware

This command identifies your connected hardware and flashes the compiled `.hex` file.

```bash
./oemga flash

```

The script will attempt to flash using `nrfjprog` first; if unavailable, it will attempt a direct copy to the mounted JLINK USB drive.

## License
This project is licensed under the Apache License 2.0. See the `LICENSE` file for details.