# CK-5200 authoritative readiness record

This file owns the current readiness and authorization status. Other guides must defer to it.

| Gate | Current status |
| --- | --- |
| M1 stock recorder software | Host/simulator checks and unsigned iPhone CI passed at `ef3a5b7`; signed diagnostics app installed and launched over Wi-Fi |
| M1 stock diagnostics baseline | COMPLETE, accepted for the observed identity, stock getters, mixed-key recording, exports, and session reopening |
| Comprehensive physical CK-5200 characterization | UNVERIFIED beyond the recorded observations; remaining gaps do not block the diagnostics baseline |
| M2 application-level rescue | COMPLETE, accepted at `ee45b6b` after independent review as a software-only secondary mechanism. Not installed or physically verified. See [acceptance and evidence](application-rescue.md#acceptance). Does not satisfy R3. |
| R3 iPhone-only independent recovery | UNMET. No proven rescue from an unbootable application using the iPhone alone without added hardware. [Current decision frontier](recovery-research.md#installation-decision-frontier-2026-10-18). |
| Custom firmware flashing | NO-GO, no custom image authorized |
| M1 stock firmware writes and resets | Disabled; not authorized for this stage |
| Default iOS Debug and Release | Diagnostics-only; controller permits only exact demonstrated stock getters on the known identity |
| StockFirmwareManager / AuthorizedStockUpdate configurations | Physical writes disabled. Exact identity query and offline official-image preparation only. |
| M3 verified stock firmware manager | COMPLETE/CLOSED for software scope in the R3 investigation. Software checks and independent review complete at `a7cf3f7`. [Delivery, app downloads and hashes](stock-manager-readiness.md). Stock-to-stock and QM/RS-to-stock are host workflows, not physical recovery claims. |

The goals remain official stock installation, separately authorized custom transitions, independent iPhone-only recovery,
preservation of the only keyboard, and reuse of maintained dependencies.
M1, M2, and M3 are closed within their recorded scopes. The current priority is evidence-first R3 research and offline modeling.
The completed QMK/keymap work stays on its separate unmerged PRs.
Rudi has ruled out buying another keyboard. No spare purchase or physical installation is part of this milestone.

The custom target is CH32X035. Custom keymap storage is transient RAM and loses edits on power loss.
No flash-backed EEPROM partition is demonstrated. Preserve presumed installer `0x0000–0x1FFF`, application `0x2000–0x89FF`,
staging `0x8A00+`, and upper reserved flash.

An acknowledged transfer, an acknowledged commit, a session disconnect, functional typing, and verified installation
are different observations. None proves independent recovery. A same-version reply cannot prove that installation occurred.
The Mac dispatcher depends on an executable application. It is not a recovery backup for an unbootable image.

The [2026-10-14 recovery audit](recovery-audit-2026-10-14.md) rechecked the application binaries, updater paths, and host restore harness.
The [2026-10-15 decision](adr/0001-independent-iphone-recovery.md) retains NO-GO and rejects patched-stock installation as circular evidence acquisition.
The missing evidence is an independent boot service with iPhone transport, validated stock restoration, preservation, and interruption safety.
Installer readback could help but would not satisfy these requirements by itself.
See [iPhone transport limits](iphone-recovery-transport.md) for the separate public-API constraint.

## Installation feasibility, 2026-10-18

The completed runtime and binary assurance does not establish installation
readiness. The [current decision frontier](recovery-research.md#installation-decision-frontier-2026-10-18)
ranks the missing OEM entry, iPhone transport, and preservation evidence.
The exact reviewed QMK image places instructions in `[0x68D8,0x6900)`, whose
original contents and OEM ownership remain unknown. The earlier protected
classification was a conservative model policy, not a recovered OEM reservation.
See the [2026-10-19 correction](installation-feasibility-2026-10-19.md).
Installer acceptance and recovery still require their own evidence.
External adapters and replacement-loader installation do not meet the current
constraints. R3 remains UNMET. Physical flashing remains NO-GO.

## R3 investigation status, 2026-10-16

**2026-10-17 follow-up:** outcome 2, exact vendor dependency. No preservation-safe factory-ISP sequence is established.
The [focused report](../../../reports/CK5200%20USB%20recovery%20feasibility.md#2026-10-17-follow-up-outcome-2-exact-vendor-dependency) separates PROGRAM addressing, selective ERASE, and the unknown destination tail.
The technical inquiry is ready but unsent. This evidence update changes no authorization.

The [USB recovery feasibility report](../../../reports/CK5200%20USB%20recovery%20feasibility.md) extends the existing ADR with a conceptual BLE-controlled USB host adapter.
The adapter could avoid the direct iPhone raw-USB API limit. It does not resolve keyboard entry or preservation.
PC17-high power-up entry is documented at chip level. Connector routing, VDD timing, loader identity, and protection remain unverified.
System FLASH is mutable, not immutable ROM. The inspected client programs from zero, and no safe selective factory erase is established.
The stock image's final page has 40 bytes absent from the V122 file. Their contents are unknown.
The earlier requirement to preserve them was a conservative model policy, not a demonstrated OEM reservation.
The [2026-10-19 ownership assessment](installation-feasibility-2026-10-19.md) corrects that distinction using stock startup instructions, official image growth, and the A1 capacity check.
Full interruption recovery remains unproven.

**R3 remains UNMET. Custom flashing remains NO-GO.** This file remains the sole readiness authority.
Current work permits research and offline models only. No physical experiment, device command, reboot, probe, opening, ISP, unlock, purchase, or hardware build is authorized.
The next evidence step is model-matched documentation. The vendor query remains unsent.
The [offline lab](../recovery_lab/README.md) passed 10 unit tests on local rerun. Its default CLI returned expected exit 2 (blocked), and its hypothetical CLI returned 0.
The hypothetical result requires a stronger tail-preserving erase contract and the original in-memory journal. No such factory operation or durable journal is implemented.
The model provides no OEM normal-boot or physical recovery proof. A3/A7 protocols are not implemented.
Both scoped independent agent reviews are closed. The final ISP/lab review closed all four findings with no actionable blockers.
The electrical review found no P1/P2 blockers for conceptual scope. [Review summary and verification limits](../../../research_notes/CK5200%20USB%20recovery%20feasibility/review.md).
This closes review of the investigation, not R3 or physical recovery authorization.

The separate [binary-execution assurance work](firmware-assurance.md) evaluates direct application startup in a specified software model.
It cannot establish OEM boot handoff, physical USB behavior, or independent recovery. It changes none of the authorization gates above.

Baseline collection is closed. Use the five recorded sessions as the stock diagnostics reference.
Further physical characterization requires a specific need in a separately scoped task, not completion of every catalog trial.
No firmware writes, resets, ISP operations, protection changes, or SDI investigation belong to this task.

## Baseline acceptance, 2026-10-14

Rudi approved closing the diagnostics baseline with the documented observation gaps.
Acceptance covers signed app delivery, known accessory identity, 84 validated getter reads across two sessions,
mixed-key callbacks and applied text, inspected exports, and successful reads after reconnect and app transitions.
All 12 replies for each getter match across the two getter sessions.
This acceptance applies to the tested stock CK-5200, firmware 1.2.2 and hardware 1.2.0, with ClicksInspector 0.2.0 (4).
It does not establish complete keyboard behavior, custom firmware compatibility, or independent recovery.
The unverified observations below are deferred. No further recording is required to close this baseline.

## Physical observations, 2026-10-14

Repository reference during deployment: `ef3a5b78bf43f30d17135a4339dd06c05e31c257`.
iOS 27.0.1, ClicksInspector 0.2.0 (4), diagnostics-only banner confirmed on the phone.
Code-signature verification, Wi-Fi installation, and app launch succeeded.
Accessory identity: CK-5200, firmware 1.2.2, hardware 1.2.0, protocol `com.clickscompanion.protocol`.

The first four exports were retrieved directly from the app over Wi-Fi and inspected.
Rudi supplied the fifth export in `tmp/` after macOS blocked access to Downloads.
Original exports remain separate from this summary. Derived observations do not change their trial outcomes.

| Session UUID | Observation | JSON SHA-256 |
| --- | --- | --- |
| `EE1FEF37-4BCC-4990-8540-6C2BD117536E` | Redacted getter baseline, 21/21 validated reads, three identical replies per command | `47c6dccb09b325d088c3f03ad8eccb59613b4974e5dc74f3cebc6240ac569172` |
| `DD9B979F-FAB0-48E1-881D-DD4FED01285C` | Test-text export, three Q and three Backspace press/release cycles in both APIs; three applied `q` insertions | `fb54726b0e6d13071425a37807c796750fc003cd5aba5b76e410901bc5447ac4` |
| `6CC2D0BE-0281-4F62-AFDF-8F08C1CD39BA` | Redacted mixed-key recording, 35 UIKit cycles and 33 GameController cycles | `2140900952e816db0f2aa3338dc976dddd16af8be149553b1560c0980d418e77` |
| `C779F358-EE76-429D-8635-A83B3C9AB6C1` | Test-text mixed recording, digits, symbols, arrows, uppercase text, and an app-inactive gap | `4132aa27187e4fbaa762d52320f70ca281df7b0905767cc560b2b32d17c08983` |
| `FC7B9890-F897-4781-9CE9-373E8314371B` | Redacted lifecycle recording, 63/63 valid reads across reconnect and app background/foreground transitions | `64bf7d873befd717a1f260b2e5e34cad3a49af3310e54061fd1352916277b01b` |

Getter replies were independently checked for request, length, type, command, status, and exact version payload.
Response latency was 1.066–19.380 ms, median 18.304 ms, including iOS scheduling.

| Getter | Observed payload in all three passes |
| --- | --- |
| Version `03` | `01 20 01 22` |
| Brightness `84` | `ff` |
| Backlight delay `86` | `4e 20` (20,000 ms according to the decoded getter) |
| Battery idle interval `88` | `00 b4` (raw 180; units not established here) |
| Raw setting `8a` | `00` |
| Raw setting `8c` | `21` |
| Status flags `8d` | `00` |

In the mixed-key session, both APIs recorded all 26 letter codes, Return, Backspace,
left Shift, left Control, left Option/Alt, and left Command/GUI. X occurred twice.
UIKit event 58 records Z (code 29) with left Shift (code 225) held and modifier flag 131072 (`0x20000`).
This establishes an observed Shift+Z combination; the redacted export does not establish its inserted character.
Two additional complete UIKit-only cycles used code 430 (`0x01ae`); the physical key is unidentified.
All 33 shared cycles matched by code and per-code order. No unmatched releases, duplicate downs,
unreleased keys, or observation gaps occurred between the first down and final up.
The key window was 10,187.036–25,441.053 ms after session start; focus loss occurred afterward at 26,383.528 ms.
The exported trial remains ambiguous and labeled `r0c0/single`; this mixed-key observation does not complete that trial or assign every event to r0c0.

The fourth session ran from 19:14:28 to 19:15:58 UTC and contains 1,871 events.
It records 223 UIKit downs and 223 ups, plus 221 GameController downs and 221 ups.
After splitting at observation gaps, 220 complete cycles match across APIs by code and per-code order.
The largest difference between matched callback times is 73.922 ms.
Two additional complete cycles use UIKit-only code 430. Its physical key remains unidentified.

Both APIs record all letter codes, digit codes 30–39, all four arrow codes 79–82,
Space, Return, Backspace, punctuation, and the four left modifier codes 224–227.
Applied text events contain all 26 uppercase letters, all ten digits, and symbols.
These observations establish iOS output. They do not identify the physical layer control or electrical matrix positions.
The key events span 10,539.954–79,050.455 ms after session start, approximately 68.5 seconds of mixed input.
Shift remains held for approximately 3.6 seconds in one interval.
No ordinary key remains held longer than 280 ms in either API, and no repeated down occurs without a release.
The session therefore establishes sustained mixed typing, but not held-key repeat.

Event 1311 records the app becoming inactive at 62,421.357 ms.
Event 1312 records a gap with Command (code 227) held in both APIs.
Its later releases, events 1315 and 1316, follow the gap and carry `releaseWithoutObservedDown`.
Exclude that interval from uninterrupted cycle counts. The export does not establish what caused the app to become inactive.
The active trial remains labeled `r0c0/single` and ambiguous. Analyse this recording as mixed input, not as one physical-key trial.
There are no getter reads in this session, so it cannot establish settings stability after the interruption.

The fifth session ran from 19:20:40 to 19:21:41 UTC and contains 337 events.
Independent checks confirm 63 valid requests and exact replies, in three completed sweeps of 21 reads each.
Each getter has nine identical replies, also identical to the first baseline.
Response latency is 0.798–19.219 ms, with a median of 16.793 ms.

The first sweep ends at event 109, before detach at event 120 and attach at event 122.
Detach and attach occur at 22,514 ms and 25,930 ms after session start.
The session was already closed at detach, so this does not test interruption of an open stream or pending read.
After reconnect, a background/foreground transition and session reopening precede the second sweep, ending at event 231.
A further background transition at event 234 closes the session at event 236.
Foreground event 240 retains a closed session. Event 247 records reopening before the final sweep, ending at event 332.
Thus getter responses remain stable across the observed reconnect and app transitions, with explicit session reopening.
The 20 key-observation gaps contain no unresolved presses. There are no key events or trial attempts in this session.
Backlight and charging remain `not-observed`.

Deferred, outside the accepted baseline: physical cap-to-matrix correspondence, unidentified special keys, physical layer-control attribution,
unobserved modifier combinations, held-key repeat, detach during an open session or pending read,
idle/wake, and observed backlight/charging conditions.
Backlight and charging fields in the collected baseline were `not-observed`.
The test-text Q observations establish iOS output, not the electrical matrix location.
