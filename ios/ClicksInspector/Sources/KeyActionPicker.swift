import SwiftUI

struct KeyActionPicker: View {
    @Environment(\.dismiss) private var dismiss
    @State var action: StockExtensionMap.Action
    let title: String
    let save: (StockExtensionMap.Action) throws -> Void
    @State private var search = ""
    @State private var searching = false
    @State private var error = ""
    @State private var rawUsage = ""
    @State private var tapKey: UInt16 = 0x2b
    @State private var delay = 200

    var body: some View {
        NavigationStack {
            List {
                Section("Selected action") {
                    Text(action.kind == 3 ? "Keep native action" : KeyboardPresentation.actionName(action, layer: 0, position: 0)).font(.headline)
                        .accessibilityIdentifier("action.selected")
                    if action.kind == 0 && action.activation == 0 {
                        DisclosureGroup("Shortcut modifiers") {
                            ForEach(0..<4) { index in
                                Toggle(["Control", "Shift", "Option", "Command"][index], isOn: Binding(get: {
                                    action.mods & (1 << index) != 0
                                }, set: {
                                    if $0 { action.mods |= 1 << index } else { action.mods &= ~(1 << index) }
                                }))
                            }
                        }
                    }
                    if action.activation == 1 {
                        Picker("Tap key", selection: $tapKey) {
                            ForEach(KeyboardPresentation.usageNames.keys.sorted().filter { $0 < 0xe0 || $0 == tapKey }, id: \.self) { usage in
                                Text(KeyboardPresentation.usageNames[usage]!).tag(usage)
                            }
                        }
                        Picker("Tap speed", selection: $delay) {
                            Text("Fast").tag(150); Text("Standard").tag(200); Text("Relaxed").tag(300)
                            if ![150,200,300].contains(delay) { Text("Custom: \(delay) ms").tag(delay) }
                        }
                        Text("Selects the target while pressed. A quick release without another key sends the tap key. Tap output has no modifiers.").font(.subheadline)
                    }
                    if action.activation == 2 { Text("Selects the target while pressed. Release restores the previous layer.") }
                    if action.activation == 3 { Text("Selects the target until you press this activator again.") }
                    if action.activation == 4 { Text("Selects the target for the next ordinary key press. Expires after one second.") }
                    if action.kind == 2 { Text("Uses Base at this position. It does not inherit Symbols under Navigation. On Base, it sends nothing.") }
                    if action.kind == 3 { Text("Keeps this switch's native keyboard behavior, including its saved native settings.") }
                }
                ForEach(KeyboardPresentation.categories, id: \.self) { category in
                    let options = KeyboardPresentation.catalog.filter {
                        $0.category == category && (search.isEmpty || $0.name.localizedCaseInsensitiveContains(search) || category.localizedCaseInsensitiveContains(search))
                    }
                    if !options.isEmpty {
                        Section(category) {
                            ForEach(options) { option in
                                Button(option.name) {
                                    action = option.action
                                    searching = false
                                    tapKey = action.tapUsage
                                    delay = action.tapMilliseconds == 0 ? 200 : Int(action.tapMilliseconds)
                                }.frame(minHeight: 44).accessibilityIdentifier("action.\(option.id)")
                            }
                        }
                    }
                }
                DisclosureGroup("Advanced: raw keyboard usage") {
                    TextField("HID usage, hexadecimal 00 to ff", text: $rawUsage).textInputAutocapitalization(.never).autocorrectionDisabled()
                    Button("Use raw usage") {
                        guard let usage = UInt16(rawUsage, radix: 16), usage <= 255 else { error = "Use a value from 00 to ff."; return }
                        action = .init(usage: usage)
                    }
                    Text("Raw entry changes only this draft. It does not send a keyboard command.")
                }
                if !error.isEmpty { Text(error).foregroundStyle(.red) }
            }
            .searchable(text: $search, isPresented: $searching, placement: .navigationBarDrawer(displayMode: .always), prompt: "Find keys, shortcuts, layers")
            .navigationTitle(title).navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") {
                        do {
                            if action.activation == 1 { action.tapUsage = tapKey; action.tapMilliseconds = UInt16(delay) }
                            try save(action); dismiss()
                        } catch { self.error = "This action could not be saved." }
                    }.accessibilityIdentifier("extension.savePosition")
                }
            }.onAppear {
                tapKey = action.tapUsage
                delay = action.tapMilliseconds == 0 ? 200 : Int(action.tapMilliseconds)
            }
        }
    }
}
