import CryptoKit
import GameController
import SwiftUI
import UIKit

struct StockCharacterizationView: View {
    @ObservedObject var inspector: AccessoryInspector
    @ObservedObject var recorder: StockEvidenceRecorder
    @State private var condition = "baseline"
    @State private var backlight = "not-observed"
    @State private var charging = "not-observed"
    @State private var trialID = StockTrialCatalog.all[0].id
    @State private var includeTestText = false
    @State private var confirmNewSession = false
    @State private var confirmClear = false

    private let conditions = ["baseline", "initial connection", "foreground/background", "disconnect/reconnect",
        "each physical key", "modifiers/layers", "simultaneous keys", "long press/repeat",
        "sustained typing", "backlight timing", "idle/wake", "normal charging", "stock-app settings comparison"]

    init(inspector: AccessoryInspector) {
        self.inspector = inspector
        self.recorder = inspector.stockEvidence
    }

    var body: some View {
        List {
            Section("Stock characterization") {
                Text(CustomFirmwareController.keymapBuildEnabled
                     ? "QMK RAM CONFIGURATION: stock writes and firmware updates blocked"
                     : CustomFirmwareController.diagnosticsOnly
                     ? "DIAGNOSTICS ONLY: firmware and settings writes blocked"
                     : "AUTHORIZED STOCK UPDATE CONFIGURATION: not for M1")
                Text("Observe the stock CK-5200. This mode sends only the seven demonstrated getters. It cannot update firmware or write settings.")
                Text("\(recorder.isRecording ? "Recording" : "Stopped"): \(recorder.eventCount)/\(StockEvidenceRecorder.eventLimit) events")
                if recorder.isRecording {
                    Button("Stop and save") { inspector.stopStockRecording() }
                        .accessibilityIdentifier("stock.stop")
                } else {
                    Button("Start characterization") {
                        if recorder.bundle == nil { inspector.startStockRecording() }
                        else { confirmNewSession = true }
                    }
                    .disabled(inspector.customFirmware.phaseIsBusy)
                    .accessibilityIdentifier("stock.start")
                }
                Text("Local checkpoints retain exact test text and stream bytes every five seconds. Use test text only. Export before starting another session.")
                    .font(.caption)
            }
            Section("Connection and repeated reads") {
                Text(inspector.state.description)
                ForEach(inspector.accessories.filter { $0.modelNumber == "CK-5200" }, id: \.connectionID) { accessory in
                    Text("\(accessory.modelNumber), firmware \(accessory.firmwareRevision), hardware \(accessory.hardwareRevision)")
                    Text(accessory.protocolStrings.joined(separator: "\n")).font(.caption)
                    Button("Open stock session") { inspector.openSession(for: accessory) }
                        .disabled(!recorder.isRecording || inspector.stockSweepRemaining > 0)
                }
                Button("Refresh accessories") { inspector.refresh() }
                Button("Read seven getters, three passes") { inspector.readStockBaseline() }
                    .disabled(!recorder.isRecording || !inspector.canReadSelectedCommand || inspector.stockSweepRemaining > 0)
                    .accessibilityIdentifier("stock.read")
                Text("\(inspector.stockSweepRemaining) reads left. \(inspector.readState)")
                Text("Each error stops the sequence and closes the session. Reconnects never resume reads automatically.")
                    .font(.caption)
            }
            Section("Controlled observation") {
                Picker("Condition", selection: $condition) {
                    ForEach(conditions, id: \.self) { Text($0) }
                }
                Picker("Visible backlight", selection: $backlight) {
                    ForEach(["not-observed", "on", "off", "changed"], id: \.self) { Text($0) }
                }
                Picker("Normal charger", selection: $charging) {
                    ForEach(["not-observed", "disconnected", "connected; iOS charging", "connected; iOS not charging"], id: \.self) { Text($0) }
                }
                Button("Mark state before observation") { markState("before") }
                Button("Mark state after observation") { markState("after") }
                Text("Marks describe what you see. They are not measurements from the keyboard. Compare getter passes before and after each condition.")
                    .font(.caption)
            }
            .disabled(!recorder.isRecording)
            Section("Physical key trial") {
                Picker("Trial", selection: $trialID) {
                    ForEach(StockTrialCatalog.all) { trial in
                        Text("\(trial.key.id): \(trial.key.physicalLabel), \(trial.action.rawValue)").tag(trial.id)
                    }
                }.disabled(recorder.activeTrialID != nil)
                if let trial = StockTrialCatalog.find(recorder.activeTrialID ?? trialID) {
                    Text(trial.expectedAction)
                    Text("Expected from static decode: \(trial.expectedBehavior)").font(.caption)
                    Text("Label evidence: \(trial.key.labelEvidence.rawValue). Matrix: \(trial.key.id). Physical mapping unverified.").font(.caption)
                }
                Button("Begin key trial") {
                    markState("before-key-trial")
                    recorder.beginTrial(trialID)
                }
                .disabled(recorder.activeTrialID != nil)
                .accessibilityIdentifier("stock.trial.begin")
                Text("Tap the box, follow the action, and release all keys. A completed observation is not physical validation. Text alone cannot validate a key.")
                StockKeyCapture(recorder: recorder)
                    .id(recorder.textResetID)
                    .frame(height: 150)
                ForEach((recorder.bundle?.events.suffix(8) ?? []).filter { $0.kind == "key" || $0.source == "text-input" }, id: \.sequence) { event in
                    Text("\(event.source): \(event.fields["phase"] ?? event.kind) \(event.fields["code"] ?? "") \(event.fields["characters"] ?? "")")
                        .font(.caption.monospaced())
                }
                ForEach([StockTrialStatus.completed, .failed, .ambiguous, .unobservable], id: \.rawValue) { status in
                    Button("End trial: \(status.rawValue)") { recorder.endTrial(status) }
                        .disabled(recorder.activeTrialID == nil)
                        .accessibilityIdentifier("stock.trial.\(status.rawValue)")
                }
                Text("GCKeyboard reports a coalesced keyboard, not a proven CK-5200 source. Disconnect other keyboards. UIKit and text-input events remain separate.")
                    .font(.caption)
            }
            .disabled(!recorder.isRecording)
            Section("Evidence export") {
                Toggle("Include my test characters in JSON", isOn: $includeTestText)
                    .accessibilityIdentifier("stock.includeText")
                Text("Exports retain wall-clock times, key codes, and known revision metadata. They omit unknown strings and raw errors. Key codes can reveal typed text. Use test text only.")
                    .font(.caption)
                Button("Create JSON and report") {
                    recorder.export(in: AccessoryInspector.evidenceDirectory, includeTestText: includeTestText)
                }
                .disabled(recorder.bundle == nil || recorder.isRecording)
                .accessibilityIdentifier("stock.export")
                if !recorder.exportURLs.isEmpty, recorder.exportedWithTestText == includeTestText {
                    ShareLink(items: recorder.exportURLs) { Label("Share evidence files", systemImage: "square.and.arrow.up") }
                        .accessibilityIdentifier("stock.share")
                    NavigationLink("Inspect exported JSON") {
                        StockExportPreview(url: recorder.exportURLs[0])
                    }.accessibilityIdentifier("stock.inspectExport")
                }
                if let error = recorder.storageError { Text(error).foregroundStyle(.red) }
                Button("Clear captured text and diagnostics", role: .destructive) { confirmClear = true }
                    .disabled(recorder.isRecording || inspector.customFirmware.phaseIsBusy)
                    .accessibilityIdentifier("stock.clear")
                Text("Stop recording first. Clear deletes this app's stock checkpoints and exports, the test-text box, and on-screen session logs. It cannot delete shared copies or iOS system logs.")
                    .font(.caption)
                if recorder.bundle != nil, !recorder.isRecording {
                    Text(recorder.report).font(.caption.monospaced()).textSelection(.enabled)
                }
            }
            Section("Observation limits") {
                Text("These are iOS observations, not raw HID packets. The app cannot measure switch bounce, matrix pins, MFi authentication, or firmware timing directly.")
                Text("System shortcuts and events during suspension may be hidden. Missing callbacks are unknown, not proof of missing firmware events.")
                Text("Backgrounding closes the EA session. Return, reopen, then start another getter pass. No firmware write is needed.")
            }
        }
        .navigationTitle("Stock evidence")
        .confirmationDialog("Replace the current local session? Export it first if needed.", isPresented: $confirmNewSession) {
            Button("Start new session", role: .destructive) { inspector.startStockRecording() }
        }
        .confirmationDialog("Delete all local stock recordings and exports? Shared copies remain outside this app.", isPresented: $confirmClear) {
            Button("Delete local stock evidence", role: .destructive) { inspector.clearStockEvidence() }
        }
        .onDisappear { recorder.interruptKeys(reason: "left characterization screen") }
        .onChange(of: recorder.textResetID) { _, _ in
            condition = "baseline"
            backlight = "not-observed"
            charging = "not-observed"
            includeTestText = false
        }
    }

