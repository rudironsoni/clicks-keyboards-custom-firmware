# CK-5200 handoff

> **Authoritative current workstream:** the newest dated stopping-point
> section in this file. All earlier stopping-point sections are historical
> records of their dates, not competing claims about the present.

The authoritative current gate is [flash-readiness.md](keyboards/ck5200/docs/flash-readiness.md).
This replaces the obsolete first-flash instructions. Git history preserves them as historical evidence only.

Stock diagnostics baseline: complete and accepted on 2026-10-14.
Five inspected sessions cover stock getters, mixed typing, exports, reconnect, and app transitions.
Use the acceptance scope, evidence hashes, and deferred gaps in the authoritative readiness record before choosing further work.

The [recovery decision](keyboards/ck5200/docs/adr/0001-independent-iphone-recovery.md) records NO-GO and the supporting offline audit.
The presumed installer region is not a proven immutable recovery service. A patched-stock reader cannot justify its own installation.
HTTPS inspection is stopped. The stock URL was already known. No phone settings changed, and the vendor inquiry remains unsent.
M2 [application-level rescue](keyboards/ck5200/docs/application-rescue.md) is COMPLETE, accepted by Rudi at `ee45b6bbb2b544f19d44c036099c4af2c5aa7156` after independent review.
Acceptance covers the software-only secondary mechanism, not physical compatibility, iPhone-operated restoration, or recovery from an unbootable application.
M3 verified stock firmware manager is closed for software scope in this R3 investigation.
Its historical delivery branch was `feat/verified-stock-firmware-manager`, based on `main` at `246f2b8`.
Tested revision `a7cf3f7` passed iOS CI `38883669172` and firmware CI `38883668796`. Both independent reviews are clear.
Unsigned app archives and hashes are published in the readiness report. Physical writes remain disabled in every iOS build.
See [stock manager readiness](keyboards/ck5200/docs/stock-manager-readiness.md) for implementation, verification and remaining limits.
The QMK/keymap/spare-procedure PRs #1, #2 and #3 remain separate and unmerged. Rudi has ruled out buying a second keyboard.
R3 remains unmet. The current task is evidence-first research and offline models on `feat/r3-usb-c-recovery-study`, based on `d981763`.
No physical experiment or hardware implementation may start without a separate decision.

- Comprehensive physical characterization remains UNVERIFIED beyond the recorded observations. Baseline collection is closed.
- R3 independent recovery is UNMET. The Mac dispatcher depends on a running application and is not independent rescue.
- No custom image is authorized. No keyboard firmware write, reset, ISP operation, or protection change is authorized for M1.
- Default Debug and Release iOS builds are diagnostics-only at the controller boundary.
- Every iOS configuration blocks physical updates, including `AuthorizedStockUpdate` and `StockFirmwareManager`.
- Preserve presumed installer `0x0000–0x1FFF`, application `0x2000–0x89FF`, staging `0x8A00+`, and upper reserved flash.
- Custom CH32X035 keymap storage is transient RAM. No flash-backed persistence is established.

Further recordings require a specific need in a separately scoped task. Do not resume SDI research.

## Current stopping point, 2026-10-22 (offline RAM configuration and USB safety)

Rudi accepted `d6d3461718ce73a1d661c61e58df34e5fa2d59bf` as the bounded
native/custom ownership increment. Firmware CI `39821804798` and iOS CI
`39821804796` passed. Downloaded firmware hashes matched the local image.
The current increment is tested locally and awaits hosted CI.

- Extension-only command `0x94`, protocol 1, provides a nonce-bound
  `CKX1` capability reply, packed-map readback, bounded ordered staging,
  atomic all-up application, and abort. Old generations and incomplete or
  invalid records cannot replace the active map. USB reset discards the
  session and incomplete transaction. No command writes flash.
- The production Swift codec/client drives actual V122 `0x2208` through
  the patched command call at `0x2218`. Replies execute original EP3
  submission and completion. Native gestures then produce edited Esc/Tab
  through original EP1 submission/completion. No map/enable RAM injection
  occurs in this configuration proof. Stock V122 fails capability identity.
