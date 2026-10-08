import Combine
import ExternalAccessory
import Foundation
import OSLog

@MainActor
final class AccessoryInspector: NSObject, ObservableObject, StreamDelegate {
    static let supportedProtocol = "com.clickscompanion.protocol"

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
    private var observers: [NSObjectProtocol] = []

    private struct PendingRead {
        let command: StockReadCommand
        var requestOffset = 0
        var accumulator: StockReadAccumulator

        init(command: StockReadCommand) {
            self.command = command
            self.accumulator = StockReadAccumulator(command: command)
        }
    }

    var canReadSelectedCommand: Bool {
        guard case .open(let connectionID) = state,
              let session,
              let accessory = session.accessory,
              accessory.connectionID == connectionID,
              accessory.isConnected,
              EAAccessoryManager.shared().connectedAccessories.contains(where: { $0.connectionID == connectionID }),
              session.inputStream?.streamStatus == .open,
              session.outputStream?.streamStatus == .open,
              pendingRead == nil else { return false }
        return accessory.modelNumber == "CK-5200"
            && accessory.firmwareRevision == "1.2.2"
            && accessory.hardwareRevision == "1.2.0"
    }

    override init() {
        super.init()
        let center = NotificationCenter.default
        for name in [Notification.Name.EAAccessoryDidConnect, .EAAccessoryDidDisconnect] {
            observers.append(center.addObserver(forName: name, object: nil, queue: .main) { [weak self] note in
                MainActor.assumeIsolated {
                    guard let self else { return }
                    let accessory = note.userInfo?[EAAccessoryKey] as? EAAccessory
                    let id = accessory?.connectionID
                    self.log("Accessory \(name == .EAAccessoryDidConnect ? "connected" : "disconnected"); connection \(id.map(String.init) ?? "unknown")")
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
    }

    func openSession(for accessory: EAAccessory) {
        log("User tapped Open session; connection \(accessory.connectionID)")
        closeSession(reason: "Close previous session before opening")
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
        log(reason)
        timeout?.invalidate()
        timeout = nil
        readTimeout?.invalidate()
        readTimeout = nil
        if pendingRead != nil { readState = "Cancelled: \(reason)" }
        pendingRead = nil
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
    }

    func clearLogs() {
        logs.removeAll()
        log("User tapped Clear logs")
    }

    func foreground() {
        refresh(reason: "App entered foreground")
    }

    func background() {
        closeSession(reason: "App entered background")
    }

    func inactive() {
        log("App became inactive")
    }

    func readSelectedValue() {
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
        pendingRead = PendingRead(command: command)
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
                } else {
                    setState(.opening(id, openedStreams: openedStreams))
                }
            }
        }
        if event.contains(.hasBytesAvailable), let input = stream as? InputStream {
            var buffer = [UInt8](repeating: 0, count: StockReadCommand.maximumResponseLength + 1)
            let maxLength = pendingRead.map {
                min(buffer.count, max(1, $0.accumulator.remainingCapacity + 1))
            } ?? buffer.count
            let count = input.read(&buffer, maxLength: maxLength)
            if count > 0 {
                let received = Array(buffer.prefix(count))
                if pendingRead != nil {
                    receiveReadFragment(received)
                } else {
                    log("Unsolicited input received \(count) bytes; payload discarded")
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
            pending.requestOffset += written
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
        guard var pending = pendingRead else {
            log("Unsolicited input received \(fragment.count) bytes; payload discarded")
            return
        }
        guard pending.requestOffset == pending.command.request.count else {
            log("Unsolicited input received \(fragment.count) bytes before full request; payload discarded")
            failRead("Input arrived before the request was fully sent")
            return
        }
        log("RX \(hex(fragment))")
        do {
            if let result = try pending.accumulator.append(fragment) {
                pendingRead = nil
                readTimeout?.invalidate()
                readTimeout = nil
                lastReadResult = result.summary
                readState = "Completed"
                log("Read completed: \(result.summary)")
                return
            }
            pendingRead = pending
            readState = "Receiving \(pending.accumulator.bytes.count)/\(pending.command.expectedResponseLength) response bytes"
        } catch {
            failRead("Invalid \(pending.command.title) response; \(error)")
        }
    }

    private func failRead(_ reason: String) {
        log("Read failed: \(reason)")
        closeSession(reason: "Read failed")
        readState = "Failed: \(reason)"
        setState(.failed(reason))
    }

    private func fail(_ reason: String) {
        closeSession(reason: reason)
        setState(.failed(reason))
    }

    private func setState(_ next: SessionState) {
        state = next
        log("Session state: \(next.description)")
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
