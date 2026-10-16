#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$(mktemp -d "${TMPDIR:-/tmp}/ck5200-ios-tests.XXXXXX")"
trap 'rm -rf "$BUILD"' EXIT
cd "$ROOT"
COMMON=(Sources/KeycodeNames.swift Sources/FactoryLayout.swift Sources/KeyboardSelfTest.swift Sources/CustomKeyboardProtocol.swift Sources/StockReadCommand.swift Sources/KeymapProfile.swift)
run() {
    local name="$1"
    shift
    echo "RUN $name"
    xcrun swiftc -module-cache-path "$BUILD/cache" "$@" "Tests/$name.swift" -o "$BUILD/$name"
    "$BUILD/$name"
}
run StockReadCommandTests Sources/StockReadCommand.swift
run StockVersionRegression Sources/StockReadCommand.swift
EVIDENCE=(Sources/StockReadCommand.swift Sources/StockEvidence.swift Sources/StockEvidencePrivacy.swift Sources/StockTrial.swift Sources/FactoryLayout.swift Sources/KeycodeNames.swift)
run StockEvidenceTests "${EVIDENCE[@]}"
run StockTrialTests "${EVIDENCE[@]}"
run RecorderBenchmark "${EVIDENCE[@]}"
run ck5200_protocol_check Sources/CustomKeyboardProtocol.swift
run ck5200_layout_check "${COMMON[@]}"
run ck5200_stream_check -D STOCK_UPDATE_AUTHORIZED "${COMMON[@]}" Sources/CustomFirmwareController.swift Sources/StreamLink.swift
echo "RUN DiagnosticsOnlyTests (default build)"
xcrun swiftc -D DIAGNOSTICS_BOUNDARY_TEST "${COMMON[@]}" Sources/CustomFirmwareController.swift Tests/DiagnosticsOnlyTests.swift -o "$BUILD/diagnostics"
"$BUILD/diagnostics" Resources/iKeyboard_CK-5200_V122_120.bin
echo "RUN ck5200_flash_check"
xcrun swiftc -D STOCK_UPDATE_AUTHORIZED -module-cache-path "$BUILD/cache" "${COMMON[@]}" Sources/CustomFirmwareController.swift \
    Tests/ck5200_flash_check.swift -o "$BUILD/flash"
"$BUILD/flash" Resources/iKeyboard_CK-5200_V122_120.bin Resources/ck5200_qmk.bin
echo "RUN QMKProfileTests (production C handler and pinned QMK)"
bash Tests/build-qmk-bridge.sh "$BUILD"
xcrun swiftc -D QMK_RAM_KEYMAP -module-cache-path "$BUILD/cache" -import-objc-header Tests/QMKBridge.h \
    "${COMMON[@]}" Sources/CustomFirmwareController.swift Tests/QMKProfileTests.swift \
    -L "$BUILD" -lQMKBridge -o "$BUILD/qmk-profiles"
"$BUILD/qmk-profiles"
