# Stock Characterization, M1

This mode collects stock evidence without firmware writes, resets, configuration writes, or unknown-command probes.
It uses Apple's `EASession`, `GCKeyboard`, `UIPress`/`UIKey`, and a focused `UITextView`.
The iOS app does not implement iAP2 or MFi authentication.

## Build and run

From the repository root, with full Xcode selected:

```sh
xcodegen generate --spec ios/ClicksInspector/project.yml
bash ios/ClicksInspector/test.sh
xcodebuild -project ios/ClicksInspector/ClicksInspector.xcodeproj \
  -scheme ClicksInspector -configuration Debug \
  -destination 'generic/platform=iOS' CODE_SIGNING_ALLOWED=NO build
xcodebuild -project ios/ClicksInspector/ClicksInspector.xcodeproj \
  -scheme ClicksInspector -configuration Debug \
  -destination 'generic/platform=iOS Simulator' CODE_SIGNING_ALLOWED=NO build
open ios/ClicksInspector/ClicksInspector.xcodeproj
```

For the simulator UI test, choose an available device from `xcrun simctl list devices available`:

```sh
xcodebuild -project ios/ClicksInspector/ClicksInspector.xcodeproj \
  -scheme ClicksInspector -configuration Debug \
  -destination 'platform=iOS Simulator,name=iPhone 17 Pro' \
  -parallel-testing-enabled NO CODE_SIGNING_ALLOWED=NO test
```

The UI test starts recording, checks that reads stay disabled without an accessory, types test text, stops, and creates shareable files.
It does not send the files or simulate physical keyboard compatibility.

For a physical iPhone, choose a signing team locally and run the `ClicksInspector` scheme.
The unsigned build cannot install on an iPhone. The simulator cannot open this physical accessory.
Machine-specific Xcode wrappers and storage locations belong in your shell, not this project.

## Collect the baseline

1. Keep the original keyboard firmware installed. Close the official Clicks app.
2. Disconnect other keyboards. Unlock the iPhone and open Clicks Inspector.
3. Open **Stock characterization and key recorder**. Tap **Start characterization** before attaching the case when possible.
4. Attach the case. Tap **Refresh accessories** if needed.
5. Check model `CK-5200`, firmware `1.2.2`, hardware `1.2.0`, and the advertised protocol names.
6. Tap **Open stock session**. Both streams must open before reads become available.
7. Select `baseline`. Mark the visible backlight and charger state. Tap **Mark state before observation**.
8. Tap **Read seven getters, three passes**. Wait for zero reads left and `Completed`.
9. Tap **Mark state after observation**. Review any changed replies in the report after stopping.
10. For a failure, preserve the recording. Do not substitute opcodes or arguments. A read failure closes the session and cancels the sequence.

Each pass sends `03`, `84`, `86`, `88`, `8a`, `8c`, `8d` in order, with a 250 ms gap between requests.
The five-second request deadline includes output backpressure. There is no automatic retry.
A version payload other than `01 20 01 22` stops the sequence.
Opening or reconnecting a stock session sends no command until you request a read.
Historical `08` and unsupported `8f` evidence remains in the [command inventory](STOCK_COMMAND_INVENTORY.md).

## Record every physical key

1. Select `each physical key` as the condition. In **Trial**, choose the matrix candidate and `single` action. Read its label-evidence classification; do not guess unknown caps.
2. Tap **Begin key trial**. Tap the test-text box to give it focus.
3. Press and release that key once. Check the visible callback lines and character output.
4. Release all keys. End the active trial with completed, failed, ambiguous, or unobservable. Repeat for each identified cap and relevant action. Text alone or an incomplete down/up sequence becomes ambiguous, not a passed physical key.
5. Repeat with the catalog's `shift`, `sym`, `command`, `control`, and `cursor` actions. If a mode's stock activation is unknown, end that trial as unobservable. Do not substitute custom QMK activation rules.
6. Run the `consumerChord` trial only after both caps have been identified. Additional two-, three-, and four-key experiments can be recorded under `simultaneous keys` outside an active trial; these unassigned observations do not complete catalog coverage.
7. Select a `hold` trial. Hold the identified key for two seconds, then release it.
8. Try rapid taps and sustained test typing outside an active trial. Use no personal text or passwords.
9. For a system-consumed shortcut or unknown cap, tap **End trial: unobservable**. This records your observation, not a firmware diagnosis.

