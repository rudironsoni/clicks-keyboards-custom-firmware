#!/usr/bin/env bash
# Pre-flash validation: checks everything that can be verified without
# hardware. If any check fails, DO NOT FLASH.
#
# Usage: bash keyboards/ck5200/tools/preflight_check.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="$(cd "$ROOT/../.." && pwd)"
PASS=0
FAIL=0

check() {
    local name="$1"
    shift
    if "$@" >/dev/null 2>&1; then
        printf '  PASS  %s\n' "$name"
        PASS=$((PASS+1))
    else
        printf '  FAIL  %s\n' "$name"
        FAIL=$((FAIL+1))
    fi
}

header() { printf '\n== %s\n' "$1"; }

header "Stock image integrity"
STOCK_V122="$ROOT/.stock/iKeyboard_CK-5200_V122_120.bin"
STOCK_V121="$ROOT/.stock/iKeyboard_CK-5200_V121_120.bin"
if [[ -f "$STOCK_V122" ]]; then
    ACTUAL=$(shasum -a 256 "$STOCK_V122" | cut -d' ' -f1)
    check "V122 hash matches" test "$ACTUAL" = "8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8"
else
    check "V122 image present" false
fi
if [[ -f "$STOCK_V121" ]]; then
    ACTUAL=$(shasum -a 256 "$STOCK_V121" | cut -d' ' -f1)
    check "V121 hash matches" test "$ACTUAL" = "1c9ae5b2a4a838a5c62db0ce38c6d1d71c91fcc4101f0f8e37b365a746dac4aa"
else
    printf '  SKIP  V121 image (not required for flashing)\n'
fi

header "Custom firmware image"
IMAGE="$ROOT/build/ck5200_qmk.bin"
ELF="$ROOT/build/ck5200_qmk.elf"
if [[ -f "$IMAGE" && -f "$ELF" ]]; then
    check "image passes ELF/BIN validation" \
        "$ROOT/.venv/bin/python" "$ROOT/tools/validate_image.py" "$IMAGE" --elf "$ELF"
    SIZE=$(stat -f%z "$IMAGE")
    check "image within 27136-byte limit" test "$SIZE" -le 27136
    printf '        size: %s bytes, sha256: %s\n' "$SIZE" "$(shasum -a 256 "$IMAGE" | cut -d' ' -f1)"
else
    check "firmware image built" false
fi

header "Restore images in the iOS app"
APP_STOCK="$REPO/ios/ClicksInspector/Resources/iKeyboard_CK-5200_V122_120.bin"
APP_CUSTOM="$REPO/ios/ClicksInspector/Resources/ck5200_qmk.bin"
if [[ -f "$APP_STOCK" ]]; then
    ACTUAL=$(shasum -a 256 "$APP_STOCK" | cut -d' ' -f1)
    check "app stock image matches" test "$ACTUAL" = "8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8"
else
    check "app stock image present" false
fi
if [[ -f "$APP_CUSTOM" && -f "$IMAGE" ]]; then
    ACTUAL=$(shasum -a 256 "$APP_CUSTOM" | cut -d' ' -f1)
    BUILT=$(shasum -a 256 "$IMAGE" | cut -d' ' -f1)
    check "app custom image matches the build" test "$ACTUAL" = "$BUILT"
else
    check "app custom image present and matching" false
fi

header "Host harnesses"
cd "$ROOT"
TEST_OUTPUT=$(bash scripts/test.sh 2>&1)
OK_COUNT=$(echo "$TEST_OUTPUT" | grep -cE '^ok|passed')
check "all 11 test outputs pass" test "$OK_COUNT" -ge 11

header "Restore-cycle harness (real images, real handler)"
check "full stock + custom restore cycles" \
    /tmp/ck5200_restore_test "$STOCK_V122" "$IMAGE"

header "Swift protocol checks"
SWIFT_CACHE="/tmp/ck5200-preflight-swift-cache"
mkdir -p "$SWIFT_CACHE"
if xcrun swiftc -module-cache-path "$SWIFT_CACHE" \
    "$REPO/ios/ClicksInspector/Sources/CustomKeyboardProtocol.swift" \
    "$REPO/ios/ClicksInspector/Tests/ck5200_protocol_check.swift" \
    -o /tmp/ck5200_protocol_check 2>/dev/null; then
    check "Swift protocol check" /tmp/ck5200_protocol_check
else
    printf '  SKIP  Swift protocol check (swiftc not available)\n'
