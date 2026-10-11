/*
 * Host check for StreamLink: the accessory stream adapter extracted from
 * AccessoryInspector (review 2026-10-12). Covers the actual stream behavior
 * of the firmware-update path, which the mocked KeyboardPacketLink cannot:
 * partial writes with outbox accounting, byte-fragment forwarding, session
 * loss (stream error, end-of-stream, backgrounding), host-side close, and a
 * composed identify transaction through the adapter with partial writes on
 * the way out and fragments on the way back.
 *
 * Compile and run:
 *   swiftc Sources/KeycodeNames.swift Sources/FactoryLayout.swift \
 *     Sources/KeyboardSelfTest.swift Sources/CustomKeyboardProtocol.swift \
 *     Sources/CustomFirmwareController.swift Sources/StreamLink.swift \
 *     Tests/ck5200_stream_check.swift -o /tmp/ck5200_stream_check
 */
import Foundation

private var checkFailures = 0

private func expect(_ condition: Bool, _ what: String) {
    if condition {
        print("PASS \(what)")
    } else {
        checkFailures += 1
        print("FAIL \(what)")
    }
}

@MainActor
private func waitUntil(_ what: String, timeout seconds: Double = 5,
                       _ condition: () -> Bool) async {
    let deadline = Date().addingTimeInterval(seconds)
    while !condition() {
        if Date() > deadline {
            expect(false, "timed out waiting for: \(what)")
            return
        }
        try? await Task.sleep(for: .milliseconds(5))
    }
    expect(true, "wait: \(what)")
}

@main
struct StreamCheck {
    @MainActor
    static func main() async {
        CustomFirmwareController.resetPendingUpdateForTesting()
        defer { CustomFirmwareController.resetPendingUpdateForTesting() }

        /* 1. Partial writes: the stream accepts only part of the outbox, the
         * remainder stays buffered in order, and the next writable event
         * flushes the rest. Nothing is lost or duplicated. */
        do {
            let link = StreamLink(output: nil)
            var written: [UInt8] = []
            var acceptNext = 3
            link.write = { buffer, count in
                let n = min(count, acceptNext)
                if n > 0 { written.append(contentsOf: UnsafeBufferPointer(start: buffer, count: n)) }
                acceptNext = (acceptNext == 3) ? 0 : 8
                return n
            }
            let message: [UInt8] = Array(0..<10)
            link.send(message)
            expect(written == Array(message[0..<3]), "the first drain wrote only what the stream accepted")
            expect(link.outboxCount == 7, "the remainder stays buffered after a partial write (got \(link.outboxCount))")

            link.drain() /* writable event: the seam accepts more now */
            expect(written == message, "the next writable event flushes the rest in order")
            expect(link.outboxCount == 0, "the outbox is empty after the flush")
        }

        /* 2. Fragment forwarding: bytes reach onFragment exactly as
         * delivered, one callback per delivery, no coalescing or loss. */
        do {
            let link = StreamLink(output: nil)
            var fragments: [[UInt8]] = []
            link.onFragment = { fragments.append($0) }
            link.handleBytes([0x08, 0x02, 0x03, 0x00])
            link.handleBytes([0x01])
            link.handleBytes([0x20, 0x01, 0x22])
            expect(fragments == [[0x08, 0x02, 0x03, 0x00], [0x01], [0x20, 0x01, 0x22]],
                   "fragments forward exactly as delivered")
        }

        /* 3. Session loss (stream error, end, backgrounding): onClosed fires
         * exactly once and later sends are dropped. */
        do {
            let link = StreamLink(output: nil)
            var closures: [String] = []
            link.onClosed = { closures.append($0) }
            link.send([0x02, 0x03])
            link.handleLoss("App entered background")
            link.handleLoss("Stream error") /* second loss: no double fire */
            link.send([0x02, 0x03]) /* dropped after loss */
            expect(closures == ["App entered background"], "session loss fires onClosed exactly once")
            expect(link.outboxCount == 0, "sends after loss are dropped")
        }

        /* 4. Host-side close: buffered bytes are dropped, onClosed is NOT
         * fired (the owner already knows it is closing). */
        do {
            let link = StreamLink(output: nil)
            var fired = false
            link.onClosed = { _ in fired = true }
            link.write = { _, _ in 0 }
            link.send([0x02, 0x03])
            expect(link.outboxCount == 2, "unwritable stream keeps bytes buffered")
            link.close()
            expect(link.outboxCount == 0, "close drops the outbox")
            expect(!fired, "close does not fire onClosed")
        }

        /* 5. Composed transaction: the real controller over the real stream
         * adapter, with partial writes on the way out and fragments on the
         * way back. */
        do {
            let link = StreamLink(output: nil)
            var outgoing: [UInt8] = []
            var pendingPartial = 1
            link.write = { buffer, count in
                /* Accept one byte on the first call, then everything, to
                 * prove the accounting under fragmentation. */
                let n = (pendingPartial > 0) ? min(count, 1) : count
                if n > 0 { outgoing.append(contentsOf: UnsafeBufferPointer(start: buffer, count: n)) }
                if pendingPartial > 0 { pendingPartial -= 1 }
                return n
            }
            let controller = CustomFirmwareController()
            controller.exchangeTimeoutNanos = 2_000_000_000
            controller.connectedTarget = CustomFirmwareController.ConnectedTarget(
                model: "CK-5200", firmwareRevision: "1.2.2", hardwareRevision: "1.2.0")
            controller.attach(link)
            var replyDelivered = false
            await waitUntil("identify completes over the stream adapter") {
                /* Once the whole request is on the wire, the reply arrives
                 * in two fragments: reassembly is the controller's job. */
                if !replyDelivered, outgoing == [0x02, 0x03] {
                    replyDelivered = true
                    link.handleBytes([0x08, 0x02, 0x03, 0x00, 0x01])
                    link.handleBytes([0x20, 0x01, 0x22])
                }
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            expect(outgoing == [0x02, 0x03], "the version request reached the wire intact, one byte at a time")
            expect(controller.lastVersionPayload == [0x01, 0x20, 0x01, 0x22],
                   "fragmented reply reassembled into the exact version payload")

            /* Session loss while connected: the controller reports it. */
            link.handleLoss("Connection lost")
            await waitUntil("session loss reaches the controller") {
                if case .idle = controller.phase { return true }
                return false
            }
        }

        print(checkFailures == 0 ? "ok" : "==== \(checkFailures) failure(s) ====")
        exit(checkFailures == 0 ? 0 : 1)
    }
}
