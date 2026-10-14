# QMK integration audit, reviewer 3 of 3, 2026-10-11

Verbatim report of the QMK specialist reviewer, run against commit `3586b76`
in an isolated agent context. Provenance: review session
`ses_ed880669effex5ieCWpX78WtGk`, read-only, harnesses in scratch space. The
consolidated verdict and the fix status of every finding are in
`2026-10-11-flash-safety-review.md`.

---

# CK-5200 QMK Integration Audit, Reviewer 3 of 3

**Verdict: CHANGES REQUIRED.** Three confirmed defects will visibly break the keyboard within the first seconds of real use. I proved them by executing the production code paths on the host, not by inspection alone. The headline claim in `keyboards/ck5200/STATUS.md` ("ready for its first flash") does not survive execution.

Everything below was produced with the repository working tree strictly read-only. All binaries, harnesses, and scratch copies live in `/private/var/folders/t6/jmkhfw452wx9x27cvtj03qmh0000gq/T/opencode/ck5200_review/`. No production file was modified. Two scratch copies (one-line changes, documented) exist only in scratch space to verify proposed fixes.

## 0. What I actually ran (evidence basis)

| Command | Result |
| --- | --- |
| `bash keyboards/ck5200/scripts/test.sh` | All 9 outputs pass (`ok 13`, `ok 4`, restore cycles, etc.). Reproduces the repo claim. |
| `bash keyboards/ck5200/tools/preflight_check.sh` (read first; writes only `/tmp`) | `PREFLIGHT PASS: 10 checks, 0 failures`. Reproduces the claim. Note: none of its 10 checks execute the QMK event pipeline or the HID state machine, so it is blind to all findings below. |
| New host build A: real pinned QMK 0.26.11 core + real keymap + real `dynamic_keymap.c` + real `wear_leveling.c` + real `ck5200_keymap.c` + real `ck5200_consumer.c` + scratch copy of the backing store (address-space relocation only, all logic identical), ASan+UBSan, manual-advance time via QMK's own `platforms/test/timer.c` | 10 behavior scenarios + 200k-packet fuzz. Ran clean under sanitizers. 3 failures, all real defects (T5 x2, T8). |
| New host build B: production `firmware/platform/ch32v20x/protocol.c` (unmodified) + the same real core, with a TinyUSB-0.21-faithful stub (`tud_hid_report` accepts, completion arrives with `xferred_bytes=10` exactly as `hid_device.c:384` delivers it) | 5 scenarios. Production `protocol.c` fails P1, P2, P3, P5. A scratch copy with a one-line fix passes all five. |
| `shasum -a 256 build/ck5200_qmk.bin`, `nm`, map inspection | 23,392 bytes, `7aeb06d2...` matches HANDOFF.md exactly. No `malloc`/`_sbrk` in the final image (heap-free claim VERIFIED). `sym_defer_g.c.obj` linked, `debouncing` symbol present. RAM: `.data` 0x8c + `.bss` 0x12fc (~5 KB of 20 KB, 2 KB stack). |
| Not run (per operating rules): `scripts/build.sh`, any cmake/ninja firmware build, `tools/verify_history.sh`, `scripts/setup.sh`, any USB device interaction | Recorded as skipped. |

Pinned versions, verified from the vendored git trees (all clean, unmodified): QMK `0.26.11` (3d4da6de29c8), TinyUSB `0.21.0` (efa3e1322), WCH SDK at commit baef005.

## 1. Ranked findings

### F1. CRITICAL: a single key tap permanently freezes matrix scanning and spams the stuck report