The recorder retains down/up/cancelled/changed callbacks, API key codes, available modifiers, character edits, selection changes, and callback times.
It tracks held keys separately for GCKeyboard and UIKit. It does not combine two API observations into one fabricated HID event.
Repeated down callbacks carry `repeatedDownWithoutRelease`. Text repetition can occur without repeated key-down callbacks.
Observed hold duration is an iOS callback interval, not a switch measurement.
Focus loss, backgrounding, and detach produce observation gaps with unresolved presses. They do not synthesize releases.
`GCKeyboard.coalesced` does not prove which physical keyboard produced an event.
Use the user-marked trial to associate a physical key with the callbacks.

The text box limits visible text to 8192 UTF-16 units. Delete test text when it fills.
Each text edit stores up to 256 units or characters, with explicit full length and truncation fields.
Proposed edits and applied edits are separate records. UIKit may handle some keys before the view receives `UIPress`.

## Safe operating-state procedure

For each condition, mark state before and after. Run a getter pass on each side when a session is available.
Marks describe visible conditions. They do not change keyboard settings or measure keyboard power.

| Condition | Repeatable procedure | Observation limit |
| --- | --- | --- |
| Initial connection/authentication | Start recording, attach, refresh, open stock session, run getters | Discovery and stream-open events are visible. Apple hides the authentication exchange. |
| Foreground/background | Run getters, switch to the Home Screen, return, reopen manually, run getters | App closes its session in the background. Recordings cannot prove activity during suspension. |
| Disconnect/reconnect | Run getters, detach normally, reattach, reopen, run getters | No firmware update runs. A detach must never count as update success. |
| Repeated/sustained typing | Run the key trials, then type test text for one minute | OS scheduling affects timing. Never infer electrical debounce from callbacks. |
| Backlight timing | Mark `on` after typing, leave keys untouched, mark `off` when seen | Visual marks include human response delay. Compare getters separately because traffic may affect idle behavior. |
| Idle/wake | Finish reads, leave the keyboard untouched for a chosen interval, press one key, mark state | Stop getter traffic during the idle interval. Locking the phone may create an observation gap. |
| Normal charging | Use the case's normal charging port and supported charger. Mark visible iOS charging, type, run getters, unplug normally | No electrical measurements or power-control commands. Charging status is user-observed. |
| Stock-app settings | Save a getter pass, background Inspector, view the official app's available settings, return, reopen, repeat | Inspector never changes settings. If you deliberately change an ordinary stock-app setting, change one at a time and preserve both passes. Do not select update/reset options. |

## Save and share

