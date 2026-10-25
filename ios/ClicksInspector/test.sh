#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$(mktemp -d "${TMPDIR:-/tmp}/ck5200-ios-tests.XXXXXX")"
trap 'rm -rf "$BUILD"' EXIT
cd "$ROOT"
COMMON=(Sources/KeycodeNames.swift Sources/FactoryLayout.swift Sources/KeyboardSelfTest.swift Sources/CustomKeyboardProtocol.swift Sources/StockReadCommand.swift Sources/KeymapProfile.swift)
CONTROLLER=(Sources/CustomFirmwareController.swift Sources/StockFirmwareTransaction.swift)
export STOCK_MANAGER_TEST_STORAGE="$BUILD/StockUpdate.json"
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
run StockExtensionProtocolTests Sources/StockExtensionProtocol.swift Sources/StockExtensionTransport.swift Sources/CustomKeyboardProtocol.swift Sources/StreamLink.swift
run StockExtensionEditorTests Sources/StockExtensionProtocol.swift Sources/StockExtensionTransport.swift Sources/CustomKeyboardProtocol.swift Sources/StockExtensionProfile.swift Sources/StockExtensionController.swift Sources/V122EmulatorLink.swift Sources/KeyboardPresentation.swift Sources/KeycodeNames.swift Sources/FactoryLayout.swift
run ck5200_layout_check "${COMMON[@]}"
run ck5200_stream_check -D STOCK_MANAGER_OFFLINE_TESTING "${COMMON[@]}" "${CONTROLLER[@]}" Sources/StreamLink.swift
echo "RUN DiagnosticsOnlyTests (default build)"
xcrun swiftc -D DIAGNOSTICS_BOUNDARY_TEST -module-cache-path "$BUILD/cache" "${COMMON[@]}" "${CONTROLLER[@]}" Tests/DiagnosticsOnlyTests.swift -o "$BUILD/diagnostics"
"$BUILD/diagnostics" Resources/iKeyboard_CK-5200_V122_120.bin
echo "RUN ck5200_flash_check"
xcrun swiftc -D STOCK_MANAGER_OFFLINE_TESTING -module-cache-path "$BUILD/cache" "${COMMON[@]}" "${CONTROLLER[@]}" \
    Tests/ck5200_flash_check.swift -o "$BUILD/flash"
"$BUILD/flash" Resources/iKeyboard_CK-5200_V122_120.bin Resources/ck5200_qmk.bin

# M4 StockFlashTrial: the dedicated iOS trial configuration must exercise the
# same guarded transaction flow, and its audit trail must record every
# exchange and lifecycle event byte-for-byte.
echo "RUN ck5200_flash_check (StockFlashTrial)"
xcrun swiftc -D STOCK_FLASH_TRIAL -module-cache-path "$BUILD/cache" "${COMMON[@]}" "${CONTROLLER[@]}" \
    Tests/ck5200_flash_check.swift -o "$BUILD/flash-trial"
"$BUILD/flash-trial" Resources/iKeyboard_CK-5200_V122_120.bin Resources/ck5200_qmk.bin

echo "RUN ck5200_flash_check (ExtensionInstaller)"
xcrun swiftc -D EXTENSION_INSTALLER -module-cache-path "$BUILD/cache" "${COMMON[@]}" "${CONTROLLER[@]}" \
    Tests/ck5200_flash_check.swift -o "$BUILD/extension-installer"
"$BUILD/extension-installer" Resources/ck5200_extension.bin Resources/iKeyboard_CK-5200_V122_120.bin

# Even the old write flag and the new manager configuration cannot write.
for FLAGS in STOCK_UPDATE_AUTHORIZED STOCK_FIRMWARE_MANAGER; do
    xcrun swiftc -D "$FLAGS" -D DIAGNOSTICS_BOUNDARY_TEST -module-cache-path "$BUILD/cache" \
        "${COMMON[@]}" "${CONTROLLER[@]}" Tests/DiagnosticsOnlyTests.swift -o "$BUILD/boundary-$FLAGS"
    "$BUILD/boundary-$FLAGS" Resources/iKeyboard_CK-5200_V122_120.bin
done
bash Tests/build-stock-manager-bridge.sh "$BUILD/libStockManagerBridge.a"
xcrun swiftc -D STOCK_MANAGER_OFFLINE_TESTING -module-cache-path "$BUILD/cache" \
    -import-objc-header Tests/StockManagerBridge.h "${COMMON[@]}" "${CONTROLLER[@]}" \
    Tests/StockManagerTests.swift "$BUILD/libStockManagerBridge.a" -o "$BUILD/stock-manager"
for MODE in 0 1 2; do
    "$BUILD/stock-manager" "$MODE" ok Resources/iKeyboard_CK-5200_V122_120.bin
    "$BUILD/stock-manager" "$MODE" unauthenticated Resources/iKeyboard_CK-5200_V122_120.bin
done
for FAULT in wrong-model interrupted expired commit-failure lost-a1 lost-a2 lost-a3 partial session-swap; do
    "$BUILD/stock-manager" 2 "$FAULT" Resources/iKeyboard_CK-5200_V122_120.bin
done
echo "RUN extension transfer through production C staging (modeled stock identity, not OEM installer proof)"
xcrun swiftc -D EXTENSION_INSTALLER -module-cache-path "$BUILD/cache" \
    -import-objc-header Tests/StockManagerBridge.h "${COMMON[@]}" "${CONTROLLER[@]}" \
    Tests/StockManagerTests.swift "$BUILD/libStockManagerBridge.a" -o "$BUILD/extension-staging"
for FAULT in ok unauthenticated wrong-model interrupted commit-failure lost-a1 lost-a2 lost-a3 partial session-swap; do
    "$BUILD/extension-staging" 0 "$FAULT" Resources/ck5200_extension.bin
done
echo "RUN QMKProfileTests (production C handler and pinned QMK)"
bash Tests/build-qmk-bridge.sh "$BUILD"
xcrun swiftc -D QMK_RAM_KEYMAP -module-cache-path "$BUILD/cache" -import-objc-header Tests/QMKBridge.h \
    "${COMMON[@]}" "${CONTROLLER[@]}" Tests/QMKProfileTests.swift \
    -L "$BUILD" -lQMKBridge -o "$BUILD/qmk-profiles"
"$BUILD/qmk-profiles"
