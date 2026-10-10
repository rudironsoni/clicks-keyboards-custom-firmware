/*
 * Runnable host checks for the iOS app's keycode names, factory layout,
 * and self-test logic. Compile and run:
 *   xcrun swiftc -module-cache-path /tmp/ck5200-swift-cache \
 *     ios/ClicksInspector/Sources/KeycodeNames.swift \
 *     ios/ClicksInspector/Sources/FactoryLayout.swift \
 *     ios/ClicksInspector/Sources/KeyboardSelfTest.swift \
 *     ios/ClicksInspector/Sources/CustomKeyboardProtocol.swift \
 *     ios/ClicksInspector/Tests/ck5200_layout_check.swift \
 *     -o /tmp/ck5200_layout_check && /tmp/ck5200_layout_check
 */

import Foundation

private func expectEqual<T: Equatable>(_ what: String, _ actual: T, _ expected: T) {
    precondition(actual == expected, "\(what): expected \(expected), got \(actual)")
}

@main
struct LayoutCheck {
    static func main() {
        // KeycodeNames: basic keys.
        expectEqual("KC_A", KeycodeNames.name(for: 0x04), "A")
        expectEqual("KC_Z", KeycodeNames.name(for: 0x1d), "Z")
        expectEqual("KC_1", KeycodeNames.name(for: 0x1e), "1")
        expectEqual("KC_0", KeycodeNames.name(for: 0x27), "0")
        expectEqual("KC_SPACE", KeycodeNames.name(for: 0x2c), "Space")
        expectEqual("KC_BKSP", KeycodeNames.name(for: 0x2a), "Bksp")
        expectEqual("KC_ENTER", KeycodeNames.name(for: 0x28), "Enter")

        // KeycodeNames: modifiers.
        expectEqual("KC_LGUI", KeycodeNames.name(for: 0xe3), "LGui")
        expectEqual("KC_LCTL", KeycodeNames.name(for: 0xe0), "LCtrl")

        // KeycodeNames: layer keys.
        expectEqual("MO(1)", KeycodeNames.name(for: 0x5121), "MO(1)")
        expectEqual("MO(2)", KeycodeNames.name(for: 0x5122), "MO(2)")
        expectEqual("OSL(1)", KeycodeNames.name(for: 0x5281), "OSL(1)")
        expectEqual("LT(2,J)", KeycodeNames.name(for: 0x420d), "LT2·J")

        // KeycodeNames: special values and custom keys.
        expectEqual("KC_TRNS", KeycodeNames.name(for: 0x0001), "—")
        expectEqual("KC_NO", KeycodeNames.name(for: 0x0000), "✕")
        expectEqual("iOS-KB", KeycodeNames.name(for: 0x5da3), "iOS-KB")

        // KeycodeNames: unknown values fall back to hex.
        expectEqual("unknown hex", KeycodeNames.name(for: 0xabcd), "abcd")

        // KeycodeNames: layer detection.
        expectEqual("is MO", KeycodeNames.isMomentaryLayer(0x5121), true)
        expectEqual("is not MO", KeycodeNames.isMomentaryLayer(0x04), false)
        expectEqual("is OSL", KeycodeNames.isOneShotLayer(0x5281), true)
        expectEqual("is LT", KeycodeNames.isLayerTap(0x420d), true)
        expectEqual("LT layer", KeycodeNames.layerTapInfo(0x420d)?.layer ?? -1, 2)
        expectEqual("LT key", KeycodeNames.layerTapInfo(0x420d)?.key ?? 0, 0x0d)

        // FactoryLayout: the base layer matches the decoded stock table.
        expectEqual("L0 count", FactoryLayout.layer0.count, 36)
        expectEqual("L1 count", FactoryLayout.layer1.count, 36)
        expectEqual("L2 count", FactoryLayout.layer2.count, 36)
        expectEqual("L0 (0,0) = q", FactoryLayout.layer0[0], 0x0004)
        expectEqual("L0 (2,3) = spare", FactoryLayout.layer0[2 * 6 + 3], 0x0000)
        expectEqual("L0 (2,5) = Cmd", FactoryLayout.layer0[2 * 6 + 5], 0xe3)
        expectEqual("L0 (4,5) = Ctrl", FactoryLayout.layer0[4 * 6 + 5], 0xe0)
        expectEqual("L0 (5,4) = OSL(1)", FactoryLayout.layer0[5 * 6 + 4], 0x5281)
        expectEqual("L0 (5,2) = LT(2,J)", FactoryLayout.layer0[5 * 6 + 2], 0x420d)
        expectEqual("L0 (5,5) = iOS-KB", FactoryLayout.layer0[5 * 6 + 5], 0x5da3)
        expectEqual("L1 (0,0) = 1", FactoryLayout.layer1[0], 0x001e)
        expectEqual("L2 (0,2) = Down", FactoryLayout.layer2[2], 0x0051)
        expectEqual("L2 (1,0) = Up", FactoryLayout.layer2[6], 0x0052)
        expectEqual("L2 (4,1) = Left", FactoryLayout.layer2[4 * 6 + 1], 0x0050)

        // Self-test: keymap verification with a matching keymap passes.
        let goodReport = KeyboardSelfTest.verifyKeymap(FactoryLayout.asRowMajor)
        expectEqual("good keymap passes", goodReport.passed, true)
        expectEqual("good keymap one summary check", goodReport.totalCount, 1)

        // Self-test: a single mismatch produces exactly one failing check.
        var corrupted = FactoryLayout.asRowMajor
        corrupted[0][0][0] = 0x002c // (0,0) becomes Space instead of q
        let badReport = KeyboardSelfTest.verifyKeymap(corrupted)
        expectEqual("bad keymap fails", badReport.passed, false)
        expectEqual("bad keymap one mismatch", badReport.checks.count, 1)
        expectEqual("mismatch detail names the position",
                    badReport.checks[0].name.hasPrefix("L0 (0,0)"), true)

        // Self-test: identify verification.
        let goodIdentify = KeyboardSelfTest.verifyIdentify([0x51, 0x4d, 0x00, 0x01])
        expectEqual("custom identify passes", goodIdentify.passed, true)
        let stockIdentify = KeyboardSelfTest.verifyIdentify([0x01, 0x20, 0x01, 0x22])
        expectEqual("stock identify fails", stockIdentify.passed, false)

        // Self-test: info verification.
        let goodInfo = KeyboardSelfTest.verifyInfo(KeyboardInfo(layers: 4, rows: 6, columns: 6))
        expectEqual("correct info passes", goodInfo.passed, true)
        let wrongInfo = KeyboardSelfTest.verifyInfo(KeyboardInfo(layers: 2, rows: 6, columns: 6))
        expectEqual("wrong info fails", wrongInfo.passed, false)

        // Self-test: round-trip verification.
        let goodRoundTrip = KeyboardSelfTest.verifyRoundTrip(
            written: 0x0014, readBack: 0x0014, restored: 0x0000, original: 0x0000)
        expectEqual("round-trip passes", goodRoundTrip.passed, true)
        let badRead = KeyboardSelfTest.verifyRoundTrip(
            written: 0x0014, readBack: 0x0000, restored: 0x0000, original: 0x0000)
        expectEqual("round-trip read-back failure", badRead.passed, false)
        let badRestore = KeyboardSelfTest.verifyRoundTrip(
            written: 0x0014, readBack: 0x0014, restored: 0x00ff, original: 0x0000)
        expectEqual("round-trip restore failure", badRestore.passed, false)

        print("ok")
    }
}