- **File:** `keyboards/ck5200/firmware/platform/ch32v20x/protocol.c:188-199`, the guard at line 191; the only pending-clear at lines 196-198; task gating at lines 58-80.
- **Contract:** TinyUSB 0.21 `src/class/hid/hid_device.c:384` calls `tud_hid_report_complete_cb(instance, epin, xferred_bytes)`. This firmware always sends 10-byte reports (8 keyboard + 2 consumer, `protocol.c:16`), so `len == 10`. `sizeof(last_keyboard_report)` is 8 (`report_keyboard_t`, no `KEYBOARD_SHARED_EP`; pinned `tmk_core/protocol/report.h:140-178`). The guard `len != sizeof(last_keyboard_report)` is therefore always true, so the callback returns before the `memcmp` that clears `pending_keyboard_report_valid`.
- **Failure chain (execution-verified, build B):** QMK flushes a tap as press+release in one `keyboard_task` pass: the first report is submitted (`in_flight`), the second goes to pending. The completion callback clears `in_flight` but never clears `pending`. Every subsequent `protocol_keyboard_task` call then re-submits the same pending report forever and returns before line 79, so `keyboard_task()` never runs again. Result: matrix frozen, input dead, and the last report is re-sent to the host every cycle.
- **Measured:** after one plain tap of the Clicks key: `scans +0, submits +20` over 20 cycles; a key pressed after the tap never reaches the wire (P5). Chord variant (P1): `chord submits=7, scans +0, submits +20`. Suspend-with-pending (P3): same freeze across resume. Idle path is also dead (P2: `SET_IDLE(10)` accepted, 0 resends in 100 ms) because `last_keyboard_report_valid` is never set behind the same guard.
- **Trigger likelihood on hardware:** near-certain within seconds. Any tap, any two-key event in one scan pass, any transfer interrupted by suspend/unplug.
- **Recoverability:** USB enumeration, the iap2 stack, and the A1/A2/A3/A0 update dialect keep running (`protocol_pre_task`/`protocol_post_task` still execute), so stock remains restorable. This bug kills typing, not recovery.
- **Minimal correction (verified):** change protocol.c:191 to `if (!report || len < sizeof(last_keyboard_report)) return;`. The scratch fixed copy passes P1, P2, P3, P5 (submits=2, scans +20, idle resends=2, resume alive).
- **Regression test:** build B scenarios P1, P2, P3, P5 (already written and executed; see section 5).

### F2. HIGH: the consumer usage field is byte-swapped; all three consumer keys are unrecognized by any spec-conforming host

- **Files:** `firmware/platform/ch32v20x/ck5200_consumer.c:29-33` (`out[8] = usage >> 8; out[9] = usage`), descriptor at `firmware/usb/usb_descriptors.c:85-91`.
- **Contract:** HID report fields are little-endian; byte 8 is the LSB of the 16-bit field. The host decodes `field = out[8] | (out[9] << 8)`. QMK's own consumer path passes native-endian `report_extra_t.usage` to the host stack.
- **Failure:** execution-verified (build A, T8): the packed wire bytes are `00 b8`, so the host decodes **0xB800**, not 0x00B8. Eject (the intended iOS on-screen-keyboard toggle) and both stock usages (0x01AE, 0x029D) land out of range and are discarded. Every consumer key is dead.
- **Aggravating factors:** (a) the repo's own test locks in the wrong order: `tests/ck5200_consumer_test.c:26-27` asserts the big-endian packing while calling it "the descriptor contract" (a test verifying the implementation against itself); (b) the Eject mapping itself is inferred from a different product ("9981-style", `keymap.c:26-29`), not decoded from this keyboard's stock image, whose descriptor used three 1-bit usages (`docs/STOCK_DECODE.md:59-60`), so the toggle semantics are unverified even after the byte-order fix; (c) a single 16-bit field cannot express stock's simultaneous consumer usages (minor regression).
- **Minimal correction:** swap the two bytes in `ck5200_consumer_pack` and fix the test to assert `out[8] == (uint8_t)usage; out[9] == (uint8_t)(usage >> 8)`.
- **Regression test:** T8 already fails today and passes with the swap.

### F3. HIGH: the sticky SYM key is compiled dead; layer 1 (digits and punctuation) is unreachable out of the box

- **Files:** `firmware/keyboard/config.h:17` (`NO_ACTION_ONESHOT`), `firmware/keyboard/keymaps/diagnostic/keymap.c:57` (`OSL(1)` at matrix (5,4)).
- **Contract:** pinned `quantum/keymap_common.c:128-134` maps `QK_ONE_SHOT_LAYER` to `ACTION_LAYER_ONESHOT` only inside `#ifndef NO_ACTION_ONESHOT`; with the define, `OSL(1)` falls to `default: ACTION_NO` (line 192). Layer 1 is reachable only through this key (no MO/TG/TT anywhere, no custom layer code in `process_record_user`).
- **Failure:** execution-verified (build A, T5): press the sticky SYM key, then (0,0). Expected per the project's own docs: `KC_1` (layer 1). Actual: `KC_Q` (layer 0). The key does nothing; no digits, no punctuation, no quotes. The layout test (`tests/test_keymap_layout.py`) passes because it compares source text to a table, not behavior.
- **Minimal correction:** remove `NO_ACTION_ONESHOT` from config.h, then rebuild and check the 27,136-byte budget (image is at 86.2%, the oneshot code is small but must be measured). If it does not fit, rebind (5,4) to `MO(1)` as a stopgap (behavior change: hold instead of sticky, but the SYM layer becomes usable). The HANDOFF "swap sym_eager_pk back with static allocation" plan addresses debounce, not this; OSL was overlooked.
- **Regression test:** T5 already fails today.

