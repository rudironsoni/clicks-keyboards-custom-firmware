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
