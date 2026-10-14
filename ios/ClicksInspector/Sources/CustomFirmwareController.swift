import CryptoKit
import Foundation
import OSLog

/// Drives the custom-firmware feature set over a KeyboardPacketLink:
/// identity check, runtime-only keymap editing, keymap reset,
/// flashing an image (works against both stock and custom firmware, which
/// share the A1/A2/A3/A0 update dialect), and restoring stock.
@MainActor
final class CustomFirmwareController: ObservableObject {
    #if STOCK_UPDATE_AUTHORIZED
    static let diagnosticsOnly = false
    #else
    static let diagnosticsOnly = true
    #endif

    /// Sole controller-to-transport boundary, including the fire-and-forget reboot.
    func dispatch(_ request: [UInt8]) throws {
        try checkBuildPermission(request)
        guard let link else { throw CustomKeyboardError.transportUnavailable }
        link.send(request)
    }

    private func checkBuildPermission(_ request: [UInt8]) throws {
        if Self.diagnosticsOnly {
            guard let target = connectedTarget,
                  StockReadPolicy.allows(request, model: target.model,
                      firmware: target.firmwareRevision, hardware: target.hardwareRevision) else {
                throw CustomKeyboardError.diagnosticsOnly
            }
        }
    }
    enum Phase: Equatable {
        case idle
        case identifying
        case stockFirmware(String)
        case customFirmware(String)
        case busy(String)
        case failed(String)
        /// An update was sent but its installation was never verified.
        /// Retained across reconnects and relaunches until the user
        /// explicitly reconciles it (review 2026-10-12).
        case updateUnverified(String)
    }

    /// Identity of the connected keyboard, set by the session owner before
    /// any flash may run (review 2026-10-12: flashing requires the target's
    /// model and hardware revision, not just a working link).
    struct ConnectedTarget: Equatable {
        let model: String
        let firmwareRevision: String
        let hardwareRevision: String
    }

    /// An update whose outcome has not been verified yet. Persisted in
    /// UserDefaults so relaunching the app cannot silently turn an
    /// unresolved transaction into an ordinary idle state (review
    /// 2026-10-12).
    ///
    /// Review 2026-10-13 (P0): the uncertain record is written BEFORE A3
    /// is dispatched. From that moment until a validated acknowledgment
    /// arrives, every outcome -- timeout, disconnection, malformed reply,
    /// app termination -- leaves a `.commitUnknown` record behind, because
    /// the keyboard may already have committed the image. Only a validated
    /// acknowledgment advances the record to `.sentRebootPending`; nothing
    /// ever marks installation as successful (reboot evidence is not
    /// installation evidence).
    struct PendingUpdate: Codable, Equatable {
        enum Outcome: String, Codable {
            /// A3 was dispatched or its reply was lost/rejected: the image
            /// may already be committed.
            case commitUnknown
            /// A3 committed and A0 was sent; installation not yet verified.
            case sentRebootPending
        }
        let imageSHA256: String
        let imageSize: Int
        let date: Date
        let outcome: Outcome
    }

    private static let pendingUpdateKey = "ck5200.pendingUpdate"

    @Published private(set) var phase: Phase = .idle
    @Published private(set) var keymap: [[[UInt16]]] = [] // [layer][row][col]
    @Published private(set) var info: KeyboardInfo?
    @Published private(set) var selectedLayer = 0
    @Published private(set) var flashingProgress: Double = 0
    @Published private(set) var selfTestReport: SelfTestReport?
    @Published private(set) var selfTestRunning = false

    private let logger = Logger(subsystem: "com.rudironsoni.clicks-inspector", category: "custom")
    private var link: KeyboardPacketLink?
    private var exchange: CustomKeyboardExchange?
    private var continuation: CheckedContinuation<[UInt8], Error>?
    private var timeoutTask: Task<Void, Never>?
    private var rebootTask: Task<Void, Never>?
    private var awaitingReboot = false