### F4. MEDIUM: the wear-leveling backing store violates the QMK contract (no complement), forcing a full erase+consolidation on every boot

- **File:** `firmware/platform/ch32v20x/wear_leveling_ch32v20x.c:40-51` (raw reads/writes, no value complement).
- **Contract:** pinned `quantum/wear_leveling/wear_leveling.c:31-33`: "any reads from the backing store directly after an erase must come back as zero... if the backing store uses 0xFF for cleared bytes, it should return the complement". The upstream RP2040 flash backend does exactly this (`platforms/chibios/drivers/wear_leveling/wear_leveling_rp2040_flash.c:139` writes `~value`, line 216 reads `~loc[i]`). Erased CH32 flash reads 0xFFFFFFFF; the core's end-of-log detection is `value == 0` (wear_leveling.c:515-517), and 0xFFFFFFFF decodes as log-entry type 3, which hits `default: status = WEAR_LEVELING_FAILED` and forces `wear_leveling_consolidate_force()`.
- **Measured:** execution-verified across simulated reboots on a faithful flash model (erased=0xFF, AND-programming, 256-byte fast page erase): keymap data survives and reads back correctly every boot (self-healing works, T7), but **every boot performs 4 page erases plus a full consolidated rewrite** ("boot 1/2/3 performed 4 page erases; upstream contract: 0 on a clean boot"). The write log is dead weight; the store consolidates per boot.
- **Impact:** flash endurance is consumed per power cycle instead of amortized (WCH datasheet-typical endurance is on the order of 10K erase cycles per page; exact figure [UNVERIFIED], the datasheet PDF is not in the vendored tree). Each consolidation also stalls the bus for milliseconds while USB is live. Not a data-loss bug; a longevity and latency bug that the existing tests cannot see because they mock flash with a memset-0 model (`tests/stubs/ch32v20x.h`).
- **Also note:** `backing_store_erase` (line 33-38) ignores the `FLASH_ErasePage_Fast` return value and always returns true, so a failed erase surfaces later as write-verify failures instead of a clean error.
- **Minimal correction:** complement on both paths (`*value = ~raw` on read, program `~value` on write), and check the erase status. Regression test: the T7 boot-cycle harness asserting 0 page erases on a clean boot.

### F5. MEDIUM: the iAP2 link watchdog is decorative; a dead session never restarts

- **File:** `firmware/platform/ch32v20x/iap2.c:612` (`if (c->watchdog) c->watchdog--;`). No code path ever reads `watchdog == 0`.
- **Contract:** stock resets the watchdog to 2500 on activity (`docs/STOCK_DECODE.md:67`); the constant is ported (`WATCHDOG_TICKS 2500`) and reset in three places, but expiry is never handled. If the iPhone stalls mid-session, the stack stays in state 3 forever: no re-attach, and the EP3 companion gate stays open. A USB re-plug is the only recovery.
- **Minimal correction:** in `iap2_tick_1ms` or `iap2_task`, when the watchdog reaches 0 in state 3, run `transport_init(false, c->connected)` and restart attach (state 1). Regression: add a session-test scenario "silence after session open triggers re-attach".

### F6. MEDIUM (design risk, behavior verified): `sym_defer_g` starvation and sub-debounce loss

- **Files:** `config.h:11` (`DEBOUNCE 5`), `cmake/qmk_core.cmake:6` (sym_defer_g), pinned `quantum/debounce/sym_defer_g.c`.
- **Verified behavior (build A):** (a) any crossing that changes state more often than every 5 ms freezes ALL keys: a held KC_M was invisible for its entire 60 ms chatter window (T9); (b) any press+release shorter than DEBOUNCE vanishes entirely (T4a sub-debounce check); (c) ordinary typing, chords, 6KRO stacking, and burst alternation all work correctly (T2, T10), so the choice is survivable for typing.
- **The HANDOFF's malloc claim is true for the pinned tree** (`sym_eager_pk.c:59` calls `malloc`), so the swap itself was reasonable. But the correct fix is not a global debounce: it is a static allocation. The counter table for a 6x6 matrix is 36 bytes; patching `sym_eager_pk.c` (scratch copy) to use a static array restores per-key eager debouncing with zero heap and near-stock feel. Alternatively keep sym_defer_g for the first flash and revisit only if hardware shows chatter; the starvation property should then be documented in FLASHING.md as a known failure mode ("one chattering key freezes the keyboard").
- **Regression tests:** T9 and the sub-debounce case, plus a per-key variant once eager is restored.

