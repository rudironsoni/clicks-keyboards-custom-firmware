# Clicks Inspector for iPhone

Clicks Inspector supports stock characterization and separately gated firmware controls for the CK-5200 on iPhone 15 Pro Max.
Use [Stock Characterization](STOCK_CHARACTERIZATION.md) for the current baseline procedure, export contract, observation limits, and dependency assessment.
See [review evidence and privacy audit](REVIEW_EVIDENCE.md) for production paths and CI, and [development signing](SIGNING_AND_BASELINE.md) to install on the iPhone.

**Custom firmware: NO-GO. R3 independent recovery: UNMET.** The goal remains an iOS app and custom keyboard firmware with iPhone-only recovery.
Builds and host tests do not prove hardware reliability. The installer remains unread.

**Open session** uses the stock read-only path. It does not start the firmware controller or send automatic identification traffic.
The seven demonstrated getters require model `CK-5200`, hardware `1.2.0`, firmware `1.2.2`.
The characterization screen adds user-started three-pass reads, native keyboard-event recording, checkpoints, and redacted JSON/report export.
It sends no settings, reset, update, or unknown-command requests.

**Debug and Release are diagnostics-only.** The controller permits only the exact demonstrated stock getters on the known identity, including through legacy firmware controls. A0/A1/A2/A3, settings, and reset commands are refused.
`StockFirmwareManager` and the legacy `AuthorizedStockUpdate` configuration also block all physical writes. They permit only the exact identity query in the firmware controller. Only macOS host tests compiled with `STOCK_MANAGER_OFFLINE_TESTING` can run update transactions. [Current readiness](../../keyboards/ck5200/docs/flash-readiness.md).
Apple manages the accessory session and authentication. Opening a session does not prove memory access or recovery.

## M3 QMK RAM profiles

`QMKKeymap` is a separate build configuration with `QMK_RAM_KEYMAP`, without
`STOCK_UPDATE_AUTHORIZED`. It permits `0x90` through `0x93` only after the
current session reports model `CK-5200`, hardware `1.2.0`, and version payload
`51 4d 00 01` (`QM`). Stock, unknown identities, and rescue `RS` cannot use
these commands. A0 through A3 remain blocked at the controller's transport
boundary. Debug and Release retain their diagnostics-only policy.

Open **Edit QMK keymap and phone profiles**. A firmware-control connection reads
all four 6x6 layers. Turn on **Allow RAM writes for this session**, select a layer,
and tap a cell to edit its keycode. Every acknowledged write is read back before
the UI reports success. The permission switch resets on disconnect.

**Save current layout on iPhone** stores a named schema-version-1 profile in
`Documents/QMKProfiles.json`, with the target identity, dimensions, all 144
keycodes, and the selected desired profile. Writes replace the JSON file
atomically. Load and save errors are visible. An invalid existing file is
preserved, rather than silently replaced. Profiles survive app relaunch;
keyboard edits remain RAM-only and disappear on power loss.

After reconnect, the controller reads the device and compares it with the saved
desired profile. **Reapply saved profile to RAM** is always manual. Only changed
cells are sent, and each has its own readback. A failed operation reports the
number of verified writes and warns that the last write may have reached RAM.
After timeout or malformed readback, reconnect is required because this protocol
has no transaction IDs. Reconnect reads actual RAM before any further writes.

**Restore factory layout in RAM** sends the real `0x93` runtime reset, reads all
144 keys, and compares them with the compiled factory layout. It keeps phone
profiles. This command does not flash, reboot, or restore stock firmware.

### Software verification and simulator evidence

`Tests/QMKProfileTests.swift` runs the actual Swift controller against
`ck5200_keymap_handle`, the pinned QMK core, the real compiled keymap, and QMK's
transient EEPROM driver. `Tests/QMKBridge.c` only supplies hardware hooks.
The tests cover all four layers, persistence, runtime restore, manual reapply,
partial application, lost replies, partial-reply timeouts, reconnect, stale
session callbacks, wrong identities, and the A0-A3 boundary. Run
`bash ios/ClicksInspector/test.sh` with the pinned firmware dependencies present.

