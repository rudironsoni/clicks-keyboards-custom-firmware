# M2: application-level rescue

**M2 remains open pending independent review. Implemented and host-tested, not installed or physically tested. R3 remains UNMET. Custom flashing remains NO-GO.**
Base: `a368e0d619e26dea770544f936874514b117bca8`. This is a secondary application service, not independent boot recovery.
The [accepted recovery decision](adr/0001-independent-iphone-recovery.md) and [authorization record](flash-readiness.md) still apply.

## Entry and scope

The project overrides QMK's weak `main()` in [main.c](../firmware/platform/ch32x035/main.c).
Upstream QMK, TinyUSB, and WCH sources are unchanged. Both execution modes share one USB stack, one iAP2 stack, and one staging writer.

An explicit `02 94` software request is recognized only in normal mode on the authenticated EP3 companion channel.
It requires a live iAP2 link, the stock authentication/EA flags, and separate rescue authentication/EA-open state.
Rescue state clears on EA01, watchdog expiry, disconnect, or transport reset. AA04 alone cannot reopen a closed rescue session.
The request also requires an idle staging writer and a marker of zero or erased `0xFFFFFFFF`.
Any other marker or active/failed staging transaction blocks the rescue-entry reset.
The accepted request writes a magic word and its complement to eight RAM bytes, then calls the existing `mcu_reset()` hook.
It has no success reply because it resets. A rejected request performs no reset or write.
The current iOS controller does not authorize this request. No iOS image authorization or physical flashing gate changed.

The linker places the RAM request in `.noinit`, outside the startup BSS clear and without a flash initializer.
At `main()`, an exact matching pair selects rescue. All other values select normal operation.
`main()` clears the request before platform initialization, so it is one-shot. Entering the application's rescue path performs no flash write.
No reserved-flash flag, option-byte change, or proprietary boot-region change is involved.

**[UNVERIFIED]** The proprietary boot stage may alter this RAM or process staging metadata before our application starts.
The marker check blocks known pending/unknown markers. It does not prove the unread boot stage's behavior.
The host test injects the request through the production request function. It does not simulate that boot stage or physical reset retention.
No key chord is implemented or claimed. No GPIO mapping or physical startup timing was tested.
Software entry requires a working application session before the request. It cannot rescue a device already stuck before that session.

## Exact startup paths

Common path, before either application mode:

```text
application _start -> handle_reset (WCH startup)
  -> initialize gp/stack, copy .data, clear .bss, configure CPU/mtvec
  -> SystemInit -> project main
  -> read and clear RAM request -> platform_setup
     -> SysTick, USB clocks/pins/PHY, interrupts
```

Normal path, preserving the pinned QMK order:

```text
protocol_setup
  -> host_set_driver -> ck5200_transport_setup
     -> tud_init(0) -> iap2_init -> iap2_auth_init
  -> ck5200_backlight_init
keyboard_setup -> protocol_pre_init -> keyboard_init -> protocol_post_init
loop:
  protocol_pre_task -> tud_task
  protocol_keyboard_task -> existing QMK keyboard/report processing
  protocol_post_task -> tud_task -> apply_persisted_brightness_once -> service_iap2
  optional QMK RAW/console/painter/deferred tasks -> housekeeping_task
```

Rescue path:

```text
ck5200_staging_require_stock (RAM state only)
ck5200_transport_setup -> tud_init(0) -> iap2_init -> iap2_auth_init
loop:
  ck5200_transport_task -> tud_task -> service_iap2
    -> timer/iap2_elapsed_ms -> iap2_task
    -> cancel transaction if authorization is lost -> send pending response
```

Rescue does not register the QMK host driver or call keyboard setup/init/tasks, matrix scanning, dynamic-keymap initialization, EEPROM, backlight, or housekeeping.
HID GET_REPORT returns no report in rescue. Other HID control callbacks only maintain small protocol fields.
The production [authentication driver](../firmware/platform/ch32x035/iap2_auth.c) still communicates with the real authentication chip.
Host tests replace the chip with a model. Physical authentication of custom firmware remains unverified.

## Stock-only transaction

