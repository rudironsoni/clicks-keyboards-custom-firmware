# CK-5200 hardware notes

These are the hardware details I am using for the QMK port.

They come from the stock 1.2.2 firmware, the Clicks Android updater and the USB traffic I captured while checking the official update flow.

## Things I am confident about

- The firmware is 32-bit RISC-V code.
- The stock firmware image is a raw binary, not a larger installer package.
- The updater accepts images up to `0x6A00` bytes.
- USB VID/PID is `352e:2306`.
- Firmware updates use bulk OUT `0x02` and bulk IN `0x82`.
- The update commands are A1 start, A2 data, A3 finish and A0 reboot.
- The temporary firmware area starts at `0x08008A00`.
- Firmware bytes start at `0x08008A04`.
- The stock code writes flash in 256-byte pages.
- Some saved configuration data is referenced around `0x0800F700`.
- The key matrix uses PA0..PA5 and PB0..PB5.
- PA0..PA5 are inputs with pull-down resistors.
- PB0..PB5 are outputs.
- The keyboard drives one PB line high, then reads the six PA inputs.

## MCU

[INFERENCE] The chip looks like a 64 KiB CH32V203-class device based on the memory layout and the code I have inspected.

That fits a CH32V203C8T6 very well, but I have not physically read the marking on the chip yet, so I do not want to call the exact part number confirmed.

## Things I still need to check

- exact MCU model and package
- what code installs the staged firmware after reboot
- whether that installer accepts any valid custom binary
- whether an iPhone accepts the QMK USB device without the stock Apple-specific handling
- which physical key maps to each position in the 6x6 matrix

The diagnostic QMK keymap exists specifically to answer that last question without guessing.