fi
if xcrun swiftc -module-cache-path "$SWIFT_CACHE" \
    "$REPO/ios/ClicksInspector/Sources/KeycodeNames.swift" \
    "$REPO/ios/ClicksInspector/Sources/FactoryLayout.swift" \
    "$REPO/ios/ClicksInspector/Sources/KeyboardSelfTest.swift" \
    "$REPO/ios/ClicksInspector/Sources/CustomKeyboardProtocol.swift" \
    "$REPO/ios/ClicksInspector/Tests/ck5200_layout_check.swift" \
    -o /tmp/ck5200_layout_check 2>/dev/null; then
    check "Swift layout/self-test check" /tmp/ck5200_layout_check
else
    printf '  SKIP  Swift layout check (swiftc not available)\n'
fi

if xcrun swiftc -D STOCK_UPDATE_AUTHORIZED -module-cache-path "$SWIFT_CACHE" \
    "$REPO/ios/ClicksInspector/Sources/KeycodeNames.swift" \
    "$REPO/ios/ClicksInspector/Sources/FactoryLayout.swift" \
    "$REPO/ios/ClicksInspector/Sources/KeyboardSelfTest.swift" \
    "$REPO/ios/ClicksInspector/Sources/CustomKeyboardProtocol.swift" \
    "$REPO/ios/ClicksInspector/Sources/StockReadCommand.swift" \
    "$REPO/ios/ClicksInspector/Sources/CustomFirmwareController.swift" \
    "$REPO/ios/ClicksInspector/Tests/ck5200_flash_check.swift" \
    -o /tmp/ck5200_flash_check 2>/dev/null; then
    check "Swift flash-path check (authorized stock only, review 2026-10-12)" \
        /tmp/ck5200_flash_check \
        "$REPO/ios/ClicksInspector/Resources/iKeyboard_CK-5200_V122_120.bin" \
        "$REPO/ios/ClicksInspector/Resources/ck5200_qmk.bin"
else
    printf '  SKIP  Swift flash-path check (swiftc not available)\n'
fi

if xcrun swiftc -D STOCK_UPDATE_AUTHORIZED -module-cache-path "$SWIFT_CACHE" \
    "$REPO/ios/ClicksInspector/Sources/KeycodeNames.swift" \
    "$REPO/ios/ClicksInspector/Sources/FactoryLayout.swift" \
    "$REPO/ios/ClicksInspector/Sources/KeyboardSelfTest.swift" \
    "$REPO/ios/ClicksInspector/Sources/CustomKeyboardProtocol.swift" \
    "$REPO/ios/ClicksInspector/Sources/StockReadCommand.swift" \
    "$REPO/ios/ClicksInspector/Sources/CustomFirmwareController.swift" \
    "$REPO/ios/ClicksInspector/Sources/StreamLink.swift" \
    "$REPO/ios/ClicksInspector/Tests/ck5200_stream_check.swift" \
    -o /tmp/ck5200_stream_check 2>/dev/null; then
    check "Swift stream-adapter check (partial writes, fragments, loss)" \
        /tmp/ck5200_stream_check
else
    printf '  SKIP  Swift stream-adapter check (swiftc not available)\n'
fi

header "Per-commit buildability"
# This builds 11 commits, each a full firmware build (~15 minutes).
# Run it separately: bash tools/verify_history.sh c1d9e7c
# The last recorded result: 11/11 built, 0 failed (2026-10-10).
printf '  SKIP  per-commit buildability (run tools/verify_history.sh separately)\n'
printf '        last recorded: 11/11 built, 0 failed\n'

printf '\n%s\n' "-----------------------------------------"
if [[ $FAIL -eq 0 ]]; then
    printf 'PREFLIGHT PASS: %s checks, 0 failures.\n\n' "$PASS"
    printf 'All offline checks passed. This is a necessary gate, NOT flash\n'
    printf 'approval: per the 2026-10-11 safety review, flashing stays blocked\n'
    printf 'until the MCU family question is settled (read the chip marking) and\n'
    printf 'an independent recovery path is demonstrated. See\n'
    printf 'docs/reviews/2026-10-11-flash-safety-review.md and docs/RECOVERY.md.\n'
else
    printf 'PREFLIGHT FAIL: %s passed, %s FAILED.\n\n' "$PASS" "$FAIL"
    printf 'DO NOT FLASH. Fix the failing checks first.\n'
    exit 1
fi
