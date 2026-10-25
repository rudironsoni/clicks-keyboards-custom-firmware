import CryptoKit
import ExternalAccessory
import SwiftUI
import UIKit

@main
struct ClicksInspectorApp: App {
    @StateObject private var inspector = AccessoryInspector()
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            NavigationStack {
                if let mode = confirmationTestMode {
                    ConfirmationView(title: "Confirmation UI test",
                        detail: "Simulator view test only. No keyboard link or firmware operation exists.",
                        acknowledgementRequired: "Acknowledge the test warning.",
                        eligibility: { mode == "blocked" ? "Test preflight blocked." : nil }) {}
                } else {
                    StockExtensionView(inspector: inspector, editor: inspector.extensionEditor)
                }
            }
            .preferredColorScheme(testAppearance)
        }
        .onChange(of: scenePhase) { _, phase in
            switch phase {
            case .active: inspector.foreground()
            case .background: inspector.background()
            case .inactive: inspector.inactive()
            @unknown default: break
            }
        }
    }

    private var testAppearance: ColorScheme? {
        #if DEBUG && targetEnvironment(simulator)
        ProcessInfo.processInfo.environment["MY_CLICKS_DARK_TEST"] == "1" ? .dark : nil
        #else
        nil
        #endif
    }

    private var confirmationTestMode: String? {
        #if DEBUG && targetEnvironment(simulator)
        ProcessInfo.processInfo.environment["FIRMWARE_CONFIRMATION_UI_TEST"]
        #else
        nil
        #endif
    }
}

struct AdvancedInspectorView: View {
    @ObservedObject var inspector: AccessoryInspector
    @ObservedObject var custom: CustomFirmwareController

    init(inspector: AccessoryInspector) {
        self.inspector = inspector
        self.custom = inspector.customFirmware
    }

    private var qmkEmulatorAvailable: Bool {
        #if targetEnvironment(simulator) && QMK_RAM_KEYMAP
        true
        #else
        false
        #endif
    }

