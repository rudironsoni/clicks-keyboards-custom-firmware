# CK-5200 handoff

> **Authoritative current workstream:** the newest dated stopping-point
> section in this file. All earlier stopping-point sections are historical
> records of their dates, not competing claims about the present.

The authoritative current gate is [flash-readiness.md](keyboards/ck5200/docs/flash-readiness.md).
This replaces the obsolete first-flash instructions. Git history preserves them as historical evidence only.

## Current stopping point, 2026-10-23 (installation feasibility verdict)

**NO-GO for installation under the current constraints.** The physical
programmable keyboard is not delivered. This is not technical impossibility.
The [verdict](docs/ck5200-installation-feasibility-verdict.md) owns the decision,
minimum evidence and links to the iOS, differential, executable/OEM, fault-tree
and sanitized ledger records.

The decrypted iOS encoder and normal EAF workflow are reconstructed directly.
All 851 captured update requests match its encoding for the padded image.
Vendor success after A0 proves no installation. Exact raw/padded receive
fixtures return `CKX1`; stock, disabled hook and OEM redirect return the exact
physical `FF`. Full startup reaches unsupported RCC read `0x3BBE` / `0x40021000`.
No synthetic continuation closes that gate. The physical cause remains
undecidable without device-side evidence. OEM boot-stage copying and R3 remain
unverified. No firmware or installer-byte correction is justified.

No keyboard commands, writes, resets, reconnect/power experiments, physical
phone app changes or journal changes occurred. `sentRebootPending` is preserved.
M4 stays closed. The accepted firmware and editor remain unchanged. Do not
resume physical installation from these offline results.

Stock diagnostics baseline: complete and accepted on 2026-10-14.
Five inspected sessions cover stock getters, mixed typing, exports, reconnect, and app transitions.
Use the acceptance scope, evidence hashes, and deferred gaps in the authoritative readiness record before choosing further work.

The [recovery decision](keyboards/ck5200/docs/adr/0001-independent-iphone-recovery.md) records NO-GO and the supporting offline audit.
The presumed installer region is not a proven immutable recovery service. A patched-stock reader cannot justify its own installation.
HTTPS inspection is stopped. The stock URL was already known. No phone settings changed, and the vendor inquiry remains unsent.
M2 [application-level rescue](keyboards/ck5200/docs/application-rescue.md) is COMPLETE, accepted by Rudi at `ee45b6bbb2b544f19d44c036099c4af2c5aa7156` after independent review.
Acceptance covers the software-only secondary mechanism, not physical compatibility, iPhone-operated restoration, or recovery from an unbootable application.
M3's three software deliverables are implemented and independently reviewed: frozen QMK candidate, iPhone RAM-keymap profiles, and spare-only bring-up procedure.
See [passing CI, artifact hashes and screenshots](keyboards/ck5200/docs/m3-verification.md), [first-boot evidence](keyboards/ck5200/docs/m3-first-boot.md) and [spare procedure](keyboards/ck5200/docs/m3-spare-bring-up.md).
Stop after these deliverables and independent review. Acquisition and any physical work require a separate decision; R3 remains unmet.

The stock firmware manager is on `main`. Default, StockFirmwareManager and AuthorizedStockUpdate builds keep physical firmware updates disabled.
StockFlashTrial and ExtensionInstaller retain separate image-pinned gates. This publication authorizes no physical operation.
See [stock manager readiness](keyboards/ck5200/docs/stock-manager-readiness.md) for implementation, verification and remaining limits.
Rudi has ruled out buying a second keyboard. No physical experiment or new milestone may start without a separate decision.

- Comprehensive physical characterization remains UNVERIFIED beyond the recorded observations. Baseline collection is closed.
- R3 independent recovery is UNMET. The Mac dispatcher depends on a running application and is not independent rescue.
- No custom image is authorized. No keyboard firmware write, reset, ISP operation, or protection change is authorized for M1.
- Default Debug and Release iOS builds are diagnostics-only at the controller boundary.
- `AuthorizedStockUpdate` and `StockFirmwareManager` block physical updates. StockFlashTrial and ExtensionInstaller use their separate reviewed gates.
- Preserve presumed installer `0x0000–0x1FFF`, application `0x2000–0x89FF`, staging `0x8A00+`, and upper reserved flash.
- Custom CH32X035 keymap storage is transient RAM. No flash-backed persistence is established.

Further recordings require a specific need in a separately scoped task. Do not resume SDI research.

## Current stopping point, 2026-10-23 (post-transfer installation forensics)

