import SwiftUI

struct StockExtensionView: View {
    @ObservedObject var inspector: AccessoryInspector
    @ObservedObject var editor: StockExtensionController
    @State private var layer = 0
    @State private var position: Position?
    @State private var profileName = ""
    #if targetEnvironment(simulator)
    @State private var emulator: V122EmulatorLink?
    @State private var hidStatus = "No emulator HID check yet."
    @State private var emulatorControlBusy = false
    #endif
    private struct Position: Identifiable { let id: Int }
    private let layers = ["Base", "Symbols", "Navigation"]
    // Names identify physical switches by their original Base legends.
    private let legends = ["Q","U","K","Backspace","Enter","Cons A",
                           "W","I","D","L","V","Cons B",
                           "E","O","F","Shift","B","Cmd",
                           "R","P","G","Z","N","Space",
                           "T","A","H","X","M","Ctrl",
                           "Y","S","J","C","SYM","iOS KB"]

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("Stock-derived extension. Three layers, 36 positions each. RAM is lost on power loss. Phone profiles are kept.")
                    .font(.footnote)
                Text("Physical commands are blocked. Emulator success is not physical accessory compatibility.")
                    .font(.footnote).foregroundStyle(.orange)
                Text(editor.status).accessibilityIdentifier("extension.status")
                #if targetEnvironment(simulator)
                HStack {
                    Button("Connect V122 emulator") { connectEmulator() }
                        .accessibilityIdentifier("extension.connect")
                        .disabled(editor.ownsSession || editor.busy || emulatorControlBusy || !inspector.canOpenExtensionEditor)
                    Button("Disconnect") { inspector.closeSession(reason: "Disconnect editor emulator") }
                        .accessibilityIdentifier("extension.disconnect")
                }
                #endif
                Picker("Layer", selection: $layer) {
                    ForEach(0..<3) { Text(layers[$0]).tag($0) }
                }.pickerStyle(.segmented).accessibilityIdentifier("extension.layer")
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 3), count: 6), spacing: 6) {
                    ForEach(0..<36) { pos in
                        Button { position = Position(id: pos) } label: {
                            VStack(spacing: 3) {
                                Text(legends[pos]).font(.caption2).lineLimit(1).minimumScaleFactor(0.5)
                                Text(editor.draft.actions[layer * 36 + pos].label).font(.caption2).lineLimit(1).minimumScaleFactor(0.5)
                                Text("\(pos / 6),\(pos % 6)").font(.system(size: 8)).foregroundStyle(.secondary)
                            }.frame(maxWidth: .infinity, minHeight: 48)
                                .background(Color.accentColor.opacity(0.1)).clipShape(RoundedRectangle(cornerRadius: 5))
                        }.buttonStyle(.plain)
                            .accessibilityLabel("\(legends[pos]) \(editor.draft.actions[layer * 36 + pos].label)")
                            .accessibilityIdentifier("extension.cell.\(pos)")
                    }
                }.disabled(editor.busy)
                GroupBox("Native activation") {
                    VStack(alignment: .leading) {
                        nativeSwitch("Native Symbols", bit: 2)
                        nativeSwitch("Native Navigation", bit: 4)
                        Text("Priority: custom hold, one-shot, toggle, native Navigation, native Symbols, Base. Custom activation can target Base.")
                            .font(.footnote)
                    }
                }.disabled(editor.busy)
                Toggle("Enable RAM remapping", isOn: $editor.enabled).disabled(editor.busy)
                Text(editor.connected ? (editor.matchesRAM ? "Draft matches verified RAM." : "Draft differs from verified RAM.") : "No verified RAM map.")
                    .accessibilityIdentifier("extension.comparison")
                HStack {
                    Button("Apply draft to RAM") { Task { await editor.apply() } }
                        .disabled(!editor.canApply || emulatorControlsBusy).accessibilityIdentifier("extension.apply")
                    Button("Use readback") { editor.useReadback() }.disabled(editor.verified == nil || editor.busy)
                }
                GroupBox("Named iPhone profiles") {
                    VStack(alignment: .leading, spacing: 10) {
                        TextField("Profile name", text: $profileName).textFieldStyle(.roundedBorder)
                            .accessibilityIdentifier("extension.profileName")
                        Button("Save draft on iPhone") { editor.saveProfile(name: profileName) }
                            .disabled(editor.busy || !editor.storageAvailable)
                            .accessibilityIdentifier("extension.save")
                        ForEach(editor.profiles) { profile in
                            Button("Load \(profile.name)") { editor.loadProfile(profile) }
                                .disabled(editor.busy).accessibilityIdentifier("extension.profile.\(profile.name)")
                        }
                        Text("Loading changes the draft only. Reconnect never applies a profile. After RAM loss, load a profile and press Apply.")
                            .font(.footnote)
                    }
                }
                #if targetEnvironment(simulator)
                GroupBox("Actual V122 execution, emulator only") {
                    VStack(alignment: .leading, spacing: 10) {
                        Button("Check NAV+Q Esc and SYM+E Tab") {
                            Task {
                                if let emulator { hidStatus = await editor.verifyEmulator(emulator) }
                            }
                        }.disabled(!editor.canApply || emulatorControlBusy).accessibilityIdentifier("extension.hid")
                        Text(hidStatus).font(.footnote).accessibilityIdentifier("extension.hidStatus")
                        Button("Lose next APPLY acknowledgement") {
                            emulatorControlBusy = true
                            Task {
                                defer { emulatorControlBusy = false }
                                do { try await emulator?.control("lose-ack"); hidStatus = "Next APPLY reply will be lost." }
                                catch { hidStatus = "\(error)" }
                            }
                        }.disabled(!editor.canApply || emulatorControlBusy).accessibilityIdentifier("extension.loseAck")
                        Button("Emulate power loss") {
                            inspector.closeSession(reason: "Emulated RAM loss")
                            emulatorControlBusy = true
                            Task {
                                defer { emulatorControlBusy = false }
                                do { try await emulator?.control("power"); hidStatus = "RAM lost. Reconnect, load a profile, then apply explicitly." }
                                catch { hidStatus = "\(error)" }
                            }
                        }.disabled(editor.busy || emulatorControlBusy).accessibilityIdentifier("extension.power")
                    }
                }
                #endif
            }.padding()
        }
        .navigationTitle("Extension RAM editor")
        .sheet(item: $position) { pos in
            StockExtensionActionView(action: editor.draft.actions[layer * 36 + pos.id], title: "\(layers[layer]) / \(legends[pos.id])") { action in
                try editor.draft.set(action, layer: layer, row: pos.id / 6, column: pos.id % 6)
            }
        }
    }

    private var emulatorControlsBusy: Bool {
        #if targetEnvironment(simulator)
        emulatorControlBusy
        #else
        false
        #endif
    }

    private func nativeSwitch(_ title: String, bit: UInt8) -> some View {
        Toggle(title, isOn: Binding(get: { editor.draft.nativeMask & bit != 0 }, set: {
            if $0 { editor.draft.nativeMask |= bit } else { editor.draft.nativeMask &= ~bit }
        }))
    }

    #if targetEnvironment(simulator)
    private func connectEmulator() {
        guard inspector.claimExtensionEmulator() else { return }
        let link = V122EmulatorLink()
        emulator = link
        Task { await editor.connect(link, emulated: true) }
    }
    #endif
}