    var body: some View {
                List {
                    Section("Stock evidence") {
                        NavigationLink("Stock characterization and key recorder") {
                            StockCharacterizationView(inspector: inspector)
                        }
                    }
                    if custom.hasQMKIdentity || qmkEmulatorAvailable {
                    Section("QMK RAM profiles") {
                        NavigationLink("Edit QMK keymap and phone profiles") {
                            QMKKeymapView(custom: inspector.customFirmware, inspector: inspector)
                        }
                        .accessibilityIdentifier("qmk.open")
                    }
                    }
                    Section("Session") {
                        Text("Open session uses the stock read-only path. Firmware controls require a separate session.")
                        Text(inspector.state.description)
                        Text("An open session does not mean firmware backup or memory access is available.")
                            .font(.caption)
                        Button("Close session") { inspector.closeSession() }
                    }
                    CustomFirmwareSection(inspector: inspector)
                    Section("Boot region extraction") {
                        Text("Requires the stock-sized boot reader patch. Reads the reserved first 8 KiB twice. Original stock firmware will reject the request.")
                        Button("Read and compare boot region") {
                            Task { await custom.dumpBootloader() }
                        }
                        .disabled(custom.lastVersionPayload != CustomKeyboardCommand.stockVersionPayload
                                  || custom.phaseIsBusy || custom.bootloaderDumpRunning)
                        .accessibilityIdentifier("bootloader.read")
                        if let url = custom.bootloaderDumpURL {
                            Text("SHA-256: \(custom.bootloaderDumpSHA256)").font(.caption).textSelection(.enabled)
                            ShareLink("Export boot region (.bin)", item: url)
                                .accessibilityIdentifier("bootloader.export")
                        }
                    }
                    if inspector.customFirmware.phaseIsCustomFirmware {
                        KeyTestSection()
                        SelfTestSection(inspector: inspector)
                        LayoutPreviewSection(inspector: inspector)
                    }
                    Section("Read-only stock query") {
                        Picker("Command", selection: $inspector.selectedReadCommand) {
                            ForEach(StockReadPolicy.demonstrated) { command in
                                Text(command.title).tag(command)
                            }
                        }
                        Button("Read selected value") { inspector.readSelectedValue() }
                            .disabled(!inspector.canReadSelectedCommand || inspector.stockSweepRemaining > 0)
                        Text(inspector.readState)
                        if let result = inspector.lastReadResult {
                            Text(result).textSelection(.enabled)
                        }
                        Text("Enabled only for CK-5200 firmware 1.2.2, hardware 1.2.0, after both session streams open.")
                            .font(.caption)
                    }
                    Section("Connected accessories") {
                        if inspector.accessories.isEmpty {
                            Text("Connect the Clicks case to a physical iPhone. The simulator cannot prove USB accessory access.")
                        }
                        ForEach(inspector.accessories, id: \.connectionID) { accessory in
                            VStack(alignment: .leading, spacing: 6) {
                                Text(accessory.name.isEmpty ? "Unnamed accessory" : accessory.name)
                                    .font(.headline)
                                LabeledContent("Manufacturer", value: accessory.manufacturer)
                                LabeledContent("Model", value: accessory.modelNumber)
                                LabeledContent("Firmware", value: accessory.firmwareRevision)
                                LabeledContent("Hardware", value: accessory.hardwareRevision)
                                LabeledContent("Connection ID", value: String(accessory.connectionID))
                                Text("Advertised protocols").font(.subheadline.bold())
                                if accessory.protocolStrings.isEmpty {
                                    Text("No protocols advertised")
                                }
                                ForEach(Array(accessory.protocolStrings.enumerated()), id: \.offset) { _, name in
                                    Text(name).font(.caption.monospaced()).textSelection(.enabled)
                                }
                                Button("Open session") { inspector.openSession(for: accessory) }
                                    .disabled(!accessory.isConnected || !accessory.protocolStrings.contains(AccessoryInspector.supportedProtocol))
                                Button("Open firmware-control session") {
                                    inspector.openSession(for: accessory, firmwareControls: true)
                                }
                                .disabled(inspector.stockEvidence.isRecording || inspector.customFirmware.phaseIsBusy || !accessory.isConnected)
                                if !accessory.protocolStrings.contains(AccessoryInspector.supportedProtocol) {
                                    Text("Session unavailable: com.clickscompanion.protocol is not advertised.")
                                        .font(.caption)
                                }
                            }
                        }
                    }
                    Section("Logs (last 200 entries)") {
                        Button("Clear logs") { inspector.clearLogs() }
                        ForEach(inspector.logs.reversed()) { entry in
                            VStack(alignment: .leading) {
                                Text(entry.timestamp.formatted(date: .omitted, time: .standard))
                                    .foregroundStyle(.secondary)
                                Text(entry.message)
                            }
                            .font(.caption.monospaced())
                        }
                    }
                }
                .navigationTitle("Advanced")
                .toolbar {
                    Button("Refresh") { inspector.refresh() }
                }
                /* Keep the screen awake while flashing: a locked phone kills
                 * the session mid-A2 (review 2026-10-11, F-7). */
                .onChange(of: inspector.customFirmware.phase) { _, phase in
                    if case .busy = phase {
                        UIApplication.shared.isIdleTimerDisabled = true
                    } else {
                        UIApplication.shared.isIdleTimerDisabled = false
                    }
                }
    }
}

/// Firmware actions (review 2026-10-12): while the custom-flashing gate is
/// closed, the ONLY available operation is restoring/reflashing the bundled
/// official stock V122 artifact. The controller independently refuses any
/// other image, so the UI removal is defense in depth, not the gate itself.
/// An update whose installation was never verified stays visible until the
/// user explicitly reconciles it.
private struct CustomFirmwareSection: View {
    @ObservedObject var inspector: AccessoryInspector
    @ObservedObject var custom: CustomFirmwareController
    @State private var confirmation: Confirmation?

    private enum Confirmation: Identifiable {
        case restoreStock([UInt8])
        case bootloaderReader([UInt8], Int)
        var id: String {
            switch self {
            case .restoreStock: "restore-stock"
            case .bootloaderReader: "bootloader-reader"
            }
        }
    }

    init(inspector: AccessoryInspector) {
        self.inspector = inspector
        self.custom = inspector.customFirmware
    }

