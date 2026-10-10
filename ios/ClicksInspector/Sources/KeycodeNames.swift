import Foundation

/// Readable names for QMK keycodes, used by the keymap editor, the layout
/// preview, and the self-test report. Host-testable: pure functions.
enum KeycodeNames {
    // MARK: Basic HID usages (stock 4KRO range plus the custom set)

    static let basicKeys: [UInt16: String] = {
        var map: [UInt16: String] = [:]
        // Letters a-z: 0x04..0x1d
        for (index, letter) in "abcdefghijklmnopqrstuvwxyz".enumerated() {
            map[UInt16(0x04 + index)] = String(letter).uppercased()
        }
        // Digits 1-0: 0x1e..0x27
        for (index, digit) in "1234567890".enumerated() {
            map[UInt16(0x1e + index)] = String(digit)
        }
        // Enter 0x28, Escape 0x29, Backspace 0x2a, Tab 0x2b, Space 0x2c
        map[0x28] = "Enter"
        map[0x29] = "Esc"
        map[0x2a] = "Bksp"
        map[0x2b] = "Tab"
        map[0x2c] = "Space"
        // Punctuation and symbols
        map[0x2d] = "-"
        map[0x2e] = "="
        map[0x2f] = "["
        map[0x30] = "]"
        map[0x31] = "\\"
        map[0x33] = ";"
        map[0x34] = "'"
        map[0x36] = ","
        map[0x37] = "."
        map[0x38] = "/"
        map[0x39] = "CapsLk"
        return map
    }()

    // MARK: Modifiers

    static let modifiers: [UInt16: String] = [
        0xe0: "LCtrl", 0xe1: "LShift", 0xe2: "LAlt", 0xe3: "LGui",
        0xe4: "RCtrl", 0xe5: "RShift", 0xe6: "RAlt", 0xe7: "RGui",
    ]

    // MARK: QMK layer keycodes

    /// MO(n): momentary layer. QMK encoding: 0x5120 | (n & 0xf).
    static func isMomentaryLayer(_ keycode: UInt16) -> Bool {
        (keycode & 0xfff0) == 0x5120
    }

    static func momentaryLayerNumber(_ keycode: UInt16) -> Int? {
        guard isMomentaryLayer(keycode) else { return nil }
        return Int(keycode & 0x0f)
    }

    /// OSL(n): one-shot layer. QMK encoding: QK_ONE_SHOT_LAYER (0x5280)
    /// | (n & 0x1f), so the check is (keycode & 0xffe0) == 0x5280.
    static func isOneShotLayer(_ keycode: UInt16) -> Bool {
        (keycode & 0xffe0) == 0x5280
    }

    static func oneShotLayerNumber(_ keycode: UInt16) -> Int? {
        guard isOneShotLayer(keycode) else { return nil }
        return Int(keycode & 0x1f)
    }

    /// LT(layer, key): tap key / hold layer.
    /// QMK encoding: 0x4000 | (layer << 8) | key, layers 0..7.
    static func isLayerTap(_ keycode: UInt16) -> Bool {
        (keycode & 0xf000) == 0x4000 && keycode != 0x4000
    }

    static func layerTapInfo(_ keycode: UInt16) -> (layer: Int, key: UInt16)? {
        guard isLayerTap(keycode) else { return nil }
        return (Int((keycode >> 8) & 0xf), keycode & 0xff)
    }

    // MARK: Custom keycodes (this project's firmware)

    static let customKeys: [UInt16: String] = [
        0x5da1: "Cons-A",       // stock consumer usage 0x01AE
        0x5da2: "Cons-B",       // stock consumer usage 0x029D
        0x5da3: "iOS-KB",       // consumer Eject 0x00B8: iOS on-screen keyboard
    ]

    // MARK: Special values

    static let transparent: UInt16 = 0x0001 // KC_TRNS
    static let noKey: UInt16 = 0x0000        // KC_NO

    // MARK: Name resolution

    /// Returns a readable name for any keycode used in this project's
    /// keymap, or a hex string for unknown values.
    static func name(for keycode: UInt16) -> String {
        if keycode == transparent { return "—" }
        if keycode == noKey { return "✕" }
        if let name = basicKeys[keycode] { return name }
        if let name = modifiers[keycode] { return name }
        if let name = customKeys[keycode] { return name }
        if let layer = momentaryLayerNumber(keycode) {
            return "MO(\(layer))"
        }
        if let layer = oneShotLayerNumber(keycode) {
            return "OSL(\(layer))"
        }
        if let info = layerTapInfo(keycode) {
            let keyName = basicKeys[info.key] ?? String(format: "%02x", info.key)
            return "LT\(info.layer)·\(keyName)"
        }
        return String(format: "%04x", keycode)
    }

    /// Short form for the layout grid (max ~4 characters).
    static func shortName(for keycode: UInt16) -> String {
        let full = name(for: keycode)
        if full.count <= 5 { return full }
        if let layer = momentaryLayerNumber(keycode) { return "MO\(layer)" }
        if let layer = oneShotLayerNumber(keycode) { return "OS\(layer)" }
        if let info = layerTapInfo(keycode) {
            let keyName = basicKeys[info.key] ?? String(format: "%02x", info.key)
            return "L\(info.layer)·\(keyName.prefix(2))"
        }
        return String(full.prefix(4))
    }
}
