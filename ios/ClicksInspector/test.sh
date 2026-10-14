#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$(mktemp -d "${TMPDIR:-/tmp}/ck5200-ios-tests.XXXXXX")"
trap 'rm -rf "$BUILD"' EXIT
cd "$ROOT"
COMMON=(Sources/KeycodeNames.swift Sources/FactoryLayout.swift Sources/KeyboardSelfTest.swift Sources/CustomKeyboardProtocol.swift)
run() {
    local name="$1"
    shift
    echo "RUN $name"
    xcrun swiftc -module-cache-path "$BUILD/cache" "$@" "Tests/$name.swift" -o "$BUILD/$name"
    "$BUILD/$name"
}
run StockReadCommandTests Sources/StockReadCommand.swift
run StockEvidenceTests Sources/StockReadCommand.swift Sources/StockEvidence.swift Sources/StockEvidencePrivacy.swift
run ck5200_protocol_check Sources/CustomKeyboardProtocol.swift
run ck5200_layout_check "${COMMON[@]}"
run ck5200_stream_check "${COMMON[@]}" Sources/CustomFirmwareController.swift Sources/StreamLink.swift
echo "RUN ck5200_flash_check"
xcrun swiftc -module-cache-path "$BUILD/cache" "${COMMON[@]}" Sources/CustomFirmwareController.swift \
    Tests/ck5200_flash_check.swift -o "$BUILD/flash"
"$BUILD/flash" Resources/iKeyboard_CK-5200_V122_120.bin Resources/ck5200_qmk.bin
