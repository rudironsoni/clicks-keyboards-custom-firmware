#if os(macOS) || targetEnvironment(simulator)
import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

/// Loopback bridge to the genuine V122 runner. No firmware behavior lives here.
/// The server retains RAM across link loss. Only `power` restarts the machine.
final class V122EmulatorLink: KeyboardPacketLink {
    var onFragment: (([UInt8]) -> Void)?
    var onClosed: ((String) -> Void)?
    private let owner = UUID().uuidString
    private let url: URL
    private var claimed = false
    private var closed = false
    private var task: Task<Void, Never>?

    init(url: URL = URL(string: "http://127.0.0.1:8765")!) { self.url = url }

    func send(_ bytes: [UInt8]) {
        guard !closed, task == nil else { return }
        task = Task { @MainActor [weak self] in
            guard let self else { return }
            do {
                if !claimed { _ = try await command("claim"); claimed = true }
                let line = try await command("p " + bytes.map { String(format: "%02x", $0) }.joined())
                guard !closed else { return }
                task = nil
                if line == "LOST" { return } // Commit executed. Client must time out, never retry.
                guard line.hasPrefix("R ") else { throw StockExtensionError.framing }
                let hex = Array(line.dropFirst(2))
                guard hex.count % 2 == 0 else { throw StockExtensionError.framing }
                var reply: [UInt8] = []
                for i in stride(from: 0, to: hex.count, by: 2) {
                    guard let byte = UInt8(String(hex[i...i+1]), radix: 16) else { throw StockExtensionError.framing }
                    reply.append(byte)
                }
                // Exercise actual fragment assembly, not a full-frame callback.
                for byte in reply {
                    guard !closed else { return }
                    onFragment?([byte])
                    await Task.yield()
                }
            } catch {
                task = nil
                if !closed { onClosed?("V122 emulator bridge: \(error)") }
            }
        }
    }

    func close() {
        closed = true
        task?.cancel()
        task = nil
        onFragment = nil
        onClosed = nil
    }

    @MainActor
    func control(_ value: String) async throws { _ = try await command(value) }

    @MainActor
    func verifyHID() async throws -> String {
        let allUp = "H 0000000000000000"
        let steps = [
            ("000000000000", allUp),
            ("000020000010", allUp), // Native NAV gesture, SYM + Cmd.
            ("000000000000", allUp),
            ("010000000000", "H 0000290000000000"),
            ("000000000000", allUp),
            ("000000000010", allUp),
            ("000001000010", "H 00002b0000000000"),
            ("000000000000", allUp)
        ]
        for (keys, expected) in steps {
            let actual = try await command("k " + keys)
            guard actual == expected else { throw HIDMismatch(expected: expected, actual: actual) }
        }
        return "PASS NAV+Q: 0000290000000000; SYM+E: 00002b0000000000; releases: 0000000000000000. V122 emulation only."
    }

    private struct HIDMismatch: Error { let expected: String; let actual: String }

    @MainActor
    private func command(_ command: String) async throws -> String {
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = 10
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try JSONSerialization.data(withJSONObject: ["owner": owner, "command": command])
        let (data, response) = try await URLSession.shared.data(for: request)
        guard (response as? HTTPURLResponse)?.statusCode == 200,
              let object = try JSONSerialization.jsonObject(with: data) as? [String: String],
              let line = object["line"] else { throw StockExtensionError.disconnected }
        return line
    }
}
#endif
