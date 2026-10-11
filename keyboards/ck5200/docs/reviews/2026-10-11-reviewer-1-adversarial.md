# Adversarial code review, reviewer 1 of 3, 2026-10-11

Verbatim report of the Thermos-style adversarial reviewer, run against commit
`3586b76` in an isolated agent context. Provenance: review session
`ses_ed88066a7ffeuDa5JcSi2KCsAj`, read-only, harnesses in scratch space. The
consolidated verdict and the fix status of every finding are in
`2026-10-11-flash-safety-review.md`.

---

# Adversarial review: rudironsoni/clicks-keyboards-custom-firmware @ 3586b76

Reviewer 1 of 3. All evidence below was gathered read-only from the checkout at `/Users/rudimar.ronsoni@feverup.com/src/rudironsoni/clicks-keyboards-custom-firmware` (branch `main`, commit `3586b76`, working tree clean, verified with `git status`). No hardware or USB device was opened, addressed, or reset. All harnesses I built live in `/private/var/folders/t6/.../T/opencode/ck5200-review/`.

## 1. Verdict

**CHANGES REQUIRED.**

The shipped firmware image contains a confirmed defect that makes the keyboard unable to type at all once it enumerates (F-1), and the phone app can stage an arbitrary, unvalidated binary into the one state the project itself calls unrecoverable (F-2). A clean software review never implies permission to flash; this one actively forbids it until F-1 through F-4 are fixed and re-verified.

## 2. Findings

### F-1 (P1): Once mounted, the firmware never scans the keyboard and retransmits one HID report forever

**Files:** `keyboards/ck5200/firmware/platform/ch32v20x/protocol.c:188-199` (defect at `:191`), interacting with `:47-56`, `:58-80`, `:180-186`, `:32-41`.

**Incorrect assumption:** `tud_hid_report_complete_cb` assumes TinyUSB hands back a buffer whose length equals `sizeof(report_keyboard_t)` (8 bytes). The firmware sends 10-byte reports (`HID_REPORT_SIZE`, protocol.c:16; `submit_hid_report` calls `tud_hid_report(0, buf, 10)` at :37). The vendored TinyUSB (`external/tinyusb/src/class/hid/hid_device.c:384`, commit `efa3e1322`) invokes the callback with `xferred_bytes`, i.e. 10.

**Failure chain (verified three independent ways):**

1. `tud_mount_cb` (protocol.c:210) calls `retain_interrupted_keyboard_report()`, which sets `pending_keyboard_report_valid = true` for the current (zero) report. Any later busy-send, suspend, or resume does the same (:51-55, :180-186, :217-218).
2. On every completion, `tud_hid_report_complete_cb` clears `keyboard_report_in_flight` (:190) but then returns early at `:191` because `len (10) != sizeof(last_keyboard_report) (8)`. The only code that ever clears `pending_keyboard_report_valid` (:196-198) is dead. `sizeof(report_keyboard_t) == 8` was confirmed by compiling the real QMK header (`tmk_core/protocol/report.h:171-178`).
3. `protocol_keyboard_task` (:61) therefore takes the `pending -> submit -> return` branch on every pass while mounted. `keyboard_task()` at :79 is **never reached**. No matrix scan, no key processing, no layers, no consumer keys. The idle-report logic (:70-77) is also dead (`last_keyboard_report_valid` is never set).

**Evidence:**

- **Source reasoning:** verified line-by-line as above; there is no other write that clears the pending flag.
- **Dynamic proof (production code, unmodified):** I compiled the real `protocol.c` with the real `ck5200_consumer.c`/`ck5200_keymap.c`/`ck5200_update_protocol.c`/`ck5200_staging.c` against a faithful TinyUSB mock (completion callback invoked with the exact `xferred_bytes`, mirroring `hidd_xfer_cb`), under `-fsanitize=address,undefined`. Result:
  ```
  submissions in the first 50 cycles after mount: 50
  keyboard_task() invocations in the same 50 cycles: 0 (QMK scan runs)
  submissions after settle: 250 (before settle: 50)
  byte-identical retransmissions of the same report: 249
  ```
  50 identical submissions in 50 cycles, 0 QMK scans, no sanitizer errors. Harness: `ck5200-review/protocol_harness/harness_main.c`.