### F7. LOW: boot protocol is accepted but not honored

- **Files:** `protocol.c:18` and `:170-172`. `keyboard_protocol` is written by `tud_hid_set_protocol_cb` and never read; the submit path always sends 10 bytes.
- **Contract:** a boot-protocol keyboard must deliver 8-byte boot reports. A host that selects boot protocol (BIOS, some docks and KVMs) will read the 10-byte transfer and misparse the tail. iOS and macOS use report protocol, and the stock descriptor was also non-strict (4KRO plus bits), so real-world risk is low, but the SET_PROTOCOL handler currently only pretends.
- **Correction:** in `submit_hid_report`, send 8 bytes when `keyboard_protocol == 0` (consumer keys are lost in boot mode, which is correct boot behavior). Regression: protocol harness scenario asserting 8-byte submissions after `tud_hid_set_protocol_cb(0, 0)`.

### F8. LOW: iAP2 SYN validation trusts the declared packet length

- **File:** `iap2.c:401-409`: `match = pkt_len >= 21` uses the header-declared `pkt_len`, then reads `pl[4..10]` (buffer bytes 13-19) with only `len > 8` guaranteed. A short packet with a declared length of 21 reads stale bytes from the 64-byte endpoint buffer. No out-of-bounds beyond the hardware buffer, but the project's own fix philosophy ("clamp to the received bytes", applied at line 441 for the data path) was not applied to the SYN path. Correction: require `len >= 20` before reading the parameter block.

### F9. LOW: documentation drift and a falsified prior claim

- `keyboards/ck5200/STATUS.md` says the build is 23,388 bytes, sha256 `dad34db6...`; the on-disk image and HANDOFF.md say 23,392 / `7aeb06d2...` (I verified the latter). STATUS.md is stale.
- STATUS.md claims "Pending HID reports retry, GET_REPORT/idle/resume behavior implemented" and a prior `/tmp` ASan harness that "passed ordered key transitions, failed-release retry, GET_REPORT, idle, resume". Execution of the same production `protocol.c` with TinyUSB-faithful completion semantics proves idle, resume, and retry do not work (F1). The prior harness (not preserved) must have modeled the completion callback with `len == 8` or cleared in-flight state directly, i.e., it validated the implementation against an idealized stub. Treat "verified offline" claims in STATUS.md as untrusted; HANDOFF.md's gap list is the more honest document.
- FLASHING.md's risk table misses the three defects that will actually fire (typing lockup, dead consumer keys, dead SYM layer) and should be regenerated after the fixes.

## 2. QMK integration assessment

**Correctly integrated (execution-verified on the pinned core):**
- Matrix pipeline: `matrix_scan_custom` -> `matrix_common` -> `sym_defer_g` -> `keyboard_task` -> `action_*` -> layer switching -> `host_keyboard_send`.
- Basic typing, press/release symmetry, modifier handling (LGUI+Q produces mods 0x08 with the key), 6KRO stacking (six simultaneous keys), burst alternation ending on an empty report.
- `LT(2, KC_J)`: 80 ms tap sends KC_J; fresh 200 ms+ hold reaches layer 2 (KC_RIGHT verified). The double-tap corner behaves as upstream action_tapping dictates.
- Dynamic keymap: first boot on erased flash seeds all 144 crossings from PROGMEM through `eeconfig_init_kb` -> `dynamic_keymap_reset` (the wiring through `keyboard.c:320`/`:384` is correct); runtime `dynamic_keymap_set_keycode` takes effect on the next keypress; out-of-range writes rejected; edits survive three simulated reboots with data intact.
- The A1/A2/A3/A0 update dialect and staging writer: framing, sequential-offset enforcement, size caps, page verify, and the length-word commit are covered by the repo's restore harness (full real stock and custom images, both directions, failure injection). I ran it; it passes.
- Heap-free claim: VERIFIED, no malloc in the final ELF. LTO + `--gc-sections` works; size and hash claims match the artifact.
- The keymap-vs-stock-decode consistency test and the stock-derived layout table are genuinely independent ground truth (decoded from the vendor image, not from this codebase).

**Incorrectly implemented (confirmed):** the HID completion/pending state machine (F1), consumer report encoding (F2), one-shot layer activation (F3), wear-leveling complement semantics (F4), iAP2 watchdog expiry (F5).

