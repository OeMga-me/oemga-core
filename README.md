<h1 align="center">OeMga.me</h1>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-Apache%202.0-blue.svg" alt="License"/></a>
  <a href="https://www.zephyrproject.org/"><img src="https://img.shields.io/badge/Zephyr-RTOS-black.svg" alt="Zephyr RTOS"/></a>
  <a href="https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/"><img src="https://img.shields.io/badge/Nordic-nRF%20Connect%20SDK-00A9CE.svg" alt="nRF Connect SDK"/></a>
</p>

<p align="center">
<strong>The open-source, on-device teachable bio-interaction platform.</strong>
</p>

## Technical Overview

**oemga-core** is the central firmware repository for the OeMga platform.

We are starting with **OeMga α (oemga_alpha)** as the foundational product, this repo will grow to support additional OEMGA modules and hardware revisions over time.

To keep builds reproducible across contributors and machines, `oemga-core` uses a **Docker-first toolchain** and a pinned **NCS/Zephyr workspace**, ensuring everyone builds with the same SDK + compiler versions and avoiding environment-driven inconsistencies.

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
