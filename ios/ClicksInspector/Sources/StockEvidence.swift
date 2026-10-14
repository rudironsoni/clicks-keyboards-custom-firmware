import Combine
import Foundation

struct StockAccessoryIdentity: Codable, Equatable {
    // Deliberately omit serialNumber, connectionID, phone name, and chip identifiers.
    let model: String
    let firmware: String
    let hardware: String
    let protocols: [String]
}

struct StockObservationState: Codable, Equatable {
    var accessory: StockAccessoryIdentity?
    var connected = false
    var session = "closed"
    var app = "active"
    var condition = "baseline"
    var backlight = "not-observed"
    var charging = "not-observed"
    var focus = "none"
}

struct StockEvidenceEvent: Codable {
    let sequence: Int
    let timestamp: Date
    let elapsedMS: Double
    let source: String
    let kind: String
    let state: StockObservationState
    var fields: [String: String]
    var bytes: [UInt8]?
    let byteCount: Int?
    let readID: Int?
}

struct StockReadObservation: Codable {
    let id: Int
    let command: UInt8
    let request: [UInt8]
    let startedMS: Double
    let before: StockObservationState
    var sentMS: Double?
    var endedMS: Double?
    var responseLatencyMS: Double?
    var after: StockObservationState?
    var response: [UInt8] = []
    var outcome = "pending"
    var sameAsPrevious: Bool?
}

struct StockEvidenceBundle: Codable {
    let schemaVersion: Int
    let sessionID: UUID
    let startedAt: Date
    var endedAt: Date?
    var endReason: String?
    let environment: [String: String]
    var commandInventory = "CK-5200-V122-H120-v1"
    var privacy = "local: identifying metadata omitted; test text and raw stream bytes retained"
    var reads: [StockReadObservation] = []
    var events: [StockEvidenceEvent] = []
}

/// Records caller-delivered observations. Host tests supply explicitly labeled simulations.
@MainActor
final class StockEvidenceRecorder: ObservableObject {
    @Published private(set) var isRecording = false
    @Published private(set) var eventCount = 0
    @Published private(set) var exportURLs: [URL] = []
    @Published private(set) var exportedWithTestText: Bool?
    @Published private(set) var storageError: String?
    private(set) var bundle: StockEvidenceBundle?
    var state = StockObservationState()
    private let now: () -> Date
    private let uptime: () -> Double
    private var startUptime = 0.0
    private var held: [String: Double] = [:]
    static let eventLimit = 20_000

    init(now: @escaping () -> Date = Date.init,
         uptime: @escaping () -> Double = { ProcessInfo.processInfo.systemUptime }) {
        self.now = now
        self.uptime = uptime
    }

    var elapsedMS: Double { max(0, (uptime() - startUptime) * 1000) }

    func start(environment: [String: String]) {
        guard !isRecording else { return }
        startUptime = uptime()
        bundle = StockEvidenceBundle(schemaVersion: 1, sessionID: UUID(), startedAt: now(), environment: environment)
        held.removeAll()
        eventCount = 0
        exportURLs = []
        exportedWithTestText = nil
        storageError = nil
        isRecording = true
        record(source: "app", kind: "session-start")
    }

    func record(source: String, kind: String, fields: [String: String] = [:],
                bytes: [UInt8]? = nil, readID: Int? = nil) {
        guard isRecording else { return }
        guard eventCount < Self.eventLimit else {
            stop(reason: "event-limit; remaining observations not captured")
            return
        }
        bundle?.events.append(StockEvidenceEvent(sequence: eventCount, timestamp: now(), elapsedMS: elapsedMS,
            source: source, kind: kind, state: state, fields: fields, bytes: bytes,
            byteCount: bytes?.count, readID: readID))
        eventCount += 1
    }

    func beginRead(_ command: StockReadCommand) -> Int? {
        guard isRecording, let identity = state.accessory,
              StockReadPolicy.allows(command.request, model: identity.model,
                  firmware: identity.firmware, hardware: identity.hardware),
              bundle?.reads.last?.outcome != "pending" else { return nil }
        let id = bundle?.reads.count ?? 0
        bundle?.reads.append(StockReadObservation(id: id, command: command.rawValue, request: command.request,
                                                 startedMS: elapsedMS, before: state))
        record(source: "EA", kind: "request", bytes: command.request, readID: id)
        return id
    }

