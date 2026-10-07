# CK-5200

This is my QMK port for the Clicks CK-5200 Keyboard Case.

The keyboard is already awesome. I am doing this because I want more control over the keys without replacing the hardware.

## Before you start

You need a CK-5200 connected over USB.

On macOS:

```sh
brew install cmake ninja python libusb
```

On Ubuntu or Debian:

```sh
sudo apt-get update
sudo apt-get install -y cmake ninja-build python3 python3-venv python3-pip libusb-1.0-0
```

Clone the repo and enter this folder:

```sh
git clone https://github.com/rudironsoni/clicks-keyboards-custom-firmware.git
cd clicks-keyboards-custom-firmware/keyboards/ck5200
```

## 1. Set up the build

Run:

```sh
bash scripts/setup.sh
```

That downloads the pinned RISC-V compiler, QMK, TinyUSB and the CH32V20x SDK. It also creates a Python virtual environment for the USB tools.

The first run downloads a large compiler archive, so it takes a while.

## 2. Build the firmware

Run:

```sh
bash scripts/build.sh
```

The firmware will be here:

```text
build/ck5200_qmk.bin
```

The build also creates:

```text
build/ck5200_qmk.elf
build/ck5200_qmk.hex
build/ck5200_qmk.map
```

Run the tests separately with:

```sh
bash scripts/test.sh
```

## 3. Check that the keyboard is visible

Before flashing anything:

```sh
.venv/bin/python tools/ck5200_usb.py inspect
```

You should see the CK-5200 device and the firmware-update USB endpoints.

This command does not write to the keyboard.

## 4. See exactly what would be sent

Run:

```sh
bash scripts/flash-qmk.sh
```

That validates the QMK image and prints the firmware-update packet plan.

It still does not write anything.

## 5. Install the QMK firmware

When you are ready:

```sh
bash scripts/install.sh
```

The script asks you to type `CK-5200` before it sends anything.

It then uploads `build/ck5200_qmk.bin` through the normal USB connection and reboots the keyboard.

## Restore the original Clicks firmware

If the keyboard still exposes the firmware-update USB interface, restoring stock 1.2.2 is:

```sh
bash scripts/revert-stock.sh
```

The script downloads the official CK-5200 1.2.2 image, verifies its SHA-256, asks for confirmation, flashes it, and reboots.

The stock image it expects is:

```text
iKeyboard_CK-5200_V122_120.bin
SHA-256 8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8
```

There is one important limitation: this USB restore only works if the keyboard still boots far enough to expose the updater interface. If a custom firmware image prevents USB from coming up at all, the regular USB path cannot rescue it. That case needs a hardware programmer/debug connection.

## What the first QMK build does

The first keymap is deliberately boring.

The CK-5200 uses a 6x6 electrical matrix. Every matrix position sends a different character. That gives me a simple way to press every physical key once and map the real keyboard layout without guessing PCB traces.

Once that mapping is confirmed, the diagnostic keymap can be replaced with a normal Clicks layout and QMK features.

## iPhone support

The stock firmware has extra Apple accessory handling in addition to normal keyboard HID.

So the first milestone is to make QMK boot, enumerate, scan the keys and type correctly over USB. iPhone behavior comes after that.

See [STATUS.md](STATUS.md) for the current known state.
