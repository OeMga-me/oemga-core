# OeMga.me — oemga-core

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Zephyr RTOS](https://img.shields.io/badge/Zephyr-RTOS-black.svg)](https://www.zephyrproject.org/)
[![nRF Connect SDK](https://img.shields.io/badge/Nordic-nRF%20Connect%20SDK-00A9CE.svg)](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/)

---

## The Vision
Current wearables demand that we speak their language. **OeMga.me learns ours.**

Most systems ship as rigid pipelines — fixed FSMs, fixed heuristics, fixed assumptions. They log bio-signals and ask the user to adapt.  
OeMga.me flips the contract: **the device adapts to the user**.

This repository holds the firmware core that makes that possible — the real-time edge foundation for a **Teachable Bio-Interaction Platform**, where intelligence lives locally, responds instantly, and evolves with the person wearing it.

---

## The Foundation: OeMga α (Firmware)
`oemga-core` contains the firmware source for the foundational module of the OeMga α platform.

- Built on **Zephyr RTOS** via **Nordic nRF Connect SDK (NCS) v3.1.1**
- Structured for **upstream alignment**: we build against official Zephyr/NCS board targets
- OEMGA-specific hardware differences are applied via **standard Zephyr overlays + Kconfig**
- A single `oemga` CLI provides a clean, reproducible workflow (Docker-first)

---

## Quick Start

### 1) Setup
Initialize the workspace (pulls NCS + dependencies pinned by `west.yml`).

```bash
./oemga setup
