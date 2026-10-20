#!/usr/bin/env bash
# Manually sign the M4 StockFlashTrial app for a provisioned iPhone.
#
# WHY THIS EXISTS
#   CLI signing via `xcodebuild -allowProvisioningUpdates` fails on machines
#   where the Xcode account session is GUI-only, and `codesign` from a
#   non-GUI shell can be refused by the login keychain ACL
#   (errSecInternalComponent). This script performs the manual signing step
#   so the only interactive part is approving key access (or, if that is
#   refused too, building with Xcode GUI: open the project, select the
#   StockFlashTrial configuration and the iPhone destination, Product → Run).
#
# USAGE
#   xcodebuild -project ios/ClicksInspector/ClicksInspector.xcodeproj \
#     -scheme ClicksInspector -configuration StockFlashTrial \
#     -destination 'generic/platform=iOS' CODE_SIGNING_ALLOWED=NO \
#     -derivedDataPath "$TMPDIR/m4" build
#   bash ios/ClicksInspector/sign-trial-app.sh \
#     "$TMPDIR/m4/Build/Products/StockFlashTrial-iphoneos/ClicksInspector.app" \
#     "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles/<profile>.mobileprovision"
#   xcrun devicectl device install app --device <UDID> <that .app>
#
# MACHINE INPUTS
#   The default identity hash and the profile are local machine inputs, not
#   repository configuration. The profile must include the target iPhone in
#   its ProvisionedDevices, match the bundle id (a wildcard profile works),
#   and embed the identity's certificate.
set -euo pipefail

APP="${1:?usage: sign-trial-app.sh <ClicksInspector.app> <profile.mobileprovision>}"
PROFILE="${2:?usage: sign-trial-app.sh <app> <profile>}"
IDENTITY="${SIGN_IDENTITY:-253C9440F58480C7D11CD873256C8D8CE9C2EFD0}"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/m4sign.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# 1. Extract the profile's entitlements (codesign supplies the derived keys).
security cms -D -i "$PROFILE" > "$WORK/profile.plist"
python3 - "$WORK/profile.plist" "$WORK/entitlements.plist" <<'PY'
import plistlib, sys
p = plistlib.load(open(sys.argv[1], 'rb'))
ents = dict(p.get('Entitlements', {}))
ents.pop('application-identifier', None)
plistlib.dump(ents, open(sys.argv[2], 'wb'))
print("entitlements:", sorted(ents.keys()))
print("profile team:", p.get('TeamIdentifier'), "devices:", len(p.get('ProvisionedDevices', [])))
PY

# 2. Embed the profile and drop any stale signature.
cp "$PROFILE" "$APP/embedded.mobileprovision"
rm -rf "$APP/_CodeSignature"

# 3. Sign nested code first (frameworks, extensions), then the app.
find "$APP" \( -name "*.framework" -o -name "*.appex" \) | sort | while read -r nested; do
  /usr/bin/codesign --force --timestamp=none --sign "$IDENTITY" "$nested"
done
/usr/bin/codesign --force --timestamp=none --generate-entitlement-der \
  --entitlements "$WORK/entitlements.plist" --sign "$IDENTITY" "$APP"

# 4. Verify.
/usr/bin/codesign --verify --deep --strict --verbose=2 "$APP" 2>&1 | tail -2
echo "signed: $APP"
echo "install: xcrun devicectl device install app --device <UDID> \"$APP\""