    func sent(_ id: Int) {
        guard isRecording, bundle?.reads.indices.contains(id) == true else { return }
        bundle?.reads[id].sentMS = elapsedMS
    }

    func finishRead(_ id: Int, outcome: String, response: [UInt8] = []) {
        guard let current = bundle?.reads, current.indices.contains(id), current[id].outcome == "pending" else { return }
        var read = current[id]
        read.endedMS = elapsedMS
        read.responseLatencyMS = read.sentMS.map { max(0, elapsedMS - $0) }
        read.after = state
        read.response = response
        read.outcome = outcome
        if outcome == "validated", let previous = current.prefix(id).last(where: {
            $0.command == read.command && $0.outcome == "validated"
        }) {
            read.sameAsPrevious = previous.response == response
        }
        bundle?.reads[id] = read
        record(source: "EA", kind: "read-finished", fields: ["outcome": outcome], readID: id)
    }

    func key(source: String, phase: String, code: Int, modifiers: Int? = nil,
             characters: String? = nil, timestamp: Double? = nil) {
        guard isRecording else { return }
        let token = "\(source):\(code)"
        var fields = ["code": String(code), "phase": phase]
        if let modifiers { fields["modifiers"] = String(modifiers) }
        if let characters { fields["characters"] = String(characters.prefix(256)) }
        if let timestamp { fields["apiTimestamp"] = String(timestamp) }
        if phase == "down" {
            fields["repeatedDownWithoutRelease"] = String(held[token] != nil)
            if held[token] == nil { held[token] = elapsedMS }
        } else if phase == "up" || phase == "cancelled" {
            if let started = held.removeValue(forKey: token) {
                fields["observedHeldMS"] = String(max(0, elapsedMS - started))
            } else { fields["releaseWithoutObservedDown"] = "true" }
        }
        fields["heldInSource"] = held.keys.filter { $0.hasPrefix(source + ":") }.sorted().joined(separator: ",")
        record(source: source, kind: "key", fields: fields)
    }

    func interruptKeys(reason: String) {
        guard isRecording else { return }
        // Do not invent releases when iOS stops delivering events.
        record(source: "app", kind: "key-observation-gap", fields: [
            "reason": reason, "unresolvedPresses": held.keys.sorted().joined(separator: ","),
        ])
        held.removeAll()
    }

    func stop(reason: String = "user-stop") {
        guard isRecording else { return }
        // Seal directly so a full event buffer cannot recursively call stop().
        if let last = bundle?.reads.last, last.outcome == "pending" {
            bundle?.reads[last.id].outcome = "interrupted: \(reason)"
            bundle?.reads[last.id].after = state
            bundle?.reads[last.id].endedMS = elapsedMS
        }
        if eventCount < Self.eventLimit { interruptKeys(reason: reason) }
        held.removeAll()
        bundle?.endedAt = now()
        bundle?.endReason = reason
        isRecording = false
    }

    func encoded(shareable: Bool, includeTestText: Bool = false) throws -> Data {
        guard var copy = bundle else { throw CocoaError(.fileNoSuchFile) }
        if shareable {
            copy.privacy = includeTestText
                ? "shareable: identifiers and unvalidated bytes omitted; test text explicitly included"
                : "shareable: identifiers, test text and unvalidated bytes omitted"
            let validatedIDs = Set(copy.reads.filter { $0.outcome == "validated" }.map(\.id))
            for index in copy.events.indices {
                let event = copy.events[index]
                if event.bytes != nil && event.kind != "request" && event.kind != "tx-fragment"
                    && !validatedIDs.contains(event.readID ?? -1) {
                    copy.events[index].bytes = nil
                    copy.events[index].fields["redaction"] = "unvalidated stream payload"
                }
                if !includeTestText {
                    for key in ["characters", "text"] where event.fields[key] != nil {
                        copy.events[index].fields[key + "Length"] = String(event.fields[key]!.count)
                        copy.events[index].fields.removeValue(forKey: key)
                    }
                }
            }
            for index in copy.reads.indices where copy.reads[index].outcome != "validated" {
                copy.reads[index].response = []
            }
        }
        let encoder = JSONEncoder()
        encoder.outputFormatting = shareable ? [.prettyPrinted, .sortedKeys] : [.sortedKeys]
        encoder.dateEncodingStrategy = .iso8601
        return try encoder.encode(copy)
    }

