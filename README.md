# Clicks keyboards custom firmware

An iOS app and experimental custom firmware for the Clicks CK-5200 case for **iPhone 15 Pro Max**.

The product goals are official stock installation, separately authorized custom-firmware transitions, and iPhone-only stock recovery. Preserve the only keyboard and reuse maintained upstream code.

**Custom firmware readiness: NO-GO.** Host tests and builds do not prove boot, USB, authentication, key mapping, power, or recovery on this keyboard. The handwritten accessory-side `iap2.c` and `iap2_auth.c` remain experimental. No custom image is authorized.

**R3 independent recovery: UNMET.** The proprietary installer remains unread. An application key chord cannot recover an application that cannot execute. Debug and factory ISP research remain contingencies, not the iPhone-only recovery architecture.

Current work: [Stock Characterization in Clicks Inspector](ios/ClicksInspector/STOCK_CHARACTERIZATION.md). Collect real stock observations before firmware replacement. This mode cannot flash or change settings. The separate firmware controller retains its existing stock-only authorization.

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

- [Clicks Inspector: stock evidence and gated firmware controls](ios/ClicksInspector/README.md).
- [Current status and all eight work packages](keyboards/ck5200/STATUS.md).
- [**Flashing: procedure, risks, and recovery**](keyboards/ck5200/docs/FLASHING.md).
- [Build and image checks](keyboards/ck5200/docs/BUILD_AND_FLASH.md).
- [Stock decode record](keyboards/ck5200/docs/STOCK_DECODE.md).
- [Stock recovery requirements](keyboards/ck5200/docs/RECOVERY.md).
- [iPhone acceptance and USB evidence](keyboards/ck5200/docs/USB_AND_IPHONE.md).
- [Hardware evidence and open questions](keyboards/ck5200/docs/HARDWARE.md).
- [Pre-repair investigation](keyboards/ck5200/docs/ck5200-investigation.md).

The source lives under `keyboards/ck5200`. It combines pinned QMK core files, a custom WCH platform, and TinyUSB through CMake. Local source dependencies, toolchains, stock downloads, and build output are ignored by Git.
