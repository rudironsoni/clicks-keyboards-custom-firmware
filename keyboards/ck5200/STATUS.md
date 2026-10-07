# Status

## Implemented

- Recovered stock A1/A2/A3/A0 regular-USB firmware protocol.
- Host flasher with response status and accepted-offset validation.
- Target-side implementation of the recovered updater protocol.
- Stock staging layout at `0x08008A00`, 256-byte flash pages, image bytes from `0x08008A04`.
- Recovered 6x6 electrical matrix, PA0..PA5 inputs and PB0..PB5 driven outputs.
- QMK custom matrix scanner and 36-position diagnostic keymap.
- CH32V20x D6 startup/linker build target with image capped at `0x6A00`.
- CH32V20x timer/wait/GPIO glue.
- TinyUSB CH32V20x USBFS integration.
- Standard HID keyboard interface.
- Vendor updater interface on the recovered `0x02/0x82` endpoints.
- Build, image validation, dry-run and explicit regular-USB flash scripts.
- Static confirmation that stock A0 leads to the normal `NVIC_SystemReset` value and the normal application reset path does not perform the staging copy.

## Verified locally in this analysis environment

- Host packet generation against the supplied stock 1.2.2 image.
- Python protocol tests.
- Target update-protocol parser C unit test.
- Same-size one-byte stock probe generation.

## Not yet verified

There is no claim that `ck5200_qmk.bin` has been successfully cross-compiled in this environment. The environment does not contain the required RISC-V embedded GCC toolchain or the pinned external source trees.

The next validation gates on a machine with the toolchain are:

1. `scripts/bootstrap.sh`
2. `scripts/build.sh`
3. inspect `build/ck5200_qmk.map` and disassemble `build/ck5200_qmk.elf`
4. confirm binary is below `0x6A00`
5. enumerate on a sacrificial/recoverable target if available
6. only then run `FLASH=YES scripts/flash-qmk.sh` on the CK-5200
7. record every physical key using the diagnostic keymap
8. replace the diagnostic layout with the real Clicks layout
9. investigate iAP2/MFi only if iPhone does not accept standard HID

The largest remaining uncertainty is not the A1/A2/A3 transport. It is whether the boot-time installer accepts an arbitrary custom image and whether standard HID is accepted by the iPhone path.
