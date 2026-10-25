import CryptoKit
import Foundation
import OSLog

/// Read-only identity and official stock image preparation on iOS.
/// Host tests exercise stock, QM and RS transactions with physical writes disabled.
@MainActor
final class CustomFirmwareController: ObservableObject {
    // No shipped iOS configuration can enable firmware writes, including the legacy flag.
    // Only macOS host executables compiled for offline tests can exercise them.
    #if STOCK_MANAGER_OFFLINE_TESTING && os(macOS)
    static let stockUpdatesEnabled = true
    #else
    static let stockUpdatesEnabled = false
    #endif
    #if STOCK_FIRMWARE_MANAGER || STOCK_MANAGER_OFFLINE_TESTING
    static let stockManagerEnabled = true
    #else
    static let stockManagerEnabled = false
    #endif

    #if QMK_RAM_KEYMAP
    static let keymapBuildEnabled = true
    #else
    static let keymapBuildEnabled = false
    #endif
    static var diagnosticsOnly: Bool { !stockUpdatesEnabled && !keymapBuildEnabled }

    @Published private(set) var preparedStock: PreparedStockImage?
    @Published private(set) var stockPreparation = "Official image has not been checked."
    @Published private(set) var stockMode: StockFirmwareMode?
    @Published private(set) var stockObservation = "No post-update observation. Installation is unproven."
    private var transaction: StockFirmwareTransaction?
    private var dispatchPermit: [UInt8]?
    private var sessionGeneration = 0
    private var sessionNeedsReconnect = false
    private var operationTask: Task<Void, Never>?

    func prepareStock(_ bytes: [UInt8]) {
        do {
            preparedStock = try PreparedStockImage(bytes)
            stockPreparation = "Verified official CK-5200 V122: 18,648 bytes, SHA-256 \(preparedStock!.sha256). Physical installation is disabled."
        } catch {
            preparedStock = nil
            stockPreparation = "Preparation failed: \(error)"
        }
    }

    /// Sole controller-to-transport boundary, including the fire-and-forget reboot.
    func dispatch(_ request: [UInt8]) throws {
        try checkBuildPermission(request)
        if request.count > 1, (0xa0...0xa3).contains(request[1]) {
            guard dispatchPermit == request else { throw CustomKeyboardError.diagnosticsOnly }
            dispatchPermit = nil
        }
        guard let link else { throw CustomKeyboardError.transportUnavailable }
        link.send(request)
    }

    private func checkBuildPermission(_ request: [UInt8]) throws {
        guard !sessionNeedsReconnect, let target = connectedTarget else {
            throw CustomKeyboardError.transportUnavailable
        }
        if request.count > 1, (0xa0...0xa3).contains(request[1]) {
            guard Self.stockUpdatesEnabled, let transaction,
                  transaction.target == target, transaction.generation == sessionGeneration,
                  transaction.source == stockMode, transaction.request == request,
                  transaction.image.sha256 == CustomKeyboardImageAuthorization.authorizedStockSHA256 else {
                throw CustomKeyboardError.diagnosticsOnly
            }
            return
        }
        let command = request.count > 1 ? request[1] : 0
        if (0x90...0x93).contains(command) {
            guard Self.keymapBuildEnabled else { throw CustomKeyboardError.diagnosticsOnly }
            guard hasQMKIdentity else { throw CustomKeyboardError.notCustomFirmware }
            let lengths: [UInt8: Int] = [0x90: 2, 0x91: 5, 0x92: 7, 0x93: 2]
            guard request.count == lengths[command], Int(request[0]) == request.count else {
                throw CustomKeyboardError.invalidProfile
            }
            if command == 0x91 || command == 0x92 {
                guard request[2] < 4, request[3] < 6, request[4] < 6 else {
                    throw CustomKeyboardError.invalidProfile
                }
            }
            if command == 0x92 || command == 0x93 {
                guard ramWritesAllowed, keymapIsVerified else { throw CustomKeyboardError.ramWritesNotAllowed }
            }
            return
        }
        if Self.keymapBuildEnabled, request == CustomKeyboardCommand.versionRequest,
           target.model == "CK-5200", target.hardwareRevision == "1.2.0" {
            return
        }
        if Self.stockManagerEnabled {
            guard !flashInFlight, request == CustomKeyboardCommand.versionRequest,
                  target.model == "CK-5200", target.hardwareRevision == "1.2.0" else {
                throw CustomKeyboardError.diagnosticsOnly
            }
        } else if !StockReadPolicy.allows(request, model: target.model,
                    firmware: target.firmwareRevision, hardware: target.hardwareRevision) {
            throw CustomKeyboardError.diagnosticsOnly
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
        /// Retained across reconnects and relaunches. No manual success override.
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

    /// An unresolved update, stored atomically before A3. Legacy UserDefaults
    /// records remain readable. No record is cleared by reconnection or relaunch.
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
            /// A3 acknowledged. The separate rebootRequested field records A0 intent.
            case sentRebootPending
        }
        let imageSHA256: String
        let imageSize: Int
        let date: Date
        let outcome: Outcome
        var sourceMode: StockFirmwareMode? = nil
        var transferredBytes: Int? = nil
        var rebootRequested: Bool? = nil
    }

