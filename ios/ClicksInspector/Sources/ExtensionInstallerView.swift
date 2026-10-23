import ExternalAccessory
import SwiftUI

/// Deliberate entry to the existing updater. This never starts an update on
/// appearance, connection, identity verification, or a profile apply.
struct ExtensionInstallerView: View {
    @ObservedObject var inspector: AccessoryInspector
    @ObservedObject var custom: CustomFirmwareController
    @State private var rawImage: [UInt8]?
    @State private var prepared: PreparedFirmwareImage?
    @State private var preparation = "Checking the bundled image."
    @State private var confirmation: ExtensionInstallConfirmation?
    @State private var powerRefresh = Date()

    var body: some View {
        List {
            Section("Install once, then apply layouts to RAM") {
                Text("This rewrites the keyboard firmware. It is not a layout save.").font(.headline)
                Text("Recovery from a failed installation is not proven. A failed flash can leave the keyboard unusable. Keep keyboard power stable and keep this app open.")
                    .foregroundStyle(.red)
                Text(preparation).accessibilityIdentifier("installer.image")
                if let prepared {
                    Text("Transfer: \(prepared.bytes.count) bytes. SHA-256 \(prepared.sha256)").font(.footnote.monospaced())
                }
            }
            Section("Connect and check") {
                ForEach(inspector.accessories.filter { $0.modelNumber == "CK-5200" }, id: \.connectionID) { accessory in
                    Button("Open installer connection · \(accessory.firmwareRevision)") {
                        confirmation = nil
                        inspector.openSession(for: accessory, firmwareControls: true)
                    }.disabled(!inspector.canOpenExtensionEditor || custom.flashInFlight)
                }
                if inspector.accessories.isEmpty { Text("Connect your CK-5200 to this iPhone.") }
                Text(phaseMessage).accessibilityIdentifier("installer.status")
                if let reason = custom.extensionInstallBlockReason {
                    Text(reason).accessibilityIdentifier("installer.blockReason")
                }
                Text(custom.extensionPowerStatus).accessibilityIdentifier("installer.powerStatus")
                Button("Refresh power checks") { refreshPower() }
                    .disabled(custom.flashInFlight)
                // The state refresh reevaluates the live device power check.
                Text(powerRefresh, style: .time).font(.footnote).foregroundStyle(.secondary)
            }
            Section("Installation") {
                Button("Review and install extension", role: .destructive) {
                    confirmation = ExtensionInstallConfirmation(id: custom.installationSessionID)
                }.disabled(prepared == nil || custom.extensionInstallBlockReason != nil)
                    .accessibilityIdentifier("installer.review")
                if custom.flashInFlight {
                    ProgressView(value: custom.flashingProgress)
                    Text("Do not remove the keyboard or close the app.")
                }
                if custom.stagingAbortAvailable {
                    Button("Stop before commit", role: .destructive) { custom.requestStagingAbort() }
                }
                if custom.pendingUpdate != nil {
                    Text("The update record is kept. No automatic retry or success claim follows a reboot. Reconnect, verify stock identity, then check the installed extension from Connection.")
                }
            }
        }
        .navigationTitle("Extension installer")
        .navigationBarTitleDisplayMode(.inline)
        .navigationBarBackButtonHidden(custom.flashInFlight)
        .interactiveDismissDisabled(custom.flashInFlight)
        .onAppear { prepareImage(); refreshPower() }
        .onReceive(NotificationCenter.default.publisher(for: ProcessInfo.thermalStateDidChangeNotification)) { _ in refreshPower() }
        .onReceive(NotificationCenter.default.publisher(for: UIDevice.batteryLevelDidChangeNotification)) { _ in refreshPower() }
        .onReceive(NotificationCenter.default.publisher(for: UIDevice.batteryStateDidChangeNotification)) { _ in refreshPower() }
        .onChange(of: custom.flashInFlight) { _, busy in UIApplication.shared.isIdleTimerDisabled = busy }
        .onDisappear { if !custom.flashInFlight { UIApplication.shared.isIdleTimerDisabled = false } }
        .sheet(item: $confirmation) { consent in
            ConfirmationView(title: "Install programmable firmware",
                detail: "Install the pinned stock-derived extension on CK-5200 hardware 1.2.0, replacing V122. Keep the app open and keyboard power connected. A commit acknowledgement does not prove the installer copied the image correctly.",
                acknowledgementRequired: "I accept that this rewrites my keyboard and can leave it unusable. Independent recovery is not proven.",
                eligibility: {
                    consent.blockReason(currentSession: custom.installationSessionID,
                                        preflight: custom.extensionInstallBlockReason)
                }) {
                    guard let rawImage else { return }
                    custom.flash(image: rawImage, rebootToInstall: true, artifact: .programmable, authorizedSession: consent.id)
                }
                .interactiveDismissDisabled(custom.flashInFlight)
        }
    }

    private var phaseMessage: String {
        switch custom.phase {
        case .idle: "No installer connection."
        case .identifying: "Checking the keyboard identity."
        case .stockFirmware(let message), .customFirmware(let message), .busy(let message),
             .failed(let message), .updateUnverified(let message): message
        }
    }

    private func refreshPower() {
        powerRefresh = Date()
        custom.recordInstallationPreflight()
    }

    private func prepareImage() {
        do {
            guard let url = Bundle.main.url(forResource: "ck5200_extension", withExtension: "bin") else {
                throw CustomKeyboardError.invalidImage("Bundled extension image is missing")
            }
            let bytes = [UInt8](try Data(contentsOf: url))
            prepared = try PreparedFirmwareImage(bytes, artifact: .programmable)
            rawImage = bytes
            preparation = "Pinned extension checked. The 27,058-byte executable is unchanged. The transfer adds 78 erased-state bytes to fill the application area."
        } catch {
            rawImage = nil; prepared = nil
            preparation = "Installation blocked: \(error)"
        }
    }
}
