# Build and regular-USB flash

## Current target

The bring-up image is deliberately small:

- QMK core pinned to `3d4da6de29c8635c9cd232ce456d8fec8d31921b`.
- CH32V20x D6 startup, appropriate to CH32V203C8-class parts.
- 144 MHz HSI clock. This avoids depending on an unverified external crystal.
- TinyUSB on the USBFS peripheral at `0x50000000`, the same USB block used by stock firmware.
- Standard USB HID boot keyboard on `0x81/0x01`.
- Recovered Clicks updater protocol on vendor bulk `0x82/0x02`.
- 6x6 active-high matrix scan using PA0..PA5 and PB0..PB5.
- Link-time flash limit of `0x6A00`, matching the recovered updater limit.

The first keymap is diagnostic because the physical-key to matrix-coordinate map has not been measured yet.

## Build

Install CMake, Ninja, Python 3 and a RISC-V embedded GCC toolchain that exposes `riscv-none-elf-gcc` or `riscv-none-embed-gcc`.

Then:

```sh
scripts/bootstrap.sh
python3 -m pip install -r tools/requirements.txt
scripts/build.sh
```

Expected outputs:

```text
build/ck5200_qmk.elf
build/ck5200_qmk.bin
build/ck5200_qmk.hex
build/ck5200_qmk.map
```

The build fails if the linked image exceeds `0x6A00`. `tools/validate_image.py` also checks the binary size and that offset zero is a RISC-V JAL reset jump.

## Dry-run the USB update

```sh
scripts/flash-qmk.sh
```

That validates the image and prints the exact A1/A2/A3/A0 packet plan. It does not write USB.

## Flash

```sh
FLASH=YES scripts/flash-qmk.sh
```

The host tool then:

1. finds VID/PID `352e:2306`,
2. finds bulk endpoints `0x02/0x82`,
3. sends A1 with the image size,
4. sends the image in 32-byte A2 chunks,
5. validates the device status and accepted offset after every packet,
6. sends A3 and validates the committed size,
7. sends A0 to reset.

There is no host-side rollback after A0. Do not run the write until the generated ELF/BIN has passed static checks and you accept the device-brick risk.

## Why the custom image keeps the USB updater

The QMK image implements the same A1/A2/A3/A0 interface and the same staging layout as stock firmware. Static analysis of stock 1.2.2 shows that A0 ultimately performs the normal CH32V20x `NVIC_SystemReset` operation. The stock application's reset path does not contain the staging-copy operation.

[INFERENCE] Because official staged updates still install after that normal reset, the actual staging-to-active installation happens outside the normal application image, likely in WCH/system boot code. This is strong evidence that the same staging contract can continue to work after replacing the application, but it has not yet been proven on the physical CK-5200 with a custom image.

## iPhone warning

USB enumeration on a PC is not proof that the keyboard will work through the iPhone case. Stock firmware contains iAP2/MFi-related strings and interfaces. The first image intentionally proves MCU startup, USB, matrix scanning and QMK before attempting to reproduce that Apple-specific path.