    var body: some View {
        Section("Verified stock firmware manager") {
            if CustomFirmwareController.stockFlashTrial {
                Text("STOCK FLASH TRIAL BUILD")
                    .font(.headline)
                    .foregroundStyle(.red)
                Text("This is the authorized M4 trial configuration. It can physically rewrite the keyboard's application firmware with the official V122 image. Recovery from a failed installation is NOT established. Do not use without the reviewed trial procedure.")
                    .font(.caption)
                    .foregroundStyle(.red)
            }
            Button("Prepare official CK-5200 V122") {
                if let image = Self.stockImage() { custom.prepareStock(image) }
            }
            .accessibilityIdentifier("stockManager.prepare")
            Text(custom.stockPreparation).accessibilityIdentifier("stockManager.preparation")
            if CustomFirmwareController.extensionUpdatesEnabled {
                if let url = Bundle.main.url(forResource: "ck5200_bootloader_reader", withExtension: "bin"),
                   let data = try? Data(contentsOf: url) {
                    Button("Review boot reader installation", role: .destructive) {
                        confirmation = .bootloaderReader([UInt8](data), custom.installationSessionID)
                    }
                    .disabled(custom.extensionInstallBlockReason != nil)
                    .accessibilityIdentifier("bootloader.install")
                }
            }
            Text("Stock to stock: functioning updater required. QM/RS to stock: functioning authenticated application service required. Unbootable to stock: R3 unmet.")
                .font(.caption)
            Text(CustomFirmwareController.stockFlashTrial
                 ? "Trial configuration active: installation is enabled below, only after the explicit risk acknowledgement and the reviewed procedure."
                 : CustomFirmwareController.extensionUpdatesEnabled
                 ? "The pinned boot reader and extension require current-session confirmation before installation."
                 : "Default configurations physically disable installation. The dedicated M4 StockFlashTrial configuration is required for the authorized trial.")
                .font(.caption)
            switch custom.phase {
            case .idle: Text("No session.")
            case .identifying: Text("Identifying firmware...")
            case .stockFirmware(let message): Text(message)
            case .customFirmware(let message): Text(message)
            case .busy(let message):
                Text(message)
                ProgressView(value: custom.flashingProgress)
            case .failed(let message): Text(message)
            case .updateUnverified(let message):
                Text(message)
                .font(.callout)
                Text(custom.stockObservation)
                Text("A version reply or typing test cannot prove application replacement. The record cannot be cleared by marking it verified.")
                    .font(.caption)
                if let pending = custom.pendingUpdate, !pending.isResolved {
                    Button("Conclude trial (evidence captured)") {
                        custom.concludePendingUpdate(
                            observation: "Post-update observation: reconnect identity \(custom.lastVersionPayload.map { $0.map { String(format: "%02x", $0) }.joined() } ?? "none") at \(Date().formatted()).")
                    }
                    Text("Concluding closes the trial operationally and clears the controller gate for a future transaction. It never claims the application was rewritten; the install control stays off for this journal record, and the resolution persists in the audit log.")
                        .font(.caption)
                }
            }

            let sessionReady = custom.phaseIsStockFirmware || custom.phaseIsCustomFirmware
            if sessionReady, !custom.phaseIsBusy, CustomFirmwareController.stockUpdatesEnabled {
                // The stock re-flash is authorized ONLY for a connected
                // keyboard that identifies as the expected STOCK unit
                // (review 2026-10-13, P1): the controller's gate requires
                // stock 1.2.2, so offering the button while custom QMK runs
                // would promise an operation the controller rejects.
                if custom.stockMode != nil {
                    if let image = Self.stockImage() {
                        if CustomFirmwareController.stockFlashTrial {
                            Button("Install official stock V122") {
                                confirmation = .restoreStock(image)
                            }
                        } else {
                            Button("Install official stock V122 (disabled)") {}
                                .disabled(true)
                        }
                    } else {
                        Text("Bundled stock image missing from the app bundle.")
                            .font(.caption)
                            .foregroundStyle(.red)
                    }
                } else {
                    Text("Unknown firmware mode. Installation is refused.")
                        .font(.caption)
                }
                Text(custom.pendingUpdate.map { pending in
                    pending.isResolved
                     ? "A concluded trial record exists. The install control stays off for it by design (one physical trial per record); the resolution is retained in the audit log."
                     : "A previous update record is unresolved; installation stays blocked until the trial is concluded after capturing its evidence."
                } ?? (CustomFirmwareController.stockFlashTrial
                      ? "Only the exact official CK-5200 V122 image can be installed. QMK installation remains disabled in every configuration."
                      : "Physical installation is disabled in this configuration. Only the dedicated M4 StockFlashTrial configuration can enable it, under the reviewed trial procedure."))
                    .font(.caption)
                Text("Transfer, commit acknowledgement, reboot request, stock observed, and installation proven are separate states. This app cannot prove the original installer's copy operation.")
                    .font(.caption)
            }
            if custom.stagingAbortAvailable {
                Button("Abort update (staging only)", role: .destructive) {
                    custom.requestStagingAbort()
                }
                Text("Aborting stops staging new pages. It is refused once the commit (A3) has been requested, because the device may already have committed.")
                    .font(.caption)
            }
        }
        .sheet(item: $confirmation) { confirmation in
            let (title, detail, acknowledgement): (String, String, String?) = {
                switch confirmation {
                case .bootloaderReader:
                    ("Install boot reader",
                     "Replace the application with the pinned 18,648-byte stock-derived reader. Then reconnect and read the boot region under Advanced. Installation and hardware read access remain unverified.",
                     "I accept that this writes application flash and can leave the keyboard unusable. Recovery is not proven.")
                case .restoreStock(let image):
                    if CustomFirmwareController.stockFlashTrial {
                        ("Re-flash stock firmware (M4 trial)",
                         "Stages the bundled Clicks V122 image (\(image.count) bytes, sha256 \(Self.sha256(image).prefix(12))…) and reboots the keyboard to install it. This is a real flash operation with nonzero risk: the keyboard's application firmware is rewritten, and if the installation fails, no recovery path is established for this device.",
                         "I understand this rewrites the keyboard's firmware, that a same-version reply cannot prove the rewrite happened, and that recovery from a failed install is not established.")
                    } else {
                        ("Re-flash stock firmware",
                         "Stages the bundled Clicks V122 image (\(image.count) bytes, sha256 \(Self.sha256(image).prefix(12))…) and reboots the keyboard to install it.",
                         nil)
                    }
                }
            }()
            ConfirmationView(title: title, detail: detail,
                              acknowledgementRequired: acknowledgement,
                              eligibility: {
                                  if case .bootloaderReader(_, let session) = confirmation {
                                      return ExtensionInstallConfirmation(id: session).blockReason(
                                          currentSession: custom.installationSessionID, preflight: custom.extensionInstallBlockReason)
                                  }
                                  return nil
                              }) {
                switch confirmation {
                case .bootloaderReader(let image, let session):
                    custom.flash(image: image, rebootToInstall: true, artifact: .bootloaderReader, authorizedSession: session)
                case .restoreStock(let image):
                    custom.flash(image: image, rebootToInstall: true)
                }
            }
        }
    }