- **Shipped image:** the bug is in the built `build/ck5200_qmk.elf` (the exact image the iOS app bundles; hashes match `ios/ClicksInspector/Resources/ck5200_qmk.bin`). Disassembly: `hidd_xfer_cb` at `0x58c8` contains `0x58fc: li a5, 0x8` / `0x58fe: bne a2, a5, 0x5932` (skip on mismatch, always taken) while `submit_hid_report` at `0x59b2: li a2, 0xa` queues 10-byte transfers. The complete-callback body after `0x58fe` is dead code in the shipped binary.

**Reachable scenario:** the first flash boots, USB enumerates, the dispatcher and iAP2 stack and backlight all work, but the keyboard types nothing on any host, and it spams a duplicate 10-byte report once per endpoint completion cycle (~1/ms at bInterval=1) forever.

**Confidence:** VERIFIED (all three layers: source, dynamic execution, machine code of the exact artifact).

**Smallest reliable correction:** in `protocol.c:191`, compare against the wire size, and copy only the keyboard part:
```c
if (!report || len != HID_REPORT_SIZE) return;          /* was: sizeof(last_keyboard_report) */
memcpy(&last_keyboard_report, report, sizeof(last_keyboard_report));
```

**Regression test:** commit the protocol harness above to `tests/` and wire it into `scripts/test.sh`. Before the fix it must FAIL with `keyboard_task invocations == 0` and `>4` submissions in the settle window; after the fix it must PASS with scans running, at most one retransmission of a pended report, and idle-rate reports flowing. This file previously had zero committed test coverage; the only tests that ever exercised it were temporary `/tmp` harnesses from the prior session, which no longer exist (`/tmp/ck5200-fw-io-harness/` is gone) and whose passing results are therefore un-inspectable claims.

### F-2 (P1): The iOS app stages any imported file with no validation, making the unrecoverable state reachable by picking the wrong file

**Files:** `ios/ClicksInspector/Sources/ClicksInspectorApp.swift:159-171` (import), `:172-189` (confirmation), `CustomFirmwareController.swift:230-257` (flash); contrast `keyboards/ck5200/tools/ck5200_usb.py:244-256` (the Mac tool refuses unknown images without `--allow-unknown-image` and runs `validate_binary()` on them).

**Incorrect assumption:** the phone flash path assumes the only images a user stages are the two bundled ones. The file importer accepts any non-empty file (`allowedContentTypes: [.data]`), and the confirm sheet only says "Stages N bytes".

**Reachable failure scenario:** user imports any ≤27,136-byte file (wrong file, partial download, wrong-model firmware). The firmware's staging layer checks only size and offsets (`ck5200_staging.c:69-123`), so A1/A2/A3 succeed, A0 reboots, and the stock installer installs the garbage as the application. If the result does not enumerate, the device is in exactly the state `docs/FLASHING.md:314-321` calls the only unrecoverable case, and the only rescue is the physical procedure in `docs/RECOVERY.md` that is untested and requires equipment the owner does not have. `FLASHING.md:320-321` states "No normal flash operation can cause this"; the app's import path is a normal flash operation that can.

**Evidence:** code reading, VERIFIED. The asymmetry with the Mac tool is exact: `ck5200_usb.py:244-247` refuses; the app does not.

**Smallest reliable correction:** port `validate_binary`'s checks into Swift and run them on every imported image before showing the confirm sheet: first word decodes as `jal x0` to a 2-byte-aligned target inside the image, vector word at offset 4 equals `0x2000`, size ≤ `0x6A00`, plus display the SHA-256 in the confirmation. Keep bundled paths hash-pinned (they already match; preflight verifies).

**Regression test:** a host-runnable Swift check (extend `Tests/ck5200_protocol_check.swift`) that an imported garbage image is refused and the two bundled images pass.

### F-3 (P2): FLASHING.md's power-loss safety claim is unsupported and contradicts RECOVERY.md; the commit-marker design allows a mixed-image install after an interrupted update

**Files:** `keyboards/ck5200/firmware/platform/ch32v20x/ck5200_staging.c:69-79` (A1 does not invalidate the old marker), `:125-163` (A3 writes the marker last); `docs/FLASHING.md:305-312` ("A power loss during A2 ... a fresh A1/A2/A3 cycle rewrites it"); `docs/RECOVERY.md:68-72` ("Update interruption remains unresolved ... Do not claim rollback").