- Rescue exposes only version `03` and the existing A0–A3 transaction on authenticated EP3. EP2 cannot bypass this gate.
- The version response is `08 02 03 00 52 53 00 01` (`RS`), distinct from both stock and normal custom firmware.
- A1 accepts only 18,648 bytes. It invalidates the previous marker through the existing bounded staging writer.
- A2 retains the existing ordered, bounded chunk checks. It can write only staging pages.
- A3 requires every byte, flushes the final page, then hashes the **stored flash bytes**, not just the received stream.
- The required SHA-256 is `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`, the exact official V122 image.
- Only a matching digest permits the length-marker commit. A mismatch fails the transaction and requires a fresh A1.
- A0 is allowed only after a successful stock commit in the current authorized session.
- USB unmount, EA session closure, or observed link timeout cancels transaction state and pending replies. Reconnection requires fresh authentication and A1.
- Elapsed milliseconds update all iAP2 timers with saturating subtraction. RX accounts for elapsed time before any packet can refresh the watchdog.

Abort changes RAM only. It does not erase a previously committed marker or claim that an install was undone.
Disconnect never proves successful recovery. A successful A3 reply proves only staging commit, not installer execution or boot success.
Power-loss windows in the existing marker page transaction remain unresolved. This implementation does not establish rollback.

All page writes remain within `0x08008A00–0x0800F4FF`.
The application, presumed installer, serial page `0x0800F600`, configuration page `0x0800F700`, and reserved upper flash remain outside that writer.
These bounds do not establish what the proprietary installer does after reset.

