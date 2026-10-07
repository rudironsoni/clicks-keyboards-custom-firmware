# Clicks keyboards custom firmware

[![CK-5200 build](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/workflows/ck5200.yml/badge.svg)](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/workflows/ck5200.yml)

I love the Clicks Keyboard Case and wanted to see how far I could take it with QMK.

This is a hobby project. The goal is to keep the hardware that already works really well and add the things I miss from a programmable keyboard: custom layouts, layers, shortcuts, macros, and whatever else turns out to be useful on a tiny phone keyboard.

The CK-5200 is the first keyboard I am working on. Other Clicks models can live next to it later.

## CK-5200 quick start

On macOS:

```sh
brew install cmake ninja python libusb

git clone https://github.com/rudironsoni/clicks-keyboards-custom-firmware.git
cd clicks-keyboards-custom-firmware/keyboards/ck5200

bash scripts/setup.sh
bash scripts/build.sh
```

The firmware is written to:

```text
build/ck5200_qmk.bin
```

Check the keyboard without writing anything:

```sh
.venv/bin/python tools/ck5200_usb.py inspect
bash scripts/flash-qmk.sh
```

Install the QMK build:

```sh
bash scripts/install.sh
```

Restore the official Clicks 1.2.2 firmware while the USB updater is still reachable:

```sh
bash scripts/revert-stock.sh
```

The full CK-5200 notes are in [keyboards/ck5200](keyboards/ck5200/).

## Current build

The CK-5200 CI build is passing.

The current QMK test image is 18,940 bytes, which is below the `0x6A00` limit used by the stock updater.

```text
ck5200_qmk.bin
SHA-256 f23d782b15b3b445d540243306cd404a4bc0ebd17f5df4084998cbf6a3053c3d
```

The latest build is also available from the `ck5200-firmware` artifact on the [CK-5200 Actions page](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/workflows/ck5200.yml).

This only means the source compiles, the tests pass, and the image passes the static checks. It does not mean I have proven the first custom flash and recovery path on real hardware yet.

## Repo layout

```text
keyboards/
  ck5200/
    firmware/   QMK + CH32V20x code
    scripts/    setup, build, install, restore
    tools/      USB updater and image tools
    tests/
    docs/
```

Each keyboard gets its own folder until there is something genuinely useful to share between models.