    private func markState(_ phase: String) {
        recorder.state.condition = condition
        recorder.state.backlight = backlight
        recorder.state.charging = charging
        recorder.record(source: "user", kind: "state-mark", fields: ["phase": phase])
    }
}

/// Bound text layout cost on older iOS runtimes. Pages are exact file substrings.
private struct StockExportPreview: View {
    let url: URL
    @State private var pages: [String] = []
    @State private var page = 0

    private var pageMetadata: String {
        let bytes = Data(pages[page].utf8)
        let digest = SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined()
        return "\(page + 1):\(pages.count):\(bytes.count):\(digest)"
    }

    var body: some View {
        VStack {
            if !pages.isEmpty {
                Text("Page \(page + 1) of \(pages.count)")
                    .accessibilityIdentifier("stock.export.page")
                    .accessibilityValue(pageMetadata)
                HStack {
                    Button("Previous JSON page") { page -= 1 }.disabled(page == 0)
                    Button("Next JSON page") { page += 1 }
                        .disabled(page + 1 == pages.count)
                        .accessibilityIdentifier("stock.export.next")
                }
                TextEditor(text: .constant(pages[page]))
                    .font(.caption.monospaced())
                    .disabled(true)
                    .accessibilityIdentifier("stock.export.json")
            }
        }
        .navigationTitle("Exported JSON")
        .task {
            guard pages.isEmpty else { return }
            #if DEBUG
            // Preserve the actual file for hosted UI/source comparison before the deletion test.
            if ProcessInfo.processInfo.arguments.contains("--capture-stock-export") {
                do {
                    let directory = FileManager.default.temporaryDirectory.appendingPathComponent("stock-export-ui-evidence")
                    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                    try Data(contentsOf: url).write(to: directory.appendingPathComponent(url.lastPathComponent), options: .atomic)
                } catch {
                    assertionFailure("Cannot preserve stock export for UI verification: \(error)")
                }
            }
            #endif
            let text = (try? String(contentsOf: url, encoding: .utf8)) ?? "Cannot read export"
            var start = text.startIndex
            while start < text.endIndex {
                let end = text.index(start, offsetBy: 16384, limitedBy: text.endIndex) ?? text.endIndex
                pages.append(String(text[start..<end]))
                start = end
            }
        }
    }
}

