# CK-5200 status, 2026-10-08

**Target: Clicks for iPhone 15 Pro Max. Custom firmware is not ready to flash.**

## Work package results

| Package | Result | Remaining requirement |
| --- | --- | --- |
| Preserve evidence | Stock V122 application, official APK, live USB descriptors and handshake inspected. Investigation saved in `docs/ck5200-investigation.md`. | Downloaded application is not a full device backup. |
| Stock transport | Exact V122 handlers traced. Rudi's physical iPhone screenshots confirm all seven fixed reads completed through the local iOS inspector. Raw Mac USB query still returns the handshake. | No memory-read service or update session is established. |
| Independent recovery | Documented programmer readback route and region checklist in `docs/RECOVERY.md`. | Board/chip/pads, debug probe, protection state, full dump and restore test are missing. |
| Image layout | Application linked at `0x2000`, maximum `0x6a00`, end `0x8a00`. ELF startup/vector/RAM checks and exact ELF-to-BIN comparison pass. | Stock installer acceptance has not been tested. |
| HID and staging | Pending HID reports retry, GET_REPORT/idle/resume behavior implemented, vendor reply retries, staging failures latch. Unsupported remote wake advertisement removed. | Synchronous macros can coalesce reports; no real USB timing, power-loss, or firmware-log validation. |
| Configuration and scripts | Removed duplicate QMK config and unused keymap rules. Flash wrapper is offline-only; install refuses writes; stock download-only path works. | Experimental custom-updater restore remains unverified. |
| Verification | Existing tests, firmware build, full image checks, and focused host C harnesses pass locally. | No new CI run or physical-device firmware test. |
| Hardware and iOS | iPhone 15 Pro Max acceptance checklist recorded. | Physical key map, usable layers, backlight, Apple accessory support and iPhone runtime checks remain open. |

## Local verification

Commands completed with exit code 0:

```sh
bash keyboards/ck5200/scripts/test.sh
bash keyboards/ck5200/scripts/build.sh
```

Tests: **5 Python tests passed**, C protocol test printed `ok`.

Final local build:

```text
file     build/ck5200_qmk.bin
size     19640 bytes (0x4cb8)
maximum  27136 bytes (0x6a00)
base     0x2000
sha256   c8c11dd35f06f38ec88b3b1f1c6c7e0acef6fcfd14dbb991e26356beea5b065c
```

The build passed full ELF/BIN validation. It still emits dependency warnings for QMK inline declarations, deprecated TinyUSB `tud_init`, and newlib syscall stubs; it is not warning-free. The prior zero-based binary was rejected by the new validator. These checks establish the offline image layout, not hardware execution.

Temporary ASan/UBSan C harnesses compiled the production `protocol.c`, update parser, and staging writer. They passed ordered key transitions, failed-release retry, GET_REPORT, idle, resume, busy vendor-response retry, and rejection after a simulated page-write failure. They stub the USB and flash drivers; they do not prove the actual drivers or flash hardware. Harness sources remain in `/tmp/ck5200-fw-io-harness/`, not the repository test suite.

The offline flash wrapper also passed with `FLASH=YES`; it still produced only a packet plan. The stock download-only path verified the V122 hash without opening USB. A mocked stock-device flash attempt was rejected before any transaction. Shell syntax, Python compilation and `git diff --check` passed during this work.

## Device boundary

Rudi's screenshots establish stock External Accessory discovery, an open session, and successful version and six settings/status reads at 14:39 through 14:43. Reported identity is CK-5200, hardware `1.2.0`, firmware `1.2.2`. This is a stock-app experiment, not custom-firmware acceptance. The exact returned payloads, command audit, and zero-byte firmware-backup coverage are recorded in [USB_AND_IPHONE.md](docs/USB_AND_IPHONE.md#stock-ios-session-and-read-command-investigation-2026-10-08).

Live checks read USB descriptors, sent the raw USB version query `02 03`, and completed the seven fixed reads through the iOS app. No A1/A2/A3/A0, erase, flash, reset, boot-mode or protection-change command was sent during this investigation. The custom firmware has not been installed.

Skipped checks: full firmware extraction, restore, boot acceptance, matrix mapping, backlight, charging, sleep/wake, phone enumeration, layers and companion-app operation. These require missing physical access, a verified recovery path, and separate flash consent. **I cannot verify iOS compatibility.**

Use [the recovery investigation](docs/RECOVERY.md) to establish full readback first. Use [the iPhone checklist](docs/USB_AND_IPHONE.md) for final acceptance. A successful Mac build or Android protocol trace does not satisfy that checklist.
