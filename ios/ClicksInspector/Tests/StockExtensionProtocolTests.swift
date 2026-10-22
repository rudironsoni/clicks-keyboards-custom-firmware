import Foundation

private enum TestFailure: Error { case failed(String) }
private func check(_ condition: Bool, _ message: String) throws {
    if !condition { throw TestFailure.failed(message) }
}

private final class ExtensionEmulator {
    let process = Process()
    let input = Pipe(), output = Pipe()
    init(runner: String, image: String, trace: String) throws {
        process.executableURL = URL(fileURLWithPath: runner)
        process.arguments = [image, trace, "config-server"]
        process.standardInput = input; process.standardOutput = output
        var environment = ProcessInfo.processInfo.environment
        environment["UBSAN_OPTIONS"] = "halt_on_error=1"
        process.environment = environment
        try process.run()
        try check(readLine() == "ready", "emulator did not start")
    }
    private func readLine() -> String {
        var data = Data()
        while true {
            let byte = output.fileHandleForReading.readData(ofLength: 1)
            if byte.isEmpty || byte.first == 10 { break }
            data += byte
        }
        return String(decoding: data, as: UTF8.self)
    }
    func line(_ text: String) -> String {
        input.fileHandleForWriting.write(Data((text + "\n").utf8))
        return readLine()
    }
    func exchange(_ bytes: [UInt8]) throws -> [UInt8] {
        let reply = line("p " + bytes.map { String(format: "%02x", $0) }.joined())
        try check(reply.hasPrefix("R "), "missing firmware reply: \(reply)")
        let chars = Array(reply.dropFirst(2))
        try check(chars.count % 2 == 0, "odd hex reply")
        return try stride(from: 0, to: chars.count, by: 2).map {
            guard let byte = UInt8(String(chars[$0...($0 + 1)]), radix: 16) else { throw StockExtensionError.framing }
            return byte
        }
    }
    func stop() throws {
        input.fileHandleForWriting.write(Data("q\n".utf8))
        process.waitUntilExit()
        try check(process.terminationStatus == 0, "emulator failed")
    }

    // Exercise the production partial-write adapter and async frame assembler.
    @MainActor func transport(dropApplyAck: Bool = false) -> StockExtensionTransport {
        let link = StreamLink(output: nil)
        var request: [UInt8] = []
        var notificationTail: [UInt8] = []
        link.write = { [weak link] pointer, count in
            if !notificationTail.isEmpty {
                let tail = notificationTail
                notificationTail = []
                link?.handleBytes(tail)
            }
            let accepted = min(3, count)
            request += Array(UnsafeBufferPointer(start: pointer, count: accepted))
            if request.count == Int(request[0]) {
                let packet = request
                request = []
                Task { @MainActor in
                    do {
                        let reply = try self.exchange(packet)
                        if dropApplyAck && packet[3] == 4 { return }
                        // OEM traffic may precede a reply in the same delivery.
                        link?.handleBytes([11,2,0,0,0,0,0,0,0,0,0] + Array(reply.prefix(2)))
                        for (index, byte) in reply.dropFirst(2).enumerated() {
                            await Task.yield()
                            link?.handleBytes(index == reply.count - 3 ? [byte,4,4] : [byte])
                        }
                        notificationTail = [1,0] // Completes after the next request starts.
                    } catch { link?.handleLoss("emulator error: \(error)") }
                }
            }
            return accepted
        }
        let adapter = StockExtensionTransport(link)
        return adapter
    }
}