**Incorrect assumption:** FLASHING.md treats power loss during A2 as harmless. Power loss means reboot. At that moment the staging area holds a mix of the old image and a new prefix, while the 4-byte length marker at `0x08008A00` still holds the *previous* A3-committed value, because neither stock's A1 handler (stock `0x37e2` region) nor the port's `staging_begin` clears it. If the boot-region installer treats a nonzero marker as "install pending" (the plausible reading; RECOVERY.md:286-294 admits the marker semantics is not fully established), the installer writes the mixed image over the application, which can produce the non-enumerating state. RECOVERY.md already says this is unresolved; FLASHING.md's §7 table contradicts it on the highest-stakes scenario.

**Evidence:** VERIFIED that the marker is not cleared at A1 in both the port (source) and stock (disassembly of the A1 handler at stock `0x37a8..0x37e2`); INFERENCE (not VERIFIED) that the installer acts on a stale marker, because the boot region has never been read.

**Smallest reliable correction:** in `staging_begin`, before accepting any A2 data, erase/program the marker word to 0 (invalid), then let A3 commit the new length as today. Then an interrupted cycle can only ever present "no install pending" to the installer, and the running application survives. Also align FLASHING.md §7 with RECOVERY.md's language.

**Regression test:** extend `tests/ck5200_restore_test.c`: after a successful full cycle, begin a new A1, and assert the read-back marker is 0/invalid before the first A2; then inject a "power loss" (abort mid-A2) and assert the marker never points at a mixed image. Fails today, passes after the fix.

### F-4 (P2): The auth-chip I2C timing is ~2x faster than stock, and both in-code justifications for it are false

**Files:** `keyboards/ck5200/firmware/platform/ch32v20x/iap2_auth.c:63-66` (`auth_bit_delay = SystemCoreClock / 4000000u` with the comment "~2 half-bit delays per 100 kHz bit" and "SystemCoreClock gives the same result without the hidden global"); `CMakeLists.txt:112` (`SYSCLK_FREQ_144MHz_HSI`); `docs/FLASHING.md:137` (risk table: "derived from the same clock as stock"); `HANDOFF.md:348-351` ("every timing constant ... traces back to a specific instruction in the stock image").

**What the stock image actually does (VERIFIED by disassembling the hash-pinned V122 image via `tools/stock_audit/mkelf.py`):**

- Stock delay loop at `0x4fec`: ~4 cycles per iteration (`addi; bnez; nop; j`), count computed at `0x5350` as `60000000 / (16 * a0)` with `a0 = 50000` (`0xC350`, loaded at `0x6408`), stored to the global at `gp-0x7e0`; the I2C callers use `global >> 2` = **18 iterations** (e.g. `0x501a: lw s1, -0x7e0(gp); srli s1, s1, 0x2`).
- Stock's clock: its `SystemInit` (stock `0x3bba`) never enables the PLL; its clock-update routine (stock `0x3bfc`) assumes a 48 MHz PLL (constant `0x02DC6C00`). The stock application runs at 48 MHz handed over by the boot region. Therefore stock's half-bit is ~18 x 4 cycles / 48 MHz ≈ 1.5 µs (~330 kHz SCL).
- The port runs at 144 MHz (`SetSysClockTo144_HSI`, verified in `external/tinyusb/hw/bsp/ch32v20x/system_ch32v20x.c`) and uses 36 iterations ≈ 36 x 3-4 cycles / 144 MHz ≈ 0.75-1.0 µs (~500-660 kHz SCL).

So the port is ~2x faster than stock in wall-clock time, and ~5-6x faster than the "100 kHz" the comment claims. The comment's two justifications (equivalence to stock, 100 kHz) are both false, and HANDOFF.md's "every timing constant traces back to the stock image" is wrong for this constant. The MFi auth chip is the least-documented silicon in the system; overspeeding a proprietary Apple auth chip on the only unit, on the first live test, is exactly the "appears correct in tests, fails on hardware" class: the waveform harness (`tests/iap2_auth_test.c`) validates bit structure, not timing.

**Consequence if it fails:** auth chip NAKs, the iAP2 session never opens, the phone app cannot reach the keyboard. HID typing still works and the Mac path restores stock, so it is recoverable, but it silently kills the headline feature and burns the first-flash opportunity.

**Smallest reliable correction:** scale the constant to stock's wall-clock value: `auth_bit_delay = SystemCoreClock / 8000000u` (18 iterations at 144 MHz) or, cleaner, reproduce stock's derivation exactly (`(SystemCoreClock/1000000) * 75 / 100 >> 2`) with a comment that documents the true measured rate, not 100 kHz. Correct FLASHING.md:137 and HANDOFF.md accordingly.

