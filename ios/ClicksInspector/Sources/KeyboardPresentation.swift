import Foundation

/// Cap geometry follows the vendor's iPhone 15 product photograph.
/// This is presentation data, not a second firmware keymap implementation.
enum KeyboardPresentation {
    struct Key: Identifiable {
        let id: String
        let position: Int?
        var width: Double = 1
    }
    static let layers = ["Base", "Symbols", "Navigation"]
    static let rows: [[Key]] = [
        zip(["Q","W","E","R","T","Y","U","I","O","P"], [0,6,12,18,24,30,1,7,13,19]).map { Key(id: $0, position: $1) },
        zip(["A","S","D","F","G","H","J","K","L","Delete"], [25,31,8,14,20,26,32,2,9,3]).map { Key(id: $0, position: $1) },
        zip(["Shift","Z","X","C","V","B","N","M","Return"], [15,21,27,33,10,16,22,28,4]).map { Key(id: $0, position: $1, width: $0 == "Return" ? 2 : 1) },
        [Key(id: "123", position: 34), Key(id: "Globe", position: nil), Key(id: "Cmd", position: 17),
         Key(id: "Space", position: 23, width: 3), Key(id: "Clicks", position: nil),
         Key(id: "Shortcuts", position: nil), Key(id: "Microphone", position: nil)]
    ]
    // Firmware identifies these switches, but not their printed cap assignment.
    static let unverifiedSystemPositions = [5,11,29,35]
    static func keyName(_ position: Int) -> String {
        rows.flatMap { $0 }.first { $0.position == position }?.id
            ?? [5: "System action A", 11: "System action B", 29: "Native Control", 35: "System action C"][position]!
    }

    static let usageNames: [UInt16: String] = {
        var names = KeycodeNames.basicKeys
        names.merge([0: "No key", 0x28: "Return", 0x29: "Escape", 0x2a: "Delete", 0x39: "Caps Lock",
                     0x49: "Insert", 0x4a: "Home", 0x4b: "Page Up", 0x4c: "Forward Delete", 0x4d: "End",
                     0x4e: "Page Down", 0x4f: "Right Arrow", 0x50: "Left Arrow", 0x51: "Down Arrow", 0x52: "Up Arrow",
                     0xe0: "Control", 0xe1: "Shift", 0xe2: "Option", 0xe3: "Command"]) { _, new in new }
        for i in 0..<12 { names[UInt16(0x3a + i)] = "F\(i + 1)" }
        return names
    }()

    static func actionName(_ action: StockExtensionMap.Action, layer: Int, position: Int) -> String {
        if action.kind == 3 {
            if layer == 0 { return keyName(position) }
            if position == 32 { return "Native setting" }
            if position == 21 && layer == 1 { return "Native symbol" }
            let value = FactoryLayout.allLayers[layer][position]
            if value == 1 { return keyName(position) }
            if let name = usageNames[value] { return name }
            if value & 0xff00 == 0x0200 { return "Shift + \(usageNames[value & 255] ?? "symbol")" }
            return "Native action"
        }
        if action.kind == 1 { return "No action" }
        if action.kind == 2 { return "Use Base" }
        if action.activation != 0 {
            return "\(["", "Tap / hold", "Hold", "Toggle", "One-shot"][Int(action.activation)]) \(layers[Int(action.target)])"
        }
        let modifiers = (0..<4).filter { action.mods & (1 << $0) != 0 }.map { ["Ctrl", "Shift", "Option", "Cmd"][$0] }
        let key = usageNames[action.usage] ?? "Custom key"
        return (modifiers + (action.usage == 0 && !modifiers.isEmpty ? [] : [key])).joined(separator: " + ")
    }

    struct Option: Identifiable {
        let category: String
        let name: String
        let action: StockExtensionMap.Action
        var id: String { category + "." + name }
    }
    static let categories = ["Keys", "Navigation", "Modifiers", "Shortcuts", "Layers", "Native actions"]
    static let catalog: [Option] = {
        var choices = usageNames.keys.sorted().filter { $0 != 0 }.map { usage in
            Option(category: usage >= 0xe0 ? "Modifiers" : (0x49...0x52).contains(usage) ? "Navigation" : "Keys",
                   name: usageNames[usage]!, action: usage >= 0xe0 ? .init(mods: UInt8(1 << (usage - 0xe0))) : .init(usage: usage))
        }
        for (name, usage) in [("Copy", UInt16(6)), ("Cut", 0x1b), ("Paste", 0x19), ("Undo", 0x1d), ("Find", 9), ("Select All", 4), ("Spotlight", 0x2c)] {
            choices.append(Option(category: "Shortcuts", name: name, action: .init(usage: usage, mods: 8)))
        }
        for layer in UInt8(0)..<3 {
            for mode in UInt8(1)...4 {
                let name = ["", "Tap Tab / hold", "Hold", "Toggle", "One-shot"][Int(mode)] + " " + layers[Int(layer)]
                choices.append(Option(category: "Layers", name: name,
                                      action: .init(activation: mode, target: layer, tapUsage: mode == 1 ? 0x2b : 0)))
            }
        }
        choices += [Option(category: "Native actions", name: "Keep keyboard's native action", action: .oem),
                    Option(category: "Native actions", name: "Use Base layer action", action: .init(kind: 2)),
                    Option(category: "Native actions", name: "No action", action: .init(kind: 1))]
        return choices
    }()
}
