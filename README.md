# OEMGA Core ⚡️

**OEMGA Core** is the open-source firmware foundation for **OEMGA** — a clean, reproducible, one-command developer experience built on **Nordic nRF Connect SDK (NCS) v3.1.1**.

This repo is designed for **fast onboarding** and **scalable product evolution**:
- Contributors build against the official upstream Zephyr/NCS board targets.
- OEMGA hardware differences are applied via **standard overlays + Kconfig**, keeping everything compatible with upstream tooling.
- A lightweight `oemga` CLI provides a **single entrypoint** for setup + builds (Docker-first).

---

## What you can do here

✅ Initialize a fully pinned NCS workspace  
✅ Build OEMGA targets with a human-friendly name (e.g. `oemga_alpha`)  
✅ Produce flashable artifacts with deterministic outputs  
✅ Keep OEMGA deltas cleanly separated via overlays (no custom board fork required)

---

## Repository Layout

```text
oemga-core/
├─ apps/                # Firmware applications (Zephyr/NCS)
├─ west.yml             # Workspace manifest (pins NCS + module revisions)
└─ oemga                # CLI wrapper (setup/build via Docker)
