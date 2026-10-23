# Build and inspect the CK-5200 image

Offline build instructions and protocol reference. [flash-readiness.md](flash-readiness.md) owns current readiness. No live flashing is authorized here.

## Build and test

macOS needs CMake, Ninja, Python, and libusb. The setup script downloads a checksum-verified RISC-V toolchain and pins QMK, TinyUSB, and the WCH SDK. Run from the repository root:

```sh
bash keyboards/ck5200/scripts/setup.sh
bash keyboards/ck5200/scripts/build.sh
bash keyboards/ck5200/scripts/test.sh
```

Source dependencies are in `keyboards/ck5200/external`. They are nested Git checkouts managed by `scripts/bootstrap.sh`, not tracked submodules. Do not modify or repin them as a build workaround. The build output is in `keyboards/ck5200/build`, not in the source tree.

## Image contract

- Application start: `0x00002000`.
- Maximum application size: `0x6A00` bytes.
- Application end must not cross `0x00008A00`, the start of staging through the flash alias.
- Configured RAM: `0x20000000..0x20005000`, including a 2048-byte reserved stack.
- Reset and interrupt targets must be within executable application code.

The binary validator checks reset/vector information and image extent. Its `--elf` option also checks the linked artifact. Use both for custom builds:

```sh
keyboards/ck5200/.venv/bin/python keyboards/ck5200/tools/validate_image.py \
  keyboards/ck5200/build/ck5200_qmk.bin \
  --elf keyboards/ck5200/build/ck5200_qmk.elf
bash keyboards/ck5200/scripts/flash-qmk.sh
```

## Pre-flash validation

Before flashing, run the preflight check:

```sh
bash keyboards/ck5200/tools/preflight_check.sh
```

This validates image integrity, both restore images, the full restore-cycle harness with real images, all host harnesses, per-commit buildability, and the Swift protocol checks. If any check fails, do not flash.

## USB inspection

```sh
keyboards/ck5200/.venv/bin/python keyboards/ck5200/tools/ck5200_usb.py inspect
```

This does not send SET_CONFIGURATION, detach drivers, or claim an interface. It establishes descriptor presence only. The custom firmware reports `bcdDevice 0x9001` (the custom-firmware routing marker; stock reports `0x0122`), so the tool can distinguish the two and refuse to flash a stock keyboard by mistake.

The optional `inspect --query-version` command sends `02 03`, the version request. Custom firmware replies `08 02 03 00 51 4d 00 01` ("QM"); stock replies `08 02 03 00 01 20 01 22`.

## Custom firmware protocol

The custom image answers the stock framing `[len, cmd, ...]` on its vendor bulk endpoint, with replies `[len, 0x02, cmd, status, payload...]` (big-endian values, matching the stock protocol and QMK's dynamic keymap layout):

| Command | Request | Reply | Meaning |
| --- | --- | --- | --- |
| `0x03` | `02 03` | 8 B, payload `51 4d 00 01` ("QM") | Identifies the custom firmware. Stock answers `01 20 01 22`, so the app gates all writes on this reply. |
| `0x82` | `03 82 raw` | 4 B | Set runtime backlight brightness; disabled in M1. |
| `0x84` | `02 84` | 5 B, payload `raw` | Read backlight brightness. |
| `0x90` | `02 90` | 8 B, payload `layers rows cols flags` | Keymap dimensions. |
| `0x91` | `05 91 layer row col` | 6 B, payload keycode | Read one key. |
| `0x92` | `07 92 layer row col kc_hi kc_lo` | 4 B | Set one runtime key in transient RAM. Lost on power loss; disabled in M1. |
| `0x93` | `02 93` | 4 B | Reset keymap to the factory layout. |
| `0xA1/0xA2/0xA3/0xA0` | stock update dialect | as stock | Staging/commit/reboot protocol, blocked by diagnostics builds. No restoration guarantee. |

Commands `0x86`/`0x88`/`0x8a`/`0x8c`/`0x8d` (backlight delay, idle, other settings) are not implemented and return the unsupported-command status. They are future work, not a regression.

The experimental dispatcher implements two application-dependent channels, neither proven on the custom image in the physical case:

1. **The Apple session (EP3)**: once the MFi handshake completes, the phone app opens an EA session and the dispatcher runs on the raw data channel.
2. **The Mac dispatcher (EP2)**: a non-iPhone host can address the application dispatcher without an Apple session. This requires a running application and is not independent recovery.

The old [flashing guide](FLASHING.md) is retired.

## iOS app

The [Clicks Inspector app](../../../ios/ClicksInspector/README.md) defaults to diagnostics-only builds. Legacy write controls cannot bypass the controller gate.
