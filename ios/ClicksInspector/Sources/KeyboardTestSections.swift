import SwiftUI

/// Key test: a focused text field where the user types on the physical
/// keyboard. Each character that appears proves that HID report path
/// works end to end. The counter shows how many characters arrived.
struct KeyTestSection: View {
    @State private var typedText = ""
    @FocusState private var keyTestFocused: Bool

    var body: some View {
        Section("Key test (type on the physical keyboard)") {
            TextField("Tap here, then type on the Clicks keyboard...", text: $typedText, axis: .vertical)
                .textFieldStyle(.roundedBorder)
                .focused($keyTestFocused)
                .autocorrectionDisabled()
                .textInputAutocapitalization(.never)
                .lineLimit(3...6)
                .font(.body.monospaced())
            HStack {
                Text("\(typedText.count) characters")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                Button("Clear") { typedText = "" }
                    .font(.caption)
            }
            if !keyTestFocused && typedText.isEmpty {
                Text("Tap the field to focus it, then press keys on the Clicks case. Every character that appears proves the HID report path works. Modifier keys (Shift, Ctrl, Cmd) do not produce visible characters on their own.")
                    .font(.caption)
            }
        }
        .onAppear { keyTestFocused = true }
    }
}

/// Self-test section: runs the full verification chain and reports.
struct SelfTestSection: View {
    @ObservedObject var custom: CustomFirmwareController

    init(inspector: AccessoryInspector) {
        self.custom = inspector.customFirmware
    }

    var body: some View {
        Section("Self-test") {
            if custom.selfTestRunning {
                HStack {
                    ProgressView()
                    Text("Running \(custom.selfTestReport?.passCount ?? 0) checks...")
                }
            } else if let report = custom.selfTestReport {
                HStack {
                    Image(systemName: report.passed ? "checkmark.circle.fill" : "xmark.circle.fill")
                        .foregroundStyle(report.passed ? .green : .red)
                    Text("\(report.passCount)/\(report.totalCount) checks passed")
                        .font(.headline)
                }
                ForEach(report.checks) { check in
                    VStack(alignment: .leading, spacing: 2) {
                        HStack {
                            Image(systemName: check.passed ? "checkmark" : "xmark")
                                .foregroundStyle(check.passed ? .green : .red)
                            Text(check.name).font(.caption.bold())
                        }
                        Text(check.detail)
                            .font(.caption.monospaced())
                            .foregroundStyle(.secondary)
                    }
                }
            } else {
                Text("Runs identify, info, all-layer keymap verification against the decoded stock layout, and a write round-trip.")
                    .font(.caption)
            }
            Button("Run self-test") { custom.runSelfTest() }
                .disabled(!custom.phaseIsCustomFirmware || custom.selfTestRunning)
        }
    }
}

/// Layout preview: the keymap displayed with readable key names.
struct LayoutPreviewSection: View {
    @ObservedObject var custom: CustomFirmwareController
    @State private var selectedLayer = 0

    init(inspector: AccessoryInspector) {
        self.custom = inspector.customFirmware
    }

    var body: some View {
        Section("Layout preview") {
            if custom.phaseIsCustomFirmware, custom.layers > 0 {
                Picker("Layer", selection: $selectedLayer) {
                    ForEach(0..<custom.layers, id: \.self) { layer in
                        Text("Layer \(layer): \(Self.layerNames[layer])").tag(layer)
                    }
                }
                ForEach(0..<custom.rows, id: \.self) { row in
                    HStack(spacing: 4) {
                        ForEach(0..<custom.columns, id: \.self) { column in
                            if let keycode = custom.keycode(layer: selectedLayer, row: row, column: column) {
                                let name = KeycodeNames.shortName(for: keycode)
                                Text(name)
                                    .font(.system(size: 10, design: .monospaced))
                                    .frame(maxWidth: .infinity, minHeight: 28)
                                    .background(
                                        RoundedRectangle(cornerRadius: 4)
                                            .fill(Self.backgroundColor(for: keycode))
                                    )
                                    .overlay(
                                        RoundedRectangle(cornerRadius: 4)
                                            .stroke(Color.secondary.opacity(0.3), lineWidth: 0.5)
                                    )
                            }
                        }
                    }
                }
                Text("Each cell shows the decoded key at that matrix crossing. Tap cells in the Keymap section below to edit.")
                    .font(.caption)
            } else {
                Text("Connect the custom firmware to preview the layout.")
                    .font(.caption)
            }
        }
    }

    private struct Self {
        static let layerNames = ["Base (QWERTY)", "SYM (numbers/symbols)", "Cursor (hold Clicks key)", "Reserved"]
        static func backgroundColor(for keycode: UInt16) -> Color {
            if keycode == KeycodeNames.transparent { return Color.gray.opacity(0.08) }
            if keycode == KeycodeNames.noKey { return Color.gray.opacity(0.15) }
            if KeycodeNames.isMomentaryLayer(keycode) || KeycodeNames.isOneShotLayer(keycode) || KeycodeNames.isLayerTap(keycode) {
                return Color.blue.opacity(0.12)
            }
            if KeycodeNames.modifiers[keycode] != nil { return Color.orange.opacity(0.12) }
            if KeycodeNames.customKeys[keycode] != nil { return Color.purple.opacity(0.12) }
            return Color.clear
        }
    }
}