private struct StockExtensionActionView: View {
    @Environment(\.dismiss) private var dismiss
    @State var action: StockExtensionMap.Action
    let title: String
    let save: (StockExtensionMap.Action) throws -> Void
    @State private var usage = ""
    @State private var tapUsage = ""
    @State private var milliseconds = ""
    @State private var error = ""

    var body: some View {
        NavigationStack {
            Form {
                Picker("Action", selection: $action.kind) {
                    Text("Key / shortcut / activation").tag(UInt8(0))
                    Text("Disabled").tag(UInt8(1))
                    Text("Transparent").tag(UInt8(2))
                    Text("OEM native action").tag(UInt8(3))
                }.accessibilityIdentifier("extension.kind")
                if action.kind == 0 {
                    Picker("Activation", selection: $action.activation) {
                        Text("None").tag(UInt8(0))
                        Text("Tap-hold").tag(UInt8(1))
                        Text("Hold").tag(UInt8(2))
                        Text("Toggle").tag(UInt8(3))
                        Text("One-shot").tag(UInt8(4))
                    }.accessibilityIdentifier("extension.activation")
                    if action.activation == 0 {
                        HStack {
                            Button("Esc") { usage = "29" }.accessibilityIdentifier("extension.esc")
                            Button("Tab") { usage = "2b" }.accessibilityIdentifier("extension.tab")
                        }.buttonStyle(.bordered)
                        TextField("HID usage in hex, 00..ff", text: $usage)
                            .accessibilityIdentifier("extension.usage")
                        Text("00 plus a modifier creates a modifier-only key. Use a key plus modifiers for shortcuts.").font(.footnote)
                    } else {
                        Picker("Target layer", selection: $action.target) {
                            Text("Base").tag(UInt8(0)); Text("Symbols").tag(UInt8(1)); Text("Navigation").tag(UInt8(2))
                        }
                        if action.activation == 1 {
                            TextField("Tap HID usage in hex", text: $tapUsage)
                            TextField("Hold threshold in milliseconds", text: $milliseconds).keyboardType(.numberPad)
                            Text("The target layer applies while pressed. Release before the threshold without another key to send the tap key. 0 uses 200 ms. Tap output has no modifiers.")
                        }
                        if action.activation == 2 { Text("Select the target while pressed. Release restores the previous layer.") }
                        if action.activation == 3 { Text("Press to select the target. Press this activator again to remove its toggle.") }
                        if action.activation == 4 { Text("Select the target for the next ordinary key press. Expires after 1000 ms. Native gesture keys do not consume it.") }
                    }
                    if action.activation == 0 {
                        ForEach(0..<4) { index in
                            Toggle(["Ctrl", "Shift", "Alt", "Cmd"][index], isOn: Binding(get: {
                                action.mods & (1 << index) != 0
                            }, set: {
                                if $0 { action.mods |= 1 << index } else { action.mods &= ~(1 << index) }
                            }))
                        }
                    }
                }
                Text("OEM keeps the stock action. Transparent uses Base at this position, not Symbols under Navigation. Transparent on Base and Disabled send nothing.").font(.footnote)
                Text(error).foregroundStyle(.red)
                Button("Save position") { commit() }.accessibilityIdentifier("extension.savePosition")
            }.navigationTitle(title)
                .toolbar { Button("Cancel") { dismiss() } }
                .onAppear {
                    usage = String(action.usage, radix: 16)
                    tapUsage = String(action.tapUsage, radix: 16)
                    milliseconds = String(action.tapMilliseconds)
                }
        }
    }