    private static func sha256(_ bytes: [UInt8]) -> String {
        SHA256.hash(data: Data(bytes)).map { String(format: "%02x", $0) }.joined()
    }

    private static func stockImage() -> [UInt8]? {
        guard let url = Bundle.main.url(forResource: "iKeyboard_CK-5200_V122_120", withExtension: "bin"),
              let data = try? Data(contentsOf: url) else { return nil }
        return Array(data)
    }
}

struct ConfirmationView: View {
    let title: String
    let detail: String
    var acknowledgementRequired: String? = nil
    var eligibility: () -> String? = { nil }
    let confirm: () -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var acknowledged = false

    var body: some View {
        let blockedReason = eligibility()
        ScrollView {
        VStack(spacing: 16) {
            Text(title).font(.headline)
            Text(detail).font(.subheadline)
            if let acknowledgement = acknowledgementRequired {
                Toggle(acknowledgement, isOn: $acknowledged)
                    .font(.caption)
                    .accessibilityIdentifier("firmware.risk")
            }
            if let blockedReason {
                Text(blockedReason)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .accessibilityIdentifier("firmware.blockReason")
            }
            HStack {
                Button("Cancel") { dismiss() }
                Button("Flash", role: .destructive) {
                    guard eligibility() == nil, acknowledgementRequired == nil || acknowledged else { return }
                    dismiss()
                    confirm()
                }
                .buttonStyle(.borderedProminent)
                .disabled(blockedReason != nil || acknowledgementRequired != nil && !acknowledged)
                .accessibilityIdentifier("firmware.confirm")
            }
        }
        .padding()
        }
    }
}

/// Layer-by-layer keymap editor for the custom firmware.
private struct QMKKeymapView: View {
    @ObservedObject var custom: CustomFirmwareController
    @ObservedObject var inspector: AccessoryInspector
    @State private var editingCell: Cell?
    @State private var profileName = ""
    @FocusState private var profileNameFocused: Bool