- Full 978-byte records apply and read back. Lost acknowledgements close
  the Swift session without retry. A new handshake/readback recovers the
  actual committed state. The client is not connected to AccessoryInspector
  or an editor UI. Physical dispatch remains disabled.
- Real SysTick/USB IRQ entry/return, nested USB completion, preview IRQ
  deferral, suspend/resume, bus reset, companion backpressure, notification
  retention, saturation, overflow, and final all-up recovery pass in the
  bounded model. USB configuration and authenticated iAP2 state are still
  synthetic. Full startup, EP0 enumeration, and iAP2 authentication are open.
- The write audit found an actual PWM side effect in OEM previews. A
  reversible hook at `0x3062` suppresses only preview PWM calls. Removing
  the hook reproduces the audit failure at `0x4F0E`, `0x40012C20`.
  A real native backlight gesture still updates PWM and its settings bit,
  including when native NAV is disabled. Five projected-consumer checks
  compare full HID and companion packets, including Cursor status payloads.
- All 37 Python tests, existing native/Swift suites, 20 V122 cases, and
  Swift-to-V122 integration pass locally. The original 299 layer checks
  and selected 130-settings comparisons remain intact.
- Image: 27,058 bytes, extent `0x89B2`, headroom 78 bytes. No complete
  unused application page remains. Two maximum A/B records need at least
  eight 256-byte pages before journal code. Decision: RAM-only extension
  phase with iPhone-backed profiles. Keyboard-flash persistence is not met.

