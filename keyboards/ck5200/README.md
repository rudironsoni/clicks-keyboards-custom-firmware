# CK-5200 for iPhone 15 Pro Max

This is an experimental custom QMK port. Its required end result is a working keyboard on iOS, with custom keys and layers. Desktop enumeration alone is not acceptance.

**Custom flashing is blocked.** The stock USB session, independent recovery, physical key map, and iOS behavior remain unverified. See [STATUS.md](STATUS.md) for each work package.

## Build and inspect

From the repository root:

```sh
bash keyboards/ck5200/scripts/setup.sh
bash keyboards/ck5200/scripts/test.sh
bash keyboards/ck5200/scripts/build.sh
keyboards/ck5200/.venv/bin/python keyboards/ck5200/tools/ck5200_usb.py inspect
bash keyboards/ck5200/scripts/flash-qmk.sh
```

The result is `keyboards/ck5200/build/ck5200_qmk.bin`, plus ELF, HEX, and map files. The application links at `0x00002000` to match the verified stock image. The build checks linked addresses, RAM bounds, vectors, and image size.

The diagnostic keymap maps the proposed electrical matrix positions to A-Z and 1-0. No normal Clicks layout is claimed until the physical mapping is verified.

`flash-qmk.sh` never opens USB. `inspect` reads descriptors without configuring or claiming the device. An optional `inspect --query-version` sends the stock read-only command `02 03`; the connected iPhone case currently returns an accessory handshake instead of a version reply.

## Stock application

```sh
bash keyboards/ck5200/scripts/revert-stock.sh --download-only
```

This preserves and verifies the official stock application. It is not a full flash backup and does not prove recovery. Read [RECOVERY.md](docs/RECOVERY.md) before any hardware write.

## Required behavior

See [USB_AND_IPHONE.md](docs/USB_AND_IPHONE.md) for typing, layers, modifiers, sleep/wake, backlight, charging, and restoration checks on the iPhone 15 Pro Max. These are required hardware checks, not optional later features.
