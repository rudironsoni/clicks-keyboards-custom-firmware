import Foundation

/// One self-test check result.
struct SelfTestCheck: Identifiable, Equatable {
    let id = UUID()
    let name: String
    let passed: Bool
    let detail: String
}

/// The full self-test report.
struct SelfTestReport: Equatable {
    var checks: [SelfTestCheck] = []
    var passed: Bool { checks.allSatisfy(\.passed) }
    var passCount: Int { checks.filter(\.passed).count }
    var totalCount: Int { checks.count }
}

/// Drives the self-test sequence against the connected custom firmware.
/// Each check is one protocol exchange or a comparison; the controller
/// runs them in order and collects the report. Host-testable: the
/// expected exchanges and comparisons are pure data.
enum KeyboardSelfTest {
    /// The exchange sequence for a full self-test, in order.
    /// The controller maps each to its own perform() call.
    enum Step: Equatable {
        case identify                          // version -> "QM" 00 01
        case readInfo                          // info -> layers/rows/cols
        case readKey(layer: Int, row: Int, column: Int)
        case verifyKeymap                      // compare all reads to factory
        case roundTrip(layer: Int, row: Int, column: Int, keycode: UInt16)
        // roundTrip: write keycode, read back, restore original, read again
    }

    static let expectedInfo = KeyboardInfo(layers: 4, rows: 6, columns: 6)

    /// The default test sequence.
    static let steps: [Step] = [
        .identify,
        .readInfo,
        .verifyKeymap,
        // Round-trip on the spare crossing (2,3): write a distinctive
        // keycode, read it back, restore to KC_NO.
        .roundTrip(layer: 0, row: 2, column: 3, keycode: 0x0014),
    ]

    /// Verifies the keymap read back from the keyboard matches the
    /// decoded factory layout, position by position.
    static func verifyKeymap(_ actual: [[[UInt16]]]) -> SelfTestReport {
        var report = SelfTestReport()
        let expected = FactoryLayout.allLayers
        for layer in 0..<min(actual.count, expected.count) {
            for row in 0..<6 {
                for column in 0..<6 {
                    guard row < actual[layer].count, column < actual[layer][row].count else {
                        report.checks.append(SelfTestCheck(
                            name: "L\(layer) (\(row),\(column))",
                            passed: false,
                            detail: "Position missing"))
                        continue
                    }
                    let got = actual[layer][row][column]
                    let want = expected[layer][row * 6 + column]
                    if got != want {
                        report.checks.append(SelfTestCheck(
                            name: "L\(layer) (\(row),\(column))",
                            passed: false,
                            detail: "Expected \(KeycodeNames.name(for: want)), got \(KeycodeNames.name(for: got))"))
                    }
                }
            }
        }
        // Only record mismatches; matching positions are the pass case.
        if report.checks.isEmpty {
            report.checks.append(SelfTestCheck(
                name: "Factory keymap",
                passed: true,
                detail: "All \(expected.count) layers x 36 positions match the decoded layout"))
        }
        return report
    }

    /// Checks the identify payload.
    static func verifyIdentify(_ payload: [UInt8]) -> SelfTestCheck {
        let passed = CustomKeyboardIdentity.isCustomFirmware(payload)
        return SelfTestCheck(
            name: "Identify",
            passed: passed,
            detail: passed ? "Custom QMK firmware" : "Firmware did not identify as custom (got \(payload.map { String(format: "%02x", $0) }.joined(separator: " ")))")
    }

    /// Checks the info payload against the expected dimensions.
    static func verifyInfo(_ info: KeyboardInfo) -> SelfTestCheck {
        let passed = info == expectedInfo
        return SelfTestCheck(
            name: "Keymap dimensions",
            passed: passed,
            detail: passed ? "4 layers, 6x6 matrix" : "Expected 4 layers 6x6, got \(info.layers) layers \(info.rows)x\(info.columns)")
    }

    /// Checks a round-trip write: the read-back must return the written
    /// keycode, and the restore must return the original.
    static func verifyRoundTrip(written: UInt16, readBack: UInt16, restored: UInt16, original: UInt16) -> SelfTestCheck {
        let writeOk = written == readBack
        let restoreOk = restored == original
        let passed = writeOk && restoreOk
        let detail: String
        if passed {
            detail = "Write \(KeycodeNames.name(for: written)), read back, restored to \(KeycodeNames.name(for: original))"
        } else if !writeOk {
            detail = "Wrote \(String(format: "%04x", written)), read back \(String(format: "%04x", readBack))"
        } else {
            detail = "Restored to \(String(format: "%04x", restored)), expected \(String(format: "%04x", original))"
        }
        return SelfTestCheck(name: "Write round-trip", passed: passed, detail: detail)
    }
}
