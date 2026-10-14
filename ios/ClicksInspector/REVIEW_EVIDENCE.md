# M1 review evidence

This is a code and evidence index. Physical CK-5200 acceptance remains **UNVERIFIED**.
The original implementation is commit [`d2f679d`](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/commit/d2f679d0279d6d2507f00cd2233fbdc10e183b02).
Its parent is `cab182e3bb801ce315009fbb6f4f9dc09678534b`.
It was local-only when first reported. It is now published without rewriting history.

## Reproduce and inspect complete outputs

The [iOS workflow](../../.github/workflows/ios.yml) runs on pushes and pull requests that change `ios/ClicksInspector/**` or the workflow.
It runs on `macos-15`, records the checkout SHA and toolchain, reproduces the version/session/diagnostics failures against `8bde397`, runs the original six programs plus new regressions and the checkpoint benchmark, builds unsigned simulator and generic iPhone apps, and executes receiver and UI integration tests.
Its `ios-verification-<SHA>` artifact contains full stdout/stderr and the UI `.xcresult`, including warnings, commands, and test attachments, even after failure.
Signing, physical-device access, and firmware operations are not CI steps.
`Tests/reproduce_red.py` retains complete expected-failure output. `StockReceiverTests` verifies that the mismatched version stops the real receive path without any validated observation. The UI test reads actual exported files after UIKit typing, in both privacy modes, and checks replacement/deletion.
`RecorderBenchmark` prints event counts, peak process RSS, synchronous checkpoint duration, and enqueue-plus-next-callback duration. These are host measurements, not iPhone callback timing evidence. Production checkpoints use the serial background queue immediately on start and then every five seconds.

```sh
gh run list --workflow ios.yml --limit 5
# Substitute the run ID for the commit under review.
gh run view RUN_ID --log
gh run download RUN_ID --dir ios/ClicksInspector/build/verification
```

Local reproduction, from the repository root:

```sh
mkdir -p ios/ClicksInspector/build/verification
set -o pipefail
bash ios/ClicksInspector/test.sh 2>&1 | tee ios/ClicksInspector/build/verification/swift-tests.log
xcodegen generate --spec ios/ClicksInspector/project.yml
xcodebuild -project ios/ClicksInspector/ClicksInspector.xcodeproj \
  -scheme ClicksInspector -configuration Debug \
  -destination 'generic/platform=iOS Simulator' CODE_SIGNING_ALLOWED=NO build \
  2>&1 | tee ios/ClicksInspector/build/verification/simulator-build.log
```

