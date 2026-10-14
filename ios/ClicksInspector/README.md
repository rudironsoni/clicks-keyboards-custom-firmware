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
The `AuthorizedStockUpdate` build configuration retains the stock-only update path for a later separately authorized milestone. Do not install that configuration for M1. [Current readiness](../../keyboards/ck5200/docs/flash-readiness.md).
Apple manages the accessory session and authentication. Opening a session does not prove memory access or recovery.

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

## New capabilities, 2026-10-10

The app bundles both images, but the full product lifecycle is not yet authorized or verified:

- **Flash custom firmware**: DISABLED in this build (2026-10-12): no CH32X035 image has been authorized. The bundled QMK image targets CH32X035 but stays unauthorized; the controller refuses any image that is not the authorized stock artifact, and the buttons and the import path are removed until a separately reviewed image exists.
- **Stock update implementation**: retained behind `AuthorizedStockUpdate`; default builds refuse it before any firmware packet. Restoring stock from custom firmware remains unauthorized and unverified.
- **Self-test, custom mode only**: reads identity and keymaps, then round-trips a write on the spare crossing. This is not part of stock characterization.
- **Stock key recorder**: focused UIKit text input plus `UIPress`/`UIKey` and `GCKeyboard` callbacks. Visible text proves that iOS delivered text, not that all HID behavior is correct.
- **Layout preview**: the keymap displayed as a grid with readable key names (not raw hex), color-coded by key type (layer keys blue, modifiers orange, consumer purple, transparent gray).
- **Keymap editor**: now shows both the readable name and the hex value per cell, with an expanded preset list including arrows and the iOS keyboard toggle.
- **Post-flash state**: after A0 the session dies with the reboot. The app never claims installation success: it enters an `updateUnverified` state that survives reconnects and relaunches, refuses any further flashing, and clears only when you explicitly mark the update verified after the keyboard demonstrably works. A lost final-step reply is reported as commit-unknown, never as "nothing was written" (the outcome genuinely cannot be distinguished through this protocol).

## Run the focused host checks

Run the Swift checks and checkpoint benchmark, including the recorder, diagnostics boundary, and retained stock-update simulations:

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

## Updating the bundled custom firmware

The bundled `Resources/ck5200_qmk.bin` is the current build output from `keyboards/ck5200/build/ck5200_qmk.bin`. After rebuilding the firmware, refresh the app's copy:

```sh
cp keyboards/ck5200/build/ck5200_qmk.bin ios/ClicksInspector/Resources/ck5200_qmk.bin
shasum -a 256 keyboards/ck5200/build/ck5200_qmk.bin ios/ClicksInspector/Resources/ck5200_qmk.bin
```

Then regenerate the Xcode project (`xcodegen generate`) and rebuild the app.

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