**Regression test:** a host test asserting `auth_bit_delay == 18` for the shipped 144 MHz clock, with a comment pointing at stock `0x5350/0x501a`; the current code fails it.

### F-5 (P2): The iAP2 watchdog is decremented but never consumed; a dead phone session wedges `iap2_session_active()` forever, blocking the Mac recovery path until replug

**Files:** `keyboards/ck5200/firmware/platform/ch32v20x/iap2.c:27` (`WATCHDOG_TICKS 2500`), `:452`, `:601`, `:612`, `:642`, `:653` (set/decrement only; no reader); `protocol.c:115-118` and `:249-253` (behavior gated on `iap2_session_active()`); `iap2.h:33` documents the watchdog as "link silence".

**Evidence:** grep for `watchdog` across all firmware sources shows exactly one decrement and three writers, no reader. The stock image reads it: stock `0x61f4` loads `ctx+0xc8` and changes its return value depending on whether the watchdog has expired (disassembly VERIFIED: `0x61fe: lw a4, 0xc8(a0); ... 0x6204: bnez a4, 0x620c`). The port dropped the only consumer.

**Reachable failure scenario:** the phone app dies or the EA session goes silent without a USB disconnect (USB stays configured, so `tud_umount_cb` never fires). Outer state stays 3 forever. Then: EP3 stays in dispatcher mode, and every EP2 packet routes into `iap2_ep2_rx` (protocol.c:249-254), so the Mac update/restore path that `FLASHING.md:311` calls "completely independent" is dead until the cable is pulled. A recovery path that silently depends on the user knowing to replug is not "always available" as preflight_check.sh:121 prints.

**Smallest reliable correction:** in `iap2_task` state 3, when `c->watchdog == 0`, restart the attach phase (`transport_init(false, c->connected); c->state = 1;`), matching stock's watchdog-expiry behavior at `0x61f4`'s caller, or document the divergence in `iap2.h`.

**Regression test:** extend `tests/iap2_session_test.c`: open a session, run 2500+ ticks with no traffic, assert `iap2_session_active()` returns false and EP2 dispatcher packets flow again. Fails today.

### F-6 (P2): The documented Mac restore command does not exist

**Files:** `docs/FLASHING.md:239` and `:296` document `tools/ck5200_usb.py restore-stock`; `tools/ck5200_usb.py:291-311` defines only `inspect`, `packets`, and `flash`.

**Impact:** an operator following FLASHING.md §5/§7 during a stressful recovery gets an argparse error instead of a restore. The real path is `scripts/revert-stock.sh` (typed confirmation, then `ck5200_usb.py flash <stock.bin> --confirm CK-5200`). The bcdDevice 0x9001 gate (ck5200_usb.py:262-266) is good design, but the doc must name the working command. VERIFIED by reading the parser; no device was touched.

**Correction:** replace both occurrences with `bash keyboards/ck5200/scripts/revert-stock.sh` and note the bcdDevice gate means the Mac path only works on custom firmware.

### F-7 (P2): The iOS update driver has no timeout, and the firmware silently drops dispatcher requests while a response is pending

**Files:** `ios/ClicksInspector/Sources/CustomFirmwareController.swift:261-270` (`withCheckedThrowingContinuation` with no timeout, `perform`); `keyboards/ck5200/firmware/platform/ch32v20x/protocol.c:224-226` (`handle_dispatcher_packet` returns without action if `pending_update_response_len != 0 || update_response_in_flight`).

**Failure scenario:** during the 732-chunk A2 stream, any dropped request (firmware busy window) or any lost response fragment leaves the app's continuation pending forever: the UI stays in "busy Writing N/23392" with no error and no retry. The Mac client has a 1-second timeout (`ck5200_usb.py:147`); the phone has none. A wedged mid-A2 session is safe data-wise (fresh A1 restarts cleanly, restore-harness-verified) but the operator gets no signal. VERIFIED by code reading.

**Correction:** add a 1-2 s timeout per exchange and retry the exact request once (safe: A2 carries the offset; a retry of the same offset is idempotent given the accepted-offset reply), then surface an error.

### F-8 (P3): The status docs pin a stale image hash and size, disagreeing with the artifacts and with each other