    private struct Cell: Identifiable {
        let row: Int
        let column: Int
        var id: String { "\(row)-\(column)" }
    }

    var body: some View {
        List {
            Section("Connection and permission") {
                if !CustomFirmwareController.keymapBuildEnabled {
                    Text("This build is diagnostics-only for keymaps. Use the QMKKeymap configuration to edit RAM.")
                }
                Text("Only CK-5200 hardware 1.2.0 with QM 00 01 identity can use custom commands.")
                #if targetEnvironment(simulator) && QMK_RAM_KEYMAP
                Text("SIMULATOR: software keyboard fixture. This does not test USB or a physical keyboard.")
                    .font(.caption)
                Button("Connect simulated QMK keyboard") { connectSimulation() }
                    .accessibilityIdentifier("qmk.connect")
                    .disabled(custom.keymapOperationRunning)
                Button("Simulate power loss and reconnect") { connectSimulation() }
                    .accessibilityIdentifier("qmk.reconnect")
                    .disabled(custom.keymapOperationRunning)
                #endif
                Toggle("Allow RAM writes for this session", isOn: $custom.ramWritesAllowed)
                    .accessibilityIdentifier("qmk.permission")
                    .disabled(!custom.hasQMKIdentity || !custom.keymapIsVerified || custom.keymapOperationRunning)
                Text(custom.keymapIsVerified ? "All 144 keys read from keyboard RAM" : "Open a firmware-control session to read the keyboard.")
                    .accessibilityIdentifier("qmk.connection")
                if custom.keymapOperationRunning {
                    ProgressView("Verified \(custom.verifiedWrites)/\(custom.plannedWrites) writes")
                }
                Text(custom.profileMessage).accessibilityIdentifier("qmk.status")
                if case .failed(let message) = custom.phase { Text(message) }
            }
            Section("Phone profiles") {
                TextField("Profile name", text: $profileName)
                    .accessibilityIdentifier("qmk.profileName")
                    .focused($profileNameFocused)
                    .submitLabel(.done)
                    .onSubmit { profileNameFocused = false }
                Button("Save current layout on iPhone") {
                    custom.saveProfile(name: profileName)
                }
                .accessibilityIdentifier("qmk.save")
                .disabled(!custom.keymapIsVerified || custom.keymapOperationRunning || profileName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                ForEach(custom.profiles) { profile in
                    Button {
                        custom.selectProfile(profile.id)
                    } label: {
                        Label(profile.name, systemImage: custom.desiredProfile?.id == profile.id ? "checkmark.circle.fill" : "circle")
                    }
                    .disabled(custom.keymapOperationRunning)
                }
                if let profile = custom.desiredProfile {
                    Text("Desired profile: \(profile.name). Version \(profile.schemaVersion), \(profile.layers) layers, \(profile.rows)x\(profile.columns).")
                    if custom.keymapIsVerified {
                        Text(custom.needsReapply ? "Keyboard RAM differs. Reapply only when you choose." : "Keyboard RAM matches the saved profile.")
                            .accessibilityIdentifier("qmk.comparison")
                    }
                    Button("Reapply saved profile to RAM") { custom.applyDesiredProfile() }
                        .accessibilityIdentifier("qmk.reapply")
                        .disabled(!custom.canEditKeymap || !custom.needsReapply)
                }
                Text("Profiles are stored in QMKProfiles.json on this iPhone. RAM changes disappear when the keyboard loses power. Reconnect reads all layers and never applies a profile automatically.")
                    .font(.caption)
            }
            if custom.keymapIsVerified {
                Section("Keymap (keyboard RAM)") {
                Picker("Layer", selection: Binding(
                    get: { custom.selectedLayer },
                    set: { custom.selectLayer($0) }
                )) {
                    ForEach(0..<max(custom.layers, 1), id: \.self) { layer in
                        Text("Layer \(layer)").tag(layer)
                    }
                }
                .pickerStyle(.segmented)
                .accessibilityIdentifier("qmk.layer")
                ForEach(0..<max(custom.rows, 1), id: \.self) { row in
                    HStack(spacing: 8) {
                        ForEach(0..<max(custom.columns, 1), id: \.self) { column in
                            let keycode = custom.keycode(layer: custom.selectedLayer, row: row, column: column)
                            Button {
                                editingCell = Cell(row: row, column: column)
                            } label: {
                                VStack(spacing: 1) {
                                    Text(keycode.map { KeycodeNames.shortName(for: $0) } ?? "----")
                                        .font(.system(size: 9, design: .monospaced))
                                    Text(keycode.map { String(format: "%04x", $0) } ?? "0000")
                                        .font(.system(size: 7, design: .monospaced))
                                        .foregroundStyle(.secondary)
                                }
                                .frame(maxWidth: .infinity)
                            }
                            .buttonStyle(.bordered)
                            .accessibilityIdentifier("qmk.cell.\(row).\(column)")
                            .disabled(!custom.canEditKeymap)
                        }
                    }
                }
                Button("Restore factory layout in RAM") { custom.resetKeymap() }
                    .accessibilityIdentifier("qmk.restore")
                    .disabled(!custom.canEditKeymap)
                Text("Restore sends runtime command 0x93 and reads all 144 keys. It keeps your phone profiles.").font(.caption)
                }
            }
        }
        .navigationTitle("QMK RAM profiles")
        .sheet(item: $editingCell) { cell in
            KeymapCellEditor(custom: custom, layer: custom.selectedLayer,
                             row: cell.row, column: cell.column)
        }
    }

    #if targetEnvironment(simulator) && QMK_RAM_KEYMAP
    private func connectSimulation() {
        guard inspector.claimFirmwareEmulator() else { return }
        custom.connectedTarget = .init(model: "CK-5200", firmwareRevision: "QMK", hardwareRevision: "1.2.0")
        custom.attach(SimulatedQMKLink())
    }
    #endif
}

private struct KeymapCellEditor: View {
    @ObservedObject var custom: CustomFirmwareController
    let layer: Int
    let row: Int
    let column: Int
    @State private var hexInput = ""
    @Environment(\.dismiss) private var dismiss

