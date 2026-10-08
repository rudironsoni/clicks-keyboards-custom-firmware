# Clicks Inspector for iPhone

A read-only experiment for the Clicks case on iPhone 15 Pro Max. It lists accessories visible to Apple's External Accessory framework and attempts a session with `com.clickscompanion.protocol` when you tap **Open session**. An open session enables selected stock queries for model `CK-5200`, hardware `1.2.0`, firmware `1.2.2` only.

The app sends a fixed read or diagnostic request only when you tap **Read selected value**. It has no arbitrary-command, settings-write, update, reboot, or memory-read operation. iOS manages accessory session setup. Opening a session is not proof that flash reading, firmware updates or recovery are supported. The app does not spoof the official Clicks app's bundle identifier.

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

Two additional diagnostics test specific static findings. **Probe checksum service 0x08** sends only `05 08 00 00 00`, expected reply `04 02 08 00`. **Probe newer user-key read 0x8F** sends only `06 8f 00 00 00 00`, expected reply `08 02 8f ff 00 00 00 00`. The latter explicitly expects an unsupported-command response, not a keymap. Only this probe permits status `ff`, and it also requires four zero payload bytes. Any different response closes the session and remains in the bounded RX log. These are fixed packets, not an opcode scan. Their [handler analysis](../../keyboards/ck5200/docs/USB_AND_IPHONE.md#bounded-service-probes) explains why they do not write keyboard state.

After installing the diagnostic build, open the session, select each probe, and tap **Read selected value** once. Record the result and RX log before closing. Do not treat the expected acknowledgement or unsupported response as firmware readback.

One request can run at a time. The app handles partial writes and fragmented replies, bounds replies to the selected command's expected length, and checks the reply type, echoed command, and status. A five-second timer covers output availability and the reply. Queued input before a request or input before a full send stops the attempt. Opening, refreshing, reconnecting, or choosing a command never sends a query automatically.

## Logs and lifecycle

The screen retains the latest 200 timestamped entries in memory, newest first. Unified system logs use subsystem `com.rudironsoni.clicks-inspector`, category `accessory`. User actions, command selection, connection changes, session state, stream errors, and read results are logged. TX logs contain only the fixed request bytes. RX logs contain at most nine bytes per read while a query is active. Unsolicited input is discarded with a byte count. Serial numbers are not logged. The app has no log files, upload, or network feature; iOS manages its own system-log retention.

Leaving the app for the background closes the session. Disconnecting the active accessory also closes it. Returning to the foreground refreshes the list but never opens a session automatically. **Clear logs** clears the on-screen buffer, not the operating system's logs.

## What the test can establish

Apple's [External Accessory framework](https://developer.apple.com/documentation/externalaccessory/) exposes protocols supported by a connected accessory. The manufacturer controls which third-party apps may communicate. Adding a protocol string to this app's Info.plist does not guarantee acceptance.

A simulator can validate the interface and empty state. It cannot validate this physical USB-C accessory, iAP session acceptance, flash readback, or recovery. A successful stock query proves only the selected field is readable through this session. No full firmware dump is available from the implemented commands.

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
