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
                List {
                    Section("Session") {
                        Text("Only the selected read or diagnostic request is sent when you tap Read selected value.")
                        Text(inspector.state.description)
                        Text("An open session does not mean firmware backup or memory access is available.")
                            .font(.caption)
                        Button("Close session") { inspector.closeSession() }
                    }
                    CustomFirmwareSection(inspector: inspector)
                    if inspector.customFirmware.phaseIsCustomFirmware {
                        KeyTestSection()
                        SelfTestSection(inspector: inspector)
                        LayoutPreviewSection(inspector: inspector)
                    }
                    KeymapSection(inspector: inspector)
                    Section("Read-only stock query") {
                        Picker("Command", selection: $inspector.selectedReadCommand) {
                            ForEach(StockReadCommand.allCases) { command in
                                Text(command.title).tag(command)
                            }
                        }
                        Button("Read selected value") { inspector.readSelectedValue() }
                            .disabled(!inspector.canReadSelectedCommand)
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
                .navigationTitle("Clicks Inspector")
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
        .onChange(of: scenePhase) { _, phase in
            switch phase {
            case .active: inspector.foreground()
            case .background: inspector.background()
            case .inactive: inspector.inactive()
            @unknown default: break
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
        var id: String { "restore-stock" }
    }

    init(inspector: AccessoryInspector) {
        self.inspector = inspector
        self.custom = inspector.customFirmware
    }

    var body: some View {
        Section("Firmware") {
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
                Button("Mark update verified") {
                    custom.markPendingUpdateVerified()
                }
                .disabled(custom.pendingUpdate == nil)
                Text("The update is only verified when the keyboard works: type on it, run the self-test, then mark it verified.")
                    .font(.caption)
            }

            let sessionReady = custom.phaseIsStockFirmware || custom.phaseIsCustomFirmware
            if sessionReady, !custom.phaseIsBusy {
                // Restore works from both states: the update dialect is
                // identical, so stock-on-stock is a valid re-flash.
                if let image = Self.stockImage() {
                    Button(custom.phaseIsCustomFirmware ? "Restore stock firmware (V122)" : "Re-flash stock firmware (V122)") {
                        confirmation = .restoreStock(image)
                    }
                } else {
                    Text("Bundled stock image missing from the app bundle.")
                        .font(.caption)
                        .foregroundStyle(.red)
                }
                Text("Custom firmware flashing is disabled in this build: no CH32X035 image has been authorized (the bundled QMK image is built for the wrong MCU and the controller refuses it). Only the bundled stock V122 artifact can be staged.")
                    .font(.caption)
                Text("Flashing stages the image and reboots the keyboard to install it. Do not detach the case while this runs. After the reboot, re-open the session and verify before relying on the keyboard.")
                    .font(.caption)
            }
        }
        .sheet(item: $confirmation) { confirmation in
            let (title, detail): (String, String) = {
                switch confirmation {
                case .restoreStock(let image):
                    ("Restore stock firmware",
                     "Stages the bundled Clicks V122 image (\(image.count) bytes, sha256 \(Self.sha256(image).prefix(12))…) and reboots the keyboard to install it.")
                }
            }()
            ConfirmationView(title: title, detail: detail) {
                switch confirmation {
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

private struct ConfirmationView: View {
    let title: String
    let detail: String
    let confirm: () -> Void
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(spacing: 16) {
            Text(title).font(.headline)
            Text(detail).font(.subheadline)
            HStack {
                Button("Cancel") { dismiss() }
                Button("Flash", role: .destructive) {
                    dismiss()
                    confirm()
                }
                .buttonStyle(.borderedProminent)
            }
        }
        .padding()
    }
}

/// Layer-by-layer keymap editor for the custom firmware.
private struct KeymapSection: View {
    @ObservedObject var custom: CustomFirmwareController
    @State private var editingCell: Cell?

    private struct Cell: Identifiable {
        let row: Int
        let column: Int
        var id: String { "\(row)-\(column)" }
    }

    init(inspector: AccessoryInspector) {
        self.custom = inspector.customFirmware
    }

    var body: some View {
        Section("Keymap (custom firmware)") {
            switch custom.phase {
            case .customFirmware:
                Picker("Layer", selection: Binding(
                    get: { custom.selectedLayer },
                    set: { custom.selectLayer($0) }
                )) {
                    ForEach(0..<max(custom.layers, 1), id: \.self) { layer in
                        Text("Layer \(layer)").tag(layer)
                    }
                }
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
                        }
                    }
                }
                Button("Reset keymap to factory") { custom.resetKeymap() }
                Text("Each applied key persists in the keyboard's flash immediately.")
                    .font(.caption)
            case .idle, .identifying, .stockFirmware, .busy, .failed, .updateUnverified:
                Text("Keymap editing is available when the custom QMK firmware is connected.")
                    .font(.caption)
            }
        }
        .sheet(item: $editingCell) { cell in
            KeymapCellEditor(custom: custom, layer: custom.selectedLayer,
                             row: cell.row, column: cell.column)
        }
    }
}

private struct KeymapCellEditor: View {
    @ObservedObject var custom: CustomFirmwareController
    let layer: Int
    let row: Int
    let column: Int
    @State private var hexInput = ""
    @Environment(\.dismiss) private var dismiss

    private static let presets: [(String, UInt16)] = [
        ("A", 0x04), ("0", 0x1e), ("Space", 0x2c), ("Enter", 0x28),
        ("Bksp", 0x2a), ("Tab", 0x2b),
        ("Left Shift", 0xe1), ("Left Ctrl", 0xe0), ("Left Cmd", 0xe3),
        ("Up", 0x52), ("Down", 0x51), ("Left", 0x50), ("Right", 0x4f),
        ("MO(1)", 0x5121), ("MO(2)", 0x5122), ("MO(3)", 0x5123),
        ("OSL(1)", 0x5281),
        ("iOS-KB (Eject)", 0x5da3),
        ("Trans/none", 0x0000),
    ]

    var body: some View {
        VStack(spacing: 16) {
            Text("Layer \(layer), row \(row), column \(column)").font(.headline)
            Text("Current: \(custom.keycode(layer: layer, row: row, column: column).map { String(format: "%04x", $0) } ?? "----")")
                .font(.caption.monospaced())
            TextField("Keycode hex, e.g. 0021", text: $hexInput)
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
