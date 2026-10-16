import Foundation

@main
struct StockTrialTests {
    @MainActor static func main() async throws {
        let recorder = StockEvidenceRecorder()
        recorder.start(environment: ["source": "host simulation"])
        let first = StockTrialCatalog.all[0].id
        let second = StockTrialCatalog.all[1].id
        precondition(recorder.beginTrial(first))
        precondition(!recorder.beginTrial(second), "overlapping trial accepted")
        recorder.record(source: "text-input", kind: "text-changed", fields: ["text": "abc"])
        recorder.endTrial(.completed)
        precondition(recorder.bundle!.trials!.last!.status == .ambiguous, "text alone validated a physical key")
        precondition(recorder.beginTrial(second))
        recorder.key(source: "UIPress", phase: "down", code: 4)
        recorder.stop()
        precondition(recorder.bundle!.trials!.last!.status == .ambiguous, "incomplete trial passed")
        let shared = StockEvidencePrivacy.project(recorder.bundle!)
        precondition(shared.coverage!.count == StockTrialCatalog.all.count)
        precondition(shared.coverage!.first!.appliedTextEvents.count == 1)
        precondition(shared.coverage!.first!.attempts[0].status == .ambiguous)
        precondition(shared.events.filter { $0.kind == "text-changed" }.allSatisfy { $0.trialID == first && $0.trialAttempt == 0 })
        recorder.start(environment: [:])
        precondition(recorder.activeTrialID == nil && recorder.bundle!.trials!.isEmpty)
        precondition(recorder.beginTrial(first))
        recorder.key(source: "UIPress", phase: "down", code: 4)
        recorder.key(source: "UIPress", phase: "up", code: 4)
        recorder.endTrial(.completed)
        precondition(recorder.bundle!.trials!.last!.status == .completed)
        for status: StockTrialStatus in [.failed, .unobservable] {
            precondition(recorder.beginTrial(second))
            recorder.endTrial(status)
            precondition(recorder.bundle!.trials!.last!.status == status)
        }
        let keys = FactoryLayout.stockPhysicalKeys
        precondition(keys.filter { $0.row != nil }.count == 35)
        precondition(!keys.contains { $0.row == 2 && $0.column == 3 })
        precondition(keys.first!.labelEvidence == .conflictingSources)
        precondition(keys.first { $0.id == "r5c2" }!.cursor == "Left")
        // Historical v1 definitions must not follow M3's corrected QMK map.
        precondition(keys.first { $0.id == "r1c2" }!.sym == ";" && FactoryLayout.layer1[8] == 0x0233)
        precondition(keys.contains { $0.id == "unmapped-shift" } && FactoryLayout.layer0[15] == 0x00e1)
        precondition(Set(StockTrialCatalog.all.map(\.id)).count == StockTrialCatalog.all.count)
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        recorder.checkpointInBackground(in: directory)
        recorder.stop()
        recorder.start(environment: [:])
        let newID = recorder.bundle!.sessionID
        recorder.checkpointInBackground(in: directory) // Immediate start snapshot, no later callback needed.
        await recorder.waitForCheckpoints()
        let recovered = StockEvidenceRecorder()
        recovered.recoverCheckpoint(in: directory)
        precondition(recovered.bundle!.sessionID == newID && recovered.bundle!.events.count == 1)
        recorder.checkpointInBackground(in: directory)
        recorder.clear(in: directory)
        await recorder.waitForCheckpoints()
        precondition(!FileManager.default.fileExists(atPath: directory.appendingPathComponent("stock-local.json").path))
        print("PASS trials: overlap, text-only, incomplete, replacement, completed observation; 35 decoded crossings plus unmapped Shift")
        print("PASS checkpoint ordering: new identity saved immediately; queued writes cannot resurrect deletion")
    }
}