In the QMKKeymap simulator build, **Connect simulated QMK keyboard** and
**Simulate power loss and reconnect** drive the same editor and controller using
a labelled software fixture. The fixture is compiled out of iPhone builds.
`QMKProfileUITests` edits a key, saves a profile, terminates and relaunches the app,
manually reapplies the profile, and restores factory RAM. The simulator fixture
uses the same production C/QMK bridge as the host tests, not a Swift command
mock. Xcode builds that library only for QMKKeymap simulator destinations.
It attaches screenshots
named `qmk-editing`, `qmk-reapplied`, and `qmk-restored` to the result bundle.

CI builds Debug, Release, and QMKKeymap, runs the host suite and simulator UI
tests, and uploads `evidence/qmk-ui-tests.xcresult` plus exported
`evidence/qmk-screenshots/` in `ios-verification-<sha>`.
For local runs, use ignored `ios/ClicksInspector/build/m3/qmk-ui-tests.xcresult`
and export attachments to `ios/ClicksInspector/build/m3/qmk-screenshots/`.
These are software evidence only. M1 and M2 remain accepted; R3 is unmet and
custom flashing remains no-go.

Local verification status for this change: **blocked before Xcode execution**.
The required machine check could not run because `xcode-offload` and
`external-ssd-root` are missing from the required PATH. The Xcode wrappers are
also absent from `~/.local/bin`. Hosted macOS CI supplied passing builds, Swift/C
tests, simulator workflows and screenshots instead. See the [M3 verification
record](../../keyboards/ck5200/docs/m3-verification.md) for exact tested revisions,
CI links, downloaded artifacts and evidence limits.

## Build and install

Requirements: full Xcode with an iOS SDK, XcodeGen 2.46.0 or later, and an Apple development signing team for the physical iPhone. XcodeGen generates the local project from `project.yml`; generated project files and build output are ignored.

From the repository root:

```sh
xcodegen generate --spec ios/ClicksInspector/project.yml
open ios/ClicksInspector/ClicksInspector.xcodeproj
```

In Xcode, select the `ClicksInspector` scheme. Under **Signing & Capabilities**, select your personal team. Connect and unlock the iPhone, trust the Mac if prompted, then select it as the run destination. Enable [Developer Mode](https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device/) if Xcode requires it. Run the app.