    /// Synchronous single-operation guard, set and cleared on the main
    /// actor before any Task runs, so two rapid submissions cannot both
    /// pass (review 2026-10-12).
    private var flashInFlight = false

    /// The connected keyboard's reported identity, and the version payload
    /// from the last successful identify. Flashing requires both (review
    /// 2026-10-12: an unrecognized version reply must not authorize it).
    /// Set by the session owner when the accessory connects.
    var connectedTarget: ConnectedTarget?
    private(set) var lastVersionPayload: [UInt8]?
    private(set) var pendingUpdate: PendingUpdate?

    /// Per-exchange timeout, and the window after A0 in which the keyboard
    /// must reboot (its session dies). Injectable so the host check can
    /// exercise the timeout paths quickly (review 2026-10-11, F-7).
    var exchangeTimeoutNanos: UInt64 = 5_000_000_000
    var rebootConfirmTimeoutNanos: UInt64 = 10_000_000_000

    init() {
        pendingUpdate = Self.loadPendingUpdate()
    }

    /// Host-test hook: clear the persisted unresolved-update record.
    static func resetPendingUpdateForTesting() {
        UserDefaults.standard.removeObject(forKey: pendingUpdateKey)
    }

    private static func loadPendingUpdate() -> PendingUpdate? {
        guard let data = UserDefaults.standard.data(forKey: pendingUpdateKey) else { return nil }
        return try? JSONDecoder().decode(PendingUpdate.self, from: data)
    }

    private func recordPendingUpdate(imageSHA256: String, imageSize: Int, outcome: PendingUpdate.Outcome) {
        let record = PendingUpdate(imageSHA256: imageSHA256, imageSize: imageSize, date: Date(), outcome: outcome)
        pendingUpdate = record
        if let data = try? JSONEncoder().encode(record) {
            UserDefaults.standard.set(data, forKey: Self.pendingUpdateKey)
        }
    }

    /// Advance the persisted record to a later outcome (review 2026-10-13):
    /// only a validated A3 acknowledgment may do this, moving the record
    /// from the pre-dispatch unknown to the sent/reboot-pending state.
    private func advancePendingUpdate(to outcome: PendingUpdate.Outcome) {
        guard let current = pendingUpdate else { return }
        let record = PendingUpdate(imageSHA256: current.imageSHA256, imageSize: current.imageSize,
                                    date: current.date, outcome: outcome)
        pendingUpdate = record
        if let data = try? JSONEncoder().encode(record) {
            UserDefaults.standard.set(data, forKey: Self.pendingUpdateKey)
        }
    }

    /// Explicit reconciliation only (review 2026-10-12): reconnecting or
    /// relaunching must never silently clear an unresolved update.
    func markPendingUpdateVerified() {
        guard pendingUpdate != nil else { return }
        pendingUpdate = nil
        UserDefaults.standard.removeObject(forKey: Self.pendingUpdateKey)
        if link != nil {
            identify()
        } else {
            phase = .idle
        }
    }

    private static func sha256Hex(_ bytes: [UInt8]) -> String {
        SHA256.hash(data: Data(bytes)).map { String(format: "%02x", $0) }.joined()
    }

    var layers: Int { info?.layers ?? 0 }
    var rows: Int { info?.rows ?? 0 }
    var columns: Int { info?.columns ?? 0 }

