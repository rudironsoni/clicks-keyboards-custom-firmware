import Foundation
import OSLog

/// Drives the custom-firmware feature set over a KeyboardPacketLink:
/// identity check, keymap editing (durable per-key writes), keymap reset,
/// flashing an image (works against both stock and custom firmware, which
/// share the A1/A2/A3/A0 update dialect), and restoring stock.
@MainActor
final class CustomFirmwareController: ObservableObject {
    enum Phase: Equatable {
        case idle
        case identifying
        case stockFirmware(String)
        case customFirmware(String)
        case busy(String)
        case failed(String)
    }

    @Published private(set) var phase: Phase = .idle
    @Published private(set) var keymap: [[[UInt16]]] = [] // [layer][row][col]
    @Published private(set) var info: KeyboardInfo?
    @Published private(set) var selectedLayer = 0
    @Published private(set) var flashingProgress: Double = 0

    private let logger = Logger(subsystem: "com.rudironsoni.clicks-inspector", category: "custom")
    private var link: KeyboardPacketLink?
    private var exchange: CustomKeyboardExchange?
    private var continuation: CheckedContinuation<[UInt8], Error>?

    var layers: Int { info?.layers ?? 0 }
    var rows: Int { info?.rows ?? 0 }
    var columns: Int { info?.columns ?? 0 }

    func attach(_ link: KeyboardPacketLink) {
        detach()
        self.link = link
        link.onFragment = { [weak self] fragment in self?.receive(fragment) }
        link.onClosed = { [weak self] reason in
            self?.link = nil
            guard case .busy = self?.phase else {
                self?.phase = .idle
                return
            }
            self?.phase = .failed("Link closed: \(reason)")
        }
        identify()
    }

    func detach() {
        link?.close()
        link = nil
        resumePending(with: CustomKeyboardError.transportUnavailable)
        phase = .idle
    }

    private func resumePending(with error: Error) {
        exchange = nil
        let pending = continuation
        continuation = nil
        pending?.resume(throwing: error)
    }

    private func fail(_ message: String) {
        logger.error("\(message, privacy: .public)")
        phase = .failed(message)
        resumePending(with: CustomKeyboardError.transportUnavailable)
    }

    // MARK: Entry points

    func identify() {
        guard link != nil, exchange == nil else { return }
        phase = .identifying
        Task {
            do {
                let payload = try await perform(.version())
                if CustomKeyboardIdentity.isCustomFirmware(payload) {
                    phase = .customFirmware("Custom QMK firmware detected")
                    try await loadKeymap()
                } else {
                    phase = .stockFirmware("Stock firmware detected; flashing and stock reads available, keymap editing is not")
                }
            } catch {
                fail("Identify failed; \(error)")
            }
        }
    }

    private func loadKeymap() async throws {
        let payload = try await perform(.info())
        guard let parsed = CustomKeyboardIdentity.info(fromInfoPayload: payload) else {
            throw CustomKeyboardError.rejected(status: 0xff)
        }
        info = parsed
        keymap = Array(repeating: Array(repeating: Array(repeating: 0, count: parsed.columns), count: parsed.rows),
                       count: parsed.layers)
        for layer in 0..<parsed.layers {
            try await readKeymap(layer: UInt8(layer))
        }
        phase = .customFirmware("Custom firmware ready; \(parsed.layers) layers, \(parsed.rows)x\(parsed.columns)")
    }

    func selectLayer(_ layer: Int) {
        guard info != nil, layer < layers else { return }
        selectedLayer = layer
    }

    func readKeymap(layer: UInt8) async throws {
        guard let info else { return }
        for row in 0..<info.rows {
            for column in 0..<info.columns {
                let payload = try await perform(.getKey(layer: layer, row: UInt8(row), column: UInt8(column)))
                guard payload.count == 2 else { throw CustomKeyboardError.rejected(status: 0xff) }
                keymap[Int(layer)][row][column] = UInt16(payload[0]) << 8 | UInt16(payload[1])
            }
        }
    }

    /// Per-key writes are durable in the firmware; there is no save step.
    func setKey(layer: Int, row: Int, column: Int, keycode: UInt16) {
        guard let info, layer < info.layers, row < info.rows, column < info.columns else { return }
        Task {
            do {
                _ = try await perform(.setKey(layer: UInt8(layer), row: UInt8(row), column: UInt8(column), keycode: keycode))
                keymap[layer][row][column] = keycode
            } catch {
                fail("Set key failed; \(error)")
            }
        }
    }

    func resetKeymap() {
        Task {
            do {
                _ = try await perform(.resetKeymap())
                for layer in 0..<layers {
                    try await readKeymap(layer: UInt8(layer))
                }
                phase = .customFirmware("Keymap reset to the factory layout")
            } catch {
                fail("Keymap reset failed; \(error)")
            }
        }
    }

    /// Stages an image with A1/A2/A3, then optionally reboots (A0) to install.
    /// Works against stock firmware (first flash) and custom firmware
    /// (updates and restore-to-stock): the update dialect is identical.
    func flash(image: [UInt8], rebootToInstall: Bool) {
        Task {
            do {
                phase = .busy("Starting update session")
                flashingProgress = 0
                _ = try await perform(.beginUpdate(size: image.count))
                var chunker = try UpdateChunker(image: image)
                while let next = chunker.nextExchange {
                    phase = .busy("Writing \(chunker.offset)/\(image.count) bytes")
                    let reply = try await perform(next)
                    guard reply.count == 4 else { throw CustomKeyboardError.rejected(status: 0xff) }
                    try chunker.applyAcceptedOffset(reply.reduce(0) { ($0 << 8) | UInt32($1) })
                    flashingProgress = Double(chunker.offset) / Double(image.count)
                }
                phase = .busy("Finalizing update")
                _ = try await perform(.finishUpdate(size: image.count))
                if rebootToInstall {
                    link?.send(CustomKeyboardCommand.rebootRequest)
                    phase = .idle
                    detach()
                } else {
                    phase = .customFirmware("Update staged; it installs on the next keyboard reboot.")
                }
            } catch {
                fail("Update failed; \(error)")
            }
        }
    }

    // MARK: Exchange plumbing

    private func perform(_ exchange: CustomKeyboardExchange) async throws -> [UInt8] {
        guard let link else { throw CustomKeyboardError.transportUnavailable }
        guard self.exchange == nil, continuation == nil else { throw CustomKeyboardError.transportUnavailable }
        let request = exchange.request
        return try await withCheckedThrowingContinuation { continuation in
            self.exchange = exchange
            self.continuation = continuation
            link.send(request)
        }
    }

    private func receive(_ fragment: [UInt8]) {
        guard var current = exchange else { return }
        guard let pendingContinuation = continuation else { return }
        do {
            if let payload = try current.accumulator.append(fragment) {
                exchange = nil
                continuation = nil
                pendingContinuation.resume(returning: payload)
            } else {
                exchange = current
            }
        } catch {
            exchange = nil
            continuation = nil
            pendingContinuation.resume(throwing: error)
            fail("Invalid response; \(error)")
        }
    }
}
