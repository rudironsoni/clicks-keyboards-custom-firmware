# CK-5200 status, 2026-10-12

**Target: Clicks for iPhone 15 Pro Max. FLASHING IS BLOCKED.** The firmware is now built for the CH32X035 family the stock firmware proves (USB `0x40023400`, IRQ 45, 48 MHz HSI): see [docs/X035-PORT.md](docs/X035-PORT.md) for the port evidence and [docs/reviews/2026-10-11-flash-safety-review.md](docs/reviews/2026-10-11-flash-safety-review.md) for the standing veto. The block lifts only after an independent recovery path is demonstrated on this specific keyboard ([docs/recovery-research.md](docs/recovery-research.md): the PC17 BOOT investigation is the live lead). Every family-independent software defect the three reviewers found is fixed and covered by committed regression tests; the iOS app authorizes only the exact stock V122 artifact.

## Work package results

| Package | Result | Remaining requirement |
| --- | --- | --- |
| Preserve evidence | Stock V121 and V122 applications archived, official APK, live USB descriptors and handshake inspected. No bootloader image is published by the vendor. Investigation saved in `docs/ck5200-investigation.md`. | Downloaded applications are not a full device backup. |
| Stock transport | Exact V122 handlers traced. Rudi's physical iPhone screenshots confirm all seven fixed reads completed through the local iOS inspector. Raw Mac USB query still returns the handshake. Offline parser audit found no memory-disclosure defect. | No memory-read service or update session is established; software-only readback is closed. |
| Apple session stack | Fully decoded from the V122 image and ported byte-faithfully: attach/link framing, sync exchange, Identify, the MFi auth relay (bit-banged I2C to the auth chip, not SPI), session open, and the raw EP3 companion channel. Decode record tracked in `docs/STOCK_DECODE.md`; extraction tooling committed under `tools/stock_audit/`. | Hardware run: the first flash is the first live test of the stack and the auth-chip bus. |
| Independent recovery | Documented programmer readback route and region checklist in `docs/RECOVERY.md`. | Board/chip/pads, debug probe, protection state, full dump and restore test are missing. |
| Image layout | Application linked at `0x2000`, maximum `0x6a00`, end `0x8a00`. ELF startup/vector/RAM checks and exact ELF-to-BIN comparison pass. | Stock installer acceptance has not been tested. |
| HID and staging | Pending HID reports retry, GET_REPORT/idle/resume behavior implemented and now proven by the committed protocol harness (the review found the original completion-callback guard froze the scan after one tap; fixed), vendor reply retries, staging failures latch, and A1 invalidates the committed staging length. Unsupported remote wake advertisement removed. | Synchronous macros can coalesce reports; no real USB timing, power-loss, or firmware-log validation. |
| Configuration and scripts | Removed duplicate QMK config and unused keymap rules. Flash wrapper is offline-only; install refuses writes; stock download-only path works. The custom firmware now has a QMK dynamic keymap (4 layers, 6x6) with wear-leveled EEPROM persistence at `0x0800F800`. Build uses LTO and the heap-free `sym_defer_g` debounce to fit the session stack. | iPhone runtime checks remain open. |
| Phone app | The iOS app implements firmware identify, flashing (stock update dialect, works from stock), bundled stock restore, and the keymap editor; protocol layer covered by a runnable Swift check. | The phone app's custom-firmware path needs the first flash to succeed; the session stack is ported but hardware-unverified. |
| Verification | Tests, firmware build, full image checks, and host harnesses pass locally, including two harnesses ported from the adversarial review (real QMK core behavior; real `protocol.c` against a TinyUSB-faithful stub). The restore-cycle harness executes the complete A1/A2/A3/A0 flow with the real stock and custom images. | No CI run; no physical-device test. The QMK-core harness's T9 row documents the known `sym_defer_g` chatter-starvation limitation. |
| Hardware and iOS | The physical keymap (all 36 stock crossings), the SYM and cursor layers, the consumer keys including the iOS on-screen-keyboard toggle, and the TIM1 backlight with persisted brightness are decoded and ported. iPhone 15 Pro Max acceptance checklist recorded. | Runtime checks on the physical device remain open: first flash is the first live test. |