**Intentionally unsupported (documented, acceptable):** NKRO (`send_nkro_impl` no-op; descriptor is 6KRO only, consistent), mouse/extra reports, settings commands 0x86/0x88/0x8a/0x8c/0x8d (return unsupported), backlight auto-off state machine, multi-packet link-frame reassembly (stock-faithful limitation).

**Incompletely integrated:** boot protocol handling (F7); `usb_device_state.c` is compiled but never updated (dead code, harmless); `bootmagic.c`/`process_magic.c` compiled but inert without `BOOTMAGIC_ENABLE` (good: no accidental bootloader jump on a held key at boot; `bootloader_jump` is a plain reset).

**Not independently verified (offline-impossible or not attempted):** MFi auth chip behavior (I2C address, timing, certificate streaming against a real chip), iPhone acceptance of the session and the EA alt-setting flow, real USB enumeration (the alt-setting-1 EP3 pair has never been exercised by a host), matrix electrical behavior (diode presence, ghosting: `MATRIX_HAS_GHOST` is not defined, so no ghost blocking; stock behavior unknown), backlight LED wiring, stock installer's acceptance of a staged non-vendor image, actual I2C bit rate of the bit-bang driver (`auth_bit_delay` computes to roughly 36 loop iterations per half bit at 144 MHz; the true cycle count of the compiled loop was not measured, so the effective bus speed is [UNVERIFIED] versus the claimed ~100 kHz).

## 3. Test coverage matrix

| Subsystem | Existing coverage (repo; I ran all) | Missing cases | Proposed test | Environment |
| --- | --- | --- | --- | --- |
| QMK event pipeline (matrix->action->report) | None. No repo test executes `keyboard_task`/`action.c`. | Everything: taps, holds, layers, mods, 6KRO, chatter | Build A harness (already written+run; commit it as `tests/host_core`) | Host, real pinned core, QMK `platforms/test` timer |
| HID protocol state machine | None executable. STATUS.md's claimed prior coverage is contradicted by execution. | Completion callbacks, pending retry, idle, suspend/resume, GET/SET_REPORT, boot protocol | Build B harness P1-P5 (written+run; commit as `tests/host_protocol` with the TinyUSB-0.21 stub) | Host, real `protocol.c` |
| Consumer keys | `tests/ck5200_consumer_test.c` (unit; asserts the buggy byte order) | Host-decoded field value per HID rules; simultaneous usages | T8 (written+run); fix the existing unit test's expected order | Host |
| Dynamic keymap + persistence | `tests/ck5200_keymap_test.c` (dispatcher framing against stubs) | Real `dynamic_keymap.c` + real wear leveling + flash semantics; reboot cycles; erased-flash boot; erase-count budget | T1, T6, T7 (written+run) | Host, flash model |
| Debounce | None | Chatter starvation, sub-debounce loss, burst typing | T4a/T9/T10 (written+run) | Host |
| iAP2 session | `tests/iap2_session_test.c`, 13 scripted scenarios, byte-exact outgoing packets (good) | Watchdog expiry; SYN short-packet robustness | New session scenarios | Host (existing harness) |
| MFi auth I2C | `tests/iap2_auth_test.c`, 4 waveform-level scenarios | Real chip timing/address | Cannot be done offline | Hardware |
| Update dialect + staging | `tests/ck5200_restore_test.c`: full real images through real handler+writer, mock flash, failure injection (strong) | Real flash timing, power loss mid-page | HIL on a CH32V203 dev board | Hardware |
| USB descriptors | `tests/test_usb_protocol.py` (host-tool packets, not descriptor parsing) | Descriptor parse by an independent HID parser; iOS acceptance | Add a descriptor parse test (e.g., Python `hid` parser or unit-test against the stock dump); real enumeration only on hardware | Host, then hardware |
| iOS app | 3 Swift checks (run in preflight, pass) | Live accessory session | First-flash checklist | Hardware |

## 4. Executable testing plan (priority order before any flash)

**Already executed this session (no implementation work needed), keep as permanent tests:**

1. `tests/host_core`: real pinned QMK core + real keymap. Commands (scratch dir preserved):
   - `bash .../ck5200_review/build_core.sh && .../qmk_core_test`
   - Scenarios T1-T11 including the 200k-packet dispatcher fuzz (1317 accepted, ASan/UBSan clean, no OOB).