- The physical extension check failed. The saved evidence cannot distinguish
  staging/marker failure, incomplete reset, boot-stage rejection/skipped copy,
  or an installed but bypassed/corrupted hook. No physical cause is established.
  Rudi confirmed that normal typing still works. Full remapping remains unavailable.
- A0 was acknowledged at `06:56:11.231726` UTC. The audit has no subsequent
  stream-closure or EA detach/reattach record. Eleven retained stock/checkpoint
  bundles have no events in `06:50–07:40` UTC. This gap cannot establish reset.
- The complete V122 flash-helper trace checks BUSY only. It has no error-flag
  check, post-write comparison, retry, timeout, or flash-success result.
  A2/A3 acknowledgements prove application progress, not flash contents.
  [CORRECTION] A1 initializes the invalid marker in RAM. A3 accepts cursor
  equality OR declared-length equality. This transfer satisfies both equalities.
- Six offline probes pass. Stock, a restored OEM command hook, and an OEM
  redirect each produce the exact physical `FF` reply. The exact uploaded
  image produces `CKX1` in the bounded runner. Skipped initialization fails
  a host BSS guard. An erased extension traps before replying. These selected
  cases do not identify the physical failure or cover all memory corruption.
  A follow-up probe through the original receive adapter at `0x629E` also
  reaches the hook. It returns `CKX1` with the exact image and `FF` with the
  command hook removed. Authentication remains synthetic, not physical proof.
- The retained iOS EAF worker calls A0 at `0x10017B184`, then selects success
  without checking its return. No second install command or reset wait appears
  before continuation return. This static trace does not cover every app route.
  No model-matched boot-stage executable was found in retained packages or
  the bounded public search. Bytes `[0x0000,0x2000)` remain unavailable.
- The unrelated hosted editor failure selected a hidden navigation button.
  The one-line test fix is published at `aa590fa59348b766d6f7ab849dc11937c213d5af`.
  Local receiver/editor checks pass. Hosted run `39932153737` passed both
  `extension-editor` and `verify` at that exact commit. The jobs completed at
  `09:50:43` and `09:59:14` UTC on 2026-10-23. Hosted acceptance covers software
  checks only, not physical extension execution.
  Firmware CI `39923157723` passed at `fa4725a`; iOS `39923157707` passed
  `verify` but failed `extension-editor`. Do not claim that workflow passed.
- Rudi requested publication of all local project changes. The forensic replay
  and documentation form this increment. Retained app packages and raw build/
  evidence artifacts stay local. No app or firmware code changed in the forensic
  work. Original audit and journal hashes still match
  section 18. Preserve `sentRebootPending`. Do not reflash, repeat capability
  reads, change padding, reset/power-cycle for diagnosis, or change the journal.
  No vendor contact, hardware operation, app replacement, or device command ran.
- All six replay cases, 37 existing Python tests, original-output protection,
  readiness, local link paths, and whitespace checks pass. The full Swift suite
  and firmware build were not repeated for this offline tool/document change.

