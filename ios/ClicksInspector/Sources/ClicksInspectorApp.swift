import ExternalAccessory
import SwiftUI

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

/// Firmware actions: flash the QMK image (works against stock, which shares
/// the A1/A2/A3/A0 update dialect, and against the custom firmware), restore
/// the bundled stock image, and reset the keymap on custom firmware.
private struct CustomFirmwareSection: View {
    @ObservedObject var inspector: AccessoryInspector
    @ObservedObject var custom: CustomFirmwareController
    @State private var importingFirmware = false
    @State private var confirmation: Confirmation?

    private enum Confirmation: Identifiable {
        case flashCustom([UInt8])
        case restoreStock([UInt8])
        var id: String {
            switch self {
            case .flashCustom: "flash-custom"
            case .restoreStock: "restore-stock"
            }
        }
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
            }

            let sessionReady = custom.phaseIsStockFirmware || custom.phaseIsCustomFirmware
            if sessionReady, !custom.phaseIsBusy {
                Button("Flash custom firmware (.bin)...") { importingFirmware = true }
                if custom.phaseIsCustomFirmware {
                    Button("Restore stock firmware (V122)") {
                        if let image = Self.stockImage() {
                            confirmation = .restoreStock(image)
                        } else {
                            assertionFailure("bundled stock image missing")
                        }
                    }
                }
                Text("Flashing stages the image and reboots the keyboard to install it. Do not detach the case while this runs.")
                    .font(.caption)
            }
        }
        .fileImporter(isPresented: $importingFirmware, allowedContentTypes: [.data]) { result in
            switch result {
            case .success(let url):
                guard url.startAccessingSecurityScopedResource(),
                      let image = try? Data(contentsOf: url) else { return }
                defer { url.stopAccessingSecurityScopedResource() }
                if !image.isEmpty {
                    confirmation = .flashCustom(Array(image))
                }
            case .failure:
                break
            }
        }
        .sheet(item: $confirmation) { confirmation in
            let (title, detail): (String, String) = {
                switch confirmation {
                case .flashCustom(let image):
                    ("Flash custom firmware",
                     "Stages \(image.count) bytes and reboots the keyboard to install. Keep the case attached until it restarts.")
                case .restoreStock(let image):
                    ("Restore stock firmware",
                     "Stages the bundled Clicks V122 image (\(image.count) bytes) and reboots the keyboard to install it.")
                }
            }()
            ConfirmationView(title: title, detail: detail) {
                switch confirmation {
                case .flashCustom(let image), .restoreStock(let image):
                    custom.flash(image: image, rebootToInstall: true)
                }
            }
        }
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
                                Text(keycode.map { String(format: "%04x", $0) } ?? "----")
                                    .font(.caption.monospaced())
                                    .frame(maxWidth: .infinity)
                            }
                            .buttonStyle(.bordered)
                        }
                    }
                }
                Button("Reset keymap to factory") { custom.resetKeymap() }
                Text("Each applied key persists in the keyboard's flash immediately.")
                    .font(.caption)
            case .idle, .identifying, .stockFirmware, .busy, .failed:
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
        ("MO(1)", 0x5121), ("MO(2)", 0x5122), ("MO(3)", 0x5123),
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

private extension CustomFirmwareController {
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
