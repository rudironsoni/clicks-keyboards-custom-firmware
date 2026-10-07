# Clicks keyboards custom firmware

Experimental open-source firmware, reverse engineering notes, host tools, and bring-up work for Clicks keyboards.

This repository is organized by keyboard model. The CK-5200 is the first target, not the repository itself.

## Targets

| Model | Status | Firmware |
| --- | --- | --- |
| [CK-5200](keyboards/ck5200/) | QMK/CH32V20x bring-up in progress | Build target present, first custom binary not hardware-validated |

## Repository layout

```text
keyboards/
  ck5200/
    firmware/       target firmware
    scripts/        build and flash entry points
    tools/          host-side updater and image tools
    tests/          protocol tests
    docs/           hardware and USB findings
```

Each keyboard directory owns its build, hardware notes, tools, and status. Shared code should only move out of a keyboard directory once a second target actually needs it.

## CK-5200

The CK-5200 work currently includes:

- recovered A1/A2/A3/A0 regular-USB update protocol
- recovered 6x6 key matrix
- CH32V20x startup and linker configuration
- TinyUSB HID plus the recovered vendor updater interface
- QMK diagnostic keymap
- explicit host-side USB flasher
- image-size and reset-vector validation
- CI build workflow

See [keyboards/ck5200/STATUS.md](keyboards/ck5200/STATUS.md) for the exact verified and unverified state.

## Warning

This repository contains experimental firmware for hardware you own and control. A successful compile does not prove that a custom image is accepted by the device boot path or that recovery is possible after a failed flash. Read the target-specific status and hardware notes before writing anything.