**Files:** `keyboards/ck5200/STATUS.md:35-38` (23,388 bytes, sha256 `dad34db6...`), `docs/RECOVERY.md:48` ("real custom image (23388 bytes)") vs. the actual `build/ck5200_qmk.bin` = 23,392 bytes, sha256 `7aeb06d2...` (my `shasum -a 256`), matching `docs/USB_AND_IPHONE.md:193` and `HANDOFF.md:147-148`. My `scripts/test.sh` run printed "full cycles ok (stock 18648, custom 23392)". RECOVERY.md therefore describes a restore-cycle verification executed against an image that is not the current one. Fix the two stale documents. VERIFIED.

### F-9 (P3): matrix.c claims a stock derivation that HARDWARE.md explicitly disowns

**Files:** `firmware/keyboard/matrix.c:1-8` ("Electrical matrix recovered from stock 1.2.2 static analysis") vs. `docs/HARDWARE.md` ("The earlier agent attributed this to stock analysis, but it did not supply a capture or annotated derivation. Treat the pin map ... as **UNVERIFIED**"). My stock disassembly shows an EXTI setup loop on PA0..PA5 (stock `0x2650-0x2692`) but I found no captured derivation of the scan polarity or the 2 µs settle. If the electrical map is wrong, the keyboard types nothing or the wrong keys, which collides with F-1's symptom and would misdirect first-flash debugging. Fix the comment to say "inferred, hardware-unverified", matching HARDWARE.md. VERIFIED contradiction; the underlying hardware fact remains UNVERIFIED.

### F-10 (P3): build.sh does not verify the dependency commits it claims to pin

**Files:** `scripts/build.sh:28-31` only checks `external/*/.git` exists; the pins live in `scripts/bootstrap.sh:7-12`. I verified all three checkouts currently sit exactly at the pinned commits and are clean (`qmk 3d4da6de29...`, `tinyusb efa3e1322...`, `wch baef005...`). A drifted local checkout would build silently. Add a `git rev-parse HEAD` check against the pins in build.sh (or a lockfile), so "reproducible" does not depend on local state.

### F-11 (P3): Safety-relevant comment contradicts the first-flash design

**File:** `ios/ClicksInspector/Sources/CustomKeyboardProtocol.swift:12-19` says "every write below is unreachable on stock", while the entire first-flash plan (CustomFirmwareController.swift:6-7, FLASHING.md §5, app README) depends on A1/A2/A3 being reachable on stock. The comment is true for 0x90-0x93 only. Fix the comment; a reader auditing the first-flash safety currently gets opposite signals from two files in the same app.

### Notes (not defects, owner should know)