    private func commit() {
        do {
            var next = StockExtensionMap.Action(kind: action.kind)
            if next.kind == 0 {
                next.mods = action.activation == 0 ? action.mods : 0
                next.activation = action.activation
                if next.activation == 0 {
                    guard let value = UInt16(usage, radix: 16), value <= 255 else { throw StockExtensionError.invalidRecord }
                    next.usage = value
                } else {
                    next.target = action.target
                    if next.activation == 1 {
                        guard let tap = UInt16(tapUsage, radix: 16), tap <= 255,
                              let delay = UInt16(milliseconds) else { throw StockExtensionError.invalidRecord }
                        next.tapUsage = tap
                        next.tapMilliseconds = delay
                    }
                }
            }
            try save(next)
            dismiss()
        } catch { self.error = "Invalid action. Use HID 00..ff and a threshold from 0 to 65535." }
    }
}

extension StockExtensionMap.Action {
    var label: String {
        if kind == 3 { return "OEM" }
        if kind == 2 { return "Transparent" }
        if kind == 1 { return "Disabled" }
        if activation != 0 { return "\(["", "Tap-hold", "Hold", "Toggle", "One-shot"][Int(activation)]) L\(target)" }
        let key = usage == 0x29 ? "Esc" : usage == 0x2b ? "Tab" : String(format: "%02x", usage)
        let modifiers = (0..<4).filter { mods & (1 << $0) != 0 }.map { ["Ctrl", "Shift", "Alt", "Cmd"][$0] }
        return (modifiers + [key]).joined(separator: "+")
    }
}