    private static let pendingUpdateKey = "ck5200.pendingUpdate"
    private static var journalURL: URL {
        #if (STOCK_MANAGER_OFFLINE_TESTING || QMK_RAM_KEYMAP) && os(macOS)
        if let path = ProcessInfo.processInfo.environment["STOCK_MANAGER_TEST_STORAGE"] {
            return URL(fileURLWithPath: path)
        }
        #endif
        return FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("ClicksInspector/StockUpdate.json")
    }
    private var journalUnreadable = false

    @Published private(set) var phase: Phase = .idle
    @Published private(set) var keymap: [[[UInt16]]] = [] // [layer][row][col]
    @Published private(set) var info: KeyboardInfo?
    @Published private(set) var selectedLayer = 0
    @Published private(set) var flashingProgress: Double = 0
    @Published private(set) var selfTestReport: SelfTestReport?
    @Published private(set) var selfTestRunning = false
    @Published private(set) var profiles: [KeymapProfile] = []
    @Published private(set) var desiredProfile: KeymapProfile?
    @Published private(set) var profileMessage = "Profiles are saved on this iPhone. Keyboard changes are RAM-only."
    @Published private(set) var keymapIsVerified = false
    @Published private(set) var keymapOperationRunning = false
    @Published private(set) var verifiedWrites = 0
    @Published private(set) var plannedWrites = 0
    @Published var ramWritesAllowed = false
    private var library = KeymapProfileLibrary()
    private let profilesURL: URL
    private var profileStorageAvailable = true
    private var keymapTask: Task<Void, Never>?

    var hasQMKIdentity: Bool {
        link != nil && !sessionNeedsReconnect && connectedTarget?.model == "CK-5200"
            && connectedTarget?.hardwareRevision == "1.2.0"
            && lastVersionPayload.map(CustomKeyboardIdentity.isCustomFirmware) == true
    }

    var canEditKeymap: Bool {
        Self.keymapBuildEnabled && hasQMKIdentity && ramWritesAllowed
            && keymapIsVerified && !keymapOperationRunning && !selfTestRunning
    }

    var needsReapply: Bool {
        keymapIsVerified && desiredProfile.map { $0.keymap != keymap } == true
    }

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
    var connectedTarget: ConnectedTarget? {
        didSet {
            if oldValue != connectedTarget {
                lastVersionPayload = nil
                stockMode = nil
                keymapIsVerified = false
                ramWritesAllowed = false
                if transaction != nil { detach() }
            }
        }
    }
    private(set) var lastVersionPayload: [UInt8]?
    private(set) var pendingUpdate: PendingUpdate?

    /// Per-exchange timeout, and the window after A0 in which the keyboard
    /// must reboot (its session dies). Injectable so the host check can
    /// exercise the timeout paths quickly (review 2026-10-11, F-7).
    var exchangeTimeoutNanos: UInt64 = 5_000_000_000
    var rebootConfirmTimeoutNanos: UInt64 = 10_000_000_000

