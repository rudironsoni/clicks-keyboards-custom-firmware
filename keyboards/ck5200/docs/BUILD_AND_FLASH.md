# Build and flash the CK-5200 firmware

This is the current QMK test build for the CK-5200.

I am keeping the first version intentionally small so there are fewer things to debug at once.

It currently uses:

- QMK core pinned to a known commit
- CH32V20x D6 startup code
- 144 MHz internal clock
- TinyUSB on the CH32V20x USBFS controller
- standard USB keyboard HID
- the same USB firmware-update endpoints used by the stock keyboard
- a 6x6 matrix scanner on PA0..PA5 and PB0..PB5
- a hard firmware-size limit of `0x6A00`

The first keymap is only for testing the matrix.

## Build it

You need:

- CMake
- Ninja
- Python 3
- a RISC-V embedded GCC toolchain

The compiler needs to be available as either:

```text
riscv-none-elf-gcc
```

or:

```text
riscv-none-embed-gcc
```

Then run:

```sh
scripts/bootstrap.sh
python3 -m pip install -r tools/requirements.txt
scripts/build.sh
```

A successful build should produce:

```text
build/ck5200_qmk.elf
build/ck5200_qmk.bin
build/ck5200_qmk.hex
build/ck5200_qmk.map
```

The build stops if the firmware is larger than `0x6A00`.

The validator also checks that the binary starts with the expected RISC-V jump instruction.

## Check the flash plan without writing anything

Run:

```sh
scripts/flash-qmk.sh
```

This validates the image and prints the USB packet plan.

It does not flash the keyboard.

## Flash over the normal USB connection

Run:

```sh
FLASH=YES scripts/flash-qmk.sh
```

The script will:

1. find the CK-5200 USB device
2. find the firmware-update endpoints
3. tell the keyboard how large the image is
4. send the image in 32-byte chunks
5. check the keyboard's response after every chunk
6. finish the transfer
7. ask the keyboard to reboot

There is no guaranteed recovery path after the reboot.

That is why I want the generated ELF, map file and BIN checked before using this on the keyboard.

## Why the custom firmware keeps the update interface

I want custom builds to remain updateable through the normal USB connection.

So the QMK firmware includes the same A1, A2, A3 and A0 command flow used by the stock firmware, along with the same staging area in flash.

[INFERENCE] The official firmware also uses a normal system reset after staging an update. That suggests the final copy into the active firmware area happens outside the main application code.

That is encouraging, but it is still not something I consider proven until a custom image has gone through the full process on the actual keyboard.

## iPhone support

A QMK build working on a Mac or PC does not automatically mean it will work through the Clicks case on an iPhone.

The stock firmware contains Apple-specific accessory handling that plain QMK does not provide.

The first goal is therefore much smaller: boot the chip, enumerate over USB, scan keys correctly and send normal keyboard reports.

After that works, I can test what the iPhone accepts.