/// Uses Apple's responder chain unchanged. No shortcut interception or synthetic key releases.
private struct StockKeyCapture: UIViewRepresentable {
    let recorder: StockEvidenceRecorder

    func makeUIView(context: Context) -> StockCaptureTextView {
        let view = StockCaptureTextView()
        view.recorder = recorder
        view.accessibilityIdentifier = "stock.text"
        view.delegate = view
        view.font = .monospacedSystemFont(ofSize: 17, weight: .regular)
        view.backgroundColor = .secondarySystemBackground
        view.autocorrectionType = .no
        view.autocapitalizationType = .none
        view.spellCheckingType = .no
        view.smartQuotesType = .no
        view.smartDashesType = .no
        view.smartInsertDeleteType = .no
        let toolbar = UIToolbar()
        toolbar.items = [UIBarButtonItem(systemItem: .flexibleSpace),
                         UIBarButtonItem(title: "Done typing", style: .done, target: view,
                                         action: #selector(StockCaptureTextView.finishTyping))]
        toolbar.sizeToFit()
        view.inputAccessoryView = toolbar
        view.installKeyboardNotifications()
        return view
    }

    func updateUIView(_ view: StockCaptureTextView, context: Context) {
        view.isEditable = recorder.isRecording
        view.updateKeyboardCapture()
    }

    static func dismantleUIView(_ view: StockCaptureTextView, coordinator: ()) {
        view.endCapture()
    }
}

private final class StockCaptureTextView: UITextView, UITextViewDelegate {
    @objc func finishTyping() { resignFirstResponder() }
    private weak var owner: StockEvidenceRecorder?
    private var captureSessionID: UUID?
    var recorder: StockEvidenceRecorder? {
        get { owner?.bundle?.sessionID == captureSessionID ? owner : nil }
        set { owner = newValue; captureSessionID = newValue?.bundle?.sessionID }
    }
    private var keyboard: GCKeyboard?
    private var observers: [NSObjectProtocol] = []
    private var previousText = ""

    func installKeyboardNotifications() {
        for name in [Notification.Name.GCKeyboardDidConnect, .GCKeyboardDidDisconnect] {
            observers.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated {
                    self?.recorder?.interruptKeys(reason: "GCKeyboard connection changed")
                    self?.updateKeyboardCapture()
                }
            })
        }
    }

