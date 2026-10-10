# Build and inspect the CK-5200 image

Custom installation is blocked until the stock transport and independent recovery are verified. These instructions build and inspect files; they do not establish iOS compatibility.

## Existing tools

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

The old zero-based image must fail validation. The stock application can be checked without an ELF, but that is a narrower check and is labeled as such.

## USB reads

```sh
keyboards/ck5200/.venv/bin/python keyboards/ck5200/tools/ck5200_usb.py inspect
```

This does not send SET_CONFIGURATION, detach drivers, or claim an interface. It establishes descriptor presence only.

The optional `inspect --query-version` command sends `02 03`, a read-only stock version request. It currently fails against the connected iPhone firmware with `ff 55 02 00 ee 10`. The tool records the bytes and stops. See [USB_AND_IPHONE.md](USB_AND_IPHONE.md).

## Installation and restoration

`install.sh` refuses installation. `flash-qmk.sh` remains offline even if `FLASH=YES` is set. The direct Python updater refuses stock-device update commands because the stock iPhone session is unsupported. Do not remove this check merely because the image builds.

The existing Python update implementation remains available for the experimental custom updater only. Its status, command, offset, and commit-size checks remain mandatory. Neither a descriptor version nor an explicit confirmation proves that recovery works.

Use `scripts/revert-stock.sh --download-only` to preserve the stock application. Actual transfer, power-loss behavior, boot acceptance, and stock restoration remain unverified. See [RECOVERY.md](RECOVERY.md).

## Custom firmware protocol, 2026-10-09

The custom image answers the stock framing `[len, cmd, ...]` on its vendor bulk endpoint, with replies `[len, 0x02, cmd, status, payload...]` (big-endian values, matching the stock protocol and QMK's dynamic keymap layout):

| Command | Request | Reply | Meaning |
| --- | --- | --- | --- |
| `0x03` | `02 03` | 8 bytes, payload `51 4d 00 01` ("QM") | Identifies the custom firmware. Stock answers `01 20 01 22`, so the app gates all writes on this reply. |
| `0x90` | `02 90` | 8 bytes, payload `layers rows cols flags` | Keymap dimensions. |
| `0x91` | `05 91 layer row col` | 6 bytes, payload keycode | Read one key. |
| `0x92` | `07 92 layer row col kc_hi kc_lo` | 4 bytes | Set one key. Durable immediately: QMK dynamic keymap over wear-leveled EEPROM (four flash pages at `0x0800F800`). |
| `0x93` | `02 93` | 4 bytes | Reset keymap to the factory layout. |
| `0xA1/0xA2/0xA3/0xA0` | stock update dialect | as stock | Stage an image and reboot to install. Identical to stock, so the same flow flashes the custom image onto stock firmware and restores stock onto the custom firmware. |

The iOS app (see `ios/ClicksInspector/README.md`) implements all of this over the EASession:

- Against stock firmware: identify shows "stock"; Flash custom firmware (.bin)... stages the built `keyboards/ck5200/build/ck5200_qmk.bin` through A1/A2/A3 and reboots the keyboard to install. This is the same transport the official app uses for updates.
- Against the custom firmware: the same update flow (reflash or restore the bundled stock V122 image), plus the keymap editor (layers, per-key hex keycodes, presets including MO(n), factory reset).
- The custom firmware does not implement the Apple accessory session (MFi link and authentication), so after the first flash the phone app can no longer open a session to it: keymap editing and phone-side restore require that session, and until it exists the Mac vendor-interface path remains the working channel for restore and configuration. Flashing from the phone while stock runs, and every protocol piece in the app, are implemented and testable now.

Verification: `scripts/test.sh` covers the update and keymap command handlers (C), and `ios/ClicksInspector/Tests/ck5200_protocol_check.swift` covers the app's protocol layer (run with `swiftc`, prints `ok`).
