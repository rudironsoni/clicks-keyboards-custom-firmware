import Foundation

@main
struct StockEvidenceTests {
    static let identity = StockAccessoryIdentity(model: "CK-5200", firmware: "1.2.2", hardware: "1.2.0",
                                                 protocols: ["com.clickscompanion.protocol"])

    @MainActor
    static func main() throws {
        if CommandLine.arguments.contains("--session-reset") {
            resetsObservationState()
            return
        }
        try recordsRealInputsAndExports()
        try refusesUnsafeReadsAndPreservesUncertainty()
        try auditsEveryExportSurfaceAndClearsData()
        resetsObservationState()
        print("3 stock evidence checks passed (host simulation, no physical device)")
    }

    @MainActor
    static func resetsObservationState() {
        let recorder = StockEvidenceRecorder()
        recorder.start(environment: [:])
        let first = recorder.bundle!.sessionID
        recorder.state.condition = "normal charging"
        recorder.state.backlight = "on"
        recorder.state.charging = "connected; iOS charging"
        recorder.state.focus = "test-text-input"
        recorder.stop()
        // The session owner has just refreshed these live fields.
        recorder.state.accessory = identity
        recorder.state.connected = true
        recorder.state.session = "open"
        recorder.start(environment: [:])
        precondition(recorder.bundle!.sessionID != first)
        precondition(recorder.state.condition == "baseline", "new session kept old condition")
        precondition(recorder.state.focus == "none" && recorder.state.backlight == "not-observed")
        precondition(recorder.state.charging == "not-observed")
        precondition(recorder.state.accessory == identity && recorder.state.connected && recorder.state.session == "open")
        precondition(recorder.bundle!.events.count == 1 && recorder.bundle!.events[0].state == recorder.state)
        print("PASS two-session reset")
    }

    @MainActor
    static func recordsRealInputsAndExports() throws {
        var clock = 100.0
        let recorder = StockEvidenceRecorder(now: { Date(timeIntervalSince1970: clock) }, uptime: { clock })
        recorder.state.accessory = identity
        recorder.state.connected = true
        recorder.state.session = "open"
        recorder.start(environment: ["test": "host simulation"])
        let reply: [UInt8] = [5, 2, 0x84, 0, 0xff] // Test fixture, never physical evidence.
        for pass in 0..<3 {
            let observedReply = pass == 2 ? [5, 2, 0x84, 0, 0xfe] as [UInt8] : reply
            let id = recorder.beginRead(.brightness)!
            precondition(recorder.beginRead(.version) == nil, "Concurrent reads must fail")
            clock += 0.010
            recorder.record(source: "EA", kind: "tx-fragment", bytes: [2, 0x84], readID: id)
            recorder.sent(id)
            var parser = StockReadAccumulator(command: .brightness)
            for fragment in [Array(observedReply.prefix(2)), Array(observedReply.dropFirst(2))] {
                clock += 0.025
                recorder.record(source: "EA", kind: "rx-fragment", bytes: fragment, readID: id)
                _ = try parser.append(fragment)
            }
            recorder.state.backlight = pass == 0 ? "on" : "off"
            recorder.finishRead(id, outcome: "validated", response: parser.bytes)
        }
        recorder.key(source: "UIPress", phase: "down", code: 4, modifiers: 2, characters: "a")
        clock += 0.5
        recorder.key(source: "UIPress", phase: "down", code: 4, characters: "a")
        recorder.key(source: "GCKeyboard", phase: "down", code: 4)
        recorder.key(source: "UIPress", phase: "up", code: 4)
        recorder.record(source: "text-input", kind: "replacement-proposed", fields: ["text": "private-test-text"])
        recorder.interruptKeys(reason: "background")
        recorder.stop()
        let count = recorder.eventCount
        recorder.key(source: "UIPress", phase: "down", code: 5)
        precondition(recorder.eventCount == count)
        precondition(recorder.bundle!.reads.count == 3)
        precondition(abs(recorder.bundle!.reads[0].responseLatencyMS! - 50) < 0.01)
        precondition(recorder.bundle!.reads[0].before.backlight == "not-observed")
        precondition(recorder.bundle!.reads[0].after?.backlight == "on")
        precondition(recorder.bundle!.reads[1].sameAsPrevious == true)
        precondition(recorder.bundle!.reads[2].sameAsPrevious == false)
        precondition(recorder.bundle!.events.contains { $0.fields["repeatedDownWithoutRelease"] == "true" })
        precondition(recorder.bundle!.events.contains { $0.fields["unresolvedPresses"] == "GCKeyboard:4" })
        let local = String(decoding: try recorder.encoded(shareable: false), as: UTF8.self)
        let shared = String(decoding: try recorder.encoded(shareable: true), as: UTF8.self)
        precondition(local.contains("private-test-text") && !shared.contains("private-test-text"))
        let textExport = String(decoding: try recorder.encoded(shareable: true, includeTestText: true), as: UTF8.self)
        precondition(textExport.contains("private-test-text"))
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        let decoded = try decoder.decode(StockEvidenceBundle.self, from: recorder.encoded(shareable: true))
        precondition(decoded.schemaVersion == 2 && decoded.reads[0].response == reply)
        precondition(decoded.events.filter { $0.kind == "rx-fragment" }.compactMap(\.bytes).count == 6)
        precondition(!shared.contains("serialNumber") && !shared.contains("connectionID"))
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        recorder.checkpoint(in: directory)
        recorder.export(in: directory, includeTestText: false)
        precondition(recorder.storageError == nil && recorder.exportURLs.count == 2)
        let recovered = StockEvidenceRecorder()
        recovered.recoverCheckpoint(in: directory)
        precondition(recovered.bundle?.sessionID == recorder.bundle?.sessionID && !recovered.isRecording)
        precondition(recorder.report.contains("R3 independent recovery: UNMET"))
    }

