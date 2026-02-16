# OEMGA Core

This repository contains the firmware source for the OEMGA project.
It relies on the nRF Connect SDK (NCS) v3.1.1.

## Directory Structure

- `apps/`: Application source code.
- `west.yml`: Manifest file pinning the SDK version.
- `oemga`: CLI wrapper for building via Docker.

## Quick Start

### 1. Setup
Initialize the workspace (downloads NCS and dependencies).
```bash
./oemga setup
```

### 2. Build
Build the Alpha firmware (maps to upstream DVK + Overlays).
```bash
./oemga build oemga_alpha
```

The output binaries will be in `build/zephyr/zephyr.hex`.

### 3. Flash
If you have local tools installed:
```bash
west flash
```
Or use the nRF Connect Programmer app.