The checked-in project has no team ID or provisioning profile. Signing choices remain local. [Apple's device-running guide](https://developer.apple.com/documentation/xcode/running-your-app-on-simulated-or-physical-devices) explains team selection and automatic signing.

For an unsigned simulator build:

```sh
xcodebuild -project ios/ClicksInspector/ClicksInspector.xcodeproj \
  -scheme ClicksInspector -configuration Debug \
  -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath ios/ClicksInspector/build/simulator \
  CODE_SIGNING_ALLOWED=NO build
```

If `xcode-select -p` points to Command Line Tools, prefix the command with `DEVELOPER_DIR` set to the actual Xcode `Contents/Developer` directory. Do not copy another machine's Xcode path without checking it.

## Test with the Clicks case

1. Install the app before replacing the Mac cable with the Clicks case. Wireless Xcode access is useful for logs but is not required to read the on-screen result.
2. Close the official Clicks app so it does not hold the same accessory session.
3. Attach the case to the unlocked iPhone. Keep Clicks Inspector in the foreground and tap **Refresh**.
4. Read the model, hardware/firmware versions and all advertised protocol names.
5. If `com.clickscompanion.protocol` appears, tap **Open session** for that accessory.
6. Leave **Version** selected and tap **Read selected value** once. Keep the app in the foreground. The expected response for the audited stock image is `08 02 03 00 01 20 01 22`. The screen retains raw hardware bytes `01 20` and firmware bytes `01 22`.
7. Record the result and newest TX/RX logs. A timeout, malformed reply, or error closes the session without an automatic retry. Do not substitute another command or framing to make a failed version test pass.
8. After a successful version exchange, select one settings read at a time. Repeat the same read to compare its raw payload. Runtime flags can change; settings field values do not constitute a firmware backup. Tap **Close session** when finished.

An empty list means this app cannot currently see an External Accessory connection. It does not prove the keyboard is disconnected or cannot type as a HID keyboard. An empty protocol list can reflect incomplete authentication. A missing expected protocol leaves the open button disabled. A session creation failure, stream error or ten-second timeout is a failed access test, not evidence that a flash command is needed.

**Open** requires both streams to report that they opened. This proves only session access. The fixed read list is `02 03`, `02 84`, `02 86`, `02 88`, `02 8a`, `02 8c`, and `02 8d`. The [stock command audit](../../keyboards/ck5200/docs/USB_AND_IPHONE.md#stock-ios-session-and-read-command-investigation-2026-10-08) records each handler and its limits.

Historical diagnostics used `05 08 00 00 00`, with reply `04 02 08 00`, and `06 8f 00 00 00 00`, with reply `08 02 8f ff 00 00 00 00`.
They established an acknowledgement and an unsupported-command response, not a checksum, firmware dump, or keymap.
Their parsers and [evidence](../../keyboards/ck5200/docs/USB_AND_IPHONE.md#bounded-service-probes) remain available for offline checks.

M1 removes both probes from the executable UI and rejects them through the stock policy. See the [version-specific command inventory](STOCK_COMMAND_INVENTORY.md).

One request can run at a time. The app handles partial writes and fragmented replies, bounds replies to the selected command's expected length, and checks the reply type, echoed command, and status. A five-second timer covers output availability and the reply. Queued input before a request or input before a full send stops the attempt. Opening, refreshing, reconnecting, or choosing a command never sends a query automatically.

## Logs and lifecycle

The legacy session screen retains the latest 200 log entries. Unified logs use subsystem `com.rudironsoni.clicks-inspector`, category `accessory`.
Stock Characterization stores a separate bounded session, including stream fragments, lifecycle events, read outcomes, and focused keyboard events.
It writes local checkpoints and exports JSON plus Markdown. Default share exports omit characters and unvalidated bytes.
Serial numbers and chip identifiers are not collected as metadata. No upload occurs automatically.

Leaving the app for the background closes the session. Disconnecting the active accessory also closes it. Returning to the foreground refreshes the list but never opens a session automatically. **Clear logs** clears the on-screen buffer, not the operating system's logs.

## What the test can establish

Apple's [External Accessory framework](https://developer.apple.com/documentation/externalaccessory/) exposes protocols supported by a connected accessory. The manufacturer controls which third-party apps may communicate. Adding a protocol string to this app's Info.plist does not guarantee acceptance.

A simulator can validate the interface and empty state. It cannot validate this physical USB-C accessory, iAP session acceptance, flash readback, or recovery. A successful stock query proves only the selected field is readable through this session. No full firmware dump is available from the implemented commands.

## Verified stock firmware manager, 2026-10-16

- **Prepare official V122** checks the bundled 18,648-byte image, exact SHA-256 and image structure without opening an accessory session. The app package excludes the QMK image.
- **Offline installation workflows** cover stock-to-stock and authenticated QM/RS-to-stock. The controller admits only the next exact pinned-image packet for the current session. Stock identity is `01 20 01 22`; custom modes are `QM 00 01` and `RS 00 01`. Exact model `CK-5200` and hardware `1.2.0` are required.
- **Physical installation** stays disabled in every iOS configuration. There is no UI, build flag or consent switch that authorizes it. A separate implementation decision and explicit authorization are required.
- **Outcome journal** is written atomically before A3 and retained across reconnect/relaunch. Missing or invalid responses stop the session and discard queued output. A same-version response or manual acknowledgement cannot clear the record. An unreadable journal blocks later installation.
- **Stock key recorder** remains available with the accepted M1 definitions. The QMKKeymap configuration provides the separate RAM-keymap editor and phone profiles.

See [stock manager readiness](../../keyboards/ck5200/docs/stock-manager-readiness.md) for protocol comparison, tests, artifacts and limits. Application-level rescue is not recovery from an unbootable application.

## Run the focused host checks

Run the Swift checks and checkpoint benchmark, including the recorder, diagnostics boundary, stock-update simulations and production Swift/C integration. The C bridge requires clean pinned QMK and SHA-256 checkouts from `keyboards/ck5200/scripts/bootstrap.sh`:

```sh
bash ios/ClicksInspector/test.sh
```

The individual commands below remain useful for focused checks. Run from the repository root with the appropriate `DEVELOPER_DIR` if needed:

```sh
# Protocol layer (requests, accumulator, chunker).
xcrun swiftc -module-cache-path /tmp/ck5200-swift-cache \
  ios/ClicksInspector/Sources/CustomKeyboardProtocol.swift \
  ios/ClicksInspector/Tests/ck5200_protocol_check.swift \
  -o /tmp/ck5200_protocol_check && /tmp/ck5200_protocol_check

# Layout, keycodes, factory layout, self-test logic.
xcrun swiftc -module-cache-path /tmp/ck5200-swift-cache \
  ios/ClicksInspector/Sources/KeycodeNames.swift \
  ios/ClicksInspector/Sources/FactoryLayout.swift \
  ios/ClicksInspector/Sources/KeyboardSelfTest.swift \
  ios/ClicksInspector/Sources/CustomKeyboardProtocol.swift \
  ios/ClicksInspector/Tests/ck5200_layout_check.swift \
  -o /tmp/ck5200_layout_check && /tmp/ck5200_layout_check

# Stock read commands.
xcrun swiftc -module-cache-path /tmp/ck5200-swift-cache \
  ios/ClicksInspector/Sources/StockReadCommand.swift \
  ios/ClicksInspector/Tests/StockReadCommandTests.swift \
  -o /tmp/ck5200-stock-read-tests && /tmp/ck5200-stock-read-tests
```

`Resources/ck5200_qmk.bin` remains a negative authorization fixture for host tests. It is excluded from app resources and is not an install candidate for this milestone.

## Run the focused read-parser checks

These use the production parser without an accessory or an Xcode test target. Run from the repository root with the appropriate `DEVELOPER_DIR` if needed:

```sh
xcrun swiftc -module-cache-path /tmp/ck5200-read-swift-cache \
  ios/ClicksInspector/Sources/StockReadCommand.swift \
  ios/ClicksInspector/Tests/StockReadCommandTests.swift \
  -o /tmp/ck5200-stock-read-tests
/tmp/ck5200-stock-read-tests
```

The two checks cover a fragmented known version reply and fixed getters, plus rejection of malformed or mismatched replies. They do not simulate iOS stream scheduling or keyboard firmware.

## Verification, 2026-10-08

The app built for iOS Simulator and as a development-signed arm64 iPhone app with Xcode 27.0. The signed bundle passed `codesign --verify --strict`. It launched in the iPhone 17 Pro simulator on iOS 27.0 and displayed the closed session, empty accessory list and startup logs. Both builds reported only the App Intents metadata notice because this app has no App Intents dependency.

After adding the read commands, both app builds and strict signature verification passed again. The two focused Swift checks, five existing Python tests, and existing C protocol test passed. The new app launched in Simulator and displayed the disabled read button with no accessory connected. T3's device panel could not enumerate iOS simulators because its `xcrun` lookup failed; the existing explicit-Xcode `simctl` path provided the screenshot. The unsigned build also logged sandbox restrictions on CoreSimulator service discovery, despite completing successfully.

After a direct USB connection, the original inspector installed and launched on the iPhone 15 Pro Max running iOS 27.0.1. Rudi's screenshots then showed physical CK-5200 discovery and an open session. The update with read commands also installed successfully. Its remote launch attempt was denied because the phone was locked, with CoreDevice error 10002 and `FBSOpenApplicationErrorDomain` code 7. Rudi subsequently opened it and supplied screenshots of all seven reads completing at 14:39 through 14:43. Their [exact payloads and evidence limits](../../keyboards/ck5200/docs/USB_AND_IPHONE.md#stock-ios-session-and-read-command-investigation-2026-10-08) are recorded in the stock command audit.

Wireless Xcode access, repeated-read comparisons, timeout behavior, and disconnect handling remain **UNVERIFIED**. No keyboard firmware was flashed. No full device backup or restore test has been performed.

The later diagnostic build passed the two extended Swift checks, five Python tests, C test, signed iPhone build, and strict signature verification. After an initial installation failure and a 30-second timeout, a wireless retry installed the diagnostic build and its launch succeeded. CoreDevice reported `transportType=localNetwork`, `tunnelState=connected`, and enabled Developer Mode, while USB showed no phone. Tailscale ping also succeeded, but this does not establish that Xcode used the Tailscale route. A later screenshot request failed with CoreDevice error 4016, so the developer connection remains intermittent. Rudi's 15:16 and 15:17 screenshots then confirmed the checksum acknowledgement and the unsupported newer user-key command on the physical keyboard. Both matched the predicted replies; neither returned firmware or keymap data. Simulator checks were not repeated for this build; the earlier UI evidence applies to the seven-read build.
