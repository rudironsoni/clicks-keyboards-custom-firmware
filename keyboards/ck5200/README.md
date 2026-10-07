# CK-5200

This folder contains my work on getting QMK running on the Clicks CK-5200 Keyboard Case.

The keyboard is already great as-is. I want to keep that hardware and give it more flexibility with QMK.

The main goals are:

- custom keymaps
- layers
- shortcuts and macros
- a firmware update flow that works over the normal USB connection
- no need to open the case for every firmware test

## What I have figured out so far

The stock keyboard scans a 6x6 key matrix.

It uses:

- PA0..PA5 as inputs
- PB0..PB5 as outputs

The stock firmware drives one PB line at a time and reads PA0..PA5. The QMK matrix scanner in this folder follows the same pattern.

I also found the regular USB path used by the official firmware updater. That gives us a practical way to send firmware to the keyboard without soldering or opening the case.

## Check the keyboard over USB

Install PyUSB:

```sh
python3 -m pip install -r tools/requirements.txt
```

Then:

```sh
python3 tools/ck5200_usb.py inspect
```

This only checks that the keyboard and firmware-update interface are visible. It does not write anything.

## Inspect a firmware image

For the stock 1.2.2 firmware:

```sh
python3 tools/ck5200_usb.py packets iKeyboard_CK-5200_V122_120.bin
```

That image should produce:

```text
size        0x48d8
A2 packets  583
start       06 a1 00 00 48 d8
finish      06 a3 00 00 48 d8
reboot      02 a0
```

## Flash over normal USB

The write command is deliberately explicit:

```sh
python3 tools/ck5200_usb.py flash firmware.bin \
  --confirm CK-5200 \
  --allow-unknown-image
```

The tool checks every response from the keyboard while sending the image.

For the known stock 1.2.1 and 1.2.2 images, `--allow-unknown-image` is not needed.

## QMK test firmware

The first QMK keymap is a diagnostic one.

Each position in the 6x6 matrix sends a different character. That makes it easy to press every physical key and build the real key map without guessing which switch is connected where.

The firmware source is under `firmware/`.

## What still needs to work

QMK does not have a ready-made CH32V203 target, so this project has to provide the low-level startup, USB and hardware glue itself.

That work is in progress now.

See [STATUS.md](STATUS.md) for what already works and what still needs testing.

Also read [docs/USB_AND_IPHONE.md](docs/USB_AND_IPHONE.md) before assuming that a QMK build that works on a Mac or PC will automatically work with an iPhone.