@main enum StockExtensionProtocolTests {
    @MainActor static func main() async throws {
        var record: [UInt8] = [67,75,77,4,108,6]
        for layer in UInt8(0)..<3 { for pos in UInt8(0)..<36 { record += [pos,layer,0x22] } }
        let original = try StockExtensionMap(record: record)
        try check(try original.encode() == record, "native map round-trip")
        var maximum = original
        for i in 0..<108 {
            maximum.actions[i] = .init(mods: 15, activation: 1, target: UInt8(i / 36), tapUsage: 255, tapMilliseconds: 65535)
        }
        let maximumBytes = try maximum.encode()
        try check(maximumBytes.count == 978, "maximum record shrank")
        try check(try StockExtensionMap(record: maximumBytes) == maximum, "maximum typed map round-trip")
        try check(try JSONDecoder().decode(StockExtensionMap.self, from: JSONEncoder().encode(maximum)) == maximum,
                  "iPhone-backed maximum profile round-trip")
        var invalid = maximumBytes; invalid[5] |= 0x80
        do { _ = try StockExtensionMap(record: invalid); throw TestFailure.failed("reserved mask accepted") }
        catch StockExtensionError.invalidRecord {}
        var sends = 0
        let stock = StockExtensionSession(nonce: 123) { _ in sends += 1; return [8,2,0x94,255,0,0,0,0] }
        do { try await stock.open(); throw TestFailure.failed("stock identity accepted") }
        catch StockExtensionError.framing {}
        do { try await stock.apply(original, enabled: true); throw TestFailure.failed("stock mutation allowed") }
        catch StockExtensionError.identity {}
        try check(sends == 1, "mutation sent before capability identity")

        let silentLink = StreamLink(output: nil)
        let silent = StockExtensionTransport(silentLink)
        silent.timeoutNanoseconds = 20_000_000
        let waiting = StockExtensionSession(nonce: 130, transfer: silent.exchange)
        let opening = Task { try await waiting.open() }
        await Task.yield()
        silentLink.handleBytes([27,2,0x94,0,1]) // Truncated HELLO must time out and discard queued output.
        do { try await waiting.open(); throw TestFailure.failed("parallel operation accepted") }
        catch StockExtensionError.busy {}
        do { try await opening.value; throw TestFailure.failed("missing reply accepted") }
        catch StockExtensionError.timedOut {}
        try check(!waiting.identified && silentLink.outboxCount == 0, "timeout retained stale output")
        let lostLink = StreamLink(output: nil)
        let lost = StockExtensionTransport(lostLink)
        let disconnect = Task { try await lost.exchange([12,0x94,1,0,1,0,0,0,0,0,0,0]) }
        await Task.yield()
        lostLink.handleLoss("disconnect")
        do { _ = try await disconnect.value; throw TestFailure.failed("disconnect accepted") }
        catch StockExtensionError.disconnected {}
        let cancelledLink = StreamLink(output: nil)
        let cancelled = StockExtensionTransport(cancelledLink)
        let cancelTask = Task { try await cancelled.exchange([12,0x94,1,0,1,0,0,0,0,0,0,0]) }
        await Task.yield()
        cancelTask.cancel()
        do { _ = try await cancelTask.value; throw TestFailure.failed("cancel accepted") }
        catch is CancellationError {}

        if CommandLine.arguments.count == 5 {
            let args = CommandLine.arguments
            let stockDevice = try ExtensionEmulator(runner: args[1], image: args[3], trace: args[4] + "/swift-stock.trace.jsonl")
            let stockAdapter = stockDevice.transport()
            let stockSession = StockExtensionSession(nonce: 124, transfer: stockAdapter.exchange)
            do { try await stockSession.open(); throw TestFailure.failed("real stock identity accepted") }
            catch StockExtensionError.framing {}
            do { try await stockSession.apply(original, enabled: true); throw TestFailure.failed("stock mutation sent") }
            catch StockExtensionError.identity {}
            stockAdapter.close()
            try stockDevice.stop()

            let device = try ExtensionEmulator(runner: args[1], image: args[2], trace: args[4] + "/swift-extension.trace.jsonl")
            try check(device.line("r ") == "U 00", "initial USB reset")
            try check(device.line("c ") == "U 01", "synthetic configuration handoff")
            let adapter = device.transport()
            let session = StockExtensionSession(nonce: 125, transfer: adapter.exchange)
            try await session.open()
            var map = try await session.read()
            try check(map == original, "initial firmware map differs")
            try map.set(.init(usage: 0x29), layer: 2, row: 0, column: 0)
            try map.set(.init(usage: 0x2B), layer: 1, row: 2, column: 0)
            try await session.apply(map, enabled: true)
            try check(device.line("k 000000000010") == "H 0000000000000000", "native Symbols activation")
            try check(device.line("k 000001000010") == "H 00002b0000000000", "iOS SYM+E did not emit Tab")
            try check(device.line("k 000000000000") == "H 0000000000000000", "Tab release")
            try check(device.line("k 000020000010") == "H 0000000000000000", "native Cursor activation")
            _ = device.line("k 000000000000")
            try check(device.line("k 010000000000") == "H 0000290000000000", "iOS NAV+Q did not emit Esc")
            _ = device.line("k 000000000000")
            try await session.apply(maximum, enabled: false)
            try await session.apply(map, enabled: true)
            adapter.close()
            session.close()
            let lostAckAdapter = device.transport(dropApplyAck: true)
            let interrupted = StockExtensionSession(nonce: 126, transfer: lostAckAdapter.exchange)
            try await interrupted.open()
            var changed = map
            try changed.set(.init(usage: 0x2B), layer: 0, row: 0, column: 0)
            do { try await interrupted.apply(changed, enabled: true); throw TestFailure.failed("lost ack accepted") }
            catch StockExtensionError.timedOut {}
            try check(!interrupted.identified, "lost acknowledgement did not close client session")
            let recoveredAdapter = device.transport()
            let recovered = StockExtensionSession(nonce: 127, transfer: recoveredAdapter.exchange)
            try await recovered.open()
            try check(try await recovered.read() == changed, "atomic commit was not recoverable by readback")
            recoveredAdapter.close()
            try device.stop()
            print("PASS iOS codec -> V122 command adapter -> EP3 reply -> atomic RAM map -> native gestures -> EP1 HID")
            print("PASS async fragmented streams, partial writes, NAV+Q Esc, SYM+E Tab, reconnect and lost-ack readback")
        }
        print("PASS extension identity, typed map, and maximum-size codec checks")
    }
}