    func attach(_ link: KeyboardPacketLink) {
        detach()
        self.link = link
        link.onFragment = { [weak self] fragment in self?.receive(fragment) }
        link.onClosed = { [weak self] reason in
            guard let self else { return }
            self.link = nil
            self.timeoutTask?.cancel()
            self.timeoutTask = nil
            self.rebootTask?.cancel()
            self.rebootTask = nil
            if self.awaitingReboot {
                /* Expected: the keyboard reboots to install after A0, which
                 * kills the session. A lost session proves the reboot, not
                 * the installation: retain the unverified state until the
                 * user reconnects and verifies (review 2026-10-12). */
                self.awaitingReboot = false
                self.resumePending(with: CustomKeyboardError.transportUnavailable)
                self.phase = .updateUnverified("Update sent; the keyboard is rebooting to install. Reconnect and verify before relying on it.")
                return
            }
            /* Resume whatever exchange was in flight so the UI fails
             * cleanly instead of hanging in .busy forever (review
             * 2026-10-11, F-7). */
            self.resumePending(with: CustomKeyboardError.transportUnavailable)
            guard case .busy = self.phase else {
                self.phase = .idle
                return
            }
            self.phase = .failed("Link closed: \(reason)")
        }
        identify()
    }

    func detach() {
        link?.close()
        link = nil
        timeoutTask?.cancel()
        timeoutTask = nil
        rebootTask?.cancel()
        rebootTask = nil
        awaitingReboot = false
        resumePending(with: CustomKeyboardError.transportUnavailable)
        phase = .idle
    }

