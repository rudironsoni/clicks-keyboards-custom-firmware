import Foundation

/// Host-testable adapter for the custom-firmware protocol over a pair of
/// accessory session streams.
///
/// Extracted from AccessoryInspector's stream handling (review 2026-10-12)
/// so the actual stream behavior is testable without ExternalAccessory:
/// partial-write accounting on the outbox, byte-fragment forwarding to the
/// controller, and one-time session-loss notification. The owner (the
/// AccessoryInspector, which also services the read-only stock query path
/// on the same streams) routes stream events into this adapter.
///
/// Semantics (all identical to the previous inline behavior):
///  - `send` buffers into an outbox and drains immediately; a write that
///    accepts only part of the bytes keeps the remainder and retries on the
///    next writable event, in order, with nothing lost or duplicated.
///  - `handleBytes` forwards received bytes to `onFragment` exactly as
///    delivered; reassembly is the controller's job.
///  - `handleLoss` fires `onClosed` exactly once; further sends are dropped.
///  - `close` is the host side stopping: the outbox is dropped and
///    `onClosed` is NOT fired.
final class StreamLink: KeyboardPacketLink {
    var onFragment: (([UInt8]) -> Void)? {
        didSet { if onFragment != nil { onReceiverReady?() } }
    }
    var onReceiverReady: (() -> Void)?
    var onClosed: ((String) -> Void)?

    private let output: OutputStream?
    private var outbox: [UInt8] = []
    private var lost = false

    /// Injectable write seam: (buffer, length) -> accepted byte count.
    /// Defaults to the real stream write; tests script partial acceptance.
    var write: ((UnsafePointer<UInt8>, Int) -> Int)?

    init(output: OutputStream?) {
        self.output = output
    }

    func send(_ bytes: [UInt8]) {
        guard !lost, !bytes.isEmpty else { return }
        outbox.append(contentsOf: bytes)
        drain()
    }

    /// Host-side stop. Drops buffered bytes; never notifies `onClosed`.
    func close() {
        lost = true
        outbox.removeAll()
        onFragment = nil
        onReceiverReady = nil
        onClosed = nil
    }

    /// Session loss (stream error, end-of-stream, backgrounding, accessory
    /// disconnect). Fires `onClosed` once; later sends are dropped.
    func handleLoss(_ reason: String) {
        guard !lost else { return }
        lost = true
        outbox.removeAll()
        onClosed?(reason)
    }

    /// Received bytes, exactly as the stream delivered them: forwarded
    /// unmodified, one callback per delivery, no coalescing and no loss.
    func handleBytes(_ bytes: [UInt8]) {
        guard !lost, !bytes.isEmpty else { return }
        onFragment?(bytes)
    }

    /// Writable event (or an immediate drain after send): writes as much of
    /// the outbox as the stream accepts, preserving order.
    func drain() {
        guard !lost else { return }
        while !outbox.isEmpty {
            let accepted = outbox.withUnsafeBufferPointer { buffer -> Int in
                guard let base = buffer.baseAddress else { return 0 }
                if let write {
                    return write(base, outbox.count)
                }
                guard let output, output.hasSpaceAvailable else { return 0 }
                return output.write(base, maxLength: outbox.count)
            }
            guard accepted > 0 else { return }
            outbox.removeFirst(accepted)
        }
    }

    var outboxCount: Int { outbox.count }
}
