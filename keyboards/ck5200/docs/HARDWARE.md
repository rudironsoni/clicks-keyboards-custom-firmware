# CK-5200 hardware evidence

The physical target is Rudi's Clicks case for iPhone 15 Pro Max. The connected device exposes `352e:2306` and `bcdDevice=0x0122`. USB identity alone does not identify the exact chip or board revision.

## Verified stock application layout

Analysis uses `iKeyboard_CK-5200_V122_120.bin`, SHA-256 `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`, 18,648 bytes. Obtain and verify it with `scripts/revert-stock.sh --download-only`.

- The first instruction is a RISC-V JAL to file offset `0x285c`.
- The reset vector word at file offset 4 is `0x00002000`.
- Standard AUIPC/ADDI instruction pairs at offsets `0x285c`, `0x2864`, and `0x2874` resolve to `gp=0x20000840`, `sp=0x20005000`, and initialized RAM destination `0x20000000` when the application starts at `0x2000`.
- At a zero load address, that RAM destination would instead be `0x1fffe000`.
- Stock A1 code at offsets `0x17a8..0x1808` checks the `0x6A00` size bound and builds an eight-byte response.
- Stock A2/A3 code references physical staging address `0x08008A00`. Its payload starts after the four-byte length. Stock configuration references include `0x0800F700`.

The resulting application interval is `[0x00002000, 0x00008A00)`. Flash physical addresses use the `0x08000000` alias in the stock write code. The maximum staged payload ends at `0x0800F404`; page-rounded writes end at `0x0800F500`.

[INFERENCE] The first 8 KiB holds a boot/install component. Its contents, checks, and recovery entry have not been read. The application binary is not evidence of a complete device backup.

## Chip and keyboard wiring

[INFERENCE] The code and address map are consistent with a CH32V203-class MCU with 20 KiB RAM. The exact part, package, board revision, flash protection, oscillator, and debug-pad layout remain **UNVERIFIED**.

[CORRECTION] A 64 KiB application-flash description is not a complete backup boundary. The [CH32V203 datasheet, table 2-1 note 1](https://cdn-learn.adafruit.com/assets/assets/000/131/418/original/CH32V203DS0.PDF?1721655401=) distinguishes the fast program area from additional flash, with 224 KiB combined. Confirm the actual part and cover its additional storage before calling a dump complete. See [RECOVERY.md](RECOVERY.md).

The inherited matrix scanner uses PA0..PA5 as pull-down inputs and PB0..PB5 as selected-high outputs. The earlier agent attributed this to stock analysis, but it did not supply a capture or annotated derivation. Treat the pin map, physical key map, scan delay, diode direction, and rollover behavior as **UNVERIFIED** until checked. Do not add a guessed normal keymap.

The build uses WCH D6 startup, 144 MHz HSI, and TinyUSB USBFS. Compilation checks API compatibility only. Hardware clock accuracy, interrupt nesting, scan timing, and USB operation still require board evidence and runtime tests.

## Reproduce the static checks

The existing `tools/validate_image.py` checks binary layout. The custom build also supplies its ELF for linked-section, vector, and RAM checks. See [BUILD_AND_FLASH.md](BUILD_AND_FLASH.md).

Some stock compressed instructions are not decoded correctly by the installed generic disassembler. Address conclusions above use standard 32-bit instruction decoding and stored vector words, not those unsupported mnemonics.

See [ck5200-investigation.md](ck5200-investigation.md) for the pre-repair findings and [USB_AND_IPHONE.md](USB_AND_IPHONE.md) for current transport evidence.