    private static let presets: [(String, UInt16)] = [
        ("A", 0x04), ("0", 0x27), ("Space", 0x2c), ("Enter", 0x28),
        ("Bksp", 0x2a), ("Tab", 0x2b),
        ("Left Shift", 0xe1), ("Left Ctrl", 0xe0), ("Left Cmd", 0xe3),
        ("Up", 0x52), ("Down", 0x51), ("Left", 0x50), ("Right", 0x4f),
        ("MO(1)", 0x5221), ("MO(2)", 0x5222), ("MO(3)", 0x5223),
        ("OSL(1)", 0x5281),
        ("iOS-KB (Eject)", 0x7e42),
        ("Transparent", 0x0001), ("No key", 0x0000),
    ]

    var body: some View {
        ScrollView {
        VStack(spacing: 16) {
            Text("Layer \(layer), row \(row), column \(column)").font(.headline)
            Text("Current: \(custom.keycode(layer: layer, row: row, column: column).map { String(format: "%04x", $0) } ?? "----")")
                .font(.caption.monospaced())
            TextField("Keycode hex, e.g. 0021", text: $hexInput)
                .accessibilityIdentifier("qmk.hex")
                .textFieldStyle(.roundedBorder)
                .autocorrectionDisabled()
                .textInputAutocapitalization(.characters)
            Button("Apply keycode") {
                if let value = UInt16(hexInput, radix: 16) {
                    custom.setKey(layer: layer, row: row, column: column, keycode: value)
                    dismiss()
                }
            }
            .buttonStyle(.borderedProminent)
            .accessibilityIdentifier("qmk.applyKey")
            .disabled(UInt16(hexInput, radix: 16) == nil)
            Divider()
            ForEach(Self.presets, id: \.1) { name, keycode in
                Button(name) {
                    custom.setKey(layer: layer, row: row, column: column, keycode: keycode)
                    dismiss()
                }
            }
        }
        .padding()
        }
    }
}

extension CustomFirmwareController {
    var phaseIsStockFirmware: Bool {
        if case .stockFirmware = phase { return true }
        return false
    }

    var phaseIsCustomFirmware: Bool {
        if case .customFirmware = phase { return true }
        return false
    }

    var phaseIsBusy: Bool {
        if case .busy = phase { return true }
        return false
    }

    func keycode(layer: Int, row: Int, column: Int) -> UInt16? {
        guard keymap.indices.contains(layer),
              keymap[layer].indices.contains(row),
              keymap[layer][row].indices.contains(column) else { return nil }
        return keymap[layer][row][column]
    }
}