## Local verification

Commands completed with exit code 0:

```sh
bash keyboards/ck5200/scripts/test.sh
bash keyboards/ck5200/scripts/build.sh
```

Tests: **5 Python tests + 2 keymap-layout tests passed**; the C update-protocol, keymap (including the brightness commands), backlight (TIM1 registers against the decode), session replay (`ok 15`, including watchdog-gate and short-SYN scenarios), auth I2C waveform + timing (`ok 4`), consumer (little-endian wire order), restore (including A1 marker invalidation) harnesses all pass, plus the two review harnesses: the real pinned QMK core on a host flash model (typing, modifiers, `LT(2,KC_J)`, sticky SYM, consumer encoding, reboot persistence with zero page erases, chatter starvation, 6KRO, 200k-packet fuzz) and the real `protocol.c` against a TinyUSB-0.21-faithful completion stub (chord/pending lifecycle, idle resend, suspend/resume, GET_REPORT, plain tap). The keymap test asserts every crossing of every layer against the decoded stock table, and the backlight test asserts every TIM1 register value.

Final local build (CH32X035 re-target, X035 SDK pinned at `c6253942`):

```text
file     build/ck5200_qmk.bin
size     24124 bytes (0x5e3c)
maximum  27136 bytes (0x6a00)
base     0x2000
sha256   7799f866ee18ea4c3ba93852b5c6cb7b32864a29efc88fc79301af7f84d09623
```

Flash use is 88.90 percent of the application region on the X035 build. RAM use is 29.38 percent. Persistence is runtime-only (transient RAM EEPROM): no writable flash region exists on this part for this firmware, and flash-backed keymaps stay disabled until a storage partition is demonstrated. These checks establish the offline image layout, not hardware execution.

The temporary `/tmp` ASan harnesses an earlier session used to claim protocol coverage are superseded: the 2026-10-11 review proved their completion-callback model was idealized (it passed while the production path froze the scan after one tap) and their sources no longer exist. The committed `tests/ck5200_protocol_task_test.c` replaces them with a TinyUSB-0.21-faithful stub.

The offline flash wrapper also passed with `FLASH=YES`; it still produced only a packet plan. The stock download-only path verified the V122 hash without opening USB. A mocked stock-device flash attempt was rejected before any transaction. Shell syntax, Python compilation and `git diff --check` passed during this work.

## Device boundary

Rudi's screenshots establish stock External Accessory discovery, an open session, and successful version and six settings/status reads at 14:39 through 14:43. Reported identity is CK-5200, hardware `1.2.0`, firmware `1.2.2`. This is a stock-app experiment, not custom-firmware acceptance. The exact returned payloads, command audit, and zero-byte firmware-backup coverage are recorded in [USB_AND_IPHONE.md](docs/USB_AND_IPHONE.md#stock-ios-session-and-read-command-investigation-2026-10-08).

Live checks read USB descriptors, sent the raw USB version query `02 03`, and completed the seven fixed reads through the iOS app. No A1/A2/A3/A0, erase, flash, reset, boot-mode or protection-change command was sent during this investigation. The custom firmware has not been installed.

Skipped checks: full firmware extraction, restore, boot acceptance, matrix mapping, backlight, charging, sleep/wake, phone enumeration, layers and companion-app operation. These require missing physical access, a verified recovery path, and separate flash consent. **I cannot verify iOS compatibility.**

Use [the recovery investigation](docs/RECOVERY.md) to establish full readback first. Use [the iPhone checklist](docs/USB_AND_IPHONE.md) for final acceptance. A successful Mac build or Android protocol trace does not satisfy that checklist.