Evidence and exact wire format:
[section 14](keyboards/ck5200/docs/stock-extension-feasibility.md#14-offline-ram-configuration-and-usb-safety).
R3 remains unmet. M4 stays closed. The vendor inquiry stays unsent. No
physical command, flash write, settings write, reset, or probe occurred.
The exact-QMK assurance gate and physical dispatch policy stay unchanged.

## Current stopping point, 2026-10-22 (native/custom layer ownership)

Rudi accepted `21a985c` as the bounded active-OEM-compatibility baseline.
Firmware CI `39804442651` and iOS CI `39804442643` passed at that commit.
Its downloaded firmware evidence matched the local hashes. The current
increment adds native/custom layer ownership. Its evidence is
local only until a new commit passes hosted CI.

- Native Symbols and Cursor select programmable SYM/NAV when remapping
  is enabled. `native_mask` independently disables each native mode.
  The embedded default enables both. Remapping itself still starts disabled.
- Custom hold, toggle, one-shot, and tap-hold activators target any layer,
  including Base, ahead of native fallback. Releases run first. Activators
  visible in the initial layer run next with snapshotted targets. Other
  presses run last. Each phase uses row/column order.
- Replacing original Symbols or GUI removes its native role. Genuine OEM
  gestures still own their participating keys unless custom activation wins.
- OEM keys selected under custom activation retain their output-layer
  snapshot until release. This fixes one-shot consumption before OEM
  rendering and supports simultaneous OEM presses from different layers.
- Packed format v4 adds the native mask to the existing six-byte header.
  Records remain at most 978 bytes. The default remains 330 bytes.
- All 37 Python tests and 18 genuine V122 cases pass locally. The eight
  new cases check 299 complete host HID states, including all 108 positions.
  Existing native settings comparisons still match 2,502 HID submissions
  and 2,542 companion submissions across 130 selected combinations.
- Image: 25,466 bytes, extent `0x837A`, headroom 1,670 bytes. SHA-256:
  `bb83d17bb13d1846e2548da271b430bb94ff4debcffaf8657c77e21bb8e89e7f`.

Next gate: audit every preview write and execute real interrupt/USB paths.
Shared capture flags, nested interrupts, companion backpressure, overflow,
suspend/reconnect, and complete startup remain unproven. RAM editing and
persistence remain separate work. The current flash headroom is not an
allocated persistence region. No physical command ran. R3 stays unmet,
M4 stays closed, and the vendor inquiry stays unsent. QMK and iOS source
remain untouched.

Evidence: [section 13](keyboards/ck5200/docs/stock-extension-feasibility.md#13-nativecustom-layer-ownership-after-the-21a985c-review).

## Current stopping point, 2026-10-22 (active OEM compatibility)

`57fef66` remains Rudi's accepted bounded-execution baseline. Both of its
workflows passed. The current increment preserves native policy while
the remapping engine is enabled, not only through disabled delegation.
Full OEM integration and R3 remain open. No physical operation is authorized.

- The multiple-pending-tap regression failed before the fix. Per-key
  pending releases now clear both usages without another matrix event.
- Native OEM actions remain in V122's builder and latch policy. Custom
  actions replace their crossings. A copied-state preview removes a
  generic activity marker's native usage while preserving sticky effects.
- **[CORRECTION]** V122's 97-byte HID descriptor defines four keyboard
  slots and consumer bits, not six slots. Overlay output preserves those
  consumer bits. Unsupported right modifier bits are rejected.
- Ten bounded execution cases pass locally. Enabled native comparisons
  preserve 2,502 HID submissions and 2,542 companion packets across 130
  flag/layout/currency combinations. A mixed remapped-letter test verifies
  sticky Shift consumption. NAV remaps, a four-key/consumer chord, and
  idle TAP produce the expected 18 ordered reports, including delayed EP1.
- Packed format v3 adds explicit OEM actions. The device default is 330
  bytes. The maximum remains 978 bytes. No storage journal is implemented.

Next gate: actual interrupt and USB lifecycle execution, including nested
preview/capture safety, companion backpressure, FIFO overflow, timer
rollover/coalescing, suspend, and reconnect. The current shared capture
flags are not proven safe against nested USB calls. Default OEM gestures
do not automatically select custom engine layer numbers. Resolve the
activation contract before the iPhone editor's typed RAM operations.
Persistent A/B records follow RAM editing, in a separately verified region.

Detailed evidence and reproduction:
[stock-extension-feasibility.md section 12](keyboards/ck5200/docs/stock-extension-feasibility.md#12-active-native-policy-preservation-after-the-57fef66-review).

The QMK gate, iOS source, closed M4 trial, vendor inquiry, and all physical
authorization decisions remain unchanged.

## Current stopping point, 2026-10-22 (OEM executable integration)

Rudi accepted `9113dab` only as an offline correctness-and-build milestone.
The **OEM executable-integration milestone remains open**. No physical
keyboard operation is authorized. M4 stays closed and R3 stays unmet.

Implemented after that review:

- Asserted callback address `0x6900`, ELF symbol/disassembly checks, SRAM
  placement and startup initialization, plus reversible startup/timer JAL hooks.
- Corrected report framing and separate companion/EP1 submissions. The
  device clock now counts nominal SysTick services, not builder callbacks.
  An eight-report FIFO waits for EP1 NAK and handles idle tap releases.
- Corrected activation snapshots, explicit NO/TRANSPARENT actions, and
  two-pass record validation. Format v2 maximum is 978 bytes, not 512.
- Added a separate bounded V122 execution profile. Default delegation
  matches 74 full stock submissions. Enabled fixtures execute Esc, Tab,
  Undo, Redo, Up, and idle tap release, including delayed host readiness.
  Removing the timer hook demonstrably loses the final release.
- Firmware CI now retains the research ELF, BIN, map, disassembly,
  symbols, hashes, manifest, report, and traces.

**[CORRECTION]** Registration stores the builder at `scan_obj+0x18` and
the layer handler at `+0x1C`, not the reverse. SysTick is `0x3AE6`.

The bounded profile executes CRT through the SystemInit call, then uses
a separate scanner fixture. It does not execute full startup, interrupt
entry/return, USB enumeration, real completion IRQs, or iAP2 delivery.
Its keymap settings are synthetic RAM inputs, not USB editor operations.

Next: preserve consumer, sticky modifier, currency, and layout behavior
with remapping enabled. Extend the profile through genuine IRQ and USB
completion paths. Verify FIFO overflow. Configuration/readback and
recoverable flash storage follow only after executable equivalence passes.
The QMK-pinned assurance gate and iOS app remain untouched.

Reproduction and detailed evidence:
[stock-extension-feasibility.md section 11](keyboards/ck5200/docs/stock-extension-feasibility.md#11-oem-integration-increment-after-the-9113dab-review).

## Current stopping point, 2026-10-21 (settings protocol)

**Stock configuration protocol — evidence phase** is the current workstream
per Rudi's 2026-10-21 direction. M4 software stays closed (the trial app is
installed on his iPhone; physical firmware execution remains unauthorized).
No implementation and no physical writes are authorized.

Static evidence delivered on `docs/ck5200-stock-settings-protocol`:
[stock-settings-protocol.md](keyboards/ck5200/docs/stock-settings-protocol.md)
(the `0x81–0x8D` contract with the zero-mask 0x83 static proof) and
[stock-capability-ledger.md](keyboards/ck5200/docs/stock-capability-ledger.md)
(the 36-position classification and the no-remapping boundary).

Rudi's 2026-10-21 review of `ec9f869` accepted the settings work as a
baseline and set the next milestone: offline programmable stock-extension
feasibility (no keyboard writes), with the record corrections applied (the
40-byte tail claim superseded; both firmware-change routes preserved in the
ledger; this file's authoritative-workstream note added). First increment
delivered: interception proven and demonstrated in
[stock-extension-feasibility.md](keyboards/ck5200/docs/stock-extension-feasibility.md)
with the reversible 8-byte registration redirect and its offline patcher
(`tools/stock_extension_patch.py`, 5 tests, part of a 26-check local run).

2026-10-22: milestone deliverables 2–4 completed offline — the builder ABI
reconstructed from the scan path, the three-layer/36-position engine
implemented with all four activation modes and a 36-check behavioral suite
(35 host tests total, zero failures), the target footprint measured at 772
bytes (total extension ≈1.5 KiB inside the 8,488-byte capacity), and the
QMK comparison recorded. Firmware CI now runs the host Python tests.
Remaining before any installable claim: the callback shim, the
full-pipeline harness, and the unchanged R3 recovery gate.

2026-10-22 (later): the review's four findings are fixed and the
correctness-and-integration increment is delivered — engine v2 with
per-layer actions, immutable press snapshots, an activator stack, and
millisecond deadline timing (tap release proven without any further key
event); the 2/03 Left Shift correction verified at V122 `0x2AE6` and
applied to the shared fixture and the ledger; the 108-position sweep
plus no-base-regression, overlap, and malformed-input tests (258 engine
checks + 21 shim/serialization checks, all hosted in firmware CI); the
measured packed record (350 B) and the fully linked research image
(21,272 B total, extension 2,624 B at 0x6900, headroom 5,864 B); and the
replacement shim, which emits exactly one frame per tick and never
duplicates OEM output. Remaining, recorded as pinned open items: the
exact 0x278C framing, the tick RAM address, real-image RAM placement,
and the V122 emulator profile for the decisive end-to-end verification.

All physical operations remain unauthorized.

## Current stopping point, 2026-10-21 (M4 software closed)

**M4 (first physical stock firmware flash) remains the current task.** The
2026-10-20 review corrections are applied on
`feat/ck5200-m4-stock-flash-trial`: audit defect fixed with completeness and
abort regressions, fail-closed audit, conclude-trial reconciliation, independent review disposed, 248 host
checks green. The V121 downgrade is explicitly not approved.

**M4 software is COMPLETE and closed by Rudi, 2026-10-21** (both workflows
green at `499c13b`). Do not add implementation work. The remaining sequence is
physical preflight without flashing: the read-only Mac USB capture with the
CK-5200 attached, the signed StockFlashTrial build inspected on his iPhone
(banner, identity, V122 hash, consent dialog; no Flash press), then his one
explicit V122-to-V122 trial decision with the residual brick risk stated in
[keyboards/ck5200/docs/m4-stock-flash-trial.md](keyboards/ck5200/docs/m4-stock-flash-trial.md).

The operating model stays: iPhone primary (stock and QMK installs, updates,
keymaps, profiles), Mac emergency fallback (read-only inspection first;
restoration only if ever established).

**No physical firmware write is authorized.** The flash memory-ownership
corrections and the boot-stage questions (vendor inquiry 0a-0f, UNSG) remain
the governing evidence.

## Current stopping point, 2026-10-20

## Current stopping point, 2026-10-17

The focused follow-up selects **outcome 2: exact vendor dependency**.
See [actual WCH implementation analysis](research_notes/CK5200%20USB%20recovery%20feasibility/isp-loader-followup.md) and [V122 tail evidence](research_notes/CK5200%20USB%20recovery%20feasibility/v122-tail.md).
PROGRAM addressing does not establish selective ERASE. The 36 staging fill bytes do not establish the 40 destination bytes.
Static WCH library analysis found count-only A4, erase despite `IsEraseAllCFlash=false`, and configuration writes in the normal programming flow.
`IAPStartAddr` does not supply the X035 erase/program base. Target-side implicit erase remains unknown because no matching factory loader was recovered.
The [ready-to-send inquiry](research_notes/CK5200%20USB%20recovery%20feasibility/vendor-inquiry.md) remains UNSENT.
The hardware concept remains a bounded study. No adapter build or keyboard operation is authorized.

### Original study and offline review, 2026-10-16

The [USB recovery feasibility report](reports/CK5200%20USB%20recovery%20feasibility.md) synthesizes the three research notes.
It adds a conditional inner-plug BLE/USB-host concept, source-pinned ISP audit, alternatives, and eight falsifiable R3 criteria.
The [ADR extension](keyboards/ck5200/docs/adr/0001-independent-iphone-recovery.md#2026-10-16-extension-external-adapter-research-is-in-scope) supersedes the old next-M2 task.
M1, M2, and M3 stay closed within their recorded scopes.

Chip-level PC17 power-up entry is documented. Actual connector routes, early CC/data states, VDD timing, safe bias, and retained BOOT remain unknown.
The factory loader is mutable System FLASH, not immutable ROM.
The inspected `wchisp` programs from zero. No preservation-safe selective factory erase or USB RAM-helper route is established.
Protection state, device-specific state at `0xF600`/`0xF700`, and power-loss
recovery remain critical blockers. The former "40 unknown tail bytes" concern
is corrected by the flash-ownership evidence: the tail is ordinary unused
application flash erased by every OEM update; the open risk is the boot
stage's unknown marker/copy/interruption behavior, not those bytes.
OEM startup and retained staging could also affect a directly restored application. Their behavior remains unknown.

Next: obtain model-matched vendor/source evidence. Both scoped independent agent reviews are closed.
The report writer inspected all three files in `keyboards/ck5200/recovery_lab/` and reran the checks.
All 10 unit tests passed. Captured CLI checks confirmed default exit 2 (blocked) and hypothetical exit 0 (model match only).
The final ISP/lab review closed all four findings: exact operation/image/state guards, strict tail preservation, restart journal requirements, and actual A5 decoding/rejection.
The hypothetical `tail_preserving_erase` contract is required before any mutation. It is not known CH32 page erase or a factory wire opcode.
The restart fixture loses host transaction state and requires the original in-memory journal. No durable journal or OEM normal-boot proof exists.
A1 identity fixtures, A4 count decoding, A5 packet decoding, and A6 comparison fixtures exist. A3/A7 and hardware transports are not implemented.
The coordinator reports no actionable ISP/lab blockers and no P1/P2 electrical blockers for conceptual scope, without hardware testing.
The final reviewer checked 10 existing tests, direct forbidden mutations, 11 A5 cases, and 292 fault/host-restart/retry combinations across all 73 pages.
Strict tail preservation passed. Missing, altered, and other-instance journals were refused. Default/hypothetical exits remained 2/0.
The [durable review summary](research_notes/CK5200%20USB%20recovery%20feasibility/review.md) records agent findings and limits, not a full transcript or vendor proof.
The expanded vendor query is UNSENT. A future generic-chip fixture requires separate authorization and cannot prove CK-5200 wiring.
No keyboard operation, firmware installation, reboot, probing, opening, ISP, unlock, purchase, or hardware build is authorized.
The report does not authorize flashing. [flash-readiness.md](keyboards/ck5200/docs/flash-readiness.md) retains NO-GO.