    init(profilesURL: URL = KeymapProfileLibrary.fileURL) {
        self.profilesURL = profilesURL
        do { pendingUpdate = try Self.loadPendingUpdate() }
        catch {
            journalUnreadable = true
            stockObservation = "Stored transaction cannot be read. It is preserved; installation is blocked."
        }
        do {
            library = try KeymapProfileLibrary.load(from: profilesURL)
            profiles = library.profiles
            desiredProfile = profiles.first { $0.id == library.desiredID }
        } catch {
            profileStorageAvailable = false
            profileMessage = "Cannot load phone profiles: \(error). The file was preserved."
        }
    }

    /// Host-test hook: clear the persisted unresolved-update record.
    #if (STOCK_MANAGER_OFFLINE_TESTING || QMK_RAM_KEYMAP) && os(macOS)
    static func resetPendingUpdateForTesting() {
        UserDefaults.standard.removeObject(forKey: pendingUpdateKey)
        try? FileManager.default.removeItem(at: journalURL)
    }
    #endif

    private static func loadPendingUpdate() throws -> PendingUpdate? {
        if FileManager.default.fileExists(atPath: journalURL.path) {
            return try JSONDecoder().decode(PendingUpdate.self, from: Data(contentsOf: journalURL))
        }
        guard let data = UserDefaults.standard.data(forKey: pendingUpdateKey) else { return nil }
        return try JSONDecoder().decode(PendingUpdate.self, from: data)
    }

    private func persist(_ record: PendingUpdate) throws {
        let url = Self.journalURL
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try JSONEncoder().encode(record).write(to: url, options: .atomic)
        pendingUpdate = record
    }

    private func recordPendingUpdate(imageSHA256: String, imageSize: Int, outcome: PendingUpdate.Outcome) throws {
        var record = PendingUpdate(imageSHA256: imageSHA256, imageSize: imageSize, date: Date(), outcome: outcome)
        record.sourceMode = stockMode
        record.transferredBytes = transaction?.offset
        record.rebootRequested = false
        try persist(record)
    }

    /// Advance the persisted record to a later outcome (review 2026-10-13):
    /// only a validated A3 acknowledgment may do this, moving the record
    /// from the pre-dispatch unknown to the sent/reboot-pending state.
    private func advancePendingUpdate(to outcome: PendingUpdate.Outcome) throws {
        guard let current = pendingUpdate else { return }
        var record = PendingUpdate(imageSHA256: current.imageSHA256, imageSize: current.imageSize,
                                    date: current.date, outcome: outcome)
        record.sourceMode = current.sourceMode
        record.transferredBytes = current.transferredBytes
        record.rebootRequested = current.rebootRequested
        try persist(record)
    }

    /// Explicit reconciliation only (review 2026-10-12): reconnecting or
    /// relaunching must never silently clear an unresolved update.
    func markPendingUpdateVerified() {
        guard pendingUpdate != nil else { return }
        // Kept as a fail-closed API for old callers. User acknowledgement and
        // a same-version response cannot prove that the installer copied bytes.
        phase = .updateUnverified("Installation cannot be marked verified manually. The transaction record is retained.")
    }

    var layers: Int { info?.layers ?? 0 }
    var rows: Int { info?.rows ?? 0 }
    var columns: Int { info?.columns ?? 0 }

    func attach(_ link: KeyboardPacketLink) {
        detach()
        sessionNeedsReconnect = false
        self.link = link
        let generation = sessionGeneration
        link.onFragment = { [weak self] fragment in
            guard let self, generation == self.sessionGeneration else { return }
            self.receive(fragment)
        }
        link.onClosed = { [weak self] reason in
            guard let self, generation == self.sessionGeneration else { return }
            let rebootRequested = self.awaitingReboot
            let wasBusy = self.flashInFlight
            self.invalidateSession(with: CustomKeyboardError.transportUnavailable)
            if rebootRequested {
                self.phase = .updateUnverified("Session closed after reboot request. Reboot, installer execution and installation remain unproven.")
            } else if wasBusy {
                self.phase = .failed("Link closed: \(reason)")
            } else {
                self.phase = .idle
            }
        }
        identify()
    }