    @MainActor
    static func refusesUnsafeReadsAndPreservesUncertainty() throws {
        // Exhaust the policy locally. No opcode is sent to a device by this test.
        let allowed: Set<UInt8> = [0x03, 0x84, 0x86, 0x88, 0x8a, 0x8c, 0x8d]
        for opcode in UInt8.min...UInt8.max {
            precondition(StockReadPolicy.allows([2, opcode], model: "CK-5200", firmware: "1.2.2", hardware: "1.2.0") == allowed.contains(opcode))
            precondition(!StockReadPolicy.allows([3, opcode, 0], model: "CK-5200", firmware: "1.2.2", hardware: "1.2.0"))
        }
        for command in StockReadCommand.allCases {
            precondition(!StockReadPolicy.allows(command.request, model: "CK-5200", firmware: "custom", hardware: "1.2.0"))
            precondition(!StockReadPolicy.allows(command.request, model: "other", firmware: "1.2.2", hardware: "1.2.0"))
            precondition(!StockReadPolicy.allows(command.request, model: "CK-5200", firmware: "1.2.2", hardware: "1.2.1"))
        }
        let recorder = StockEvidenceRecorder()
        recorder.start(environment: ["test": "host simulation"])
        precondition(recorder.beginRead(.version) == nil)
        recorder.state.accessory = identity
        precondition(recorder.beginRead(.checksumService) == nil && recorder.beginRead(.newerUserKeyProbe) == nil)
        let id = recorder.beginRead(.version)!
        recorder.record(source: "EA", kind: "rx-fragment", bytes: Array("chip-identifier".utf8), readID: id)
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        recorder.checkpoint(in: directory) // Simulate process loss before a reply completes.
        let recovered = StockEvidenceRecorder()
        recovered.recoverCheckpoint(in: directory)
        precondition(!recovered.isRecording && recovered.bundle!.reads[0].outcome.contains("interrupted"))
        recorder.finishRead(id, outcome: "failed: timeout", response: Array("chip-identifier".utf8))
        recorder.stop()
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        let shared = try decoder.decode(StockEvidenceBundle.self, from: recorder.encoded(shareable: true))
        precondition(shared.reads[0].response.isEmpty && shared.reads[0].sameAsPrevious == nil)
        let fragment = shared.events.first { $0.kind == "rx-fragment" }!
        precondition(fragment.bytes == nil && fragment.byteCount == 15)
        recorder.start(environment: ["test": "event limit"])
        for _ in 0...StockEvidenceRecorder.eventLimit { recorder.record(source: "test", kind: "event") }
        precondition(!recorder.isRecording && recorder.eventCount == StockEvidenceRecorder.eventLimit)
        precondition(recorder.bundle!.endReason!.contains("event-limit"))
    }

