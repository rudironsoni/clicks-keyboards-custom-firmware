# Install and collect the CK-5200 baseline

Status: **stock diagnostics baseline complete**, accepted on 2026-10-14. See the [acceptance scope](../../keyboards/ck5200/docs/flash-readiness.md#baseline-acceptance-2026-10-14) and [physical observations](../../keyboards/ck5200/docs/flash-readiness.md#physical-observations-2026-10-14). Comprehensive keyboard characterization remains incomplete. No further recording is required for this baseline. The installation procedure below is a reference for future sessions.

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
8. Select the **ClicksInspector** scheme, **Debug** configuration, and that physical iPhone as the destination. Run with **Product → Run**. Do not select `AuthorizedStockUpdate`.
9. Confirm **DIAGNOSTICS ONLY** on the recorder screen and app version `0.2.0`, build `4` in the export. Record the source commit with the baseline.

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
7. Mark state after observation. Focus the test box and collect continuous mixed typing, modifiers, symbols, and navigation. Catalog trials are optional. Analyse events individually and split observations at gaps. Do not assign mixed input to one matrix candidate. If using a catalog trial, release every key and end the trial **before tapping Done typing**. Leaving the text box during an active trial records a gap and makes a requested completed outcome ambiguous. Only one trial can run at once.
8. Test ordinary detach/reconnect and foreground/background. Reopen manually, then request another getter pass.
9. Tap **Stop and save**, then **Create JSON and report**. Review privacy choices before using **Share evidence files**.
10. Record the commit, signing/install result, and the JSON session UUID with your observations.

For separately scoped follow-up observations, use [the operating-state procedure](STOCK_CHARACTERIZATION.md#safe-operating-state-procedure).
`CK-5200-V122-H120-trials-v1` is the immutable historical M1 catalogue from `246f2b8`. It retains 35 decoded crossings, an unmapped Shift entry, and the Q mapping-conflict label so restored captures keep their original definitions. These are historical catalogue statements, not current matrix facts or new verification of old trial IDs. The corrected M3 matrix is documented in [M3 first-boot evidence](../../keyboards/ck5200/docs/m3-first-boot.md) and used by the separate QMK factory map. Printed cap-to-matrix mappings remain unverified. Unknown consumer caps and unknown mode controls must remain unobservable until identified.
The exported `coverage` array lists every trial, including `not-started` entries, with callback, applied-text, and gap event references. A completed trial means an observation was finished, not that a physical key passed validation. Compare the actual case with the evidence before accepting a mapping.
The app cannot observe system-consumed shortcuts, raw HID packets, or events during suspension. Mark those cases as unobservable.

## Acceptance record

Keep this record with the actual exported files. Fill it from observations, not expected values:

| Field | Observation |
| --- | --- |
| Repository reference, app version/build, iOS version | `ef3a5b78bf43f30d17135a4339dd06c05e31c257`, 0.2.0 (4), iOS 27.0.1 |
| Signed install and launch | Passed over Wi-Fi, 2026-10-14 |
| Actual accessory identity/protocols | CK-5200, firmware 1.2.2, hardware 1.2.0, `com.clickscompanion.protocol` |
| 21-request baseline: completed/failed, latency, consistency | 21/21 valid, 0 failures, 1.066–19.380 ms; replies identical across three passes |
| Every physical key: trial labels, callbacks, text, gaps | Partial: letters, digits, symbols, arrows, Space, Return, Backspace, and left modifiers observed. Applied text includes all uppercase letters and digits. Code 430 and matrix positions remain unidentified. Held-key repeat remains unverified. |
| Detach/reconnect and foreground/background outcomes | Session `FC7B9890-F897-4781-9CE9-373E8314371B`: 63/63 valid reads in three sweeps, identical replies across reconnect and app transitions with session reopening. Session was closed at detach. Open-session or pending-read interruption remains unverified. |
| Idle/backlight/charging observations | Not collected |
| Export session UUID and files | Five inspected sessions and JSON hashes recorded in the authoritative readiness record |
| Unobservable combinations and remaining failures | One Command interval crosses an app-inactive gap. Physical layer-control attribution and systematic modifier coverage remain incomplete. |

Do not select **Open firmware-control session**, firmware update, reset, erase, or recovery experiments during this procedure.
Custom flashing stays disabled. **R3 independent recovery: UNMET.**