    var report: String {
        guard let bundle else { return "No stock observations." }
        let good = bundle.reads.filter { $0.outcome == "validated" }
        let changed = good.filter { $0.sameAsPrevious == false }
        let latencies = good.compactMap(\.responseLatencyMS)
        let lines = StockReadPolicy.demonstrated.map { command in
            let reads = good.filter { $0.command == command.rawValue }
            let values = Set(reads.map { $0.response.map { String(format: "%02x", $0) }.joined(separator: " ") }).sorted()
            return "- \(command.title): \(reads.count) validated. Replies: \(values.joined(separator: " / "))"
        }
        return """
        # Stock characterization v1
        Session: \(bundle.sessionID)
        Source: \(bundle.environment["source"] ?? bundle.environment["test"] ?? "unknown")
        iOS: \(bundle.environment["iOS"] ?? "n/a"). App: \(bundle.environment["appVersion"] ?? "n/a") (\(bundle.environment["appBuild"] ?? "n/a")).
        Status: \(bundle.endReason ?? "recording; snapshot only")
        Events: \(bundle.events.count). Reads: \(bundle.reads.count). Validated: \(good.count).
        Failed/interrupted: \(bundle.reads.filter { $0.outcome != "validated" && $0.outcome != "pending" }.count).
        Replies changed from previous read: \(changed.count). Changes can reflect operating conditions.
        Response latency range (ms): \(latencies.min().map { String(format: "%.2f", $0) } ?? "n/a") to \(latencies.max().map { String(format: "%.2f", $0) } ?? "n/a").
        \(lines.joined(separator: "\n"))

        Evidence: iOS callbacks and user-marked conditions, not USB captures.
        Key events: \(bundle.events.filter { $0.kind == "key" }.count). Text edits: \(bundle.events.filter { $0.source == "text-input" }.count).
        Key observation gaps: \(bundle.events.filter { $0.kind == "key-observation-gap" }.count).
        See JSON for timestamps, fragments, before/after state, held keys, and per-read outcomes.
        Serial numbers and chip identifiers are omitted as metadata. This report omits typed text.

        Unobservable: raw HID reports, matrix crossings, electrical debounce, MFi exchanges,
        system-consumed shortcuts, events while suspended, installer state, and independent recovery.
        No callback does not prove that firmware sent no event. GCKeyboard is a coalesced source.
        R3 independent recovery: UNMET. No firmware write occurs in this mode.
        """
    }

    /// Checkpoints are local and exact. Share exports use a separate, redacted projection.
    func checkpoint(in directory: URL) {
        guard bundle != nil else { return }
        do {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            try encoded(shareable: false).write(to: directory.appendingPathComponent("stock-local.json"), options: .atomic)
            storageError = nil
        } catch { storageError = "Evidence save failed: \(error.localizedDescription)" }
    }

    func export(in directory: URL, includeTestText: Bool) {
        guard let bundle else { return }
        do {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            let base = "stock-\(bundle.sessionID)-\(includeTestText ? "test-text" : "redacted")"
            let json = directory.appendingPathComponent(base + ".json")
            let markdown = directory.appendingPathComponent(base + ".md")
            try encoded(shareable: true, includeTestText: includeTestText).write(to: json, options: .atomic)
            try report.write(to: markdown, atomically: true, encoding: .utf8)
            exportedWithTestText = includeTestText
            exportURLs = [json, markdown]
            storageError = nil
        } catch { storageError = "Evidence export failed: \(error.localizedDescription)" }
    }

    func recoverCheckpoint(in directory: URL) {
        guard bundle == nil else { return }
        let file = directory.appendingPathComponent("stock-local.json")
        guard FileManager.default.fileExists(atPath: file.path) else { return }
        do {
            let decoder = JSONDecoder()
            decoder.dateDecodingStrategy = .iso8601
            var recovered = try decoder.decode(StockEvidenceBundle.self, from: Data(contentsOf: file))
            guard recovered.schemaVersion == 1 else { throw CocoaError(.fileReadCorruptFile) }
            if recovered.endedAt == nil {
                recovered.endReason = "interrupted; recovered last checkpoint, later events unknown"
                for index in recovered.reads.indices where recovered.reads[index].outcome == "pending" {
                    recovered.reads[index].outcome = "interrupted; outcome not observed"
                }
            }
            bundle = recovered
            eventCount = recovered.events.count
        } catch { storageError = "Evidence recovery failed: \(error.localizedDescription)" }
    }
}
