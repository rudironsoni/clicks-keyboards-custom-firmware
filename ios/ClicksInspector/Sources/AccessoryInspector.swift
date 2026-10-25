import Combine
import ExternalAccessory
import Foundation
import OSLog
import UIKit

@MainActor
final class AccessoryInspector: NSObject, ObservableObject, StreamDelegate {
    static let supportedProtocol = "com.clickscompanion.protocol"

    let customFirmware = CustomFirmwareController()
    let extensionEditor = StockExtensionController()
    let stockEvidence = StockEvidenceRecorder()
    @Published private(set) var stockSweepRemaining = 0
    private var stockQueue: [StockReadCommand] = []
    private var sweepTimer: Timer?
    private var checkpointTimer: Timer?
    private var firmwareSession = false
    private var extensionSession = false
    private var identifyOnOpen = false
    @Published private(set) var stockVersionVerified = false
    private var managementBusy: Bool {
        customFirmware.phaseIsBusy || customFirmware.flashInFlight
            || customFirmware.keymapOperationRunning || customFirmware.selfTestRunning
    }
    static var evidenceDirectory: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("StockEvidence", isDirectory: true)
    }
    private var customStreamLink: StreamLink?
    private var customLinkAttached = false

    private func attachCustomFirmwareLink(extensionFramer: KeyboardPacketFramer? = nil) {
        if extensionSession {
            let link = StreamLink(output: session?.outputStream)
            customStreamLink = link
            link.onReceiverReady = { [weak self] in self?.readReadyInput() }
            let target = session?.accessory
            let verified = target?.modelNumber == "CK-5200" && target?.hardwareRevision == "1.2.0"
            let framer = extensionFramer ?? KeyboardPacketFramer(maximumLength: 36)
            Task {
                await extensionEditor.connect(link, emulated: false, accessoryIdentityVerified: verified,
                    incomingFramer: framer, diagnostic: { [weak self] event, bytes, detail in
                        self?.customFirmware.recordExtensionDiagnostic(event, bytes: bytes, detail: detail)
                    })
            }
            return
        }
        guard firmwareSession, !stockEvidence.isRecording, !customLinkAttached else { return }
        customLinkAttached = true
        /* The custom protocol now flows through the host-testable stream
         * adapter; the inspector keeps servicing the dual-use session
         * streams and the read-only stock query path (review 2026-10-12). */
        let link = StreamLink(output: session?.outputStream)
        customStreamLink = link
        link.onReceiverReady = { [weak self] in self?.readReadyInput() }
        if let accessory = session?.accessory {
            customFirmware.connectedTarget = CustomFirmwareController.ConnectedTarget(
                model: accessory.modelNumber,
                firmwareRevision: accessory.firmwareRevision,
                hardwareRevision: accessory.hardwareRevision)
        }
        customFirmware.attach(link)
    }

    enum SessionState {
        case closed
        case opening(Int, openedStreams: Set<ObjectIdentifier>)
        case open(Int)
        case failed(String)

        var description: String {
            switch self {
            case .closed: return "Closed"
            case .opening(let id, _): return "Opening connection \(id)"
            case .open(let id): return "Open on connection \(id)"
            case .failed(let reason): return "Failed: \(reason)"
            }
        }
    }

    struct LogEntry: Identifiable {
        let id = UUID()
        let timestamp = Date()
        let message: String
    }

    @Published private(set) var accessories: [EAAccessory] = []
    @Published private(set) var state: SessionState = .closed
    @Published private(set) var logs: [LogEntry] = []
    @Published var selectedReadCommand: StockReadCommand = .version {
        didSet {
            if oldValue != selectedReadCommand {
                log("User selected read command 0x\(hex([selectedReadCommand.rawValue]))")
            }
        }
    }
    @Published private(set) var readState = "Not started"
    @Published private(set) var lastReadResult: String?
    private let logger = Logger(subsystem: "com.rudironsoni.clicks-inspector", category: "accessory")
    private var session: EASession?
    private var timeout: Timer?
    private var readTimeout: Timer?
    private var pendingRead: PendingRead?
    private var stockReplyFramer = KeyboardPacketFramer(maximumLength: 36)
    private var observers: [NSObjectProtocol] = []

    private struct PendingRead {
        let command: StockReadCommand
        let evidenceID: Int?
        var requestOffset = 0
        var accumulator: StockReadAccumulator

        init(command: StockReadCommand, evidenceID: Int?) {
            self.command = command
            self.evidenceID = evidenceID
            self.accumulator = StockReadAccumulator(command: command)
        }
    }

    var canReadSelectedCommand: Bool {
        guard !extensionSession, !extensionEditor.ownsSession, !firmwareSession, !customLinkAttached, case .open(let connectionID) = state,
              let session,
              let accessory = session.accessory,
              accessory.connectionID == connectionID,
              accessory.isConnected,
              EAAccessoryManager.shared().connectedAccessories.contains(where: { $0.connectionID == connectionID }),
              session.inputStream?.streamStatus == .open,
              session.outputStream?.streamStatus == .open,
              pendingRead == nil else { return false }
        return StockReadPolicy.allows(selectedReadCommand.request, model: accessory.modelNumber,
            firmware: accessory.firmwareRevision, hardware: accessory.hardwareRevision)
    }

    override init() {
        super.init()
        stockEvidence.recoverCheckpoint(in: Self.evidenceDirectory)
        let center = NotificationCenter.default
        for name in [Notification.Name.EAAccessoryDidConnect, .EAAccessoryDidDisconnect] {
            observers.append(center.addObserver(forName: name, object: nil, queue: .main) { [weak self] note in
                MainActor.assumeIsolated {
                    guard let self else { return }
                    let accessory = note.userInfo?[EAAccessoryKey] as? EAAccessory
                    let id = accessory?.connectionID
                    self.log("Accessory \(name == .EAAccessoryDidConnect ? "connected" : "disconnected"); connection \(id.map(String.init) ?? "unknown")")
                    if accessory?.modelNumber == "CK-5200" {
                        self.stockEvidence.state.connected = name == .EAAccessoryDidConnect
                        self.stockEvidence.record(source: "EA", kind: name == .EAAccessoryDidConnect ? "attach" : "detach")
                    }
                    if name == .EAAccessoryDidDisconnect, id == self.session?.accessory?.connectionID {
                        self.closeSession(reason: "Accessory disconnected")
                    }
                    self.refresh(reason: "Accessory notification")
                }
            })
        }
        EAAccessoryManager.shared().registerForLocalNotifications()
        log("Accessory notifications registered")
        refresh(reason: "App started")
    }

    deinit {
        timeout?.invalidate()
        readTimeout?.invalidate()
        sweepTimer?.invalidate()
        checkpointTimer?.invalidate()
        for observer in observers { NotificationCenter.default.removeObserver(observer) }
        EAAccessoryManager.shared().unregisterForLocalNotifications()
        let streams: [Stream?] = [session?.inputStream, session?.outputStream]
        for stream in streams.compactMap({ $0 }) {
            stream.delegate = nil
            stream.close()
            stream.remove(from: .main, forMode: .common)
        }
    }

    func refresh(reason: String = "User tapped Refresh") {
        log(reason)
        accessories = EAAccessoryManager.shared().connectedAccessories
        log("Found \(accessories.count) connected accessories")
        for accessory in accessories {
            log("Connection \(accessory.connectionID); model \(accessory.modelNumber); protocols \(accessory.protocolStrings.joined(separator: ", "))")
        }
        if let id = session?.accessory?.connectionID,
           !accessories.contains(where: { $0.connectionID == id && $0.isConnected }) {
            closeSession(reason: "Active accessory is no longer connected")
        }
        updateEvidenceIdentity()
        stockEvidence.record(source: "EA", kind: "accessory-snapshot")
    }

    var canOpenExtensionEditor: Bool {
        !managementBusy && !stockEvidence.isRecording && pendingRead == nil
            && stockSweepRemaining == 0 && !extensionEditor.busy
    }

    func openKeyboardSession(for accessory: EAAccessory) {
        guard canOpenExtensionEditor,
              StockReadPolicy.allows(StockReadCommand.version.request, model: accessory.modelNumber,
                                    firmware: accessory.firmwareRevision, hardware: accessory.hardwareRevision) else { return }
        openSession(for: accessory)
        identifyOnOpen = true
    }

    func openExtensionSession(for accessory: EAAccessory, capabilityReadAuthorized: Bool) {
        guard capabilityReadAuthorized, stockVersionVerified, canOpenExtensionEditor,
              session?.accessory?.connectionID == accessory.connectionID,
              accessory.modelNumber == "CK-5200", accessory.hardwareRevision == "1.2.0",
              !extensionSession, !firmwareSession, customStreamLink == nil,
              case .open = state else { return }
        // Keep the verified EASession and any partial OEM notification. Reopening
        // it here drops stream state and is not needed to send a read-only HELLO.
        extensionSession = true
        stockVersionVerified = false
        let framer = stockReplyFramer
        stockReplyFramer = KeyboardPacketFramer(maximumLength: 36)
        log("Hand off the open stock-read session to extension capability reads")
        attachCustomFirmwareLink(extensionFramer: framer)
    }

    #if targetEnvironment(simulator)
    func claimExtensionEmulator() -> Bool {
        guard canOpenExtensionEditor, !extensionSession else { return false }
        closeSession(reason: "Switch exclusively to V122 emulator")
        extensionSession = true
        return true
    }

    func claimFirmwareEmulator() -> Bool {
        guard !managementBusy, !extensionEditor.busy, !stockEvidence.isRecording,
              pendingRead == nil, stockSweepRemaining == 0 else { return false }
        closeSession(reason: "Switch exclusively to QMK emulator")
        firmwareSession = true
        return true
    }
    #endif

    func openSession(for accessory: EAAccessory, firmwareControls: Bool = false) {
        guard !managementBusy, !extensionEditor.busy,
              !(firmwareControls && stockEvidence.isRecording) else { return }
        log("User tapped Open session; connection \(accessory.connectionID)")
        closeSession(reason: "Close previous session before opening")
        firmwareSession = firmwareControls
        extensionSession = false
        guard accessory.isConnected,
              EAAccessoryManager.shared().connectedAccessories.contains(where: { $0.connectionID == accessory.connectionID }) else {
            fail("Accessory is no longer connected")
            return
        }
        guard accessory.protocolStrings.contains(Self.supportedProtocol) else {
            fail("Required protocol is not advertised")
            return
        }
        guard let newSession = EASession(accessory: accessory, forProtocol: Self.supportedProtocol),
              let input = newSession.inputStream, let output = newSession.outputStream else {
            fail("iOS could not create a session with both streams")
            return
        }
        session = newSession
        updateEvidenceIdentity()
        setState(.opening(accessory.connectionID, openedStreams: []))
        for stream in [input as Stream, output as Stream] {
            stream.delegate = self
            stream.schedule(in: .main, forMode: .common)
            stream.open()
        }
        timeout = Timer.scheduledTimer(withTimeInterval: 10, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, case .opening = self.state else { return }
                self.fail("Session streams did not open within 10 seconds")
            }
        }
    }

    func closeSession(reason: String = "User tapped Close session") {
        extensionEditor.disconnect()
        extensionSession = false
        firmwareSession = false
        identifyOnOpen = false
        stockVersionVerified = false
        stockReplyFramer = KeyboardPacketFramer(maximumLength: 36)
        log(reason)
        stockEvidence.record(source: "EA", kind: "session-close", fields: ["reason": reason])
        timeout?.invalidate()
        timeout = nil
        readTimeout?.invalidate()
        readTimeout = nil
        sweepTimer?.invalidate()
        stockQueue.removeAll()
        stockSweepRemaining = 0
        if let pending = pendingRead {
            readState = "Cancelled: \(reason)"
            if let id = pending.evidenceID {
                stockEvidence.state.session = "closed"
                stockEvidence.finishRead(id, outcome: "interrupted: \(reason)", response: pending.accumulator.bytes)
            }
        }
        pendingRead = nil
        if customLinkAttached {
            customLinkAttached = false
            /* Notify the controller exactly once: it drops the link, resumes
             * any in-flight exchange, and retains its unresolved-update
             * state; without this a session death mid-flash hung the app in
             * .busy forever (review 2026-10-11, F-7). */
            customStreamLink?.handleLoss(reason)
            customStreamLink = nil
            customFirmware.connectedTarget = nil
        } else {
            // Simulator QMK links attach directly, without EASession streams.
            // They must also relinquish ownership before an editor switch.
            customFirmware.detach()
        }
        customStreamLink = nil
        if let session {
            let streams: [Stream?] = [session.inputStream, session.outputStream]
            for stream in streams.compactMap({ $0 }) {
                stream.delegate = nil
                stream.close()
                stream.remove(from: .main, forMode: .common)
            }
        }
        session = nil
        setState(.closed)
        stockEvidence.interruptKeys(reason: reason)
        stockEvidence.checkpointInBackground(in: Self.evidenceDirectory)
    }

    func clearLogs() {
        logs.removeAll()
        log("User tapped Clear logs")
    }

    func foreground() {
        stockEvidence.state.app = "active"
        stockEvidence.record(source: "app", kind: "foreground")
        refresh(reason: "App entered foreground")
    }

    func background() {
        stockEvidence.state.app = "background"
        stockEvidence.record(source: "app", kind: "background; callbacks may stop")
        closeSession(reason: "App entered background")
        let task = UIApplication.shared.beginBackgroundTask(withName: "Save stock evidence")
        Task {
            await stockEvidence.waitForCheckpoints()
            if task != .invalid { UIApplication.shared.endBackgroundTask(task) }
        }
    }

    func inactive() {
        stockEvidence.state.app = "inactive"
        stockEvidence.record(source: "app", kind: "inactive")
        stockEvidence.interruptKeys(reason: "app inactive")
        log("App became inactive")
    }

    func readSelectedValue() {
        guard stockSweepRemaining == 0 else { return }
        performStockRead()
    }

    private func performStockRead() {
        let command = selectedReadCommand
        log("User tapped Read selected value; command 0x\(hex([command.rawValue]))")
        guard canReadSelectedCommand else {
            log("Read blocked; requires open CK-5200 firmware 1.2.2 hardware 1.2.0 session")
            readState = "Read blocked: supported CK-5200 session is not ready"
            return
        }
        lastReadResult = nil
        guard let input = session?.inputStream else {
            failRead("Input stream is unavailable")
            return
        }
        guard !input.hasBytesAvailable else {
            failRead("Input data was already queued before the request")
            return
        }
        let wasRecording = stockEvidence.isRecording
        let evidenceID = stockEvidence.beginRead(command)
        guard !wasRecording || (stockEvidence.isRecording && evidenceID != nil) else { return }
        pendingRead = PendingRead(command: command, evidenceID: evidenceID)
        readState = "Waiting to send \(command.title)"
        readTimeout?.invalidate()
        readTimeout = Timer.scheduledTimer(withTimeInterval: 5, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, self.pendingRead != nil else { return }
                self.failRead("Read timed out after 5 seconds")
            }
        }
        writePendingReadIfPossible()
    }

    nonisolated func stream(_ aStream: Stream, handle eventCode: Stream.Event) {
        // Both streams are scheduled only on the main run loop.
        MainActor.assumeIsolated { handleStream(aStream, event: eventCode) }
    }

    private func readReadyInput() {
        guard let input = session?.inputStream, input.hasBytesAvailable else { return }
        handleStream(input, event: .hasBytesAvailable)
    }

    private func handleStream(_ stream: Stream, event: Stream.Event) {
        guard let session,
              stream === session.inputStream || stream === session.outputStream else { return }
        let direction = stream === session.inputStream ? "Input" : "Output"
        if event.contains(.errorOccurred) {
            let error = stream.streamError as NSError?
            fail("\(direction) stream error; domain \(error?.domain ?? "unknown"), code \(error?.code ?? 0)")
            return
        }
        if event.contains(.endEncountered) {
            closeSession(reason: "\(direction) stream ended")
            return
        }
        if event.contains(.openCompleted) {
            log("\(direction) stream reported open")
            if case .opening(let id, var openedStreams) = state {
                openedStreams.insert(ObjectIdentifier(stream))
                if openedStreams.count == 2 {
                    timeout?.invalidate()
                    timeout = nil
                    setState(.open(id))
                    attachCustomFirmwareLink()
                    if identifyOnOpen {
                        identifyOnOpen = false
                        selectedReadCommand = .version
                        performStockRead()
                    }
                } else {
                    setState(.opening(id, openedStreams: openedStreams))
                }
            }
        }
        if event.contains(.hasBytesAvailable), let input = stream as? InputStream, input.hasBytesAvailable {
            // Do not discard a notification prefix before its session owner
            // has installed a receiver. onReceiverReady drains existing input.
            if firmwareSession || extensionSession {
                guard customStreamLink?.onFragment != nil else { return }
            }
            var buffer = [UInt8](repeating: 0, count: StockReadCommand.maximumResponseLength + 1)
            let maxLength = pendingRead.map {
                min(buffer.count, max(1, $0.accumulator.remainingCapacity + 1))
            } ?? buffer.count
            let count = input.read(&buffer, maxLength: maxLength)
            if count > 0 {
                let received = Array(buffer.prefix(count))
                stockEvidence.record(source: "EA", kind: "rx-fragment", bytes: received, readID: pendingRead?.evidenceID)
                if pendingRead != nil {
                    receiveReadFragment(received)
                } else if customLinkAttached || extensionSession {
                    /* Fragments forward exactly as delivered, in order;
                     * reassembly is the controller's job (review
                     * 2026-10-12). */
                    customStreamLink?.handleBytes(received)
                } else {
                    // Keep notification boundaries across the next read request.
                    receiveReadFragment(received)
                }
            } else if count < 0 {
                let error = input.streamError as NSError?
                fail("Input read failed; domain \(error?.domain ?? "unknown"), code \(error?.code ?? 0)")
            } else {
                closeSession(reason: "Input reached end of stream")
            }
        }
        if event.contains(.hasSpaceAvailable), stream === session.outputStream {
            writePendingReadIfPossible()
            customStreamLink?.drain()
        }
    }

    private func writePendingReadIfPossible() {
        guard var pending = pendingRead,
              let output = session?.outputStream else { return }
        let request = pending.command.request
        while pending.requestOffset < request.count, output.hasSpaceAvailable {
            let remaining = Array(request.dropFirst(pending.requestOffset))
            let written = remaining.withUnsafeBufferPointer { buffer -> Int in
                guard let baseAddress = buffer.baseAddress else { return 0 }
                return output.write(baseAddress, maxLength: buffer.count)
            }
            guard written >= 0 else {
                let error = output.streamError as NSError?
                failRead("Output write failed; domain \(error?.domain ?? "unknown"), code \(error?.code ?? 0)")
                return
            }
            guard written > 0 else { break }
            log("TX \(hex(Array(remaining.prefix(written))))")
            stockEvidence.record(source: "EA", kind: "tx-fragment", bytes: Array(remaining.prefix(written)), readID: pending.evidenceID)
            pending.requestOffset += written
            if pending.requestOffset == request.count, let id = pending.evidenceID { stockEvidence.sent(id) }
        }
        pendingRead = pending
        if pending.requestOffset == request.count {
            readState = "Waiting for response"
        } else if pending.requestOffset == 0 {
            readState = "Waiting for output space"
        } else {
            readState = "Sent \(pending.requestOffset)/\(request.count) request bytes"
        }
    }

    private func receiveReadFragment(_ fragment: [UInt8]) {
        log("RX \(hex(fragment))")
        do {
            let ready = pendingRead.map { $0.requestOffset == $0.command.request.count } ?? false
            guard let packet = try stockReplyFramer.append(fragment, awaitingReply: ready), var pending = pendingRead else { return }
            let result: StockReadResult?
            do { result = try pending.accumulator.append(packet) }
            catch { pendingRead = pending; throw error }
            if let result {
                if let id = pending.evidenceID {
                    stockEvidence.finishRead(id, outcome: "validated", response: pending.accumulator.bytes)
                }
                pendingRead = nil
                readTimeout?.invalidate()
                readTimeout = nil
                lastReadResult = result.summary
                if pending.command == .version { stockVersionVerified = true }
                readState = "Completed"
                log("Read completed: \(result.summary)")
                scheduleNextStockRead()
                return
            }
            pendingRead = pending
            readState = "Receiving \(pending.accumulator.bytes.count)/\(pending.command.expectedResponseLength) response bytes"
        } catch {
            failRead("Invalid \(pendingRead?.command.title ?? "stock") response; \(error)")
        }
    }

    #if DEBUG && targetEnvironment(simulator)
    /// Simulator integration fixture: exercises the real receiver and cancellation path.
    /// No EASession or output stream exists, so this cannot send a packet.
    func simulateVersionResponse(_ bytes: [UInt8]) {
        stockEvidence.start(environment: ["source": "simulator iOS callbacks; no physical accessory evidence"])
        stockEvidence.state.accessory = StockAccessoryIdentity(model: "CK-5200", firmware: "1.2.2", hardware: "1.2.0", protocols: [Self.supportedProtocol])
        stockQueue = [.brightness, .backlightDelay]
        stockSweepRemaining = 3
        pendingRead = PendingRead(command: .version, evidenceID: stockEvidence.beginRead(.version))
        pendingRead?.requestOffset = 2
        receiveReadFragment(bytes)
    }
    #endif

    private func failRead(_ reason: String) {
        log("Read failed: \(reason)")
        stockEvidence.state.session = "failed"
        stockEvidence.record(source: "EA", kind: "read-error", fields: ["reason": reason], readID: pendingRead?.evidenceID)
        if let pending = pendingRead, let id = pending.evidenceID {
            stockEvidence.finishRead(id, outcome: "failed: \(reason)", response: pending.accumulator.bytes)
        }
        closeSession(reason: "Read failed")
        readState = "Failed: \(reason)"
        setState(.failed(reason))
    }

    private func fail(_ reason: String) {
        stockEvidence.record(source: "EA", kind: "error", fields: ["reason": reason])
        closeSession(reason: reason)
        setState(.failed(reason))
    }

    private func setState(_ next: SessionState) {
        state = next
        switch next {
        case .closed: stockEvidence.state.session = "closed"
        case .opening: stockEvidence.state.session = "opening"
        case .open: stockEvidence.state.session = "open"
        case .failed: stockEvidence.state.session = "failed"
        }
        stockEvidence.record(source: "EA", kind: "session-state", fields: ["state": stockEvidence.state.session])
        log("Session state: \(next.description)")
    }

    private func updateEvidenceIdentity() {
        let connected = EAAccessoryManager.shared().connectedAccessories
        let accessory = connected.first { $0.connectionID == session?.accessory?.connectionID }
            ?? connected.first { $0.modelNumber == "CK-5200" }
        stockEvidence.state.connected = accessory?.isConnected ?? false
        stockEvidence.state.accessory = nil
        if let accessory {
            stockEvidence.state.accessory = StockAccessoryIdentity(model: accessory.modelNumber,
                firmware: accessory.firmwareRevision, hardware: accessory.hardwareRevision, protocols: accessory.protocolStrings)
        }
    }

    func startStockRecording() {
        guard !managementBusy, !extensionEditor.busy, !stockEvidence.isRecording else { return }
        if extensionSession || extensionEditor.ownsSession || firmwareSession || pendingRead != nil { closeSession(reason: "Switch to stock characterization") }
        firmwareSession = false
        selectedReadCommand = .version
        readState = "Not started"
        lastReadResult = nil
        updateEvidenceIdentity()
        switch UIApplication.shared.applicationState {
        case .active: stockEvidence.state.app = "active"
        case .background: stockEvidence.state.app = "background"
        default: stockEvidence.state.app = "inactive"
        }
        #if targetEnvironment(simulator)
        let observationSource = "simulator iOS callbacks; no physical accessory evidence"
        #else
        let observationSource = "physical iOS API observations; not a firmware-image fixture"
        #endif
        stockEvidence.start(environment: [
            "iOS": UIDevice.current.systemVersion,
            "appVersion": Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "unknown",
            "appBuild": Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "unknown",
            "source": observationSource,
        ])
        stockEvidence.checkpointInBackground(in: Self.evidenceDirectory)
        checkpointTimer?.invalidate()
        checkpointTimer = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self else { return }
                self.stockEvidence.checkpointInBackground(in: Self.evidenceDirectory)
                if !self.stockEvidence.isRecording { self.checkpointTimer?.invalidate() }
            }
        }
    }

    func stopStockRecording() {
        closeSession(reason: "Stop stock characterization")
        stockEvidence.stop()
        checkpointTimer?.invalidate()
        stockEvidence.checkpointInBackground(in: Self.evidenceDirectory)
    }

    func clearStockEvidence() {
        guard !stockEvidence.isRecording, !managementBusy, !extensionEditor.busy else { return }
        closeSession(reason: "Clear stock evidence")
        checkpointTimer?.invalidate()
        stockEvidence.clear(in: Self.evidenceDirectory)
        logs.removeAll()
        readState = "Not started"
        lastReadResult = nil
        // Do not clear the firmware controller's durable unresolved-update record.
    }

    func readStockBaseline() {
        guard stockEvidence.isRecording, canReadSelectedCommand, stockQueue.isEmpty,
              stockSweepRemaining == 0 else { return }
        stockQueue = (0..<3).flatMap { _ in StockReadPolicy.demonstrated }
        stockSweepRemaining = stockQueue.count
        nextStockRead()
    }

    private func nextStockRead() {
        guard stockEvidence.isRecording, !stockQueue.isEmpty else {
            stockQueue.removeAll()
            stockSweepRemaining = 0
            return
        }
        selectedReadCommand = stockQueue.removeFirst()
        stockSweepRemaining = stockQueue.count + 1
        performStockRead()
        if pendingRead == nil {
            stockQueue.removeAll()
            stockSweepRemaining = 0
        }
    }

    private func scheduleNextStockRead() {
        guard stockSweepRemaining > 0 else { return }
        stockSweepRemaining = stockQueue.count
        guard !stockQueue.isEmpty else {
            stockEvidence.record(source: "EA", kind: "three-pass-baseline-completed")
            stockEvidence.checkpointInBackground(in: Self.evidenceDirectory)
            return
        }
        sweepTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated { self?.nextStockRead() }
        }
    }

    private func log(_ message: String) {
        logger.info("\(message, privacy: .public)")
        logs.append(LogEntry(message: message))
        if logs.count > 200 { logs.removeFirst(logs.count - 200) }
    }

    private func hex(_ bytes: [UInt8]) -> String {
        bytes.map { String(format: "%02x", $0) }.joined(separator: " ")
    }
}
