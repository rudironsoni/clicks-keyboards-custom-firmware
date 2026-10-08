# Clicks keyboards custom firmware

Experimental QMK port for the Clicks Keyboard Case for **iPhone 15 Pro Max**, identified here as CK-5200. The goal is a usable iOS keyboard with custom keys and layers.

**Do not flash this build yet.** Stock USB session setup, independent recovery, the physical key map, and iPhone operation remain unverified. The current keymap is diagnostic, not a normal typing layout. A passing build does not establish compatibility.

## Safe local work

On macOS, install the build dependencies:

```sh
brew install cmake ninja python libusb
```

From this repository root:

```sh
bash keyboards/ck5200/scripts/setup.sh
bash keyboards/ck5200/scripts/test.sh
bash keyboards/ck5200/scripts/build.sh
keyboards/ck5200/.venv/bin/python keyboards/ck5200/tools/ck5200_usb.py inspect
bash keyboards/ck5200/scripts/flash-qmk.sh
bash keyboards/ck5200/scripts/revert-stock.sh --download-only
```

`inspect` reads descriptors without changing USB configuration. `flash-qmk.sh` is an offline image check and packet plan; it never writes, including when `FLASH=YES` is set. `revert-stock.sh --download-only` preserves the verified stock application without USB access.

`install.sh` currently refuses installation. The direct Python updater also refuses stock-device transfers before any A1/A2/A3/A0 commands because that session is not implemented.

## Project state

- [Current status and all eight work packages](keyboards/ck5200/STATUS.md).
- [Build and image checks](keyboards/ck5200/docs/BUILD_AND_FLASH.md).
- [Stock recovery requirements](keyboards/ck5200/docs/RECOVERY.md).
- [iPhone acceptance and USB evidence](keyboards/ck5200/docs/USB_AND_IPHONE.md).
- [Hardware evidence and open questions](keyboards/ck5200/docs/HARDWARE.md).
- [Pre-repair investigation](keyboards/ck5200/docs/ck5200-investigation.md).

The source lives under `keyboards/ck5200`. It combines pinned QMK core files, a custom WCH platform, and TinyUSB through CMake. Local source dependencies, toolchains, stock downloads, and build output are ignored by Git.
