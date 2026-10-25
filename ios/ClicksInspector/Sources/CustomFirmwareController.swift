import CryptoKit
import Foundation
import OSLog
#if canImport(UIKit)
import UIKit
#endif

/// Identity, RAM maps, and image-pinned update transactions.
@MainActor
final class CustomFirmwareController: ObservableObject {
    // Default builds remain write-disabled. Two explicit iOS configurations
    // authorize separate pinned images, never arbitrary files or QMK firmware.
    #if STOCK_MANAGER_OFFLINE_TESTING && os(macOS)
    static let diagnosticsOnly = false
    static let stockUpdatesEnabled = true
    #elseif STOCK_FLASH_TRIAL
    static let diagnosticsOnly = false
    static let stockUpdatesEnabled = true
    #elseif EXTENSION_INSTALLER
    static let diagnosticsOnly = false
    static let stockUpdatesEnabled = false
    #else
    static let diagnosticsOnly = true
    static let stockUpdatesEnabled = false
    #endif

    #if EXTENSION_INSTALLER
    static let extensionUpdatesEnabled = true
    #else
    static let extensionUpdatesEnabled = false
    #endif
    private static var firmwareUpdatesEnabled: Bool { stockUpdatesEnabled || extensionUpdatesEnabled }

    var installationSessionID: Int { sessionGeneration }

    /// No cached consent survives a session change. UI confirmation captures
    /// this session ID, and flash checks it again before starting.
    var extensionInstallBlockReason: String? {
        guard Self.extensionUpdatesEnabled else { return "Use the ExtensionInstaller app configuration to install." }
        guard !journalUnreadable else { return "The saved update record is unreadable. Installation is blocked." }
        guard pendingUpdate == nil else { return "A previous update record exists. Review it under Advanced before another attempt." }
        guard !flashInFlight, !keymapOperationRunning, !selfTestRunning else { return "Wait for the current operation to finish." }
        guard link != nil, !sessionNeedsReconnect, stockMode == .stock,
              lastVersionPayload == CustomKeyboardCommand.stockVersionPayload,
              connectedTarget == ConnectedTarget(model: "CK-5200", firmwareRevision: "1.2.2", hardwareRevision: "1.2.0") else {
            return "Open an installer connection to CK-5200 firmware 1.2.2, hardware 1.2.0."
        }
        return Self.extensionPowerBlockReason
    }

    private static var extensionPowerBlockReason: String? {
        #if canImport(UIKit)
        guard UIApplication.shared.applicationState == .active else { return "Keep the app in the foreground." }
        if !UIDevice.current.isBatteryMonitoringEnabled { UIDevice.current.isBatteryMonitoringEnabled = true }
        let device = UIDevice.current
        guard device.batteryState == .charging || device.batteryState == .full || device.batteryLevel >= 0.5 else {
            return "Charge the iPhone to at least 50%, or connect it to power. Keep keyboard power stable."
        }
        return thermalBlockReason(ProcessInfo.processInfo.thermalState)
        #else
        return nil
        #endif
    }

    static func thermalPressureName(_ state: ProcessInfo.ThermalState) -> String {
        switch state {
        case .nominal: "nominal"
        case .fair: "fair"
        case .serious: "serious"
        case .critical: "critical"
        @unknown default: "unknown (\(state.rawValue))"
        }
    }

    static func thermalBlockReason(_ state: ProcessInfo.ThermalState) -> String? {
        guard state != .nominal && state != .fair else { return nil }
        return "iOS reports \(thermalPressureName(state)) thermal pressure. Installation is paused. This is not a surface temperature reading."
    }

    var extensionPowerStatus: String {
        #if canImport(UIKit)
        if !UIDevice.current.isBatteryMonitoringEnabled { UIDevice.current.isBatteryMonitoringEnabled = true }
        let device = UIDevice.current
        let level = device.batteryLevel < 0 ? "unknown" : "\(Int((device.batteryLevel * 100).rounded()))%"
        let powered = device.batteryState == .charging || device.batteryState == .full
        return "Battery: \(level), \(powered ? "external power" : "battery power"). iOS thermal pressure: \(Self.thermalPressureName(ProcessInfo.processInfo.thermalState))."
        #else
        return "Host test. No physical iPhone power or thermal reading."
        #endif
    }