- The EP2 dispatcher outside a session (protocol.c:245-253) is a deliberate custom addition; stock's pre-session path rejects every non-attach EP2 packet (stock `0x61ac` sends the 18-byte reject, VERIFIED in disassembly). Consequence: on custom firmware, any USB host can rewrite the flash with zero authentication; the consent gates in `ck5200_usb.py` are client-side only. This is the design that makes the Mac rescue path real, and FLASHING.md documents it, but "any host computer can reflash" deserves an explicit line in the risk table.
- The interrupt-entry convention (plain `__attribute__((interrupt))` handlers in `usb_irq.c`/`platform.c` with the D6 startup's `csrw 0x804, 3`) matches TinyUSB's own ch32v20x USBFS-port pattern (`external/tinyusb/hw/bsp/ch32v20x/family.c:51-65`), which upstream HIL-runs on real CH32V203 hardware (`test/hil/tinyusb.json`, `nanoch32v203-usbfs` variant). Stock's own handlers use the hardware-save style (stock `0x3ae6` clobbers `ra` without saving, proving HWSTK reliance). The combination is upstream-validated but stock-divergent, and nothing in this repo exercises interrupt entry at all. UNVERIFIED on this exact board; low residual risk given the upstream HIL evidence, listed here for completeness.
- Everything else I traced in the stock image matched the port byte-for-byte: attach cadence 475/50 ms (stock `0x626e-0x6290`), presession bytes 2..5 only (stock `0x6178`), reject packet (stock `0x61ac`, from `0x64EC`, 18 bytes), staging base `0x08008A00` with a 4-byte LE length committed by A3 (stock `0x38ec-0x390c`), image cap `0x6A00` (stock `0x37a8-0x37ae`), EP2/EP3 gates on outer state 3 (stock `0x629e`/`0x62cc`), tick decrements (stock `0x61c6`), handle_reset convention (`j handle_reset`, vector at +4, `mtvec|3`, `0x804=3`, sp top `0x20005000`, stock `0x485c`), and the generated ident table is byte-identical (`extract_ident.py` output diffs clean against `iap2_ident_data.h`). The vector table is correctly wired in the shipped ELF (index 12 -> SysTick at `0x678a`, index 59 -> USBFS handler at `0x67ac`). RAM has ample margin (.bss ends `0x20001388`, stack 2 KB at top of 20 KB).

## 3. Test credibility audit

I ran the full suite. Exact command and outcome: `bash keyboards/ck5200/scripts/test.sh` -> exit 0: "5 passed", "2 passed", update-protocol `ok`, backlight `ok 1`, consumer `ok 7`, keymap `ok`, restore `ok 3`, iap2 session `ok 13`, auth `ok 4`. Then `bash keyboards/ck5200/tools/preflight_check.sh` -> "PREFLIGHT PASS: 10 checks, 0 failures" (it writes only to /tmp and re-runs test.sh; I read it before running).

| Suite | Proves | Does not prove / weakens it |
| --- | --- | --- |
| `update_protocol_test.c` | Request framing, op dispatch, reboot callback, response bytes against mocked ops | Nothing about flash or transport |
| `ck5200_restore_test.c` | The real handler + real staging state machine end-to-end with the real stock and custom images, both directions, bounds and injected page failure | Mocked flash has no timing, no marker-vs-installer semantics, no power loss; fake-flash bound (`FLASH_SIZE 0x10000` from `STAGING_BASE`) is looser than real flash; does not test marker invalidation on A1 (see F-3) |
| `iap2_session_test.c` | Real `iap2.c` link machine, byte-exact packets vs scripted phone, 13 scenarios incl. ring overflow and challenge overread clamp | Phone script always sends well-formed full packets, so the SYN-parameter overread (`link_rx` reads `pl[4..10]` on a declared length with only 9 received bytes, protocol-adjacent in iap2.c:401-417) is never hit; no watchdog-expiry test (F-5); fake auth chip shares production's protocol assumptions |
| `iap2_auth_test.c` | Waveform-level bit-bang structure, retries, address toggle | Zero timing assertions; cannot catch F-4 |
| `ck5200_keymap_test.c` | Dispatcher framing/bounds/status against stubbed `dynamic_keymap` | The stub replaces the entire production persistence stack (QMK `dynamic_keymap.c` + wear leveling + `wear_leveling_ch32v20x.c`); first-boot seeding, EEPROM corruption, and real bounds behavior are untested anywhere |
| `ck5200_backlight_test.c` | Every TIM1 register value vs decode | Not the LEDs or GPIO on the board |
| `ck5200_consumer_test.c` | Press/release semantics, 10-byte packing | Integration with the (broken) report pump |
| `test_keymap_layout.py` | Every crossing of layers 0-2 vs the decoded table; layout macro geometry | Not the electrical scan (F-9) |
| `test_usb_protocol.py` | Host-side packet shapes, status enforcement, stock-handshake tripwire | Nothing on-device |
| Swift checks (protocol, layout/self-test, stock reads) | Protocol framing and factory-table equality on host | No imported-image validation (F-2); no timeout behavior (F-7) |
| `tools/preflight_check.sh` | Hashes of stock/app images, ELF/BIN validation, harness green, app bundle == build | Re-runs the same host tests, so it inherits every blind spot above; its "all 9 test outputs pass" greps for `^ok\|passed` and counts exactly 9 lines, brittle to any output change |
| `tools/verify_history.sh` ("11/11 built") | Claimed per-commit buildability | Not runnable under this review's rules (per-commit firmware builds + worktree creation inside the repo); recorded result only, UNVERIFIED by me |

**The decisive gap:** `protocol.c`, the most integration-dense file in the firmware, had **no committed test at all**. The prior session's ASan/UBSan protocol harnesses lived in `/tmp` and are gone; their "passed" results are unverifiable claims. My committed-equivalent harness found F-1 in minutes. This is the clearest example in the repo of a mock-shaped blind spot: the test suite ends exactly where the integration bugs live.

## 4. Missing verification

**Executable now (offline, no hardware):**
1. Commit a `protocol.c` harness (mine at `ck5200-review/protocol_harness/` is a working base) to `tests/` + `scripts/test.sh`; assert keyboard_task runs, no runaway retransmission, idle reports flow, dispatcher drop-and-retry, mount/unmount/suspend/resume retention.
2. Restore-harness extension for the commit-marker invalidation (F-3 regression).
3. Host harness for the real persistence trio: QMK `dynamic_keymap.c` + `wear_leveling.c` + `eeprom_wear_leveling.c` + `wear_leveling_ch32v20x.c` against a host flash model; assert first-boot seeding, persistence, and recovery from a corrupted backing store.
4. Swift check for imported-image validation (F-2 regression).
5. Timing assertion for `auth_bit_delay` (F-4 regression).
6. iap2 watchdog-expiry test (F-5 regression).
7. Re-run `tools/verify_history.sh c1d9e7c` (approx. 15 min; it creates `.history_check/` inside the repo, so I did not run it).
8. Rebuild the firmware from HEAD with output in scratch space to prove `build/` artifacts reproduce, e.g. `cmake -S keyboards/ck5200 -B /tmp/ck5200-build -G Ninja -DCMAKE_TOOLCHAIN_FILE=keyboards/ck5200/cmake/riscv-gcc-toolchain.cmake -DCMAKE_BUILD_TYPE=MinSizeRel && cmake --build /tmp/ck5200-build --target ck5200_qmk.elf`, then compare SHA-256 with `7aeb06d2...`. I was barred from firmware builds, so the shipped bin's provenance rests on timestamps plus `validate_image.py`'s ELF/BIN equality check, which passed.
9. Fix `docs/FLASHING.md` restore commands (F-6), stale numbers (F-8), and the power-loss table (F-3).

**Requires hardware or unavailable evidence (recorded for the execution phase):**
10. `docs/RECOVERY.md`'s own preconditions: read the MCU marking, establish probe access, take the full dump, and test a restore on sacrificial hardware, before the first flash.
11. A logic-analyzer or scope capture of the auth-chip I2C (PA13/PA14) during the first session attempt, to settle F-4 empirically.
12. The boot-region dump, to settle installer marker semantics (F-3) and the installer's acceptance of non-vendor images.
13. A stock-side first-flash trace: whether stock accepts A1 from the inspector app at all (the app has so far only performed seven read commands on stock).

## 5. Final challenge

1. **The single most dangerous assumption:** that the un-read boot-region installer will do the right thing with whatever the staging marker says after an interrupted update. It is the only component in the chain that can destroy the application region, it has never been dumped or tested, and two of this repo's own docs disagree about whether an interruption is survivable (F-3).
2. **Most likely to pass tests but fail on hardware:** the MFi auth-chip bit-bang. The harness proves waveform shape; nothing proves the chip tolerates a bus running ~2x faster than the only known-good configuration (F-4). The runner-up is the matrix scan, whose electrical basis is admitted UNVERIFIED in HARDWARE.md while matrix.c claims a stock derivation (F-9).
3. **Weakest existing safety claim:** FLASHING.md §7's "What does NOT lose the keyboard" list, specifically the power-loss row, contradicted by RECOVERY.md's "Update interruption remains unresolved" and by the marker design itself. Close behind: STATUS.md's "ready for first flash", pinned to a hash of an image that no longer exists (F-8), and preflight's "The Mac restore path is always available as backup", which is false before the first successful flash (the Mac tool refuses stock devices) and false after a wedged session without a replug (F-5).
4. **What I refuse to approve:** flashing this build. F-1 guarantees the keyboard cannot type on first boot and will mislead every step of the post-flash checklist; F-2 leaves the one unrecoverable state one file-picker mistake away; F-3 leaves the worst-case update interruption argued about in two documents instead of prevented in code. All three have small, testable fixes. Until they land, the harnesses pass, the preflight passes, and the keyboard would still be dead on arrival.
5. **Evidence that would change my conclusion:** a committed protocol harness showing scans and idle reports work after the fix; a restore-harness case showing an interrupted A2 can never present a valid marker to the installer; a Swift check refusing arbitrary imports; a scope capture of the auth chip ACKing at the port's actual bus speed; and, before the first flash, the RECOVERY.md hardware preconditions (chip identity, dump, tested restore). With F-1 through F-4 fixed and those artifacts in hand, my verdict would move to "no significant software findings" for the offline layer, while the first-flash authorization would still rest on the recovery evidence, not on this review.
