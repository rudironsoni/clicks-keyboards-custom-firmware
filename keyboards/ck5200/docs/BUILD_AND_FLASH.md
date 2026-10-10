# Build and inspect the CK-5200 image

Build instructions, image contract, and protocol reference. For the full flashing procedure, risk analysis, and recovery steps, see [FLASHING.md](FLASHING.md).

## Build and test

macOS needs CMake, Ninja, Python, and libusb. The setup script downloads a checksum-verified RISC-V toolchain and pins QMK, TinyUSB, and the WCH SDK. Run from the repository root:

```sh
bash keyboards/ck5200/scripts/setup.sh
bash keyboards/ck5200/scripts/test.sh
bash keyboards/ck5200/scripts/build.sh
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
| `0x82` | `03 82 raw` | 4 B | Set backlight brightness (stock formula, persists). |
| `0x84` | `02 84` | 5 B, payload `raw` | Read backlight brightness. |
| `0x90` | `02 90` | 8 B, payload `layers rows cols flags` | Keymap dimensions. |
| `0x91` | `05 91 layer row col` | 6 B, payload keycode | Read one key. |
| `0x92` | `07 92 layer row col kc_hi kc_lo` | 4 B | Set one key. Durable immediately: QMK dynamic keymap over wear-leveled EEPROM (four flash pages at `0x0800F800`). |
| `0x93` | `02 93` | 4 B | Reset keymap to the factory layout. |
| `0xA1/0xA2/0xA3/0xA0` | stock update dialect | as stock | Stage an image and reboot to install. Identical to stock, so the same flow flashes the custom image onto stock firmware and restores stock onto the custom firmware. |

Commands `0x86`/`0x88`/`0x8a`/`0x8c`/`0x8d` (backlight delay, idle, other settings) are not implemented and return the unsupported-command status. They are future work, not a regression.

The dispatcher is reachable through two independent channels:

1. **The Apple session (EP3)**: once the MFi handshake completes, the phone app opens an EA session and the dispatcher runs on the raw data channel.
2. **The Mac fallback (EP2)**: any non-iPhone host that sends dispatcher-framed packets on EP2 reaches the same dispatcher without a session. This is the recovery path that survives a broken session stack.

See [FLASHING.md](FLASHING.md) for the full procedure and risk analysis.

## iOS app

The Clicks Inspector app (see [ios/ClicksInspector/README.md](../../ios/ClicksInspector/README.md)) implements all commands over the EASession, plus a self-test, key test, layout preview, and one-tap flash/restore with both images bundled.