    func recordInstallationPreflight() {
        logUpdateAudit("preflight", detail: extensionPowerStatus)
    }

    func recordExtensionDiagnostic(_ event: String, bytes: [UInt8], detail: String?) {
        logUpdateAudit("extension.\(event)", command: 0x94,
                       request: event == "request" ? bytes : nil,
                       reply: event == "reply" || event == "invalidFragment" ? bytes : nil,
                       detail: detail)
    }

    #if STOCK_FLASH_TRIAL
    /// True only in the dedicated M4 trial configuration. Drives the visible
    /// trial banner, the enabled install control, and the explicit
    /// risk acknowledgement in the confirmation sheet.
    static let stockFlashTrial = true
    #else
    static let stockFlashTrial = false
    #endif

    #if QMK_RAM_KEYMAP
    static let keymapBuildEnabled = true
    #else
    static let keymapBuildEnabled = false
    #endif
    #if STOCK_FIRMWARE_MANAGER || STOCK_MANAGER_OFFLINE_TESTING
    static let stockManagerEnabled = true
    #else
    static let stockManagerEnabled = false
    #endif

    @Published private(set) var preparedStock: PreparedFirmwareImage?
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
            preparedStock = try PreparedFirmwareImage(bytes)
            stockPreparation = "Verified official CK-5200 V122: 18,648 bytes, SHA-256 \(preparedStock!.sha256). "
                + (Self.stockFlashTrial
                   ? "Trial build: installation is enabled after the risk acknowledgement."
                   : "Physical installation is disabled in this configuration.")
        } catch {
            preparedStock = nil
            stockPreparation = "Preparation failed: \(error)"
        }
    }

    /// Sole controller-to-transport boundary, including the fire-and-forget reboot.
    func dispatch(_ request: [UInt8]) throws {
        logger.debug("command \(request.count > 1 ? Int(request[1]) : -1, privacy: .public) requested with \(request.count, privacy: .public) bytes")
        try checkBuildPermission(request)
        if request.count > 1, (0xa0...0xa3).contains(request[1]) {
            guard dispatchPermit == request else { throw CustomKeyboardError.diagnosticsOnly }
            dispatchPermit = nil
        }
        guard let link else { throw CustomKeyboardError.transportUnavailable }
        link.send(request)
    }

    private func checkBuildPermission(_ request: [UInt8]) throws {
        guard request.count >= 2 else { throw CustomKeyboardError.diagnosticsOnly }
        guard !sessionNeedsReconnect, let target = connectedTarget else {
            throw CustomKeyboardError.transportUnavailable
        }
        let command = request[1]
        if (0xa0...0xa3).contains(command) {
            guard Self.firmwareUpdatesEnabled, !Self.diagnosticsOnly, let transaction,
                  transaction.target == target, transaction.generation == sessionGeneration,
                  transaction.source == stockMode, transaction.request == request,
                  (transaction.image.artifact == .stock
                   ? Self.stockUpdatesEnabled && transaction.image.sha256 == CustomKeyboardImageAuthorization.authorizedStockSHA256
                   : Self.extensionUpdatesEnabled && transaction.source == .stock
                        && transaction.image.sha256 == CustomKeyboardImageAuthorization.authorizedExtensionTransferSHA256) else {
                throw CustomKeyboardError.diagnosticsOnly
            }
            return
        }
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
        if Self.extensionUpdatesEnabled {
            guard request == CustomKeyboardCommand.versionRequest,
                  target.model == "CK-5200", target.firmwareRevision == "1.2.2", target.hardwareRevision == "1.2.0",
                  !flashInFlight || transaction?.step == .begin else { throw CustomKeyboardError.diagnosticsOnly }
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
        /// Operational closure of a finished trial (review 2026-10-20):
        /// records the observation evidence that ended the trial WITHOUT
        /// claiming the application was rewritten. A resolved record no
        /// longer blocks a new transaction; its contents and the audit log
        /// are retained. Installation remains unproven by construction.
        struct Resolution: Codable, Equatable {
            let date: Date
            let observation: String
            let statement: String
        }
        let imageSHA256: String
        let imageSize: Int
        let date: Date
        let outcome: Outcome
        var sourceMode: StockFirmwareMode? = nil
        var transferredBytes: Int? = nil
        var rebootRequested: Bool? = nil
        var resolution: Resolution? = nil

        var isResolved: Bool { resolution != nil }
    }

    private static let pendingUpdateKey = "ck5200.pendingUpdate"
    private static var journalURL: URL {
        #if (STOCK_MANAGER_OFFLINE_TESTING || QMK_RAM_KEYMAP || STOCK_FLASH_TRIAL || EXTENSION_INSTALLER) && os(macOS)
        if let path = ProcessInfo.processInfo.environment["STOCK_MANAGER_TEST_STORAGE"] {
            return URL(fileURLWithPath: path)
        }
        #endif
        return FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("ClicksInspector/StockUpdate.json")
    }
    private var journalUnreadable = false

    /// M4 trial audit trail: exact TX/RX frames and lifecycle events for
    /// every update-command exchange, persisted as JSON lines beside the
    /// journal. The stock image is a public artifact in this repository, so
    /// recording full frame bytes loses no secret. The log is append-only;
    /// nothing interprets it during the trial. See
    /// keyboards/ck5200/docs/m4-stock-flash-trial.md.
    private struct StockUpdateAuditEvent: Codable {
        let date: Date
        let event: String
        let command: Int?
        let requestHex: String?
        let replyHex: String?
        let detail: String?
    }

    private static var updateAuditURL: URL {
        #if (STOCK_MANAGER_OFFLINE_TESTING || STOCK_FLASH_TRIAL || EXTENSION_INSTALLER) && os(macOS)
        if let path = ProcessInfo.processInfo.environment["STOCK_MANAGER_TEST_STORAGE"] {
            return URL(fileURLWithPath: path).deletingLastPathComponent()
                .appendingPathComponent("StockUpdateAudit.jsonl")
        }
        #endif
        return FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("ClicksInspector/StockUpdateAudit.jsonl")
    }

    /// Returns false when the event could not be persisted. Callers that
    /// gate a physical frame on audit success must treat false as a hard
    /// stop (review 2026-10-20: audit write failure must not silently
    /// continue the trial).
    @discardableResult
    private func logUpdateAudit(_ event: String, command: Int? = nil,
                                request: [UInt8]? = nil, reply: [UInt8]? = nil,
                                detail: String? = nil) -> Bool {
        func hex(_ bytes: [UInt8]) -> String { bytes.map { String(format: "%02x", $0) }.joined() }
        let record = StockUpdateAuditEvent(
            date: Date(), event: event, command: command,
            requestHex: request.map(hex), replyHex: reply.map(hex), detail: detail)
        guard let data = try? JSONEncoder().encode(record) else {
            logger.error("audit log encode failed for \(event, privacy: .public)")
            return false
        }
        do {
            let url = Self.updateAuditURL
            try FileManager.default.createDirectory(at: url.deletingLastPathComponent(),
                                                    withIntermediateDirectories: true)
            if FileManager.default.fileExists(atPath: url.path) {
                let handle = try FileHandle(forWritingTo: url)
                defer { try? handle.close() }
                // A failed seek must not silently rewind the log over earlier
                // evidence (review 2026-10-21, P3-1).
                try handle.seekToEnd()
                try handle.write(data)
                try handle.write(Data("\n".utf8))
            } else {
                // Single atomic write of record AND terminator: two separate
                // atomic writes overwrite the first event with the newline
                // (review 2026-10-20 defect).
                try (data + Data("\n".utf8)).write(to: url, options: .atomic)
            }
            return true
        } catch {
            logger.error("audit log write failed: \(error.localizedDescription, privacy: .public)")
            return false
        }
    }

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
    private var replyFramer = KeyboardPacketFramer(maximumLength: 15)
    private var continuation: CheckedContinuation<[UInt8], Error>?
    private var timeoutTask: Task<Void, Never>?
    private var rebootTask: Task<Void, Never>?
    private var awaitingReboot = false

    /// Synchronous single-operation guard, set and cleared on the main
    /// actor before any Task runs, so two rapid submissions cannot both
    /// pass (review 2026-10-12). Published so the M4 trial UI can offer
    /// staging-only abort exactly while it is safe.
    @Published private(set) var flashInFlight = false

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
                if transaction != nil || keymapOperationRunning { detach() }
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
    #if (STOCK_MANAGER_OFFLINE_TESTING || QMK_RAM_KEYMAP || STOCK_FLASH_TRIAL || EXTENSION_INSTALLER) && os(macOS)
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
        phase = .updateUnverified("Installation cannot be marked verified manually. The transaction record is retained. Use 'Conclude trial' to close it operationally; installation stays unproven.")
    }

    /// Operational closure of a finished trial (review 2026-10-20).
    ///
    /// The journal's uncertainty is correct and stays: this NEVER records
    /// that the application was rewritten. It records the observation that
    /// ended the trial (for example the post-reboot identity payload and
    /// typing behavior), clears the controller gate for a future transaction,
    /// and logs a `trialConcluded` event whose detail retains the observation.
    /// The trial UI intentionally keeps its install control off while this
    /// journal record exists, so one concluded record means one physical
    /// trial per record. The journal slot is reused only by a future
    /// transaction; the resolution's evidence persists in the audit log.
    /// Use only after the trial's evidence bundle has been captured.
    func concludePendingUpdate(observation: String) {
        guard var record = pendingUpdate, !record.isResolved else { return }
        record.resolution = PendingUpdate.Resolution(
            date: Date(),
            observation: observation,
            statement: "Trial concluded from observed evidence. Application rewrite remains unproven.")
        do {
            try persist(record)
            logUpdateAudit("trialConcluded",
                           detail: "observation: \(observation); installation remains unproven")
            phase = .updateUnverified("Trial concluded. Installation remains unproven by construction. The install control stays off for this journal record; the resolution is retained in the audit log.")
        } catch {
            journalUnreadable = true
            stockObservation = "The trial record could not be updated. It is preserved; installation remains unproven."
        }
    }

    var layers: Int { info?.layers ?? 0 }
    var rows: Int { info?.rows ?? 0 }
    var columns: Int { info?.columns ?? 0 }

    func attach(_ link: KeyboardPacketLink) {
        detach()
        sessionNeedsReconnect = false
        self.link = link
        let generation = sessionGeneration
        logger.debug("session attached at generation \(generation, privacy: .public)")
        link.onFragment = { [weak self] fragment in
            guard let self, generation == self.sessionGeneration else { return }
            self.receive(fragment)
        }
        link.onClosed = { [weak self] reason in
            guard let self, generation == self.sessionGeneration else { return }
            let rebootRequested = self.awaitingReboot
            let flashWasInFlight = self.flashInFlight
            let keymapWasInFlight = self.keymapOperationRunning
            if flashWasInFlight {
                /* The realistic mid-trial interruption must be in the audit
                 * evidence bundle (review 2026-10-21, P2-2). */
                self.logUpdateAudit("sessionClosed",
                                    detail: "reason: \(reason); update was in flight; "
                                        + "rebootRequested \(rebootRequested); journal "
                                        + "\(self.pendingUpdate?.outcome.rawValue ?? "none")")
            }
            if keymapWasInFlight && self.plannedWrites > 0 {
                self.profileMessage = "Link closed: \(self.verifiedWrites)/\(self.plannedWrites) writes verified. The last write may have reached RAM. Reconnect to read the layout."
            }
            self.invalidateSession(with: CustomKeyboardError.transportUnavailable)
            if rebootRequested {
                self.phase = .updateUnverified("Session closed after reboot request. Reboot, installer execution and installation remain unproven.")
            } else if flashWasInFlight || keymapWasInFlight {
                self.phase = .failed("Link closed: \(reason)")
            } else {
                self.phase = .idle
            }
        }
        identify()
    }

    func detach() {
        logger.debug("session detach requested at generation \(self.sessionGeneration, privacy: .public)")
        if keymapOperationRunning && plannedWrites > 0 {
            profileMessage = "Disconnected: \(verifiedWrites)/\(plannedWrites) writes verified. The last write may have reached RAM. Reconnect to read the layout."
        }
        invalidateSession(with: CustomKeyboardError.transportUnavailable)
        phase = .idle
        logger.debug("session detached at generation \(self.sessionGeneration, privacy: .public)")
    }

    /// Drop queued output and cancel both operation types before an old callback can continue.
    private func invalidateSession(with error: Error) {
        if keymapOperationRunning && plannedWrites > 0 {
            profileMessage = "Disconnected: \(verifiedWrites)/\(plannedWrites) writes verified. The last write may have reached RAM. Reconnect to read the layout."
        }
        sessionGeneration += 1
        operationTask?.cancel()
        operationTask = nil
        keymapTask?.cancel()
        keymapTask = nil
        transaction = nil
        dispatchPermit = nil
        replyFramer = KeyboardPacketFramer(maximumLength: 15)
        stockMode = nil
        lastVersionPayload = nil
        flashInFlight = false
        sessionNeedsReconnect = true
        info = nil
        keymap = []
        keymapIsVerified = false
        ramWritesAllowed = false
        keymapOperationRunning = false
        selfTestRunning = false
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
        let generation = sessionGeneration
        keymapOperationRunning = true
        keymapTask = Task {
            defer { if generation == sessionGeneration { keymapOperationRunning = false } }
            do {
                try Task.checkCancellation()
                guard generation == sessionGeneration else { return }
                let payload = try await perform(.version())
                try Task.checkCancellation()
                guard generation == sessionGeneration, !sessionNeedsReconnect else { return }
                lastVersionPayload = payload
                stockMode = StockFirmwareMode(payload: payload)
                let identified: Phase
                if Self.keymapBuildEnabled && hasQMKIdentity {
                    identified = .customFirmware("Custom QMK firmware detected")
                } else if payload == CustomKeyboardCommand.stockVersionPayload {
                    identified = .stockFirmware(Self.extensionUpdatesEnabled
                        ? "Stock V122 detected. The programmable extension installer is available."
                        : Self.stockFlashTrial
                        ? "Stock V122 detected. Trial build: installation is enabled below."
                        : "Stock V122 detected. Physical installation is disabled in this configuration.")
                } else if stockMode == .rescue {
                    identified = .stockFirmware("RS 00 01 application rescue detected. This is not independent boot recovery.")
                } else {
                    identified = .stockFirmware("Unrecognized firmware identity \(payload.map { String(format: "%02x", $0) }.joined()); flashing is refused")
                }
                logger.info("firmware mode identified: \(String(describing: identified), privacy: .public)")
                if let pending = pendingUpdate {
                    /* An unresolved update is never silently resolved by a
                     * reconnect (review 2026-10-12). A concluded trial shows
                     * its resolution instead. */
                    if pending.isResolved {
                        stockObservation = "Concluded trial: \(pending.resolution?.observation ?? ""). Installation remains unproven by construction."
                    } else if stockMode == .stock {
                        stockObservation = "Stock V122 responds after reconnect. This does not prove installer execution or application replacement. Conclude the trial after capturing the evidence bundle."
                    } else {
                        stockObservation = "Expected stock V122 is not observed. Installation remains unproven."
                    }
                    phase = .updateUnverified("Previous update retained: \(pending.outcome.rawValue). \(stockObservation)")
                } else {
                    phase = identified
                }
                if case .customFirmware = identified {
                    try await loadKeymap()
                }
            } catch {
                guard generation == sessionGeneration else { return }
                keymapIsVerified = false
                ramWritesAllowed = false
                profileMessage = "Custom firmware identity or keymap read failed. No mapping is verified. \(error)"
                if let pendingUpdate {
                    stockObservation = "A new identity or keymap read failed. Installation remains unproven."
                    phase = .updateUnverified("Previous update retained: \(pendingUpdate.outcome.rawValue). \(stockObservation)")
                } else {
                    fail("Identify failed; \(error)")
                }
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
        if pendingUpdate == nil {
            phase = .customFirmware("Custom firmware ready; \(parsed.layers) layers, \(parsed.rows)x\(parsed.columns)")
        }
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
        logger.debug("key edit requested at layer \(layer, privacy: .public), row \(row, privacy: .public), column \(column, privacy: .public), keycode \(String(format: "%04x", keycode), privacy: .public)")
        guard canEditKeymap, (0..<layers).contains(layer), (0..<rows).contains(row),
              (0..<columns).contains(column) else { return }
        startKeymapOperation(total: 1) {
            try await self.writeAndVerify(layer: layer, row: row, column: column, keycode: keycode)
            self.profileMessage = "Key verified in keyboard RAM. Save a phone profile to keep it."
        }
    }

    private func writeAndVerify(layer: Int, row: Int, column: Int, keycode: UInt16) async throws {
        do {
            _ = try await perform(.setKey(layer: UInt8(layer), row: UInt8(row), column: UInt8(column), keycode: keycode))
            let payload = try await perform(.getKey(layer: UInt8(layer), row: UInt8(row), column: UInt8(column)))
            guard payload == [UInt8(keycode >> 8), UInt8(keycode & 0xff)] else {
                throw CustomKeyboardError.readbackMismatch
            }
            keymap[layer][row][column] = keycode
            verifiedWrites += 1
            logger.debug("key write/readback verified at layer \(layer, privacy: .public), row \(row, privacy: .public), column \(column, privacy: .public)")
        } catch {
            logger.error("key write/readback failed at layer \(layer, privacy: .public), row \(row, privacy: .public), column \(column, privacy: .public)")
            throw error
        }
    }

    private func startKeymapOperation(total: Int, operation: @escaping @MainActor () async throws -> Void) {
        guard canEditKeymap else { return }
        logger.debug("keymap operation started with \(total, privacy: .public) planned writes")
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
                logger.error("keymap operation stopped after \(self.verifiedWrites, privacy: .public) of \(self.plannedWrites, privacy: .public) writes")
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
        logger.info("factory keymap restore requested")
        startKeymapOperation(total: 1) {
            _ = try await self.perform(.resetKeymap())
            for layer in 0..<self.layers {
                try await self.readKeymap(layer: UInt8(layer))
            }
            guard self.keymap == FactoryLayout.asRowMajor else { throw CustomKeyboardError.readbackMismatch }
            self.verifiedWrites = 1
            self.profileMessage = "Factory layout restored and all 144 keys verified. Phone profiles are kept."
            self.logger.info("factory keymap restore verified across 144 keys")
        }
    }

    func saveProfile(name: String) {
        logger.debug("profile save requested")
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
            logger.info("profile saved; total profiles \(self.profiles.count, privacy: .public), key count 144")
        } catch {
            logger.error("profile save failed")
            profileMessage = "Profile was not saved: \(error)"
        }
    }

    func selectProfile(_ id: UUID) {
        logger.debug("profile selection requested")
        guard !keymapOperationRunning, profileStorageAvailable,
              let profile = profiles.first(where: { $0.id == id }) else { return }
        do {
            var next = library
            next.desiredID = id
            try next.save(to: profilesURL)
            library = next
            desiredProfile = profile
            profileMessage = "Selected \(profile.name). Apply is always manual."
            logger.info("profile selected; total profiles \(self.profiles.count, privacy: .public)")
        } catch {
            logger.error("profile selection persistence failed")
            profileMessage = "Profile selection was not saved: \(error)"
        }
    }

    func applyDesiredProfile() {
        guard canEditKeymap, let profile = desiredProfile else { return }
        do { try profile.validate() } catch { profileMessage = "Invalid profile: \(error)"; return }
        let changes = (0..<4).flatMap { layer in
            (0..<6).flatMap { row in (0..<6).compactMap { column -> (Int, Int, Int)? in
                keymap[layer][row][column] == profile.keymap[layer][row][column] ? nil : (layer, row, column)
            } }
        }
        logger.info("profile apply requested with \(changes.count, privacy: .public) changed keys")
        startKeymapOperation(total: changes.count) {
            for (layer, row, column) in changes {
                try await self.writeAndVerify(layer: layer, row: row, column: column, keycode: profile.keymap[layer][row][column])
            }
            for layer in 0..<self.layers {
                try await self.readKeymap(layer: UInt8(layer))
            }
            guard self.keymap == profile.keymap else { throw CustomKeyboardError.readbackMismatch }
            self.profileMessage = "Applied \(profile.name): \(changes.count) writes and all 144 keys verified in RAM."
            self.logger.info("profile apply verified after full 144-key readback")
        }
    }

    /// One updater for both explicitly enabled, hash-pinned artifacts.
    /// The extension additionally needs current-session consent and fresh V122 identity.
    func flash(image rawImage: [UInt8], rebootToInstall: Bool,
               artifact: PreparedFirmwareImage.Artifact = .stock, authorizedSession: Int? = nil) {
        guard artifact == .stock ? Self.stockUpdatesEnabled : Self.extensionUpdatesEnabled else {
            fail(CustomKeyboardError.diagnosticsOnly.description)
            return
        }
        // A duplicate request must not fail or cancel the active exchange.
        guard !flashInFlight else { return }
        if artifact == .programmable {
            guard authorizedSession == sessionGeneration else { fail("Confirm installation for this connection first."); return }
            if let reason = extensionInstallBlockReason { fail(reason); return }
        }
        guard link != nil else {
            fail("No connection; cannot flash")
            return
        }
        guard !journalUnreadable else {
            fail("Stored transaction is unreadable; installation is blocked.")
            return
        }
        if let pending = pendingUpdate, !pending.isResolved {
            phase = .updateUnverified("A previous update (sha256 \(pending.imageSHA256.prefix(12))…) is unresolved. A version response cannot clear it; conclude the trial after capturing its evidence.")
            return
        }
        /* Synchronous single-operation guard, before any Task is created:
         * two rapid submissions cannot both pass (review 2026-10-12). */
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
        let prepared: PreparedFirmwareImage
        do {
            prepared = try PreparedFirmwareImage(rawImage, artifact: artifact)
        } catch {
            flashInFlight = false
            fail("Image refused: only the bundled \(artifact == .stock ? "stock V122" : "programmable extension") artifact is accepted. \(error)")
            return
        }
        let image = prepared.bytes
        let imageSHA256 = prepared.sha256
        let generation = sessionGeneration
        transaction = StockFirmwareTransaction(image: prepared, source: mode, target: target, generation: generation)
        logUpdateAudit("trialStart", detail: "image sha256 \(imageSHA256), \(image.count) bytes; "
            + "target \(target.model) firmware \(target.firmwareRevision) hardware \(target.hardwareRevision); "
            + "mode \(String(describing: mode)); rebootToInstall \(rebootToInstall)")
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
                if artifact == .programmable {
                    let fresh = try await perform(.version())
                    try requireTransaction(generation)
                    guard fresh == CustomKeyboardCommand.stockVersionPayload else {
                        throw CustomKeyboardError.invalidImage("Stock identity changed before staging. No update was started.")
                    }
                }
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
                logUpdateAudit("commitUnknownRecorded", detail: "A3 dispatch follows; any interruption from here leaves the record")
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
                logUpdateAudit("commitAcknowledged", command: 0xa3, reply: finishReply,
                               detail: "committed size \(committed) of \(image.count)")
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
                    /* Fail-closed audit (review 2026-10-21, P2-1): this event
                     * guards the direct A0 dispatch branch below, which does
                     * not pass through perform()'s dispatch audit. */
                    guard logUpdateAudit("rebootDispatched", command: 0xa0,
                                         request: CustomKeyboardCommand.rebootRequest) else {
                        throw CustomKeyboardError.auditUnavailable
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
                logUpdateAudit("updateFailed", detail: "staging stopped at \(offset) acknowledged bytes; "
                    + "rebootRequested \(rebootRequested); error \(error)")
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
        if transaction.image.artifact == .programmable, pendingUpdate == nil,
           let reason = Self.extensionPowerBlockReason { throw CustomKeyboardError.invalidImage(reason) }
    }

    /// Abort an in-flight STAGING transfer (A1/A2). Once A3 has been
    /// dispatched the commit may already have happened on the device, so
    /// aborting then is refused; the pending-update record governs that
    /// state instead. Cancelling stops new frames; an in-flight exchange
    /// ends through its normal timeout and the session is invalidated.
    /// Staging writes only touch the keyboard's scratch area
    /// `[0x8A00,0xF500)`, so an abandoned staging session is harmless.
    func requestStagingAbort() {
        guard Self.firmwareUpdatesEnabled, flashInFlight, pendingUpdate == nil else { return }
        logUpdateAudit("abortRequested", detail: "staging-only abort; no commit was requested")
        operationTask?.cancel()
    }

    /// True while an A1/A2 staging transfer can still be safely abandoned.
    var stagingAbortAvailable: Bool {
        Self.firmwareUpdatesEnabled && flashInFlight && pendingUpdate == nil
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
        if request.count > 1, (0xa0...0xa3).contains(request[1]) {
            // Fail-closed audit (review 2026-10-20): no update frame may
            // leave this device unless its audit entry persisted first. If
            // the subsequent dispatch refuses, a corrective event is logged.
            guard logUpdateAudit("dispatch", command: Int(request[1]), request: request) else {
                throw CustomKeyboardError.auditUnavailable
            }
        }
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
            do {
                try dispatch(request)
                if request.count > 1, (0xa0...0xa3).contains(request[1]) {
                    logUpdateAudit("dispatchAccepted", command: Int(request[1]),
                                   detail: "frame sent to the transport")
                }
            } catch {
                if request.count > 1, (0xa0...0xa3).contains(request[1]) {
                    logUpdateAudit("dispatchFailed", command: Int(request[1]),
                                   detail: "frame was NOT sent: \(error)")
                }
                resumePending(with: error)
            }
        }
        if request.count > 1, (0xa0...0xa3).contains(request[1]) {
            // Evidence completeness (review 2026-10-20): the device's reply is
            // logged even when the session is being torn down (A0 closes the
            // session by design). The audit records the observation; the
            // generation guard below still discards stale results.
            guard logUpdateAudit("reply", command: Int(request[1]), reply: payload) else {
                throw CustomKeyboardError.auditUnavailable
            }
        }
        guard generation == sessionGeneration else { throw CustomKeyboardError.transportUnavailable }
        try Task.checkCancellation()
        return payload
    }

    /// A lost reply must fail the exchange cleanly instead of hanging the
    /// UI in .busy forever (review 2026-10-11, F-7).
    private func exchangeTimedOut() {
        guard continuation != nil else { return }
        logUpdateAudit("exchangeTimedOut",
                       detail: "reply never arrived; the exchange is abandoned and the session invalidated")
        if keymapOperationRunning && plannedWrites > 0 {
            profileMessage = "Timed out after \(verifiedWrites)/\(plannedWrites) writes verified. The last write may have reached RAM. Reconnect to read the layout."
        }
        invalidateSession(with: CustomKeyboardError.timedOut)
        fail(pendingUpdate == nil
             ? CustomKeyboardError.timedOut.description
             : "Update outcome is UNKNOWN after timeout. Installation remains unproven; the transaction record is retained.")
    }

    private func receive(_ fragment: [UInt8]) {
        do {
            guard let packet = try replyFramer.append(fragment, awaitingReply: exchange != nil) else { return }
            guard var current = exchange, let pendingContinuation = continuation else {
                throw CustomKeyboardError.invalidStreamFrame
            }
            if let payload = try current.accumulator.append(packet) {
                timeoutTask?.cancel()
                timeoutTask = nil
                exchange = nil
                continuation = nil
                pendingContinuation.resume(returning: payload)
            } else {
                exchange = current
            }
        } catch {
            /* Byte-persist the rejected fragment (review 2026-10-21, P3-4):
             * a malformed reply is evidence too, not only an error message. */
            logUpdateAudit("replyRejected", reply: fragment,
                           detail: "fragment rejected by the accumulator: \(error)")
            if keymapOperationRunning && plannedWrites > 0 {
                profileMessage = "Invalid response after \(verifiedWrites)/\(plannedWrites) writes verified. The last write may have reached RAM. Reconnect to read the layout."
            }
            invalidateSession(with: error)
            fail("Invalid response; \(error)")
        }
    }
}
