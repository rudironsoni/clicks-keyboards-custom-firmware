# CK-5200 QMK port, bring-up package

This is the working port package for the Clicks CK-5200 keyboard case. It is based on the supplied 1.2.1/1.2.2 firmware, Clicks Android updater APK, and firmware manifest capture.

The useful new result is the matrix. Static analysis of stock 1.2.2 shows a 6x6 scan with PA0..PA5 as pull-down inputs and PB0..PB5 as driven outputs. The stock scanner selects one PB line high and reads PA0..PA5. `firmware/keyboard/matrix.c` reproduces that electrical behavior.

## Regular USB updater

Install PyUSB, then inspect the device without writing anything:

```sh
python3 -m pip install -r tools/requirements.txt
python3 tools/ck5200_usb.py inspect
```

Show the exact packet plan for the verified stock 1.2.2 image:

```sh
python3 tools/ck5200_usb.py packets iKeyboard_CK-5200_V122_120.bin
```

Expected facts for 1.2.2:

```text
size        0x48d8
A2 packets  583
start       06 a1 00 00 48 d8
finish      06 a3 00 00 48 d8
reboot      02 a0
```

A write is intentionally explicit:

```sh
python3 tools/ck5200_usb.py flash firmware.bin \
  --confirm CK-5200 \
  --allow-unknown-image
```

For a known stock 1.2.1/1.2.2 binary, `--allow-unknown-image` is not needed. The tool validates the response status byte and the A2 accepted offset after every chunk.

## First custom-flash gate

We still have not proved what the reboot-time installer does with an arbitrary image. The lowest-information-loss experiment is a same-size stock probe, not QMK:

```sh
python3 tools/make_stock_probe.py \
  iKeyboard_CK-5200_V122_120.bin \
  ck5200-v122-probe.bin
```

That changes one byte in the visible version string and nothing else. Do not treat it as risk-free. We still do not have an independent recovery dump.

## QMK bring-up target

The first QMK keymap is intentionally diagnostic. Each electrical matrix position emits a different printable key. Once the MCU/USB port boots, pressing every physical key gives the exact physical-to-electrical mapping without guessing traces.

The source is under `firmware/keyboard/`.

## What blocks the first QMK `.bin`

QMK itself does not currently contain a CH32V203 platform. The practical reference is `O-H-M2/qmk_port_ch582`, which already demonstrates how to run QMK core on a WCH RISC-V target with a custom platform and USB implementation. CH32V203 still needs its own startup/linker, platform hooks and USBFS glue.

`scripts/bootstrap.sh` pins that reference port and the OpenWCH CH32V20x SDK so the remaining work is reproducible.

See `STATUS.md` for the exact gap. See `docs/USB_AND_IPHONE.md` before assuming a generic QMK HID image will work when plugged back into the iPhone.
