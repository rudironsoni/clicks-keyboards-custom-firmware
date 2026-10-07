# CK-5200 hardware facts used by this port

These are from the supplied stock 1.2.2 binary, Android APK, and HAR unless marked otherwise.

## Proven from the stock firmware

- Executable ISA: RV32 I/M/C with WCH/QingKe-specific CSR writes.
- Stock image is a raw executable linked at the low flash alias. No outer firmware container header was found.
- Active-image updater limit: `0x6A00` bytes.
- USB VID/PID: `352e:2306`.
- Updater transport: bulk OUT `0x02`, bulk IN `0x82`, 64-byte endpoints.
- Updater commands: A1 begin, A2 data, A3 finish, A0 reboot.
- Staging base: `0x08008A00`; staged image begins at `0x08008A04`.
- Stock staging writes 256-byte flash pages.
- Persistent configuration is referenced around `0x0800F700`.
- Matrix scan senses PA0..PA5 and drives PB0..PB5.
- PA0..PA5 are configured as pull-down inputs.
- PB0..PB5 are configured as push-pull outputs. Stock scan drives one PB line high and reads the six PA bits.

## Strong inference

The device is a 64-KiB CH32V203-class part. Public CH32V203C8T6 definitions are 64 KiB flash / 20 KiB SRAM and fit the observed address use exactly, but the exact package marking has not been physically read from this keyboard.

## Still unverified

- Exact MCU/package ID.
- What the reboot-time installer does between staged image and active image.
- Whether it accepts an arbitrary unsigned image.
- Whether the iPhone will accept a replacement firmware that omits the stock iAP2/MFi accessory path.
- Physical key to 6x6 electrical coordinate mapping. The diagnostic keymap is designed to measure this rather than guess it.
