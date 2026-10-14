# Install and collect the CK-5200 baseline

Status: **physical acceptance not collected**. This procedure installs the iOS app, not keyboard firmware.

## Development signing in Xcode

1. Install full Xcode and XcodeGen. Select that Xcode with `xcode-select` or your shell's `DEVELOPER_DIR`.
2. Run the following commands from the repository root:

   ```sh
   git pull --ff-only
   xcodegen generate --spec ios/ClicksInspector/project.yml
   bash ios/ClicksInspector/test.sh
   open ios/ClicksInspector/ClicksInspector.xcodeproj
   ```

3. In Xcode Settings → Accounts, sign in with your Apple development account.
4. Select the **ClicksInspector** application target, then **Signing & Capabilities**.
5. Enable **Automatically manage signing**. Select your team. Keep credentials and provisioning profiles out of Git.
6. If your team cannot register `com.rudironsoni.clicks-inspector`, choose a unique development bundle ID locally. Do not impersonate Clicks' bundle ID.
7. Connect and unlock the iPhone 15 Pro Max. Trust the computer and enable Developer Mode when iOS requests it.
8. Select the **ClicksInspector** scheme and that physical iPhone as the destination. Run with **Product → Run**.
9. Confirm app version `0.2.0`, build `3` in the exported environment fields. Record the source commit with the baseline.

If signing fails, preserve the exact Xcode error. Do not disable accessory authentication or modify keyboard firmware to fix an app-signing problem.

## Equivalent command-line build and installation

Use the real team ID and the iPhone identifier shown in Xcode's Devices and Simulators window.
Set `BUILD_DIR` to a local build directory appropriate for your machine. It must not be a source directory.
These environment values are local inputs, not repository configuration.

```sh
export DEVELOPMENT_TEAM='YOUR_TEAM_ID'
export IPHONE_UDID='YOUR_IPHONE_IDENTIFIER'
export BUILD_DIR="${TMPDIR%/}/clicks-inspector-device"

xcodegen generate --spec ios/ClicksInspector/project.yml
xcodebuild -project ios/ClicksInspector/ClicksInspector.xcodeproj \
  -scheme ClicksInspector -configuration Debug \
  -destination "id=$IPHONE_UDID" -derivedDataPath "$BUILD_DIR" \
  DEVELOPMENT_TEAM="$DEVELOPMENT_TEAM" CODE_SIGN_STYLE=Automatic \
  -allowProvisioningUpdates build

APP="$BUILD_DIR/Build/Products/Debug-iphoneos/ClicksInspector.app"
codesign --verify --deep --strict "$APP"
codesign -d --entitlements :- "$APP"
xcrun devicectl device install app --device "$IPHONE_UDID" "$APP"
xcrun devicectl device process launch --device "$IPHONE_UDID" com.rudironsoni.clicks-inspector
```

If you changed the bundle ID, pass `PRODUCT_BUNDLE_IDENTIFIER=your.development.identifier` to `xcodebuild` and use that ID in the launch command.
Successful signing and installation are app-delivery evidence. They are not keyboard acceptance.
An unsigned simulator build or generic unsigned iPhone build cannot substitute for this step.

## First physical session

1. Install the app before attaching the case. Close the official Clicks app and disconnect other keyboards.
2. Open **Stock characterization and key recorder**. Tap **Start characterization**.
3. Attach the stock CK-5200. Check model `CK-5200`, firmware `1.2.2`, hardware `1.2.0`, and the advertised protocol.
4. Tap **Open stock session**. Wait for both streams to open.
5. Select `baseline`. Mark visible backlight and charging state, then tap **Mark state before observation**.
6. Tap **Read seven getters, three passes**. Preserve failures as failures. Do not substitute commands.
7. Mark state after observation. Run each physical key trial in the focused text box, including modifiers and layers.
8. Test ordinary detach/reconnect and foreground/background. Reopen manually, then request another getter pass.
9. Tap **Stop and save**, then **Create JSON and report**. Review privacy choices before using **Share evidence files**.
10. Record the commit, signing/install result, and the JSON session UUID with your observations.

Use [the full baseline procedure](STOCK_CHARACTERIZATION.md#safe-operating-state-procedure) for idle/wake, backlight, sustained typing, and normal charging.
The app cannot observe system-consumed shortcuts, raw HID packets, or events during suspension. Mark those cases as unobservable.

## Acceptance record

Keep this record with the actual exported files. Fill it from observations, not expected values:

| Field | Observation |
| --- | --- |
| Source commit, app version/build, iOS version | Not collected |
| Signed install and launch | Not collected |
| Actual accessory identity/protocols | Not collected |
| 21-request baseline: completed/failed, latency, consistency | Not collected |
| Every physical key: trial labels, callbacks, text, gaps | Not collected |
| Detach/reconnect and foreground/background outcomes | Not collected |
| Idle/backlight/charging observations | Not collected |
| Export session UUID and files | Not collected |
| Unobservable combinations and remaining failures | Not collected |

Do not select **Open firmware-control session**, firmware update, reset, erase, or recovery experiments during this procedure.
Custom flashing stays disabled. **R3 independent recovery: UNMET.**