Tap **Stop and save**, then **Create JSON and report**, then **Share evidence files**.
Sharing opens Apple's share sheet. No upload occurs automatically.
The default JSON omits test characters and unvalidated payload bytes. Enable **Include my test characters in JSON** only for intentional test text.
The Markdown report always omits typed text. JSON schema 2 includes the versioned matrix-aware trial catalog and complete `coverage`, including untested entries.
The [complete export audit](REVIEW_EVIDENCE.md#field-by-field-export-audit) defines the `shareable-v2` field allowlist and retained metadata.
Wall-clock times and key codes remain in exports. Unknown strings and raw errors do not.

Local checkpoints retain exact getter requests, responses, fragments, unexpected bytes, and focused test text.
They omit serial numbers, connection IDs, phone names, and chip identifiers as metadata.
Unexpected raw bytes can contain identifiers, so local checkpoints are not shareable reports.
The app queues an atomic write to `Library/Application Support/StockEvidence/stock-local.json` immediately on start, every five seconds, after a completed baseline, and on session closure. Encoding and disk writes run on a serial background queue. A process kill before the queued write completes can lose that snapshot.
Relaunch restores that checkpoint for export. An unfinished session remains interrupted, with later observations unknown.
Starting another session replaces the checkpoint. Existing named exports remain in the app container.
Export first if you need to retain a session. The recorder stops at 20,000 events instead of silently dropping old evidence.
Storage errors appear on screen. Data since the last checkpoint can be lost on process termination.
To remove local captures, stop recording and use **Clear captured text and diagnostics**. Confirm **Delete local stock evidence**.
This also clears the text box and on-screen session logs. Shared copies and system logs remain outside the recorder's control.

## JSON contract

`schemaVersion: 2`, `commandInventory: "CK-5200-V122-H120-v1"`, trial catalog `CK-5200-V122-H120-trials-v1`.
JSON uses UTF-8, ISO-8601 UTC dates, integer byte arrays, and monotonic elapsed milliseconds.
Wall-clock dates locate observations. Use `elapsedMS` for timing, because the wall clock can change.
The random `sessionID` identifies an evidence session, not the keyboard.

| Field | Meaning |
| --- | --- |
| `environment` | iOS version, app version/build, and observation source. No phone identifier. |
| `privacy` | Local or shareable projection, including whether test text was explicitly included. |
| `endedAt`, `endReason` | End of capture, user stop, event limit, or recovery uncertainty. A live checkpoint has no end time. |
| `reads[]` | Read ID, command, exact request, start/send/end times, latency after full send, before/after state, response, outcome, previous-read consistency. |
| `events[]` | Ordered sequence, UTC time, elapsed time, source, kind, state snapshot, typed fields, optional bytes, byte count, optional read ID. |
| `state` | Accessory model/revisions/protocols, connected/session/app/focus state, user-marked condition/backlight/charging. Not a full device-state dump. |
| `sameAsPrevious` | Exact response equality against the previous validated read of that command, across conditions. Missing means no comparison. |
| `responseLatencyMS` | Last accepted request byte to final response or failure callback. Includes iOS scheduling. It is not USB wire latency. |

Sources are `EA`, `GCKeyboard`, `UIPress`, `UIKit`, `text-input`, `app`, or `user`.
Only parser-validated getter responses retain RX bytes in shareable output.
Failed, partial, unsolicited, and pending RX bytes retain timing, counts, and outcome, but omit payload bytes.
Request and fragment bytes survive export only when they match the allowlisted request or revalidated response.
Raw error strings become fixed outcome categories and reason codes. Unknown fields are omitted.
Default text redaction removes `characters` and `text`, retaining lengths, key codes, and modifiers.
Key codes can still reveal typed text. Use only test text.
No response is generated from the downloaded stock image. Test fixtures run only in the host tests.

## Behaviors still unobservable

- Raw HID reports, endpoint traffic, matrix crossings, scan frequency, electrical debounce, and true hardware rollover.
- Key combinations consumed by iOS, including some navigation and system shortcuts.
- Reliable attribution of coalesced GCKeyboard events to the case when another keyboard is attached.
- Releases during focus loss, disconnect, screen lock, or app suspension.
- Authentication chip contents, iAP2 negotiation, private credentials, and the exact reason for an EA access failure.
- Backlight current, charging current, rail state, and exact firmware idle timers from visual marks alone.
- Installer triggers, flash contents, application-corruption recovery, and power-loss guarantees.

Absent callbacks mean **unobservable**, not a guessed firmware output.

## Recovery boundary for M2

**R3 independent recovery: UNMET.** No evidence proves an iPhone-reachable independent boot stage.
The installer below `0x2000` remains unread. Preserve it, the application boundary `0x2000..0x89ff`, staging at `0x8a00`, and upper reserved flash.
The CH32X035 SDK describes 62 KB flash and 20 KB RAM. This does not establish the proprietary installer's complete flash layout.
Layer A requires executable application startup/recovery code. A QMK key chord alone is only operational recovery.
Layer B needs evidence for its trigger, independent USB/authentication, complete image reception, validation, and interrupted-install handling.
None is proven here. SDI and factory ISP remain Layer C contingencies and do not satisfy R3.

## Dependency assessment, 2026-10-14

This is the M1 reuse decision, not the completed M3 firmware audit. No upstream versions changed in M1.
The table distinguishes repository evidence from physical CK-5200 evidence.

| Candidate | Activity/release and tests | Reuse decision and limits |
| --- | --- | --- |
| Apple ExternalAccessory/EASession, UIKit, GameController | Apple platform APIs, compiled here with Xcode 27.0 SDK. Historical stock EASession/getter evidence exists. New capture screens still require device acceptance. | **Direct reuse in the iOS app.** Apple SDK terms. Runtime and USB/authentication mediation live in iOS, not the MCU. Device-specific work is the narrow getter policy, stream observation, and evidence recorder. No custom iAP2 in the app. |
| QMK upstream | Latest inspected commit `7a1bbf37c5c07da4ea0a162bb139083a46ef40a1`, 2026-10-02, keyboard addition. Latest numeric tag found: `0.34.6`, commit dated 2026-09-30. No GitHub latest-release record. Unit-test workflow and core tests exist. | **Reuse core logic, adapt the platform.** GPL-2.0 repository license, inspect individual file terms for distribution. Native C core, device-specific clock/matrix/HID/EEPROM adaptation. General QMK maintenance is not official CH32X035 support. No CK-5200 runtime proof. |
| TinyUSB upstream | Source change `5033903dc4bd97938c3fe20677b07fa08436cb17`, 2026-10-13. Release `0.21.0`, 2026-06-30. Unit, fuzz, and hardware-test infrastructure. | **Direct USB stack reuse with board adaptation.** MIT. Native C, configurable endpoint buffers. Existing `dcd_ch32_usbfs.c` and `hw/bsp/ch32x035` support are relevant. This does not prove this board's endpoint topology or iAP2 behavior. |
| WCH CH32X035 SDK | `c6253942b29ff2a3f5e2829182a874afffa45881`, 2026-01-08 merge of v2.1. No GitHub release record. Peripheral examples exist, comprehensive automated coverage not verified. | **Reuse vendor peripheral definitions and startup references.** Source notice restricts use to WCH MCUs. Native RISC-V V4C, 48 MHz, 62 KB flash, 20 KB SRAM. No manufacturer-supported Clicks/iAP2 interface established. Preservation of Clicks partitions remains device-specific. |
| `usenocturne/iap2-rs` | One inspected commit, `3b48f47dc20a24470fa86cd43f1ce15709e8127d`, 2026-05-07. No release. Two inline link tests observed. CI builds/lints but does not run `cargo test`. README attributes use to Nocturne. | **Architecture/test reference, not an embedded USB drop-in.** GPL-3.0. Tokio, Rust `std`, BlueZ/`bluer`, D-Bus, RFCOMM, dynamic allocation. Linux/Bluetooth environment exceeds this MCU's runtime model. Authentication trait is useful design evidence, not proof of this chip's interface. Distribution compatibility with all combined files needs review. |
| `wiomoc/iap2` | `032135e9b5f1276cffdf8a820baeaf10f5cf6b10`, 2022-03-08. No release or root license found. Link/control-message tests exist. Historical blog implementation. | **Reference only.** Python runtime, no CH32X035 embedded USB evidence. No copying without resolved license terms. Historical tests can inform separately written test vectors. |

The existing application partition allows 27,136 bytes, not the full chip flash capacity.
Current source pins remain in `keyboards/ck5200/scripts/pins.env`. Upstream activity does not mean those pins changed.
Exact per-library flash, RAM, CPU cost, and combined firmware timing remain unmeasured on this keyboard.
The existing hand-written `iap2.c` and `iap2_auth.c` have static-analysis and host-test support only, not iPhone acceptance.
Keep the genuine hardware authentication flow. No credential extraction, forgery, or bypass is part of this work.

Primary sources: [Apple EA](https://developer.apple.com/documentation/externalaccessory),
[Apple GCKeyboard](https://developer.apple.com/documentation/gamecontroller/gckeyboard),
[Apple UIKey](https://developer.apple.com/documentation/uikit/uikey),
[QMK](https://github.com/qmk/qmk_firmware), [TinyUSB](https://github.com/hathach/tinyusb),
[WCH](https://github.com/openwch/ch32x035), [iap2-rs](https://github.com/usenocturne/iap2-rs),
[wiomoc](https://github.com/wiomoc/iap2).
Repository commits, release APIs, source notices, tests, and READMEs were inspected for this table.

## Verification meaning

`test.sh` executes the production recorder, exact-packet authorization policy, parser, stream adapter, and existing flash controller tests.
The new checks cover fragments, timing, consistency, key gaps, redaction, checkpoints, interrupted recovery, and unsafe-packet rejection.
These are host simulations. They do not prove iOS callback coverage, physical compatibility, independent recovery, or production reliability.
No live keyboard update belongs to M1. Physical firmware acceptance remains separately authorized M5 work.

### Historical local checks for d2f679d, 2026-10-14

- Starting HEAD: `cab182e3bb801ce315009fbb6f4f9dc09678534b`, clean worktree.
- All six executables in `bash ios/ClicksInspector/test.sh` passed. These include the existing stream and stock-only flash-controller simulations.
- Unsigned arm64 iPhone build passed with Xcode 27.0, build `27A266a`.
- Generic iOS Simulator build passed with the same Xcode.
- `StockCharacterizationUITests.testStockRecorderWithoutAccessory` passed on the iPhone 17 Pro simulator, iOS 27.0. One test, zero failures.
- Inspected the actual UI-test export: schema 1, 16 events, zero reads, explicit simulator source, and `user-stop`.
- The local checkpoint retained the applied test text `abc`. The shareable JSON omitted all `text` and `characters` fields.
- `git diff --check` passed.

The build emitted the App Intents metadata notice. Clean compilation of the existing firmware controller also emitted its pre-existing weak-capture warning at line 506.
The machine-specific precheck could not run because `external-ssd-root` and `xcode-offload` were absent.
Normal `xcrun`/`xcodebuild` resolved the installed Xcode after setting `DEVELOPER_DIR`. No storage configuration changed.

Not run: a signed installation, physical keyboard capture, firmware writes, or the unchanged firmware C/Python suite.
At that historical revision, native callbacks and repeated stock reads still needed a physical baseline.
The stock diagnostics baseline is now complete. See the [current acceptance scope](../../keyboards/ck5200/docs/flash-readiness.md#baseline-acceptance-2026-10-14).