    @MainActor
    static func auditsEveryExportSurfaceAndClearsData() throws {
        let secret = "PRIVATE_SENTINEL_NOT_FOR_SHARING"
        let recorder = StockEvidenceRecorder()
        recorder.state.accessory = identity
        recorder.start(environment: ["iOS": secret, "appVersion": secret, "appBuild": secret, "source": secret, "phoneName": secret])
        let id = recorder.beginRead(.brightness)!
        let valid: [UInt8] = [5, 2, 0x84, 0, 42]
        recorder.record(source: "EA", kind: "rx-fragment", bytes: valid, readID: id)
        recorder.finishRead(id, outcome: "validated", response: valid)
        // A validated read ID alone must never authorize arbitrary event bytes.
        for kind in ["request", "tx-fragment", "rx-fragment", "future-payload"] {
            recorder.record(source: "EA", kind: kind, bytes: Array(secret.utf8), readID: id)
        }
        let malformedID = recorder.beginRead(.version)!
        recorder.finishRead(malformedID, outcome: "validated", response: Array(secret.utf8))
        let failedID = recorder.beginRead(.statusFlags)!
        recorder.finishRead(failedID, outcome: "failed: \(secret)")
        recorder.state.accessory = StockAccessoryIdentity(model: secret, firmware: secret, hardware: secret, protocols: [secret])
        recorder.state.session = secret
        recorder.state.app = secret
        recorder.state.condition = secret
        recorder.state.backlight = secret
        recorder.state.charging = secret
        recorder.state.focus = secret
        recorder.record(source: secret, kind: secret, fields: [
            "reason": secret, "outcome": "failed: \(secret)", "meaning": secret, "newFreeFormField": secret,
            "code": secret, "apiTimestamp": secret, "physicalKey": secret, "combination": secret,
            "heldInSource": secret, "unresolvedPresses": secret, "text": "intentional-test-text", "characters": "intentional-test-text",
        ])
        recorder.stop(reason: secret)
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        recorder.checkpoint(in: directory)
        // Exercise the same recovered checkpoint route used by the shipping app.
        let recovered = StockEvidenceRecorder()
        recovered.recoverCheckpoint(in: directory)
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        for includeText in [false, true] {
            recovered.export(in: directory, includeTestText: includeText)
            precondition(recovered.storageError == nil && recovered.exportURLs.count == 2)
            for url in recovered.exportURLs {
                let contents = try String(contentsOf: url, encoding: .utf8)
                precondition(!contents.contains(secret) && !url.lastPathComponent.contains(secret))
                precondition(contents.contains("intentional-test-text") == (includeText && url.pathExtension == "json"))
            }
            let shared = try decoder.decode(StockEvidenceBundle.self, from: Data(contentsOf: recovered.exportURLs[0]))
            precondition(shared.reads[0].response == valid)
            precondition(shared.reads[1].response.isEmpty && shared.reads[1].outcome.hasPrefix("failed"))
            precondition(shared.events.filter { $0.kind == "rx-fragment" || $0.kind == "tx-fragment" }.allSatisfy { $0.bytes == nil })
            precondition(shared.startedAt.timeIntervalSince1970.rounded(.down) == recorder.bundle!.startedAt.timeIntervalSince1970.rounded(.down))
        }
        let unrelated = directory.appendingPathComponent("unrelated.txt")
        try Data("preserve".utf8).write(to: unrelated)
        recovered.clear(in: unrelated) // Deterministic I/O failure, not a permission-dependent mock.
        precondition(recovered.storageError != nil && recovered.bundle != nil)
        let resetID = recovered.textResetID
        recovered.clear(in: directory)
        precondition(recovered.storageError == nil && recovered.bundle == nil && recovered.eventCount == 0)
        precondition(recovered.exportURLs.isEmpty && recovered.textResetID != resetID && !recovered.isRecording)
        let files = try FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil)
        precondition(files.map(\.lastPathComponent) == ["unrelated.txt"])
        recovered.checkpoint(in: directory) // A later timer must not recreate deleted evidence.
        let empty = StockEvidenceRecorder()
        empty.recoverCheckpoint(in: directory)
        precondition(empty.bundle == nil)
    }
}