    func detach() {
        invalidateSession(with: CustomKeyboardError.transportUnavailable)
        phase = .idle
    }

    /// Drop all queued output before an old operation can continue.
    private func invalidateSession(with error: Error) {
        if keymapOperationRunning && plannedWrites > 0 {
            profileMessage = "Disconnected: \(verifiedWrites)/\(plannedWrites) writes verified. The last write may have reached RAM. Reconnect to read the layout."
        }
        sessionGeneration += 1
        keymapTask?.cancel()
        keymapTask = nil
        info = nil
        keymap = []
        keymapIsVerified = false
        ramWritesAllowed = false
        keymapOperationRunning = false
        selfTestRunning = false
        operationTask?.cancel()
        operationTask = nil
        transaction = nil
        dispatchPermit = nil
        stockMode = nil
        lastVersionPayload = nil
        flashInFlight = false
        sessionNeedsReconnect = true
        link?.onFragment = nil
        link?.onClosed = nil
        link?.close()
        link = nil
        timeoutTask?.cancel()
        timeoutTask = nil
        rebootTask?.cancel()
        rebootTask = nil
        awaitingReboot = false
        resumePending(with: error)
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
    /// Left Shift crossing (2,3), restoring its original value. Publishes a report.
    func runSelfTest() {
        guard canEditKeymap else { return }
        selfTestRunning = true
        keymapOperationRunning = true
        verifiedWrites = 0
        plannedWrites = 2
        selfTestReport = nil
        let generation = sessionGeneration
        keymapTask = Task {
            defer {
                if generation == sessionGeneration {
                    selfTestRunning = false
                    keymapOperationRunning = false
                }
            }
            do {
                var report = SelfTestReport()

                // 1. Identify.
                let versionPayload = try await perform(.version())
                lastVersionPayload = versionPayload
                report.checks.append(KeyboardSelfTest.verifyIdentify(versionPayload))
                guard CustomKeyboardIdentity.isCustomFirmware(versionPayload) else {
                    throw CustomKeyboardError.notCustomFirmware
                }

                // 2. Info.
                let infoPayload = try await perform(.info())
                guard let parsed = CustomKeyboardIdentity.info(fromInfoPayload: infoPayload),
                      parsed == KeyboardSelfTest.expectedInfo, infoPayload[3] == 0 else {
                    throw CustomKeyboardError.invalidProfile
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

                // 4. Temporarily remap Left Shift at (2,3), then restore its read value.
                let original = keymap[0][2][3]
                try await writeAndVerify(layer: 0, row: 2, column: 3, keycode: 0x0014)
                let readBack = keymap[0][2][3]
                try await writeAndVerify(layer: 0, row: 2, column: 3, keycode: original)
                let restored = keymap[0][2][3]
                report.checks.append(KeyboardSelfTest.verifyRoundTrip(
                    written: 0x0014, readBack: readBack, restored: restored, original: original))
                keymap[0][2][3] = restored

                selfTestReport = report
                logger.info("Self-test: \(report.passCount)/\(report.totalCount) checks passed")
            } catch {
                guard generation == sessionGeneration else { return }
                keymapIsVerified = false
                ramWritesAllowed = false
                sessionNeedsReconnect = true
                profileMessage = "Self-test stopped. A RAM write may have reached the keyboard. Reconnect to read its layout. \(error)"
                var report = SelfTestReport()
                report.checks.append(SelfTestCheck(name: "Transport", passed: false,
                                                  detail: "Self-test aborted: \(error)"))
                selfTestReport = report
            }
        }
    }

    /// Observe identity after reconnect. This cannot prove application replacement.
    func verifyAfterReboot() {
        identify()
    }

    func identify() {
        guard link != nil, exchange == nil, !flashInFlight, !keymapOperationRunning,
              !selfTestRunning, !sessionNeedsReconnect else { return }
        phase = .identifying
        keymapIsVerified = false
        plannedWrites = 0
        verifiedWrites = 0
        keymapOperationRunning = true
        let generation = sessionGeneration
        operationTask = Task {
            defer { if generation == sessionGeneration { keymapOperationRunning = false } }
            do {
                try Task.checkCancellation()
                guard generation == sessionGeneration else { return }
                let payload = try await perform(.version())
                guard generation == sessionGeneration, !sessionNeedsReconnect else { return }
                lastVersionPayload = payload
                stockMode = StockFirmwareMode(payload: payload)
                let identified: Phase
                if Self.keymapBuildEnabled && hasQMKIdentity {
                    identified = .customFirmware("Custom QMK firmware detected")
                } else if payload == CustomKeyboardCommand.stockVersionPayload {
                    identified = .stockFirmware("Stock V122 detected. Physical installation is disabled.")
                } else if stockMode == .rescue {
                    identified = .stockFirmware("RS 00 01 application rescue detected. This is not independent boot recovery.")
                } else {
                    identified = .stockFirmware("Unrecognized firmware identity \(payload.map { String(format: "%02x", $0) }.joined()); flashing is refused")
                }
                if pendingUpdate != nil {
                    /* An unresolved update is never silently resolved by a
                     * reconnect (review 2026-10-12). */
                    stockObservation = stockMode == .stock
                        ? "Stock V122 responds after reconnect. This does not prove installer execution or application replacement."
                        : "Expected stock V122 is not observed. Installation remains unproven."
                    phase = .updateUnverified("Previous update retained: \(pendingUpdate!.outcome.rawValue). \(stockObservation)")
                } else {
                    phase = identified
                    if case .customFirmware = identified {
                        try await loadKeymap()
                    }
                }
            } catch {
                guard generation == sessionGeneration else { return }
                fail("Identify failed; \(error)")
            }
        }
    }

    private func loadKeymap() async throws {
        let payload = try await perform(.info())
        guard let parsed = CustomKeyboardIdentity.info(fromInfoPayload: payload),
              parsed == KeyboardInfo(layers: 4, rows: 6, columns: 6), payload[3] == 0 else {
            throw CustomKeyboardError.rejected(status: 0xff)
        }
        info = parsed
        keymap = Array(repeating: Array(repeating: Array(repeating: 0, count: parsed.columns), count: parsed.rows),
                       count: parsed.layers)
        for layer in 0..<parsed.layers {
            try await readKeymap(layer: UInt8(layer))
        }
        keymapIsVerified = true
        phase = .customFirmware("Custom firmware ready; \(parsed.layers) layers, \(parsed.rows)x\(parsed.columns)")
    }

    func selectLayer(_ layer: Int) {
        guard info != nil, layer >= 0, layer < layers else { return }
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
        guard canEditKeymap, (0..<layers).contains(layer), (0..<rows).contains(row),
              (0..<columns).contains(column) else { return }
        startKeymapOperation(total: 1) {
            try await self.writeAndVerify(layer: layer, row: row, column: column, keycode: keycode)
            self.profileMessage = "Key verified in keyboard RAM. Save a phone profile to keep it."
        }
    }

    private func writeAndVerify(layer: Int, row: Int, column: Int, keycode: UInt16) async throws {
        _ = try await perform(.setKey(layer: UInt8(layer), row: UInt8(row), column: UInt8(column), keycode: keycode))
        let payload = try await perform(.getKey(layer: UInt8(layer), row: UInt8(row), column: UInt8(column)))
        guard payload == [UInt8(keycode >> 8), UInt8(keycode & 0xff)] else {
            throw CustomKeyboardError.readbackMismatch
        }
        keymap[layer][row][column] = keycode
        verifiedWrites += 1
    }

    private func startKeymapOperation(total: Int, operation: @escaping @MainActor () async throws -> Void) {
        guard canEditKeymap else { return }
        keymapOperationRunning = true
        verifiedWrites = 0
        plannedWrites = total
        phase = .busy("Updating keyboard RAM")
        let generation = sessionGeneration
        keymapTask = Task {
            defer { if generation == sessionGeneration { keymapOperationRunning = false } }
            do {
                try await operation()
                try Task.checkCancellation()
                phase = .customFirmware("Keyboard RAM readback verified")
            } catch {
                guard generation == sessionGeneration else { return }
                keymapIsVerified = false
                sessionNeedsReconnect = true
                ramWritesAllowed = false
                lastVersionPayload = nil
                profileMessage = "Stopped: \(verifiedWrites)/\(plannedWrites) writes verified. The last write may have reached RAM. Reconnect to read the layout. \(error)"
                fail(profileMessage)
            }
        }
    }

    func resetKeymap() {
        startKeymapOperation(total: 1) {
            _ = try await self.perform(.resetKeymap())
            for layer in 0..<self.layers {
                try await self.readKeymap(layer: UInt8(layer))
            }
            guard self.keymap == FactoryLayout.asRowMajor else { throw CustomKeyboardError.readbackMismatch }
            self.verifiedWrites = 1
            self.profileMessage = "Factory layout restored and all 144 keys verified. Phone profiles are kept."
        }
    }

    func saveProfile(name: String) {
        guard keymapIsVerified, !keymapOperationRunning, profileStorageAvailable else { return }
        do {
            let profile = try KeymapProfile(name: name, keymap: keymap)
            var next = library
            next.profiles.append(profile)
            next.desiredID = profile.id
            try next.save(to: profilesURL)
            library = next
            profiles = next.profiles
            desiredProfile = profile
            profileMessage = "Saved \(profile.name) on this iPhone (four 6x6 layers)."
        } catch { profileMessage = "Profile was not saved: \(error)" }
    }

    func selectProfile(_ id: UUID) {
        guard !keymapOperationRunning, profileStorageAvailable,
              let profile = profiles.first(where: { $0.id == id }) else { return }
        do {
            var next = library
            next.desiredID = id
            try next.save(to: profilesURL)
            library = next
            desiredProfile = profile
            profileMessage = "Selected \(profile.name). Apply is always manual."
        } catch { profileMessage = "Profile selection was not saved: \(error)" }
    }

    func applyDesiredProfile() {
        guard canEditKeymap, let profile = desiredProfile else { return }
        do { try profile.validate() } catch { profileMessage = "Invalid profile: \(error)"; return }
        let changes = (0..<4).flatMap { layer in
            (0..<6).flatMap { row in (0..<6).compactMap { column -> (Int, Int, Int)? in
                keymap[layer][row][column] == profile.keymap[layer][row][column] ? nil : (layer, row, column)
            } }
        }
        startKeymapOperation(total: changes.count) {
            for (layer, row, column) in changes {
                try await self.writeAndVerify(layer: layer, row: row, column: column, keycode: profile.keymap[layer][row][column])
            }
            self.profileMessage = "Applied \(profile.name): \(changes.count) writes verified in RAM."
        }
    }

    /// Host-only stock transaction. Every iOS build refuses physical writes.
    /// Exact CK-5200 hardware, recognized stock/QM/RS identity and V122 hash required.
    func flash(image: [UInt8], rebootToInstall: Bool) {
        guard Self.stockUpdatesEnabled else {
            fail(CustomKeyboardError.diagnosticsOnly.description)
            return
        }
        guard link != nil else {
            fail("No connection; cannot flash")
            return
        }
        guard !journalUnreadable else {
            fail("Stored transaction is unreadable; installation is blocked.")
            return
        }
        if pendingUpdate != nil {
            phase = .updateUnverified("A previous update (sha256 \(pendingUpdate!.imageSHA256.prefix(12))…) is unresolved. A version response cannot clear it; further installation is blocked.")
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
        guard !sessionNeedsReconnect, let mode = stockMode,
              mode != .stock || target.firmwareRevision == "1.2.2" else {
            flashInFlight = false
            fail("Unrecognized firmware identity; refusing to flash.")
            return
        }
        let prepared: PreparedStockImage
        do {
            prepared = try PreparedStockImage(image)
        } catch {
            flashInFlight = false
            fail("Image refused: only the bundled stock V122 artifact is accepted. \(error)")
            return
        }
        let imageSHA256 = prepared.sha256
        let generation = sessionGeneration
        transaction = StockFirmwareTransaction(image: prepared, source: mode, target: target, generation: generation)
        phase = .busy("Starting update session")
        operationTask = Task {
            defer {
                if generation == sessionGeneration {
                    flashInFlight = false
                    transaction = nil
                    dispatchPermit = nil
                }
            }
            do {
                try requireTransaction(generation)
                flashingProgress = 0
                let beginReply = try await perform(.beginUpdate(size: image.count))
                try requireTransaction(generation)
                /* A1 payload: the advertised maximum image size (review
                 * 2026-10-12: the app previously ignored it). */
                guard beginReply.count == 4 else { throw CustomKeyboardError.rejected(status: 0xff) }
                let advertisedMaximum = Int(beginReply.reduce(0) { ($0 << 8) | UInt32($1) })
                guard image.count <= advertisedMaximum else {
                    throw CustomKeyboardError.updateRejected(at: "advertised maximum \(advertisedMaximum)", status: 0x01)
                }
                try transaction?.accept(beginReply)
                var chunker = try UpdateChunker(image: image)
                while let next = chunker.nextExchange() {
                    phase = .busy("Writing \(chunker.offset)/\(image.count) bytes")
                    let reply = try await perform(next)
                    try requireTransaction(generation)
                    guard reply.count == 4 else { throw CustomKeyboardError.rejected(status: 0xff) }
                    try chunker.applyAcceptedOffset(reply.reduce(0) { ($0 << 8) | UInt32($1) })
                    try transaction?.accept(reply)
                    flashingProgress = Double(chunker.offset) / Double(image.count)
                }
                phase = .busy("Finalizing update")
                /* Record the uncertain state BEFORE dispatching A3 (review
                 * 2026-10-13, P0): from the moment A3 leaves, the keyboard
                 * may commit at any time, and any interruption -- timeout,
                 * disconnection, malformed reply, app termination -- must
                 * leave this record behind. A3 is never retried; a validated
                 * acknowledgment is the only thing that advances the record. */
                try recordPendingUpdate(imageSHA256: imageSHA256, imageSize: image.count, outcome: .commitUnknown)
                let finishReply: [UInt8]
                do {
                    finishReply = try await perform(.finishUpdate(size: image.count))
                } catch CustomKeyboardError.timedOut {
                    return
                }
                try requireTransaction(generation)
                /* A3 payload: the committed size must equal the intended
                 * image size exactly (review 2026-10-12). A malformed or
                 * short acknowledgment leaves the pre-dispatch commit-unknown
                 * record in place: the commit may have happened. */
                guard finishReply.count == 4 else { throw CustomKeyboardError.rejected(status: 0xff) }
                let committed = finishReply.reduce(0) { ($0 << 8) | UInt32($1) }
                guard committed == image.count else {
                    throw CustomKeyboardError.updateRejected(at: "committed size \(committed), expected \(image.count)", status: 0x01)
                }
                try transaction?.accept(finishReply)
                /* Validated acknowledgment: advance the record. The record
                 * itself stays until explicitly reconciled: a successful
                 * commit still proves nothing about installation. */
                try advancePendingUpdate(to: .sentRebootPending)
                if rebootToInstall {
                    /* A0 asks the keyboard to reboot into the installer; the
                     * reboot kills the session. Keep the case attached. */
                    phase = .busy("Waiting for the keyboard to reboot (about 10 seconds)")
                    awaitingReboot = true
                    if var record = pendingUpdate {
                        record.rebootRequested = true
                        try persist(record)
                    }
                    if mode == .stock {
                        // V122 replies 04 02 A0 00. An ACK still proves no copying.
                        let payload = try await perform(.reboot())
                        try requireTransaction(generation)
                        try transaction?.accept(payload)
                    } else {
                        // Accepted M2 at 246f2b8 resets synchronously without an A0
                        // ACK. Record only a request, never successful installation.
                        dispatchPermit = CustomKeyboardCommand.rebootRequest
                        try dispatch(CustomKeyboardCommand.rebootRequest)
                        try requireTransaction(generation)
                    }
                    phase = .updateUnverified("Commit acknowledged; reboot requested. Installer execution and application replacement are unproven.")
                    rebootTask = Task { [weak self] in
                        guard let self else { return }
                        let confirmMillis = Int(self.rebootConfirmTimeoutNanos / 1_000_000)
                        try? await Task.sleep(for: .milliseconds(confirmMillis))
                        guard !Task.isCancelled else { return }
                        self.rebootNotConfirmed()
                    }
                } else {
                    phase = .updateUnverified("Commit acknowledged. Reboot was not requested. Installer execution and application replacement remain unproven.")
                }
            } catch {
                guard generation == sessionGeneration else { return }
                /* onClosed already reported the real cause (link closed);
                 * do not overwrite it with the generic transport error. */
                let offset = transaction?.offset ?? 0
                let rebootRequested = awaitingReboot
                invalidateSession(with: error)
                if case .updateUnverified = phase { return }
                if case .failed = phase { return }
                if rebootRequested, pendingUpdate != nil {
                    phase = .updateUnverified("Update outcome retained after \(error). Reconnect for observations; installation remains unproven.")
                } else {
                    fail("Staging interrupted at \(offset) acknowledged bytes; \(error). Reconnect before a new attempt.")
                }
            }
        }
    }

    private func requireTransaction(_ generation: Int) throws {
        try Task.checkCancellation()
        guard generation == sessionGeneration, !sessionNeedsReconnect,
              let transaction, transaction.generation == generation else {
            throw CustomKeyboardError.transportUnavailable
        }
    }

    /// Session closure is an observation, not evidence of installer execution.
    private func rebootNotConfirmed() {
        guard awaitingReboot else { return }
        awaitingReboot = false
        phase = .updateUnverified("No session closure observed after A0. Reboot and installation remain unproven. The commit record is retained.")
    }

    // MARK: Exchange plumbing

    private func perform(_ exchange: CustomKeyboardExchange) async throws -> [UInt8] {
        try Task.checkCancellation()
        try checkBuildPermission(exchange.request)
        guard link != nil else { throw CustomKeyboardError.transportUnavailable }
        guard self.exchange == nil, continuation == nil else { throw CustomKeyboardError.transportUnavailable }
        let request = exchange.request
        let generation = sessionGeneration
        let payload: [UInt8] = try await withCheckedThrowingContinuation { continuation in
            self.exchange = exchange
            self.continuation = continuation
            timeoutTask?.cancel()
            timeoutTask = Task { [weak self] in
                try? await Task.sleep(for: .milliseconds(Int((self?.exchangeTimeoutNanos ?? 5_000_000_000) / 1_000_000)))
                guard !Task.isCancelled else { return }
                self?.exchangeTimedOut()
            }
            dispatchPermit = request
            do { try dispatch(request) }
            catch { resumePending(with: error) }
        }
        guard generation == sessionGeneration else { throw CustomKeyboardError.transportUnavailable }
        try Task.checkCancellation()
        return payload
    }

    /// A lost reply must fail the exchange cleanly instead of hanging the
    /// UI in .busy forever (review 2026-10-11, F-7).
    private func exchangeTimedOut() {
        guard continuation != nil else { return }
        invalidateSession(with: CustomKeyboardError.timedOut)
        fail(pendingUpdate == nil
             ? CustomKeyboardError.timedOut.description
             : "Update outcome is UNKNOWN after timeout. Installation remains unproven; the transaction record is retained.")
    }

    private func receive(_ fragment: [UInt8]) {
        guard var current = exchange else { return }
        guard let pendingContinuation = continuation else { return }
        do {
            if let payload = try current.accumulator.append(fragment) {
                timeoutTask?.cancel()
                timeoutTask = nil
                exchange = nil
                continuation = nil
                pendingContinuation.resume(returning: payload)
            } else {
                exchange = current
            }
        } catch {
            invalidateSession(with: error)
            fail("Invalid response; \(error)")
        }
    }
}
