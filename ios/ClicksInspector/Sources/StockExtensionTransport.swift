import Foundation

/// Runs on the stream owner's main actor. A failure ends this link's lifetime.
@MainActor
final class StockExtensionTransport {
    private let link: KeyboardPacketLink
    private var pending: CheckedContinuation<[UInt8], Error>?
    private var timeout: Task<Void, Never>?
    private var frame: [UInt8] = []
    private var frameMayReply = false
    private var closed = false
    var onLoss: (() -> Void)?
    var timeoutNanoseconds: UInt64 = 5_000_000_000

    init(_ link: KeyboardPacketLink) {
        self.link = link
        link.onFragment = { [weak self] bytes in
            MainActor.assumeIsolated { self?.receive(bytes) }
        }
        link.onClosed = { [weak self] _ in
            MainActor.assumeIsolated { self?.close() }
        }
    }

    func exchange(_ request: [UInt8]) async throws -> [UInt8] {
        guard !closed else { throw StockExtensionError.disconnected }
        guard pending == nil else { throw StockExtensionError.busy }
        try Task.checkCancellation()
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                pending = continuation
                timeout = Task { [weak self] in
                    guard let self else { return }
                    do { try await Task.sleep(nanoseconds: timeoutNanoseconds) } catch { return }
                    close(StockExtensionError.timedOut)
                }
                link.send(request)
            }
        } onCancel: {
            Task { @MainActor [weak self] in self?.close(CancellationError()) }
        }
    }

    func close(_ error: Error = StockExtensionError.disconnected) {
        guard !closed else { return }
        closed = true
        timeout?.cancel()
        timeout = nil
        frame.removeAll()
        link.onFragment = nil
        link.onClosed = nil
        link.close() // Also discards partial output, never reuse after uncertainty.
        let continuation = pending
        pending = nil
        onLoss?()
        continuation?.resume(throwing: error)
    }

    private func receive(_ bytes: [UInt8]) {
        guard !closed else { return }
        var reply: [UInt8]?
        for byte in bytes {
            if frame.isEmpty { frameMayReply = pending != nil && reply == nil }
            frame.append(byte)
            guard let length = frame.first, (4...36).contains(length) else {
                close(StockExtensionError.framing); return
            }
            guard frame.count == Int(length) else { continue }
            let complete = frame
            frame.removeAll(keepingCapacity: true)
            // OEM key/status notifications share this byte stream.
            if ([11,15].contains(complete.count) && complete[1] == 2 && complete[2] == 0)
                || (complete.count == 4 && complete[1] == 4 && complete[2] == 1) { continue }
            guard frameMayReply, pending != nil, reply == nil else { close(StockExtensionError.framing); return }
            reply = complete
        }
        if let reply {
            // Keep a partial OEM notification after this reply. Its start tag
            // prevents it from becoming the next request's response.
            timeout?.cancel()
            timeout = nil
            let continuation = pending
            pending = nil
            continuation?.resume(returning: reply)
        }
    }
}
