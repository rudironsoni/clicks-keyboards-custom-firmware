import Foundation

/// Runs on the stream owner's main actor. A failure ends this link's lifetime.
@MainActor
final class StockExtensionTransport {
    private let link: KeyboardPacketLink
    private var pending: CheckedContinuation<[UInt8], Error>?
    private var timeout: Task<Void, Never>?
    private var framer = KeyboardPacketFramer(maximumLength: 36)
    private var closed = false
    private let diagnostic: ((String, [UInt8], String?) -> Void)?
    var onLoss: (() -> Void)?
    var timeoutNanoseconds: UInt64 = 5_000_000_000

    init(_ link: KeyboardPacketLink, incomingFramer: KeyboardPacketFramer = .init(maximumLength: 36),
         diagnostic: ((String, [UInt8], String?) -> Void)? = nil) {
        self.link = link
        self.framer = incomingFramer
        self.diagnostic = diagnostic
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
                diagnostic?("request", request, nil)
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
        diagnostic?("closed", [], String(describing: error))
        timeout?.cancel()
        timeout = nil
        framer = KeyboardPacketFramer(maximumLength: 36)
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
        do {
            if let reply = try framer.append(bytes, awaitingReply: pending != nil) {
                diagnostic?("reply", reply, nil)
                // Keep a partial OEM notification after this reply. Its start tag
                // prevents it from becoming the next request's response.
                timeout?.cancel()
                timeout = nil
                let continuation = pending
                pending = nil
                continuation?.resume(returning: reply)
            }
        } catch {
            diagnostic?("invalidFragment", bytes, String(describing: error))
            close(StockExtensionError.framing)
        }
    }
}