Evidence and reproduction:
[section 19](keyboards/ck5200/docs/stock-extension-feasibility.md#19-post-transfer-installation-forensics-2026-10-23).
The replay is `keyboards/ck5200/tools/post_transfer_forensics.py`.
The fully remappable physical keyboard remains undelivered. R3 remains unmet.
M4 stays closed. No new keymap or UI work belongs to this investigation.

## Historical stopping point, 2026-10-23 (My Clicks and explicit extension installer)

- Rudi requested publication of all local project changes. Contacting Clicks
  is not an option. Continue with offline analysis of retained artifacts.
  This publication request does not authorize another physical keyboard test.
- Publication checks passed: full Swift suite (`publish-swift-20261023.log`),
  37 Python tests using the repository virtual environment, V122 host editor
  workflow (`publish-editor-20261023/`), and enabled-config Simulator workflow
  (`publish-editor-ui-20261023/`). The latter passed all three receiver tests
  and the complete editor UI test, with 14 captures. These paths are under
  `ios/ClicksInspector/build/`. The separate stack-viewer plugin passed 16 tests.
  Readiness, whitespace, pinned-image equality, and staged secret scans passed.
  No new physical verification ran. Hosted CI results remain separate.

- **Installation remains unverified. Stop physical checks and update attempts.**
  `ios/ClicksInspector/build/post-install-capability-evidence-01/` contains two
  user-initiated capability reads. Requests `0c940100547becbf00000000` and
  `0c940100a60a007000000000` both received `080294ff00000000`.
  The running firmware rejects `0x94` with status `ff`. This is a complete
  negative reply, not a framing failure. The app closes the session after
  the failed capability check. No further capability read is needed.
- Offline tracing confirms A0 at `0x3954` sets bit 1 at `0x200000AC`.
  The main loop calls `0x39FA` at `0x238E`. Its reset branch writes
  `0xBEEF0080` to `0xE000E048` at `0x3A30`. No second host install command
  appears in this path or the retained OEM Android updater. The physical
  acknowledgement does not prove execution of the reset or boot-stage copier.
- The captured transfer matches the bundled extension plus 78 `FF` bytes.
  Its command hook at `0x2218` targets `ck_extension_command` at `0x715C`.
  The OEM unknown-command branch constructs the exact captured rejection.
  [INFERENCE] The capability request reached an OEM command path rather than
  the bundled extension handler. This does not establish destination flash
  contents or explain why the hook is inactive.
- The exact missing evidence is device-side staging/marker integrity,
  destination application contents, and boot-stage `[0x0000,0x2000)` behavior.
  In particular, application acceptance of length `0x6A00` does not prove
  boot-stage acceptance. Do not change padding or firmware on that basis.
  **Next: resolve this evidence gap through offline analysis of retained artifacts.
  Do not reflash, reset, power-cycle as a test, clear, or conclude the journal.**
  No physical keyboard operation, app replacement, or publication ran during
  the preceding offline trace. See section 18 for byte checks and evidence hashes.

- Rudi executed the first physical extension update, then reported that the
  capability check failed. Preserve `ios/ClicksInspector/build/post-install-evidence-01/`.
  Its audit has one A1, 848 ordered A2 frames/replies, A3 acknowledgement,
  and A0 acknowledgement. Reconstructed client TX bytes match the pinned
  27,136-byte transfer hash exactly. `analysis.json` records the verification.
  The journal is `sentRebootPending`, with `rebootRequested: true`. This proves
  protocol progress, not boot-stage copying or a running extension.
- The stock-version getter worked after the attempt, as shown in Rudi's
  screenshots. The extension keeps that same version, so it cannot distinguish
  installed extension from unmodified stock. The old app recorded no capability
  reply, and its generic framing error incorrectly implied stock was running.
- Capability checking now hands off the existing EASession and partial frame
  state instead of closing/reopening the session. Exact 0x94 requests/replies
  and framing failures append `extension.*` events to the existing audit.
  A short negative reply reports its status, not a false identity diagnosis.
  Version and capability state are labeled separately. No firmware changed.
- Full Swift checks, genuine V122 protocol integration, and the editor/ownership
  UI workflow passed (`post-install-fix-swift.log`, `post-install-genuine-protocol/`,
  `post-install-fix-ux-02/`). The first UI run caught an asynchronous toggle
  assertion; the test now waits for acknowledgement/readiness before checking.
- The signed diagnostic fix is installed. Snapshots `post-install-evidence-02/`
  and `post-install-evidence-03/` prove the commit journal survived replacement
  byte-for-byte. Before replacing the app, the audit confirmed only one trial
  and no recent update command. No agent keyboard query, reset, or flash ran.
  Rudi completed the requested capability reads. Their rejection and the current
  stop condition are recorded above. Do not repeat the reads or change the journal.

- Rudi reached the confirmation sheet, enabled the acknowledgement, and found
  Flash disabled without a reason. The sheet now carries an immutable session
  item instead of a Boolean plus separate optional session state. Eligibility
  is evaluated live when the view updates and again when Flash is pressed.
  A changed session or failed preflight has a visible explanation. The existing
  controller checks remain independent and unchanged.
- Full Swift checks and the complete editor/ownership UI workflow passed in
  `confirmation-fix-swift.log` and `confirmation-fix-ux/`. Two Simulator-only,
  view-only cases exercise the real ConfirmationView: acknowledgement enables
  Flash when eligible, and a failed check keeps it disabled with a reason.
  These cases have no keyboard link or firmware operation. They do not prove
  the physical installer. Host checks cover matching/stale session items.
- Before reinstalling, read-only retrieval of the current phone audit found
  no update transaction and no commit journal. The signed confirmation-fix
  build is installed and verified on the iPhone. No agent keyboard command,
  app launch, or physical flash ran. Physical confirmation retry is unverified.

- Rudi reported a thermal block on a cool-feeling phone, then an identity
  error: `Expected at most 8 bytes, got 9`. Read-only retrieval of the app's
  existing audit found `010008020300012001` and `000008020300012001`.
  No update-command events or commit journal were found in that capture.
- The receiver wrongly treated stream reads as replies and discarded input
  before a receiver/request was ready. Stock reads, firmware replies, and the
  extension now share one bounded frame parser. It keeps partial recognized
  OEM notifications, rejects unknown/orphan bytes, and never scans for a header.
  Input waits until the session receiver is ready. Version length stays eight.
  [INFERENCE] The observed prefixes are notification tails. Their preceding
  headers were not in the old audit, so physical retry remains unverified.
- Thermal protection is unchanged: serious/critical/unknown iOS states block.
  The screen now names the iOS thermal-pressure state instead of claiming the
  phone surface is hot. Battery and thermal notifications refresh it, and
  preflight records retain those reported states. No temperature is measured.
- The full Swift suite, new stock receiver test, editor/ownership workflow,
  signed iPhone build, and readiness check passed for the framing fix.
  Evidence: `stream-framing-fix-swift.log` and `stream-framing-fix-ux/` under
  `ios/ClicksInspector/build/`. Tests reproduce both saved nine-byte prefixes
  after a known notification header and still reject orphan bytes. No new
  physical keyboard query or flash was executed by the agent.
  The signed framing-fix build was reinstalled and verified on the iPhone.
  Its firmware resource and extension-only compilation flag remain unchanged.

- Rudi requested the enabled extension installer, stronger reliability checks,
  and an explicit weak-evidence list. `ExtensionInstaller` is a separate app
  configuration for only the pinned stock-derived extension. Default Debug
  stays write-disabled. StockFlashTrial remains stock-only and QMK flashing
  stays closed. No agent-executed physical flash is authorized or performed.
- Connection now links to Install programmable extension. It validates the
  bundled executable, adds deterministic erased-state padding, binds consent
  to the live session, requires fresh V122 identity before A1, checks phone
  power/thermal state, and reuses the existing audited updater and journal.
  Source hash is unchanged. Transfer is 27,136 bytes, SHA-256
  `438e7597b069e19ef8307e51e34ba9ab4cd07c0a7256c84713cc321f05a78aa2`.
- The enabled-build Simulator editor/ownership workflow passed in
  `ios/ClicksInspector/build/extension-installer-ux-02/`. The installer image
  loads and the install control stays disabled offline. The exact padded image
  also passed 15 genuine V122 runtime cases with unchanged HID output/coverage.
  Boot-stage copying, staging integrity readback, full USB/startup, and recovery
  remain unproven. Do not describe physical anti-brick confidence as very high.
- The full Swift suite passed (`extension-installer-swift-verified-02.log`),
  including default/M4 boundaries, the new extension guards, all existing
  stock-manager cases, and ten full-area extension staging/fault cases through
  production C. The extension cases do not invoke the stock-only copy model
  and do not prove the OEM installer. All 37 Python tests and readiness checks pass.
  A held-staging regression confirms a duplicate install request cannot fail
  or cancel the active exchange. The final signed phone build includes this fix.
- The signed `ExtensionInstaller` app is now installed on the iPhone 15 Pro Max
  `00008130-001E74A11193803A`, bundle `com.rudironsoni.clicks-inspector`,
  version `0.2.0`, build `4`. The bundled image/signature and compilation flags
  were checked. Only `EXTENSION_INSTALLER` is enabled, not M4/stock/QMK/test
  write flags. No agent app launch, keyboard command, or physical flash ran.
  The original installation procedure is historical. Do not repeat it during
  the current investigation. The preserved journal blocks another attempt.

- Review of `cf99619` is correct: the dedicated editor CI job failed before
  Simulator verification because it did not build the extension image.
  In iOS run `39858956521`, `verify` passed and `extension-editor` failed.
  The workflow as a whole failed. No hosted editor acceptance is claimed.
- The local workflow now builds the extension before assurance. A clean
  output-directory run passes all 20 V122 cases with the accepted image hash.
- The app opens into My Clicks. Diagnostics, logs, firmware tools, and QMK
  controls moved under Advanced. Stock firmware does not expose QMK controls.
  Save and Apply remain visible. Draft storage and RAM synchronization are
  separate states. Offline drafts persist across app launches.
- The editor uses QWERTY cap geometry from the vendor photo, readable action
  names, a searchable category picker, native Dynamic Type, and VoiceOver
  labels. Raw HID input is under Advanced. Four cap-to-matrix assignments
  remain unverified and are not silently guessed. Their decoded switches
  remain editable with that warning.
- RAM consent is no longer a firmware-recovery boolean. Eligible stock
  opening automatically reads the authorized version. Capability reads
  require explicit confirmation. Apply requires verified CKX1 identity and
  per-session RAM consent. Loss/disconnect revokes consent. Firmware flash
  gates remain unchanged. No physical keyboard operation ran.
- The complete redesigned UI workflow passed in `my-clicks-ux-06/` and all
  11 screenshots were inspected. Disabled Apply is readable in light/dark
  mode. Maximum accessibility sizes use a key list and a compact action bar.
  Recorder, stock-manager, and QMK UI regressions passed under Advanced.
  Recorder source bytes exactly match all three UI captures. No publication occurred.
- Rudi authorized app installation on 2026-10-23. Before the installer request, a signed Debug
  app was installed on the iPhone 15 Pro Max `00008130-001E74A11193803A`.
  `devicectl` confirms `com.rudironsoni.clicks-inspector`, version `0.2.0`,
  build `4`. The app still appears as Clicks Inspector and opens into My Clicks.
  Firmware-write and QMK-only compilation flags are absent. No app launch,
  keyboard session, settings command, or firmware write ran during installation.
  Installation is separate from the completed Simulator screenshot/UI checks.

See [section 18](keyboards/ck5200/docs/stock-extension-feasibility.md#18-first-user-run-extension-update-and-capability-check).
R3, full USB assurance, and keyboard-flash persistence remain open.

## Historical stopping point, 2026-10-22 (live editor software integration)

Rudi accepted `1cbb07ac5804c158ed39c4fc347d9f82dcba014b` as the offline
RAM-configuration milestone. Firmware CI `39846660539` and iOS CI
`39846660777` both passed. The earlier pending-iOS observation is superseded.
Rudi authorized committing and pushing the editor increment on 2026-10-22.
Hosted verification for this increment is pending. Physical commands stay blocked.

- `StockExtensionSession` now uses async transfer without a second codec.
  A complete handshake/read/apply operation rejects concurrent calls.
  `StockExtensionTransport` assembles bounded frames over `KeyboardPacketLink`,
  tolerates OEM notifications, and closes on timeout, cancellation, or loss.
  `AccessoryInspector` routes the existing streams to one session owner.
- The three-layer screen edits all 108 positions, key usages, shortcuts,
  modifiers, OEM/disabled/transparent actions, independent native modes,
  and hold/toggle/one-shot/tap-hold activation. Descriptions match the
  existing engine, including immediate tap-hold activation and Base fallback.
- Typed named profiles use `ExtensionProfiles.json`, separate from QMK.
  Unknown/corrupt files are preserved. Loading changes only the draft.
  Reconnect reads RAM and never applies a profile.
- Simulator transport uses a loopback HTTP bridge to the genuine V122
  runner. The bridge contains no mapping or HID implementation. Link loss
  keeps the emulated RAM. Power loss restarts the actual emulated machine.
  The bridge checks the image and source hashes against passing assurance.
- The visible Simulator workflow passed edit/apply/readback, exact NAV+Q
  Esc and SYM+E Tab reports, profile save/relaunch, RAM loss with manual
  reapply, and recovery after a lost APPLY reply. Ownership and background
  cancellation checks pass. The management guard checks in-flight work,
  not only its phase label. QMK simulator connections also claim ownership.
- Final local checks pass: the Swift suite, 37 Python tests, native tests,
  20 V122 cases with 299 layer checks, async Swift-to-V122 integration,
  the host editor workflow, editor/ownership Simulator tests, the existing
  QMK profile UI workflow, and an unsigned Debug iPhone build.
  Seven editor screenshots and raw traces are retained under
  `ios/ClicksInspector/build/extension-editor-verified/`.
- Hosted checks for this editor increment are pending.
  The separate exact-QMK binary assurance run was not repeated. Its pinned
  source gate is unchanged. The local QMK build is an older documented
  artifact, not a replacement for the accepted assurance image.
- Physical extension commands remain blocked before EASession creation
  and at the editor controller boundary. Firmware code, image capacity,
  the full 978-byte record bound, and the exact-QMK hash gate are unchanged.

Run instructions and evidence scope:
[section 15](keyboards/ck5200/docs/stock-extension-feasibility.md#15-live-editor-software-integration).
Full USB lifecycle assurance and keyboard-flash persistence remain separate
open gates. R3 is unmet, M4 stays closed, and the vendor inquiry stays unsent.
No physical keyboard operation occurred.

## Historical stopping point, 2026-10-22 (offline RAM configuration and USB safety)

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
[stock-extension-feasibility.md section 11](keyboards/ck5200/docs/stock-extension-feasibility.md#11-accepted-bounded-execution-baseline-57fef66).

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
