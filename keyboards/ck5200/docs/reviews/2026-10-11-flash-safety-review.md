# Consolidated flash-safety review, 2026-10-11

Three independent adversarial reviewers audited commit `3586b76` in isolated
agent contexts: a Thermos-style code reviewer, a firmware anti-bricking
reviewer with veto authority over flashing, and a QMK specialist. Their
verbatim reports:

- `2026-10-11-reviewer-1-adversarial.md` - verdict: CHANGES REQUIRED
- `2026-10-11-reviewer-2-flash-safety.md` - verdict: **NO-GO, confirmed unsafe condition (veto)**
- `2026-10-11-reviewer-3-qmk.md` - verdict: CHANGES REQUIRED

## Decision

**Flashing the only CK-5200 is blocked.** Reviewer 2's veto is decisive and
was not overridden: the stock firmware proves at the register level that the
keyboard is a CH32X035-class device (USB peripheral `0x40023400`, USB IRQ 45,
48 MHz HSI, no PLL), while the custom image is built for a CH32V203-class
device (USB `0x50000000`, IRQ 59, 144 MHz PLL wait). The image hangs in
`SystemInit` before USB or any application code runs, and every documented
restore path requires a USB-enumerating firmware. The physical chip marking
has never been read; until it is, the family finding is a five-discriminator
register-level inference with very high confidence.

The order of work is fixed by the veto:

1. Read the physical chip marking (zero risk, decisive). Owner's action.
2. If CH32X035-class is confirmed: re-target the firmware (ch32x035 SDK, USB
   base `0x40023400`, IRQ 45, 48 MHz HSI, X035 startup/vector table) and add
   build-time family assertions so a wrong-family image can never build
   silently. This is a port, not a patch.
3. Establish physical debug evidence and a full flash dump per
   `../RECOVERY.md` (two verified copies), decode the never-read installer,
   and demonstrate a physical restore verified by readback.
4. Only then revisit flashing as a controlled experiment with residual risk,
   never as "ready". The app's stock-to-stock re-flash remains the first live
   milestone once the firmware exists for the right chip.

## Fix status of the software findings (this commit)

Every family-independent software defect the reviewers confirmed is fixed in
this change set, each with a regression test that the reviewers proved red
before the fix:

| Finding | Defect | Status |
| --- | --- | --- |
| R1 F-1 / R3 F1 (P1) | HID completion guard compared 10-byte transfers against the 8-byte report size; the pending flag was never cleared; one tap froze the scan forever and spammed the stuck report | Fixed (`protocol.c`), boot-protocol length also honored; `tests/ck5200_protocol_task_test.c` (ported from the review, P1-P5) green |
| R3 F2 (HIGH) | Consumer usage packed big-endian; hosts decoded `0xB800`, every consumer key dead; the unit test locked in the wrong order | Fixed (`ck5200_consumer.c`), test corrected, core-harness T8 green |
| R3 F3 (HIGH) | `NO_ACTION_ONESHOT` compiled `OSL(1)` to a no-op; the SYM layer unreachable | Fixed (`config.h`); image still fits (24,292 of 27,136 bytes); core-harness T5 green |
| R1 F-3 (P2) | Staging length marker not invalidated at A1; an interrupted re-flash could leave a valid marker over a hybrid staging image | Fixed (`ck5200_staging.c` `invalidate_length_marker`); restore-harness "marker invalidation ok" green |
| R1 F-4 (P2) | Auth-chip I2C ran ~2x faster than stock; both in-code justifications false | Fixed (`SystemCoreClock/2000000`, stock wall-clock ~1.5 us half-bit, comment cites stock `0x5350`/`0x501a`); auth-harness timing assertion green |
| R1 F-5 / R3 F5 (P2) | iAP2 watchdog decremented, never consumed; a silent session wedged the Mac recovery path until replug | Fixed (`iap2_session_active` consumes the watchdog exactly where stock `0x61f4` does; the link machine itself untouched); session-harness scenarios green |
| R1 F-7 (P2) | iOS flash path: no timeout (hang in busy forever), `onClosed` never invoked, A0 fire-and-forget, no reentrancy guard, no keep-awake | Fixed in the controller and inspector; proven by the new `ck5200_flash_check.swift` (26 checks green) wired into the preflight |
| R1 F-2 (P1) | iOS app staged any imported file with zero validation; the one unrecoverable state was one wrong file away | Fixed (`CustomKeyboardImageValidation`, ported from `validate_binary`; import-time rejection; sha256 shown in the confirm sheet) |
| R3 F4 (MEDIUM) | Wear-leveling backing store violated the upstream complement contract; a full erase+consolidation on every boot (flash wear per power cycle) | Fixed (complement on read/write like the upstream RP2040 backend; erase verified by readback); T7 asserts 0 erases on a clean boot, green |
| R3 F7 (LOW) | Boot protocol accepted but 10-byte reports sent anyway | Fixed (8-byte submit when `keyboard_protocol == 0`) |
| R3 F8 (LOW) | iAP2 SYN trusted the declared packet length and read past the received bytes | Fixed (`len >= 20` guard); session-harness short-SYN scenario green |
| R1 F-6 (P2) | FLASHING.md documented a nonexistent `ck5200_usb.py restore-stock` command | Fixed (both occurrences now name `scripts/revert-stock.sh`) |
| R1 F-8 / R3 F9 (P3) | STATUS.md/RECOVERY.md pinned a stale image (23,388 / `dad34db6...`) | Fixed (24,292 / `679f9846...`) |
| R1 F-9 (P3) | matrix.c claimed a stock derivation HARDWARE.md disowns as UNVERIFIED | Fixed (comment now says inferred, hardware-unverified) |
| R1 F-10 (P3) | build.sh never verified the pinned dependency commits | Fixed (`scripts/pins.env` shared by bootstrap and build; build fails on a drifted checkout) |
| R1 F-11 (P3) | CustomKeyboardProtocol.swift comment said all writes are unreachable on stock | Fixed (comment now scopes that claim to 0x90..0x93) |
| R3 F6 (MEDIUM) | `sym_defer_g` global debounce: one chattering key freezes all keys; sub-5 ms taps vanish | **Open, documented**: kept for now (the swap rationale was real: pinned `sym_eager_pk` calls `malloc`); the starvation behavior is asserted in T9 and listed in FLASHING.md's risk table. The upstream per-key algorithm with a 36-byte static allocation is the known follow-up. |
| R2 findings 1-4 | Wrong MCU family; false `platform.c` hardware claim; readiness docs contradicted RECOVERY.md; preflight blind to family | **Blocked on the chip marking read** (owner's action), then the re-target. STATUS.md now states the block; the family assertion is part of the re-target work. |
| R2 unresolved risks | Installer never dumped; zero backup; no independent restore | **Blocked** on physical evidence per `../RECOVERY.md` |

## What the reviewers proved beyond the findings

- The staging dialect decode is byte-accurate against stock (reviewer 2
  re-derived it independently); the restore-cycle harness stands.
- The stock boot region (`0x08000000..0x08002000`) is absent from every
  artifact on this machine; no full backup of the device exists.
- The vendored dependencies sit exactly at their pinned commits.
- The image in `ios/ClicksInspector/Resources/` matched the build before and
  matches it again after the fixes in this change set.