    private func resumePending(with error: Error) {
        timeoutTask?.cancel()
        timeoutTask = nil
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

    /// Full self-test: identify, read info, read and verify every key
    /// against the decoded factory layout, then round-trip a write on the
    /// spare crossing. Publishes a report.
    func runSelfTest() {
        guard link != nil, !selfTestRunning else { return }
        selfTestRunning = true
        selfTestReport = nil
        Task {
            defer { selfTestRunning = false }
            do {
                var report = SelfTestReport()

                // 1. Identify.
                let versionPayload = try await perform(.version())
                report.checks.append(KeyboardSelfTest.verifyIdentify(versionPayload))
                guard CustomKeyboardIdentity.isCustomFirmware(versionPayload) else {
                    report.checks.append(SelfTestCheck(name: "Aborted", passed: false,
                                                       detail: "Firmware is not custom; keymap checks require the custom QMK firmware"))
                    selfTestReport = report
                    return
                }

                // 2. Info.
                let infoPayload = try await perform(.info())
                guard let parsed = CustomKeyboardIdentity.info(fromInfoPayload: infoPayload) else {
                    report.checks.append(SelfTestCheck(name: "Info", passed: false, detail: "Unparseable info reply"))
                    selfTestReport = report
                    return
                }
                report.checks.append(KeyboardSelfTest.verifyInfo(parsed))
                info = parsed

                // 3. Read every key of every layer.
                keymap = Array(repeating: Array(repeating: Array(repeating: 0, count: parsed.columns), count: parsed.rows),
                               count: parsed.layers)
                for layer in 0..<parsed.layers {
                    for row in 0..<parsed.rows {
                        for column in 0..<parsed.columns {
                            let payload = try await perform(.getKey(layer: UInt8(layer), row: UInt8(row), column: UInt8(column)))
                            guard payload.count == 2 else {
                                throw CustomKeyboardError.rejected(status: 0xff)
                            }
                            keymap[layer][row][column] = UInt16(payload[0]) << 8 | UInt16(payload[1])
                        }
                    }
                }
                let keymapReport = KeyboardSelfTest.verifyKeymap(keymap)
                report.checks.append(contentsOf: keymapReport.checks)

                // 4. Round-trip on the spare crossing (2,3), layer 0.
                let original = keymap[0][2][3]
                _ = try await perform(.setKey(layer: 0, row: 2, column: 3, keycode: 0x0014))
                let readBackPayload = try await perform(.getKey(layer: 0, row: 2, column: 3))
                let readBack = readBackPayload.count == 2 ? UInt16(readBackPayload[0]) << 8 | UInt16(readBackPayload[1]) : 0
                _ = try await perform(.setKey(layer: 0, row: 2, column: 3, keycode: original))
                let restoredPayload = try await perform(.getKey(layer: 0, row: 2, column: 3))
                let restored = restoredPayload.count == 2 ? UInt16(restoredPayload[0]) << 8 | UInt16(restoredPayload[1]) : 0
                report.checks.append(KeyboardSelfTest.verifyRoundTrip(
                    written: 0x0014, readBack: readBack, restored: restored, original: original))
                keymap[0][2][3] = restored

                selfTestReport = report
                logger.info("Self-test: \(report.passCount)/\(report.totalCount) checks passed")
            } catch {
                var report = SelfTestReport()
                report.checks.append(SelfTestCheck(name: "Transport", passed: false,
                                                  detail: "Self-test aborted: \(error)"))
                selfTestReport = report
            }
        }
    }

    /// Post-flash verification: after A0 reboot the keyboard re-enumerates.
    /// Call this from the UI when the user re-opens the session; it
    /// re-identifies and reports whether the expected firmware is running.
    /// Post-flash verification (review 2026-10-12): re-identify after a
    /// reboot; the unresolved-update state persists until the user
    /// explicitly marks it verified.
    func verifyAfterReboot() {
        identify()
    }

    func identify() {
        guard link != nil, exchange == nil else { return }
        phase = .identifying
        Task {
            do {
                let payload = try await perform(.version())
                lastVersionPayload = payload
                let identified: Phase
                if CustomKeyboardIdentity.isCustomFirmware(payload) {
                    identified = .customFirmware("Custom QMK firmware detected")
                } else if payload == CustomKeyboardCommand.stockVersionPayload {
                    identified = .stockFirmware("Stock firmware detected; flashing and stock reads available, keymap editing is not")
                } else {
                    identified = .stockFirmware("Unrecognized firmware identity \(payload.map { String(format: "%02x", $0) }.joined()); flashing is refused")
                }
                if pendingUpdate != nil {
                    /* An unresolved update is never silently resolved by a
                     * reconnect (review 2026-10-12). */
                    phase = .updateUnverified("Connected and identified, but a previous update (sha256 \(pendingUpdate!.imageSHA256.prefix(12))…) is not verified. Mark it verified only after the keyboard works.")
                    if case .customFirmware = identified {
                        try await loadKeymap()
                    }
                } else {
                    phase = identified
                    if case .customFirmware = identified {
                        try await loadKeymap()
                    }
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

    /// Custom X035 keymap edits are runtime-only and lost on power loss.
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
    ///
    /// Authorization (review 2026-10-12): while the custom-flashing gate is
    /// closed, the ONLY image this build may stage is the exact authorized
    /// stock V122 artifact (SHA-256 pinned), and only onto a target that
    /// identifies as the expected CK-5200 stock unit.
    func flash(image: [UInt8], rebootToInstall: Bool) {
        guard !Self.diagnosticsOnly else {
            fail(CustomKeyboardError.diagnosticsOnly.description)
            return
        }
        guard link != nil else {
            fail("No connection; cannot flash")
            return
        }
        if pendingUpdate != nil {
            phase = .updateUnverified("A previous update (sha256 \(pendingUpdate!.imageSHA256.prefix(12))…) is unresolved. Verify it before flashing anything else.")
            return
        }
        /* Synchronous single-operation guard, before any Task is created:
         * two rapid submissions cannot both pass (review 2026-10-12). */
        guard !flashInFlight else { return }
        flashInFlight = true

        /* Target identity: flashing requires the connected keyboard to be
         * the expected CK-5200 (review 2026-10-12). */
        guard let target = connectedTarget else {
            flashInFlight = false
            fail("The connected keyboard's identity is unknown; refusing to flash.")
            return
        }
        guard target.model == "CK-5200", target.hardwareRevision == "1.2.0" else {
            flashInFlight = false
            fail("Connected keyboard is \(target.model), hardware \(target.hardwareRevision); expected CK-5200 hardware 1.2.0. Refusing to flash.")
            return
        }
        let imageSHA256 = Self.sha256Hex(image)
        let isAuthorizedStock = imageSHA256 == CustomKeyboardImageAuthorization.authorizedStockSHA256
        if isAuthorizedStock {
            guard target.firmwareRevision == "1.2.2",
                  lastVersionPayload == CustomKeyboardCommand.stockVersionPayload else {
                flashInFlight = false
                fail("Unrecognized firmware identity; refusing to flash. Expected CK-5200 stock 1.2.2 (version payload 01 20 01 22).")
                return
            }
        } else if !CustomKeyboardImageAuthorization.customFirmwareFlashingEnabled {
            flashInFlight = false
            fail("Image refused: only the bundled stock V122 artifact is authorized in this build (sha256 \(imageSHA256.prefix(12))…). Custom firmware flashing is disabled until a separately reviewed CH32X035 image is authorized.")
            return
        } else {
            guard let version = lastVersionPayload,
                  CustomKeyboardIdentity.isCustomFirmware(version) || version == CustomKeyboardCommand.stockVersionPayload else {
                flashInFlight = false
                fail("Unrecognized firmware identity; refusing to flash.")
                return
            }
        }
        do {
            try CustomKeyboardImageValidation.validate(image: image)
        } catch {
            flashInFlight = false
            fail("Rejected image; \(error)")
            return
        }
        phase = .busy("Starting update session")
        Task {
            defer { flashInFlight = false }
            do {
                flashingProgress = 0
                let beginReply = try await performWithRetry(.beginUpdate(size: image.count))
                /* A1 payload: the advertised maximum image size (review
                 * 2026-10-12: the app previously ignored it). */
                guard beginReply.count == 4 else { throw CustomKeyboardError.rejected(status: 0xff) }
                let advertisedMaximum = Int(beginReply.reduce(0) { ($0 << 8) | UInt32($1) })
                guard image.count <= advertisedMaximum else {
                    throw CustomKeyboardError.updateRejected(at: "advertised maximum \(advertisedMaximum)", status: 0x01)
                }
                var chunker = try UpdateChunker(image: image)
                while let next = chunker.nextExchange() {
                    phase = .busy("Writing \(chunker.offset)/\(image.count) bytes")
                    let reply = try await performWithRetry(next)
                    guard reply.count == 4 else { throw CustomKeyboardError.rejected(status: 0xff) }
                    try chunker.applyAcceptedOffset(reply.reduce(0) { ($0 << 8) | UInt32($1) })
                    flashingProgress = Double(chunker.offset) / Double(image.count)
                }
                phase = .busy("Finalizing update")
                /* Record the uncertain state BEFORE dispatching A3 (review
                 * 2026-10-13, P0): from the moment A3 leaves, the keyboard
                 * may commit at any time, and any interruption -- timeout,
                 * disconnection, malformed reply, app termination -- must
                 * leave this record behind. A3 is never retried; a validated
                 * acknowledgment is the only thing that advances the record. */
                recordPendingUpdate(imageSHA256: imageSHA256, imageSize: image.count, outcome: .commitUnknown)
                let finishReply: [UInt8]
                do {
                    finishReply = try await perform(.finishUpdate(size: image.count))
                } catch CustomKeyboardError.timedOut {
                    fail("The final step's outcome is UNKNOWN: the update may already be committed and its reply was lost. Do not assume nothing was written. The image installs on the next keyboard reboot; reconnect and verify before doing anything else.")
                    return
                }
                /* A3 payload: the committed size must equal the intended
                 * image size exactly (review 2026-10-12). A malformed or
                 * short acknowledgment leaves the pre-dispatch commit-unknown
                 * record in place: the commit may have happened. */
                guard finishReply.count == 4 else { throw CustomKeyboardError.rejected(status: 0xff) }
                let committed = finishReply.reduce(0) { ($0 << 8) | UInt32($1) }
                guard committed == image.count else {
                    throw CustomKeyboardError.updateRejected(at: "committed size \(committed), expected \(image.count)", status: 0x01)
                }
                /* Validated acknowledgment: advance the record. The record
                 * itself stays until explicitly reconciled: a successful
                 * commit still proves nothing about installation. */
                advancePendingUpdate(to: .sentRebootPending)
                if rebootToInstall {
                    /* A0 asks the keyboard to reboot into the installer; the
                     * reboot kills the session. Keep the case attached. */
                    phase = .busy("Waiting for the keyboard to reboot (about 10 seconds)")
                    awaitingReboot = true
                    try dispatch(CustomKeyboardCommand.rebootRequest)
                    rebootTask = Task { [weak self] in
                        guard let self else { return }
                        let confirmMillis = Int(self.rebootConfirmTimeoutNanos / 1_000_000)
                        try? await Task.sleep(for: .milliseconds(confirmMillis))
                        guard !Task.isCancelled else { return }
                        self.rebootNotConfirmed()
                    }
                } else {
                    phase = .updateUnverified("Update staged; it installs on the next keyboard reboot. Verify after it restarts.")
                }
            } catch {
                /* onClosed already reported the real cause (link closed);
                 * do not overwrite it with the generic transport error. */
                if case .failed = phase { return }
                fail("Update failed; \(error)")
            }
        }
    }

    /// The session did not die within the confirmation window after A0:
    /// the image is staged (A3 committed), so it installs on the next
    /// power cycle, but the reboot may not have been delivered. The
    /// installation stays unverified either way (review 2026-10-12).
    private func rebootNotConfirmed() {
        guard awaitingReboot else { return }
        awaitingReboot = false
        phase = .updateUnverified("The keyboard did not reboot after the update command. The image is staged and installs on the next power cycle. Verify before relying on the keyboard.")
    }

    // MARK: Exchange plumbing

    /// One bounded retry on a timeout, for A1 and A2 only (review
    /// 2026-10-12). A1 re-inits staging, which is what a fresh cycle does
    /// anyway. For A2 the retry covers a lost request; a lost reply after
    /// the device accepted the chunk surfaces as an offset rejection, which
    /// fails the update cleanly, and the next full attempt resyncs via A1.
    /// A3 is NEVER retried: a lost reply after a successful commit is
    /// indistinguishable from a failed commit, so the finish path handles
    /// its timeout separately as a commit-unknown outcome.
    private func performWithRetry(_ exchange: CustomKeyboardExchange) async throws -> [UInt8] {
        do {
            return try await perform(exchange)
        } catch CustomKeyboardError.timedOut {
            return try await perform(exchange)
        }
    }

    private func perform(_ exchange: CustomKeyboardExchange) async throws -> [UInt8] {
        try checkBuildPermission(exchange.request)
        guard link != nil else { throw CustomKeyboardError.transportUnavailable }
        guard self.exchange == nil, continuation == nil else { throw CustomKeyboardError.transportUnavailable }
        let request = exchange.request
        return try await withCheckedThrowingContinuation { continuation in
            self.exchange = exchange
            self.continuation = continuation
            timeoutTask?.cancel()
            timeoutTask = Task { [weak self] in
                try? await Task.sleep(for: .milliseconds(Int((self?.exchangeTimeoutNanos ?? 5_000_000_000) / 1_000_000)))
                guard !Task.isCancelled else { return }
                self?.exchangeTimedOut()
            }
            do { try dispatch(request) }
            catch { resumePending(with: error) }
        }
    }

    /// A lost reply must fail the exchange cleanly instead of hanging the
    /// UI in .busy forever (review 2026-10-11, F-7).
    private func exchangeTimedOut() {
        guard let pending = continuation else { return }
        exchange = nil
        continuation = nil
        timeoutTask = nil
        pending.resume(throwing: CustomKeyboardError.timedOut)
    }

    private func receive(_ fragment: [UInt8]) {
        guard var current = exchange else { return }
        guard let pendingContinuation = continuation else { return }
        timeoutTask?.cancel()
        timeoutTask = nil
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
