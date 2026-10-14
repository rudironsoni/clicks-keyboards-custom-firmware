import Darwin
import Foundation

@main
struct RecorderBenchmark {
    @MainActor static func main() async throws {
        let recorder = StockEvidenceRecorder()
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        recorder.start(environment: ["source": "host simulation"])
        print("events,peak_rss_bytes,synchronous_checkpoint_ms,enqueue_and_next_callback_ms,checkpoint_bytes")
        for count in [100, 1000, 5000, 19999] {
            while recorder.eventCount < count {
                recorder.record(source: "text-input", kind: "text-changed", fields: [
                    "text": String(repeating: "x", count: 256), "UTF16Length": "8192",
                    "rangeLocation": "0", "rangeLength": "1", "insertedUTF16Length": "256"])
            }
            let start = ProcessInfo.processInfo.systemUptime
            recorder.checkpoint(in: directory)
            let ms = (ProcessInfo.processInfo.systemUptime - start) * 1000
            let callbackStart = ProcessInfo.processInfo.systemUptime
            recorder.checkpointInBackground(in: directory)
            recorder.record(source: "app", kind: "benchmark-callback")
            let callbackMS = (ProcessInfo.processInfo.systemUptime - callbackStart) * 1000
            await recorder.waitForCheckpoints()
            var usage = rusage()
            getrusage(RUSAGE_SELF, &usage)
            let size = try Data(contentsOf: directory.appendingPathComponent("stock-local.json")).count
            print("\(count),\(usage.ru_maxrss),\(String(format: "%.3f", ms)),\(String(format: "%.3f", callbackMS)),\(size)")
            precondition(recorder.storageError == nil)
        }
    }
}
