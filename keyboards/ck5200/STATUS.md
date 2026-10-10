# CK-5200 status, 2026-10-08

**Target: Clicks for iPhone 15 Pro Max. Custom firmware is not ready to flash.**

## Work package results

| Package | Result | Remaining requirement |
| --- | --- | --- |
| Preserve evidence | Stock V121 and V122 applications archived, official APK, live USB descriptors and handshake inspected. No bootloader image is published by the vendor. Investigation saved in `docs/ck5200-investigation.md`. | Downloaded applications are not a full device backup. |
| Stock transport | Exact V122 handlers traced. Rudi's physical iPhone screenshots confirm all seven fixed reads completed through the local iOS inspector. Raw Mac USB query still returns the handshake. Offline parser audit found no memory-disclosure defect. | No memory-read service or update session is established; software-only readback is closed. |
| Apple session stack | Fully decoded from the V122 image and ported byte-faithfully: attach/link framing, sync exchange, Identify, the MFi auth relay (bit-banged I2C to the auth chip, not SPI), session open, and the raw EP3 companion channel. Decode record tracked in `docs/STOCK_DECODE.md`; extraction tooling committed under `tools/stock_audit/`. | Hardware run: the first flash is the first live test of the stack and the auth-chip bus. |
| Independent recovery | Documented programmer readback route and region checklist in `docs/RECOVERY.md`. | Board/chip/pads, debug probe, protection state, full dump and restore test are missing. |
| Image layout | Application linked at `0x2000`, maximum `0x6a00`, end `0x8a00`. ELF startup/vector/RAM checks and exact ELF-to-BIN comparison pass. | Stock installer acceptance has not been tested. |
| HID and staging | Pending HID reports retry, GET_REPORT/idle/resume behavior implemented, vendor reply retries, staging failures latch. Unsupported remote wake advertisement removed. | Synchronous macros can coalesce reports; no real USB timing, power-loss, or firmware-log validation. |
| Configuration and scripts | Removed duplicate QMK config and unused keymap rules. Flash wrapper is offline-only; install refuses writes; stock download-only path works. The custom firmware now has a QMK dynamic keymap (4 layers, 6x6) with wear-leveled EEPROM persistence at `0x0800F800`. Build uses LTO and the heap-free `sym_defer_g` debounce to fit the session stack. | iPhone runtime checks remain open. |
| Phone app | The iOS app implements firmware identify, flashing (stock update dialect, works from stock), bundled stock restore, and the keymap editor; protocol layer covered by a runnable Swift check. | The phone app's custom-firmware path needs the first flash to succeed; the session stack is ported but hardware-unverified. |
| Verification | Existing tests, firmware build, full image checks, and focused host C harnesses pass locally. | No new CI run or physical-device firmware test. |
| Hardware and iOS | The physical keymap (all 36 stock crossings), the SYM and cursor layers, the consumer keys including the iOS on-screen-keyboard toggle, and the TIM1 backlight with persisted brightness are decoded and ported. iPhone 15 Pro Max acceptance checklist recorded. | Runtime checks on the physical device remain open: first flash is the first live test. |

## Local verification

Commands completed with exit code 0:

```sh
bash keyboards/ck5200/scripts/test.sh
bash keyboards/ck5200/scripts/build.sh
```

Tests: **5 Python tests + 2 keymap-layout tests passed**; the C update-protocol, keymap (including the brightness commands), backlight (TIM1 registers against the decode), session replay (`ok 13`), and auth I2C waveform (`ok 4`) harnesses all pass. The keymap test asserts every crossing of every layer against the decoded stock table, and the backlight test asserts every TIM1 register value.

Final local build (Apple session stack included):

```text
file     build/ck5200_qmk.bin
size     23388 bytes (0x5b5c)
maximum  27136 bytes (0x6a00)
base     0x2000
sha256   dad34db61f2978836b0f6c8a29e72f29d491fda0401bb1d0b45928b47906b12c
```

The build passed full ELF/BIN validation. Flash use is 86.19 percent of the application region after enabling LTO and switching to the heap-free `sym_defer_g` debounce; without those, adding the session stack exceeded the region. These checks establish the offline image layout, not hardware execution.

Temporary ASan/UBSan C harnesses compiled the production `protocol.c`, update parser, and staging writer. They passed ordered key transitions, failed-release retry, GET_REPORT, idle, resume, busy vendor-response retry, and rejection after a simulated page-write failure. They stub the USB and flash drivers; they do not prove the actual drivers or flash hardware. Harness sources remain in `/tmp/ck5200-fw-io-harness/`, not the repository test suite.

The offline flash wrapper also passed with `FLASH=YES`; it still produced only a packet plan. The stock download-only path verified the V122 hash without opening USB. A mocked stock-device flash attempt was rejected before any transaction. Shell syntax, Python compilation and `git diff --check` passed during this work.

## Device boundary

Rudi's screenshots establish stock External Accessory discovery, an open session, and successful version and six settings/status reads at 14:39 through 14:43. Reported identity is CK-5200, hardware `1.2.0`, firmware `1.2.2`. This is a stock-app experiment, not custom-firmware acceptance. The exact returned payloads, command audit, and zero-byte firmware-backup coverage are recorded in [USB_AND_IPHONE.md](docs/USB_AND_IPHONE.md#stock-ios-session-and-read-command-investigation-2026-10-08).

Live checks read USB descriptors, sent the raw USB version query `02 03`, and completed the seven fixed reads through the iOS app. No A1/A2/A3/A0, erase, flash, reset, boot-mode or protection-change command was sent during this investigation. The custom firmware has not been installed.

Skipped checks: full firmware extraction, restore, boot acceptance, matrix mapping, backlight, charging, sleep/wake, phone enumeration, layers and companion-app operation. These require missing physical access, a verified recovery path, and separate flash consent. **I cannot verify iOS compatibility.**

Use [the recovery investigation](docs/RECOVERY.md) to establish full readback first. Use [the iPhone checklist](docs/USB_AND_IPHONE.md) for final acceptance. A successful Mac build or Android protocol trace does not satisfy that checklist.