SHA-256 reuses unmodified [amosnier/sha-2](https://github.com/amosnier/sha-2/tree/565f65009bdd98267361b17d50cddd7c9beb3e6c), pinned in `scripts/pins.env`.
Its license permits use under 0BSD or the Unlicense. Bootstrap obtains the pinned source, and the build checks its revision.
Normal-mode staging behavior remains available as before. The new stock restriction is one-way for a rescue boot and has no host command to disable it.

## Verification and resource budget

The new [integration harness](../tests/ck5200_rescue_test.c) links production main, protocol, iAP2, staging, SHA-256, and the real pinned QMK core.
USB, matrix GPIO, reset, and the authentication chip are host models. The existing waveform test separately covers the production authentication driver.

| Required property | Evidence |
| --- | --- |
| Normal startup | The selected main reaches real QMK pre/post initialization, matrix initialization, and scanning. |
| Rescue skips QMK initialization/tasks | Matrix and keyboard initialization hooks fail if called. No calls occur, and EEPROM remains uninitialized. |
| USB/iAP2 services remain active | The real main repeatedly invokes transport tasks. Scripted EP2 messages drive the real parser and authentication-chip calls. |
| Unauthorized images cannot commit | A changed stock byte, wrong size, incomplete transfer, and corruption of stored staging bytes all fail before successful A3. The exact stock image commits. |
| Disconnect does not authorize | Unmount/reconnect, EA close/reopen, and watchdog expiry require a fresh A1. A0 after reconnect cannot use the previous RAM authorization. |
| Early failure is excluded | Injected platform failure exits before any transport service. No rescue or startup-fault recovery is claimed. |
| Staging bounds hold | Every modeled page access asserts its address. Bytes outside staging remain unchanged. The existing fault harness also tests boundary and partial-page failures. |
| Image/ELF/RAM limits | Linker and validator pass. The validator also checks the eight-byte `.noinit` region, its NOBITS type, and separation from BSS and stack. |

Red/green evidence: before digest validation, the tampered-stock A3 rejection assertion failed because the writer accepted the image.
After firmware-side readback hashing, that assertion and the complete host suite passed under the existing address/undefined-behavior sanitizer harness.
The first harness run also exposed invalid pointer arithmetic in the test flash model. That model was corrected before the passing run.

Original build comparison at `828627c`, xPack RISC-V GCC `15.2.0-1`, normal production target containing both modes:

| Metric | Before M2 | M2 | Change |
| --- | ---: | ---: | ---: |
| Binary / flash load | 24,124 B | 25,852 B | +1,728 B |
| Free application flash, limit 27,136 B | 3,012 B | 1,284 B | -1,728 B |
| `.data` | 136 B | 144 B | +8 B |
| `.bss` | 3,832 B | 3,836 B | +4 B |
| `.noinit` | 0 B | 8 B | +8 B |
| Linked RAM including stack reserve/alignment | 6,016 B | 6,040 B | +24 B |

RAM limit is 20,480 B. The stack reserve stays 2,048 B. Static allocation and linker bounds are measured, not physical stack high-water usage.
The new RAM request is `0x20000F8C–0x20000F93`, immediately after BSS. Disassembly confirms the strong `main` at `0x78C4` reads and clears it before initialization.
The new reset target is `0x7C92`. The image ends at `0x84FB`, below exclusive limit `0x8A00`.
Relinking changes 23,186 of the original common byte offsets and adds 1,728 bytes. This is a rebuilt application, not a binary patch.

- Before SHA-256: `7799f866ee18ea4c3ba93852b5c6cb7b32864a29efc88fc79301af7f84d09623`.
- M2 SHA-256: `a6177efbb8702af61ac19ab5d28d388b804d4b85632420b1447f8466ba2fae01`.
- Reproduction: `bash keyboards/ck5200/scripts/setup.sh`, `bash keyboards/ck5200/scripts/build.sh`, then `bash keyboards/ck5200/scripts/test.sh`.
- CI checks these same firmware build/test/ELF paths on a clean runner. Delivery reports the exact commit's firmware and iOS run results separately.

## Remaining decision

No physical firmware write, reset experiment, ISP entry, protection change, debug-pad access, or installation occurred.
Hardware-dependent items remain unverified: RAM retention through the original boot stage, custom USB enumeration/authentication, timing, physical flash programming, and installer behavior.
Corrupt application vectors, erased application code, failure before rescue service entry, and a failed USB/iAP2 stack are outside this mechanism.

M2 remains open until the corrections below pass independent review and receive acceptance.
It does not resolve R3 and does not authorize the first custom flash.

## Corrections after review of `828627c`

[CORRECTION] The original delivery did not establish strict session closure, real elapsed-time expiry, or mandatory presence of `.noinit`.
The previous tests missed these cases. No architecture change or hardware experiment was needed.

Production changes:

- Keep the decoded stock flag masks, including EA01 retaining bit 11. Track rescue authentication and EA-open state separately. Closing a session clears both rescue states, so EA01 followed by AA04 cannot authorize A1 without a new EA00.
- Subtract the full unsigned elapsed millisecond interval from attach, watchdog, and retry timers, saturating at zero. This is constant-time and handles a timer wrap. Check elapsed time at RX entry as well as in the service loop, before packet activity can refresh the watchdog. Expiry clears rescue authority and cancels the transaction.
- Require exactly one `.noinit` section of type NOBITS, size eight bytes, four-byte alignment, and WA flags. Reject overlap with BSS, the reserved stack, or any other allocated section. Missing `.noinit` is an error.

Red-first results, using the new tests against unchanged `828627c` production code:

| Regression | Observed failure before the fix |
| --- | --- |
| `closed-session` | After EA01 then AA04, `!iap2_restore_authorized()` failed. |
| `watchdog-jump` | After one `advance_time(2600)` and one service call, `!iap2_session_active()` failed. |
| `watchdog-packet` | After one 2,600 ms jump and an EP2 AA04 before service, `!iap2_restore_authorized()` failed. |
| `watchdog-ep3` | An A1 arriving on EP3 after one 2,600 ms jump was accepted. |
| ELF `missing` and `section-overlap` | Full validation did not raise an error. The valid ELF and the other four rejection cases passed. |

After the fixes, all four production-entry regressions pass. They also verify that blocked A1 requests cause no flash writes, a new EA00 is required, the old transaction cannot continue with A2/A3, and a fresh authorized A1 can succeed.
The seven [ELF cases](../tests/test_validate_image.py) run the full production validator and actual binutils against copies of the built ELF with changed section headers. They cover valid, missing, initialized, oversized, BSS-overlapping, stack-overlapping, and other-section-overlapping `.noinit` layouts.
The complete host suite, including the stock-compatible iAP2 replay tests and sanitizer-enabled production-entry tests, passes.

Corrected firmware, same toolchain:

- Size: **26,012 bytes**, +160 bytes versus `828627c`; **1,124 bytes** remain below the 27,136-byte application limit.
- SHA-256: `520225e072a938e3a47cb2487add991fe8aec8a9af2496504b48b618dd3593c3`.
- Linked RAM including stack reserve/alignment: **6,040 bytes**, unchanged. RAM limit: 20,480 bytes.
- Reset target: `0x7D32`; image ends at `0x859B`. ELF, linker, and image checks pass.
- Clean firmware and iOS CI run links are reported with the pushed correction commit. Local iOS builds and physical tests were not run for this firmware-only correction.
