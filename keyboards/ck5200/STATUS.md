# CK-5200 status

## Working in code

These parts are in the repo now:

- USB firmware update packets for A1, A2, A3 and A0
- host-side USB flash tool
- checks for device status and accepted byte offsets while flashing
- the same firmware staging area used by the stock keyboard
- 6x6 matrix scanner using PA0..PA5 and PB0..PB5
- a 36-key diagnostic QMK keymap
- CH32V20x startup and linker setup
- basic timer, wait and GPIO code
- TinyUSB support for the CH32V20x USBFS controller
- standard USB keyboard HID
- the firmware-update USB interface on endpoints `0x02` and `0x82`
- build scripts and image checks

## Tests that already pass

- packet generation for the stock 1.2.2 image
- Python protocol tests
- the C test for the firmware-update command parser
- generation of a same-size one-byte stock test image

## Still missing or not proven

The big one is the actual custom firmware binary.

I still need a clean successful cross-build with the RISC-V toolchain and then I need to inspect the ELF, map file and final BIN before I trust it.

After that, the plan is:

1. build `ck5200_qmk.bin`
2. confirm it stays below the keyboard's `0x6A00` firmware-size limit
3. inspect the generated machine code and memory map
4. test USB enumeration
5. test the diagnostic matrix keymap
6. map every physical key
7. replace the diagnostic layout with the real Clicks layout
8. test whether the iPhone accepts the QMK USB device

The biggest unknown is what happens after the keyboard reboots with a newly staged custom image.

The USB transfer itself is much better understood now. The reboot and install step is the part I still do not want to guess about.