    func updateKeyboardCapture() {
        if isFirstResponder, recorder?.isRecording == true,
           let keyboard, keyboard === GCKeyboard.coalesced { return }
        keyboard?.keyboardInput?.keyChangedHandler = nil
        keyboard = nil
        guard isFirstResponder, recorder?.isRecording == true else { return }
        keyboard = GCKeyboard.coalesced
        keyboard?.keyboardInput?.keyChangedHandler = { [weak self] _, _, key, pressed in
            // GameController callbacks need not run on the main thread.
            let received = ProcessInfo.processInfo.systemUptime
            Task { @MainActor [weak self] in
                guard let self, self.isFirstResponder, self.recorder?.isRecording == true else { return }
                self.recorder?.key(source: "GCKeyboard", phase: pressed ? "down" : "up",
                                  code: key.rawValue, timestamp: received)
            }
        }
    }

    func endCapture() {
        keyboard?.keyboardInput?.keyChangedHandler = nil
        keyboard = nil
        for observer in observers { NotificationCenter.default.removeObserver(observer) }
        observers.removeAll()
        recorder?.state.focus = "none"
        recorder?.interruptKeys(reason: "text recorder removed")
    }

    deinit { for observer in observers { NotificationCenter.default.removeObserver(observer) } }

    func textViewDidBeginEditing(_ textView: UITextView) {
        recorder?.state.focus = "test-text-input"
        recorder?.record(source: "UIKit", kind: "focus-gained", fields: ["GCKeyboardAvailable": String(GCKeyboard.coalesced != nil)])
        updateKeyboardCapture()
    }

    func textViewDidEndEditing(_ textView: UITextView) {
        keyboard?.keyboardInput?.keyChangedHandler = nil
        keyboard = nil
        recorder?.state.focus = "none"
        recorder?.interruptKeys(reason: "text focus lost")
    }

    func textView(_ textView: UITextView, shouldChangeTextIn range: NSRange, replacementText text: String) -> Bool {
        let accepted = textView.text.utf16.count - range.length + text.utf16.count <= 8192
        recorder?.record(source: "text-input", kind: "replacement-proposed", fields: ["text": String(text.prefix(256)),
            "fullUTF16Length": String(text.utf16.count), "rangeLocation": String(range.location), "rangeLength": String(range.length),
            "truncated": String(text.count > 256), "acceptedByRecorder": String(accepted)])
        // Keep the visible test buffer bounded during sustained typing.
        return accepted
    }

    func textViewDidChange(_ textView: UITextView) {
        // Store the applied edit, not an assumption that a proposed edit became text.
        let old = Array(previousText.utf16)
        let new = Array(textView.text.utf16)
        var prefix = 0
        while prefix < min(old.count, new.count), old[prefix] == new[prefix] { prefix += 1 }
        var suffix = 0
        while suffix < min(old.count, new.count) - prefix,
              old[old.count - suffix - 1] == new[new.count - suffix - 1] { suffix += 1 }
        let inserted = Array(new[prefix..<(new.count - suffix)])
        recorder?.record(source: "text-input", kind: "text-changed", fields: [
            "UTF16Length": String(new.count), "rangeLocation": String(prefix), "rangeLength": String(old.count - prefix - suffix),
            "text": String(decoding: inserted.prefix(256), as: UTF16.self), "insertedUTF16Length": String(inserted.count),
            "truncated": String(inserted.count > 256), "markedText": String(textView.markedTextRange != nil)])
        previousText = textView.text
    }

    func textViewDidChangeSelection(_ textView: UITextView) {
        recorder?.record(source: "text-input", kind: "selection-changed", fields: [
            "location": String(textView.selectedRange.location), "length": String(textView.selectedRange.length)])
    }

    private func capture(_ presses: Set<UIPress>, phase: String) {
        guard isFirstResponder else { return }
        for press in presses {
            guard let key = press.key else { continue }
            recorder?.key(source: "UIPress", phase: phase, code: key.keyCode.rawValue,
                          modifiers: key.modifierFlags.rawValue, characters: key.characters, timestamp: press.timestamp)
        }
    }

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        capture(presses, phase: "down")
        super.pressesBegan(presses, with: event)
    }
    override func pressesChanged(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        capture(presses, phase: "changed")
        super.pressesChanged(presses, with: event)
    }
    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        capture(presses, phase: "up")
        super.pressesEnded(presses, with: event)
    }
    override func pressesCancelled(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        capture(presses, phase: "cancelled")
        super.pressesCancelled(presses, with: event)
    }
}
