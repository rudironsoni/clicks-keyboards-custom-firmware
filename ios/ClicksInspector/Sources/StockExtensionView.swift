import ExternalAccessory
import SwiftUI

/// The product home. Diagnostics remain a separate Advanced destination.
struct StockExtensionView: View {
    @ObservedObject var inspector: AccessoryInspector
    @ObservedObject var editor: StockExtensionController
    @Environment(\.dynamicTypeSize) private var textSize
    @Environment(\.colorScheme) private var appearance
    @State private var layer = 0
    @State private var position: Int?
    @State private var keyList = false
    @State private var connections = false
    @State private var systemKeys = false
    @State private var capabilityTarget: EAAccessory?
    @State private var confirmCapabilities = false
    #if targetEnvironment(simulator)
    @State private var emulator: V122EmulatorLink?
    @State private var emulatorStatus = "V122 software model. No physical keyboard."
    @State private var controlBusy = false
    #endif

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                deviceCard
                if textSize.isAccessibilitySize {
                    Text(synchronization).font(.headline).accessibilityIdentifier("extension.comparison")
                    if !applyReason.isEmpty { Text(applyReason).font(.body) }
                }
                if textSize.isAccessibilitySize {
                    Text("My Keyboard").font(.title2.bold())
                } else {
                    HStack {
                        Text("My Keyboard").font(.title2.bold())
                        Spacer()
                        Button(keyList ? "Keyboard" : "Key list", systemImage: keyList ? "keyboard" : "list.bullet") { keyList.toggle() }
                            .accessibilityIdentifier("keyboard.viewMode")
                    }
                }
                layerSelector
                Text("Draft on iPhone").font(.subheadline).foregroundStyle(.secondary)
                keyboard
                profile
                DisclosureGroup("Layer behavior in this draft") {
                    VStack(alignment: .leading, spacing: 12) {
                        nativeSwitch("Use native Symbols activation", bit: 2)
                        nativeSwitch("Use native Navigation activation", bit: 4)
                        Toggle("Enable this map when applied", isOn: $editor.enabled)
                        Text("Custom hold, one-shot, and toggle actions take priority over native Navigation and Symbols. These choices change only your draft.")
                            .font(.subheadline).foregroundStyle(.secondary)
                    }.padding(.top, 12)
                }.disabled(editor.busy)
                Text(editor.status).font(.subheadline).foregroundStyle(.secondary).accessibilityIdentifier("extension.status")
            }.padding()
        }
        .background(Color(uiColor: .systemGroupedBackground))
        .navigationTitle("My Clicks")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .topBarLeading) {
                Button("Connection", systemImage: "cable.connector") { connections = true }
                    .accessibilityIdentifier("keyboard.connection")
            }
            ToolbarItem(placement: .topBarTrailing) {
                NavigationLink("Advanced", destination: AdvancedInspectorView(inspector: inspector))
                    .accessibilityIdentifier("advanced.open")
            }
        }
        .safeAreaInset(edge: .bottom) {
            VStack(alignment: .leading, spacing: 8) {
                if !textSize.isAccessibilitySize {
                    Text(synchronization).font(.subheadline.weight(.semibold)).accessibilityIdentifier("extension.comparison")
                    if !applyReason.isEmpty { Text(applyReason).font(.footnote).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true) }
                }
                if textSize.isAccessibilitySize {
                    VStack { saveButton; applyButton }
                } else {
                    ViewThatFits(in: .horizontal) {
                        HStack { saveButton; applyButton }
                        VStack { saveButton; applyButton }
                    }
                }
            }.padding().background(.regularMaterial)
                .accessibilityElement(children: .contain).accessibilityIdentifier("keyboard.actionBar")
        }
        .sheet(isPresented: $connections) { connectionSheet }
        .sheet(isPresented: $systemKeys) { systemKeySheet }
        .sheet(isPresented: Binding(get: { position != nil }, set: { if !$0 { position = nil } })) {
            if let pos = position {
                KeyActionPicker(action: editor.draft.actions[layer * 36 + pos],
                                title: "\(KeyboardPresentation.layers[layer]) · \(KeyboardPresentation.keyName(pos))") { action in
                    try editor.draft.set(action, layer: layer, row: pos / 6, column: pos % 6)
                }
            }
        }
        .tint(accent)
    }

    private var saveButton: some View {
        Button { editor.saveProfile(name: editor.profileName) } label: {
            Text(textSize.isAccessibilitySize ? "Save" : "Save on iPhone").frame(maxWidth: .infinity)
        }
            .buttonStyle(.bordered).controlSize(.large).frame(maxWidth: .infinity, minHeight: 44)
            .buttonBorderShape(.roundedRectangle(radius: 12))
            .foregroundStyle(accent).accessibilityLabel("Save on iPhone")
            .disabled(editor.busy || !editor.storageAvailable).accessibilityIdentifier("extension.save")
    }
    private var accent: Color {
        appearance == .dark ? .orange : Color(red: 0.68, green: 0.28, blue: 0)
    }
    private var applyButton: some View {
        Button {
            Task { await editor.apply() }
        } label: {
            Text(textSize.isAccessibilitySize ? "Apply" : editor.isEmulated ? "Apply to Emulator" : "Apply to Keyboard")
                .frame(maxWidth: .infinity)
        }.buttonStyle(ApplyButtonStyle(accent: accent, foreground: appearance == .dark ? .black : .white))
            .frame(maxWidth: .infinity, minHeight: 44)
            .disabled(!editor.canApply || emulatorBusy).accessibilityIdentifier("extension.apply")
            .accessibilityLabel(editor.isEmulated ? "Apply to Emulator" : "Apply to Keyboard")
            .accessibilityHint(applyReason)
    }
    private var synchronization: String {
        if editor.applyUncertain { return "Apply not confirmed" }
        if editor.connected && editor.matchesRAM { return editor.isEmulated ? "Verified in emulator" : "Verified on keyboard" }
        return "Not synchronized"
    }
    private var applyReason: String {
        if editor.applyUncertain { return "Reconnect to read actual RAM. The app will not retry this apply automatically." }
        if editor.busy { return "Waiting for the current operation to finish." }
        if !editor.connected {
            if inspector.stockVersionVerified || inspector.accessories.contains(where: { $0.modelNumber == "CK-5200" && $0.firmwareRevision == "1.2.2" }) {
                return "V122 version reported. Apply needs verified extension capabilities."
            }
            return "Apply needs a verified programmable extension."
        }
        if !editor.isEmulated && !editor.ramChangesAuthorized { return "Allow RAM changes in Connection for this session. Firmware flashing stays locked." }
        return ""
    }

    private var deviceCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            if textSize.isAccessibilitySize {
                Label(editor.isEmulated ? "CK-5200 · V122 emulator" : "CK-5200", systemImage: "keyboard").font(.headline)
                Text(editor.connected ? "Connected" : "Offline editing").font(.subheadline).foregroundStyle(.secondary)
            } else {
                HStack {
                    Image(systemName: "keyboard").foregroundStyle(accent)
                    Text(editor.isEmulated ? "CK-5200 · V122 emulator" : "CK-5200").font(.headline)
                    Spacer()
                    Text(editor.connected ? "Connected" : "Offline editing").font(.subheadline).foregroundStyle(.secondary)
                }
            }
            if editor.connected {
                Text("Programmable extension verified. Changes apply to RAM only.").font(.subheadline)
            } else if inspector.stockVersionVerified {
                Text("V122 identity verified. Check programmable capabilities in Connection.").font(.subheadline)
            } else if let accessory = inspector.accessories.first(where: { $0.modelNumber == "CK-5200" }) {
                Text("Firmware \(accessory.firmwareRevision) reported. Open Connection to verify capabilities.").font(.subheadline)
            } else {
                Text("Create your layout now. Your draft stays on this iPhone.").font(.subheadline)
            }
        }.padding().frame(maxWidth: .infinity, alignment: .leading)
            .background(Color(uiColor: .secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 18))
    }

    @ViewBuilder private var layerSelector: some View {
        let picker = Picker("Edit layer", selection: $layer) {
            ForEach(0..<3) { Text(KeyboardPresentation.layers[$0]).tag($0) }
        }.accessibilityIdentifier("extension.layer")
        if textSize.isAccessibilitySize { picker.pickerStyle(.menu).frame(minHeight: 44) }
        else { picker.pickerStyle(.segmented) }
    }

    @ViewBuilder private var keyboard: some View {
        if keyList || textSize.isAccessibilitySize {
            VStack(alignment: .leading, spacing: 8) {
                ForEach(KeyboardPresentation.rows.indices, id: \.self) { row in
                    ForEach(KeyboardPresentation.rows[row]) { key in keyButton(key, expanded: true) }
                }
            }
        } else {
            ScrollView(.horizontal) {
                VStack(spacing: 6) {
                    ForEach(KeyboardPresentation.rows.indices, id: \.self) { row in
                        HStack(spacing: 4) {
                            ForEach(KeyboardPresentation.rows[row]) { key in keyButton(key, expanded: false) }
                        }
                    }
                }.padding(8)
            }.background(Color(uiColor: .secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 18))
            Text("Swipe across the keyboard, or use Key list for larger controls. Native actions can depend on keyboard settings.")
                .font(.footnote).foregroundStyle(.secondary)
        }
        Button("About the four system keys", systemImage: "info.circle") { systemKeys = true }
            .font(.subheadline).accessibilityIdentifier("keyboard.systemKeys")
    }

    private func keyButton(_ key: KeyboardPresentation.Key, expanded: Bool) -> some View {
        let name = key.position.map { KeyboardPresentation.actionName(editor.draft.actions[layer * 36 + $0], layer: layer, position: $0) } ?? "Routing unverified"
        return Button {
            if let pos = key.position { position = pos } else { systemKeys = true }
        } label: {
            VStack(spacing: 3) {
                if !expanded, let symbol = ["Globe":"globe", "Clicks":"c.circle", "Shortcuts":"square.grid.3x3.fill", "Microphone":"mic", "Shift":"shift", "Delete":"delete.left", "Return":"return"][key.id] {
                    Image(systemName: symbol).font(.callout.weight(.semibold))
                } else { Text(key.id).font(.callout.weight(.semibold)) }
                if !expanded && key.position == nil {
                    Image(systemName: "questionmark").font(.caption).foregroundStyle(.secondary)
                } else if expanded || name != key.id { Text(name).font(expanded ? .body : .caption).foregroundStyle(.secondary).lineLimit(expanded ? nil : 2) }
            }.frame(maxWidth: expanded ? .infinity : nil, minHeight: expanded ? 52 : 66)
                .frame(width: expanded ? nil : 44 * key.width + 4 * (key.width - 1))
                .padding(expanded ? 8 : 0)
                .background(Color(uiColor: .tertiarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 10))
        }.buttonStyle(.plain).disabled(editor.busy)
            .accessibilityLabel("\(key.id). \(KeyboardPresentation.layers[layer]) draft action: \(name)")
            .accessibilityHint(key.position == nil ? "Read the unverified system-key routing before editing." : "Choose a key action.")
            .accessibilityIdentifier(key.position.map { "extension.cell.\($0)" } ?? "keyboard.cap.\(key.id)")
    }

    private var profile: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Profile").font(.headline)
            TextField("Layout name", text: $editor.profileName).textFieldStyle(.roundedBorder).accessibilityIdentifier("extension.profileName")
            Text(editor.profileStatus).font(.subheadline).foregroundStyle(.secondary).accessibilityIdentifier("extension.profileStatus")
            if !editor.profiles.isEmpty {
                Menu("Load a saved profile", systemImage: "folder") {
                    ForEach(editor.profiles) { profile in
                        Button(profile.name) { editor.loadProfile(profile) }.accessibilityIdentifier("extension.profile.\(profile.name)")
                    }
                }.accessibilityIdentifier("keyboard.profiles")
            }
        }.padding().background(Color(uiColor: .secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 18))
            .disabled(editor.busy)
    }

    private var systemKeySheet: some View {
        NavigationStack {
            List {
                Text("The vendor photograph verifies the position of Globe, Clicks, Shortcuts, and Microphone. This keyboard's cap-to-matrix routing for those four keys is not verified. We do not guess which system action belongs to each cap.")
                Section("Decoded switches, not confirmed cap names") {
                    ForEach(KeyboardPresentation.unverifiedSystemPositions, id: \.self) { pos in
                        NavigationLink(KeyboardPresentation.keyName(pos)) {
                            KeyActionPicker(action: editor.draft.actions[layer * 36 + pos], title: KeyboardPresentation.keyName(pos)) { action in
                                try editor.draft.set(action, layer: layer, row: pos / 6, column: pos % 6)
                            }
                        }
                    }
                }
            }.navigationTitle("System key routing").toolbar { Button("Done") { systemKeys = false } }
        }
    }

    private var connectionSheet: some View {
        NavigationStack {
            List {
                Section("Programmable firmware") {
                    NavigationLink("Install programmable extension") {
                        ExtensionInstallerView(inspector: inspector, custom: inspector.customFirmware)
                    }.accessibilityIdentifier("extension.installer")
                    Text("One firmware installation enables RAM layouts. Saving or applying a layout does not install firmware.")
                }
                Section("Keyboard") {
                    Text(editor.status).accessibilityIdentifier("keyboard.connectionStatus")
                    Text("Accessory session: \(inspector.state.description)")
                    Text("Stock identity read: \(inspector.readState)").font(.subheadline).foregroundStyle(.secondary)
                    ForEach(inspector.accessories.filter { $0.modelNumber == "CK-5200" }, id: \.connectionID) { accessory in
                        Button("Open CK-5200 · \(accessory.firmwareRevision)") { inspector.openKeyboardSession(for: accessory) }
                            .disabled(!inspector.canOpenExtensionEditor)
                        if inspector.stockVersionVerified {
                            Text("V122 identity verified. Stock and the extension share this version reply. Check capabilities to verify programmable support.")
                            Button("Check installed programmable extension") { capabilityTarget = accessory; confirmCapabilities = true }
                        }
                    }
                    if editor.connected && !editor.isEmulated {
                        Toggle("Allow RAM changes for this connection", isOn: $editor.ramChangesAuthorized).disabled(editor.busy)
                        Text("This permits keymap changes only after extension identity verification. It never permits firmware flashing. Permission resets on disconnect.")
                    }
                    Button("Disconnect") { inspector.closeSession(reason: "User disconnected keyboard") }.accessibilityIdentifier("extension.disconnect")
                    Button("Copy verified keyboard map into draft") { editor.useReadback() }.disabled(editor.verified == nil || editor.busy)
                }
                #if targetEnvironment(simulator)
                emulatorControls
                #endif
            }.navigationTitle("Connection").toolbar {
                Button("Done") {
                    guard !inspector.customFirmware.flashInFlight else { return }
                    connections = false
                }
            }
                .confirmationDialog("Check installed extension?", isPresented: $confirmCapabilities, titleVisibility: .visible) {
                    Button("Read extension capabilities") {
                        if let target = capabilityTarget { inspector.openExtensionSession(for: target, capabilityReadAuthorized: true) }
                    }
                } message: {
                    Text("This reads the programmable extension identity. It does not change a keymap or flash firmware. Unmodified stock V122 will be rejected.")
                }
        }
    }

    private func nativeSwitch(_ title: String, bit: UInt8) -> some View {
        Toggle(title, isOn: Binding(get: { editor.draft.nativeMask & bit != 0 }, set: {
            if $0 { editor.draft.nativeMask |= bit } else { editor.draft.nativeMask &= ~bit }
        }))
    }
    private var emulatorBusy: Bool {
        #if targetEnvironment(simulator)
        controlBusy
        #else
        false
        #endif
    }
    #if targetEnvironment(simulator)
    private var emulatorControls: some View {
        Section("Developer emulator, no physical keyboard") {
            Button("Connect V122 emulator") {
                guard inspector.claimExtensionEmulator() else { return }
                let link = V122EmulatorLink(); emulator = link
                Task { await editor.connect(link, emulated: true) }
            }.disabled(editor.ownsSession || editor.busy || controlBusy).accessibilityIdentifier("extension.connect")
            Text(emulatorStatus).accessibilityIdentifier("extension.hidStatus")
            Button("Check NAV+Q Escape and SYM+E Tab") {
                Task { if let emulator { emulatorStatus = await editor.verifyEmulator(emulator) } }
            }.disabled(!editor.canApply || controlBusy).accessibilityIdentifier("extension.hid")
            Button("Lose next APPLY acknowledgement") {
                controlBusy = true
                Task {
                    defer { controlBusy = false }
                    do { try await emulator?.control("lose-ack"); emulatorStatus = "Next APPLY reply will be lost." }
                    catch { emulatorStatus = "\(error)" }
                }
            }.disabled(!editor.canApply || controlBusy).accessibilityIdentifier("extension.loseAck")
            Button("Emulate power loss") {
                inspector.closeSession(reason: "Emulated RAM loss"); controlBusy = true
                Task {
                    defer { controlBusy = false }
                    do { try await emulator?.control("power"); emulatorStatus = "RAM lost. Reconnect and apply explicitly." }
                    catch { emulatorStatus = "\(error)" }
                }
            }.disabled(editor.busy || controlBusy || emulator == nil).accessibilityIdentifier("extension.power")
        }
    }
    #endif
}

/// Keep disabled Apply readable instead of using the prominent style's faint tint.
private struct ApplyButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled
    let accent: Color
    let foreground: Color

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(isEnabled ? foreground : Color.primary)
            .padding(.horizontal, 12).padding(.vertical, 12)
            .frame(minHeight: 44)
            .background(isEnabled ? accent : Color(uiColor: .secondarySystemFill), in: RoundedRectangle(cornerRadius: 12))
            .opacity(configuration.isPressed ? 0.75 : 1)
    }
}