The existing build-directory ignore rule excludes these local outputs. These logs can include local paths.
`test.sh` labels every executable: `StockReadCommandTests`, `StockEvidenceTests`, `ck5200_protocol_check`, `ck5200_layout_check`, `ck5200_stream_check`, `ck5200_flash_check`.
Flash tests use scripted links. They do not open an accessory or write the physical keyboard.
The separate simulator UI test exercises recording, text input, export, and deletion. Its command is in [the baseline guide](STOCK_CHARACTERIZATION.md#build-and-run).

## Production paths

| Concern | Code to review | Evidence and limit |
| --- | --- | --- |
| Exact packet authorization | `StockReadCommand.swift`: `StockReadPolicy.allows` and `demonstrated` | Requires one of seven exact packets plus model CK-5200, firmware 1.2.2, hardware 1.2.0. Host tests reject every other two-byte opcode, appended arguments, wrong revisions, and both historical probes. |
| Transport enforcement | `AccessoryInspector.swift`: `canReadSelectedCommand` → `performStockRead` → `writePendingReadIfPossible` | Both manual reads and sweeps enter the same gate. Requires live matching EA connection, open streams, no pending read, and no firmware-control session. Only the checked enum request reaches `OutputStream.write`. |
| Firmware separation | `openSession`, `attachCustomFirmwareLink`, `startStockRecording` | Default sessions never attach the firmware controller. Recording blocks firmware-control sessions. Starting recording closes an existing firmware-control session. Busy firmware operations block recording. |
| Repeated reads | `readStockBaseline`, `nextStockRead`, `scheduleNextStockRead` | Explicit user request queues exactly three passes. A 250 ms gap separates requests. Errors cancel the remaining queue. Reconnect never resumes it. |
| Fragments and timing | `handleStream`, `writePendingReadIfPossible`, `receiveReadFragment`; `StockReadAccumulator` | Records received and accepted-write fragments, read IDs, parser results, and monotonic timing. Host checks use the production accumulator and recorder, not physical EA streams. |
| Timeouts and detach | `failRead`, `closeSession`, EA notification handler; recorder `finishRead` | Five-second read deadline, cancelled queue, failed/interrupted result, before/after state, partial local bytes. Share output keeps outcome categories and normalized reason codes. |
| Keys | `StockCharacterizationView.swift`: `StockCaptureTextView` | Native GCKeyboard callback, UIPress down/up/changed/cancelled, UIKey codes/modifiers, text delegate proposed/applied edits. Focus and lifecycle gaps retain unresolved key lists. |
| Export | `StockEvidencePrivacy.swift`: `project`; recorder `encoded`, `report`, `export` | JSON and Markdown use the same explicit share projection. The projection revalidates saved replies and checks fragment concatenation. A forged `validated` outcome or read ID cannot authorize arbitrary bytes. |
| Clear | `AccessoryInspector.clearStockEvidence`, recorder `clear`; view `textResetID` | Stop first, confirm deletion, remove stock checkpoint and named exports, clear memory/text/log display. No later checkpoint recreates data. Unrelated files and durable unresolved-update state remain intact. |

## Field-by-field export audit

Schema is now 2. `privacy` identifies the `shareable-v2` field filter. Schema 1 checkpoints can still be recovered.
Schema 2 adds allowlisted catalog trial IDs, numeric attempt IDs, typed trial outcomes, and coverage regenerated from filtered events.
Catalog labels, actions, matrix coordinates, and evidence classifications come from static code, never arbitrary saved strings.
This is a filtered diagnostic export, **not anonymous data**. Wall times and key codes are deliberately retained and disclosed before sharing.

| Surface | Local checkpoint | Shareable JSON / Markdown |
| --- | --- | --- |
| Session UUID and filenames | Random session UUID | Retained. Filenames contain only `stock-<UUID>-redacted` or `-test-text`, with `.json`/`.md`. No accessory name, serial, date, path, or typed text. |
| Wall-clock dates | Actual UTC capture dates | Retained. They can reveal when the session occurred. Relative elapsed times remain available. |
| Sequence, read IDs, durations, lengths, counts | Actual numeric observations | Retained. These are diagnostics, not device identifiers. Numeric event fields must parse as finite nonnegative numbers. |
| Model, firmware, hardware, protocol names | Exact EA values, without serial/connection-ID metadata | Only known CK-5200 identity values and `com.clickscompanion.protocol` survive. Unrecognized strings become `omitted`. No name/manufacturer/phone-name fields are collected. |
| iOS/app versions and build | App-controlled strings | Only short numeric version syntax survives. Only the fixed `source` descriptions survive. Unknown environment keys disappear. |
| Condition, charger, backlight, app/session/focus | Exact observation state | Values must match the UI's fixed choices. Unknown strings become `omitted`. Before/after states use the same filter. |
| Commands and requests | Exact captured bytes | Only demonstrated enum commands with matching fixed requests survive. Other read records are excluded. |
| Responses | Exact bytes, including failed/partial replies | Only replies that pass the production parser again survive. A saved `validated` string is not sufficient. |
| TX/RX event bytes | Exact fragments, including unsolicited input | Only request bytes that match the permitted request, complete TX concatenations matching that request, and RX concatenations matching a revalidated reply survive. Other payloads lose bytes but retain counts/timing. |
| Errors, `reason`, `outcome`, `endReason` | Raw application error strings | Fixed outcome categories and reason codes only. No arbitrary error message, path, domain description, or free-form suffix. Unknown details become `omitted` or `detailsOmitted`. |
| Source, kind, key trial labels | Raw application labels | Fixed vocabulary only. Unknown source/kind/label strings become `omitted`. |
| Typed `text` and `characters` | Bounded exact test text | Omitted by default, lengths retained. Explicit opt-in includes up to 256 characters. Markdown always omits text. |
| Key codes/modifiers and held-key lists | Native API values | Retained after numeric/list validation. These can reconstruct typed text even when character strings are omitted. Never use personal text. |
| API uptime | Native callback timestamp | Omitted. Export uses per-session elapsed times and disclosed wall times instead. |
| Unknown/future fields and arbitrary payloads | Retained if recorded | Fail closed. No pass-through dictionary or generic payload field. |
| Markdown | No separate raw report | Built from the same filtered bundle. No raw `environment`, `endReason`, error string, or unvalidated reply can bypass JSON filtering. |

`StockEvidenceTests.auditsEveryExportSurfaceAndClearsData` plants a sentinel in environment values, state, identities, protocols, errors, labels, unknown fields, and raw payloads.
It recovers a real checkpoint, exports both text modes, and checks both JSON and Markdown.
It tests a malformed saved response marked `validated`, extra fragments attached to a valid read ID, explicit text opt-in, retained wall times, deletion failure, successful deletion, and no checkpoint resurrection.
The other negative check exhausts the local opcode policy and tests interrupted capture and the event limit.

## What runs without another tap?

- At launch: register EA notifications, enumerate connected accessories, and recover the app's local checkpoint. No stock command or EA session opens.
- After **Start characterization**: enqueue the new session checkpoint immediately, then every five seconds. Encoding and atomic writing use a serial utility queue. Native keyboard capture requires the test box to have focus.
- After **Open stock session**: iOS establishes the EA session and handles its own USB/authentication traffic. The app sends no getter yet.
- After **Read seven getters, three passes**: the authorized 21-request sequence runs until completion or failure. This automation is bounded by that user action.
- On background/detach: close the session, cancel queued reads, record the interruption, and checkpoint. Returning only refreshes accessory discovery.
- After export: files stay local. **Share evidence files** opens the system share sheet. No automatic upload exists in this app. System backup behavior is controlled by iOS.
- **Open firmware-control session** is a separate explicit action. Default builds still enforce diagnostics-only commands at the controller boundary. `AuthorizedStockUpdate` is a later, separately authorized configuration, not an M1 build.

## Data removal and its limits

Use **Stop and save**, then **Clear captured text and diagnostics**, then **Delete local stock evidence**.
Deletion errors stay visible. The app does not claim success if filesystem deletion fails.
This removes the stock recorder's data, including exports from earlier sessions, and resets the text view.
It does not remove copies already shared to Files, another app, a recipient, backups, or iOS unified logs.
Unified session logs do not contain the text recorder's characters. They can contain transient connection IDs and getter TX/RX bytes.
The firmware controller's unresolved-update record is safety state, not disposable diagnostic history, and is never cleared by this button.

## Physical acceptance

Use [the development-signing procedure](SIGNING_AND_BASELINE.md), then the stock baseline.
Store physical evidence separately from host/simulator logs. No physical result is inferred from this code review or CI.