2. `tests/host_protocol`: real `protocol.c` against the TinyUSB-0.21 stub.
   - `bash .../build_protocol.sh orig && for s in p1 p2 p3 p4 p5; do ./protocol_test_orig $s; done`
   - Expected today: P1/P2/P3/P5 fail (F1). After the F1 fix: all pass (verified with the fixed scratch copy).

**Requires implementation (the fixes), each with its regression test already proven red:**

3. Fix F1 (`protocol.c:191`, `len <`), F2 (byte swap + test correction), F3 (remove `NO_ACTION_ONESHOT`, verify size), F4 (complement in the backing store), F5 (watchdog expiry), F7 (boot-protocol length). Re-run builds A and B: all scenarios must pass.
4. Rebuild the firmware (I could not run this; exact command for the later execution phase): `bash keyboards/ck5200/scripts/build.sh`, then `bash keyboards/ck5200/tools/preflight_check.sh` (expect 10/10 with the new size), and `bash keyboards/ck5200/scripts/test.sh`.
5. Strongly recommended before the first flash, given one unit: run the rebuilt image on a cheap CH32V203 dev board (USBFS variant). It validates real flash (wear-leveling erase/program), real USB enumeration, and real HID typing against macOS/iPadOS, none of which any host harness can prove. The iAP2 stack and auth chip cannot be validated there.

**Differential and property testing (built into the harnesses):** releasing every key always ends with an empty report (T2/T3/T10); a completed transfer must never permanently block a later report (P1/P5); out-of-bounds matrix indices never reach storage (T6, fuzz); malformed host packets cannot cause unbounded operations (T11); the same scenarios run against the fixed copy to demonstrate intended-vs-actual.

**Emulation feasibility:** QEMU has no CH32V203 machine (no WCH USBFS block, PFIC, or the WCH SysTick with CTLR/SR/CMP registers), so the stock startup and USB path cannot run there. Renode's RISC-V support [UNVERIFIED for CH32V203 on this machine] would still lack the USBFS device model and the flash controller's fast-page semantics. Emulation could at best cover GPIO/timer logic with custom peripheral models; it cannot prove enumeration, host acceptance, or flash behavior. The host-harness approach (QMK's own `platforms/test`, which the pinned tree ships and I used) is the highest-value offline environment and should become the repo's test suite.

## 5. Final recommendations

1. **Is this a correct QMK port?** The core integration is genuinely good: the module selection, the main-loop wiring, dynamic keymap, wear-leveling plumbing, and the update dialect are correct, and the pinned QMK core behaves properly under execution. But the port is not shippable: the custom protocol layer has one fatal defect (F1), and two features the project advertises (consumer keys, sticky SYM) are provably dead (F2, F3).
2. **Which defect is most likely to fail on hardware first?** F1. The first completed tap or chord freezes matrix scanning and locks the last report on the wire. Seconds, not minutes.
3. **Which behavior is most poorly tested?** Everything between `keyboard_task` and the USB wire. The repo has zero executable coverage of the QMK action pipeline and the HID state machine; the one prior harness that claimed this coverage validated against an idealized completion callback and passed while the production path was broken.
4. **The most valuable additional test?** The `host_protocol` harness (build B): five scenarios, TinyUSB-0.21-faithful, already proven to catch F1 in both directions (production fails, one-line fix passes). Commit it and run it in CI alongside the existing suite.
5. **What should be replaced with upstream QMK code?** Nothing wholesale. Surgical: restore upstream one-shot handling by removing `NO_ACTION_ONESHOT`; restore upstream `sym_eager_pk` with a 36-byte static allocation instead of the global `sym_defer_g` (upstream logic, zero heap); make the backing store follow the upstream complement contract exactly as the RP2040 backend does. Do not swap the whole protocol layer for a stock QMK USB stack: the stock-topology descriptors and the iAP2 session stack are the point of this firmware and are outside what upstream provides for this chip.
6. **Which assumptions remain impossible to validate offline?** MFi auth chip identity/address/timing; iPhone acceptance of the session, the EA alt-setting flow, and the HID descriptor; real USB enumeration timing on the CH32 USBFS block; matrix electrical properties (diodes, ghosting, settle time of 2 us versus stock); backlight wiring; the stock boot installer's willingness to commit a non-vendor staged image; the true compiled bit rate of the bit-banged I2C.
7. **Historical recommendations:** F1, F2, F3 were blockers; F4 and F5 were additional findings. [CORRECTION] The review's former stock-restore guarantee is withdrawn. An untouched installer region does not prove independent recovery. [Current readiness](../flash-readiness.md) is NO-GO.
