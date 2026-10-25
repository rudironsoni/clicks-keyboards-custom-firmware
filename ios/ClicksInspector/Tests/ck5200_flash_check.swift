/*
 * Host check for the flash path of the REAL CustomFirmwareController.
 *
 * A scripted device implementing the decoded stock A1/A2/A3/A0 handler
 * semantics receives the bundled stock image through the real controller
 * and asserts the actual client behavior: every outgoing packet
 * byte-for-byte, staging equality, the authorization gate, target-identity
 * checks, reply-payload validation (advertised maximum, committed size),
 * exact-offset acknowledgement, timeout and retry semantics, the A3
 * commit-unknown state, session death mid-flash, A0 reboot handling, the
 * unresolved-update state across relaunch, and reentrancy (reviews
 * 2026-10-11 and 2026-10-12).
 *
 * Compile and run:
 *   swiftc Sources/KeycodeNames.swift Sources/FactoryLayout.swift \
 *     Sources/KeyboardSelfTest.swift Sources/CustomKeyboardProtocol.swift \
 *     Sources/CustomFirmwareController.swift Tests/ck5200_flash_check.swift \
 *     -o /tmp/ck5200_flash_check
 *   /tmp/ck5200_flash_check <stock.bin> <custom.bin>
 */
import Foundation

#if STOCK_FLASH_TRIAL
/// Probe for the M4 trial audit trail (StockUpdateAudit.jsonl). The trial
/// build must persist exact TX/RX frames and lifecycle events.
private struct AuditProbe: Codable {
    let event: String
    let command: Int?
    let requestHex: String?
    let replyHex: String?
}
#endif

private var checkFailures = 0

private func expect(_ condition: Bool, _ what: String) {
    if condition {
        print("PASS \(what)")
    } else {
        checkFailures += 1
        print("FAIL \(what)")
    }
}

/* ---- scripted device + link ------------------------------------------ */

final class ScriptedDeviceLink: KeyboardPacketLink {
    var onFragment: (([UInt8]) -> Void)?
    var onClosed: ((String) -> Void)?

    let image: [UInt8]              /* expected image, for staging equality */
    private(set) var closed = false
    private var staged: [UInt8] = []
    private var stagedOffset: UInt32 = 0
    private var a1Size: UInt32?

    /* Test controls. */
    var dropNextReplies = 0
    var muteA2 = false
    var closeAfterA2Count: Int?
    var closeOnA0 = true
    var advertisedMaximum: UInt32 = 0x6A00
    var a3CommittedOverride: UInt32?
    var replyStuckOffsetOnce = false
    var testOnlyIntegrityCheck = true
    var corruptOneStagedByte = false
    var versionPayload: [UInt8] = [0x01, 0x20, 0x01, 0x22]
    var dropA3ReplyOnce = false
    private var a3Dropped = false
    /* Deterministic mid-staging stall (review 2026-10-20): after a2Count
     * exceeds holdA2After, the reply is withheld until releaseA2Hold() so a
     * test can act while the controller is provably mid-transaction. */
    var holdA2After: Int?
    private var heldA2Reply: [UInt8]?
    func releaseA2Hold() {
        if let held = heldA2Reply {
            heldA2Reply = nil
            reply(held)
        }
    }
    /* P0 disconnection-during-A3 (review 2026-10-13): the keyboard commits
     * and the session dies before the acknowledgment is delivered. */
    var dieOnA3Commit = false
    /* Malformed A3 acknowledgment: a frame the accumulator must reject. */
    var malformedA3Reply = false

    /* Observed by the checks. */
    private(set) var a1Count = 0
    private(set) var a2Count = 0
    private(set) var a3Count = 0
    private(set) var a0Seen = false
    private(set) var stagingVerified = false
    private(set) var corruptedStagingAccepted = false
    private(set) var protocolErrors: [String] = []

    init(image: [UInt8]) { self.image = image }

    func send(_ bytes: [UInt8]) {
        guard !closed else { return }
        guard bytes.count >= 2, bytes[0] == bytes.count else {
            protocolErrors.append("bad frame length byte")
            return
        }
        switch bytes[1] {
        case 0x03: /* version: the device's reported identity */
            reply([0x08, 0x02, 0x03, 0x00] + versionPayload)

        case 0xa1:
            a1Count += 1
            guard bytes.count == 6 else { protocolErrors.append("A1 length"); return }
            let size = be32(bytes, 2)
            a1Size = size
            staged = []
            stagedOffset = 0
            let ok = size >= 1 && size <= 0x6A00
            reply([0x08, 0x02, 0xa1, ok ? 0x00 : 0x01] + be32bytes(advertisedMaximum))

        case 0xa2:
            a2Count += 1
            if muteA2 { return } /* a silent device ignores everything, including retries */
            guard bytes.count >= 7, bytes.count <= 58 + 6 else { protocolErrors.append("A2 length"); return }
            let offset = be32(bytes, 2)
            if offset != stagedOffset {
                /* Stock rejects a non-sequential offset and answers with the
                 * offset it expects (stock 0x3810..0x3812). */
                protocolErrors.append("A2 offset jump: \(offset) != \(stagedOffset)")
                reply([0x08, 0x02, 0xa2, 0x01] + be32bytes(stagedOffset))
                return
            }
            let replyOffset: UInt32
            if replyStuckOffsetOnce && a2Count > 1 {
                /* A misbehaving device acknowledges without progress once. */
                replyOffset = stagedOffset
            } else {
                var chunk = Array(bytes[6...])
                if corruptOneStagedByte && a2Count == 1 {
                    /* Corruption in transit: the device stages corrupted
                     * bytes and cannot know (review 2026-10-12). */
                    chunk[chunk.count / 2] ^= 0xFF
                }
                staged.append(contentsOf: chunk)
                stagedOffset += UInt32(chunk.count)
                replyOffset = stagedOffset
            }
            if let die = closeAfterA2Count, a2Count >= die {
                dieMidFlash()
                return
            }
            if let hold = holdA2After, a2Count > hold {
                heldA2Reply = [0x08, 0x02, 0xa2, 0x00] + be32bytes(replyOffset)
                return
            }
            reply([0x08, 0x02, 0xa2, 0x00] + be32bytes(replyOffset))

        case 0xa3:
            a3Count += 1
            if dropA3ReplyOnce && !a3Dropped {
                /* A committed update whose reply was lost (review
                 * 2026-10-12): indistinguishable from a failed commit. */
                a3Dropped = true
                return
            }
            guard bytes.count == 6 else { protocolErrors.append("A3 length"); return }
            let size = be32(bytes, 2)
            /* The real stock handler checks update state and length only; it
             * does NOT compare the staged bytes against the source image.
             * The equality check below is a TEST-ONLY assertion of the
             * client's output, disabled for the corrupted-payload scenario
             * (review 2026-10-12). */
            var ok = size == stagedOffset && size == a1Size
            if ok && testOnlyIntegrityCheck {
                stagingVerified = staged == image
                ok = stagingVerified
            }
            if ok && corruptOneStagedByte {
                /* Honest negative: the staged bytes differ from the source
                 * image, and the real protocol accepts anyway. */
                corruptedStagingAccepted = staged != image
            }
            let committed = a3CommittedOverride ?? stagedOffset
            if dieOnA3Commit {
                /* The commit happened; the session dies before the
                 * acknowledgment reaches the host (review 2026-10-13). */
                closed = true
                onClosed?("Accessory disconnected after committing")
                return
            }
            if malformedA3Reply {
                /* A malformed acknowledgment: the accumulator must throw. */
                reply([0x08, 0x02, 0xa3, 0x00, 0xDE, 0xAD, 0xBE, 0xEF, 0xFF])
                return
            }
            reply([0x08, 0x02, 0xa3, ok ? 0x00 : 0x01] + be32bytes(committed))

        case 0xa0:
            guard bytes.count == 2 else { protocolErrors.append("A0 length"); return }
            a0Seen = true
            reply([0x04, 0x02, 0xa0, 0x00])
            if closeOnA0 {
                closed = true
                /* The reboot kills the session; synchronous, like a real
                 * disconnect observed on the main runloop. */
                onClosed?("Accessory rebooted to install")
            }

        default:
            reply([0x08, 0x02, bytes[1], 0x01, 0, 0, 0, 0]) /* unsupported */
        }
    }

    func close() { closed = true } /* host-side close; the device stays silent */

    private func dieMidFlash() {
        closed = true
        onClosed?("Session died mid-transfer")
    }

    private func reply(_ packet: [UInt8]) {
        if dropNextReplies > 0 {
            dropNextReplies -= 1
            return
        }
        /* Delivered synchronously inside the controller's continuation
         * body: legal and deterministic on the host (the real app delivers
         * from the main runloop). */
        onFragment?(packet)
    }

    private func be32(_ bytes: [UInt8], _ at: Int) -> UInt32 {
        UInt32(bytes[at]) << 24 | UInt32(bytes[at + 1]) << 16
            | UInt32(bytes[at + 2]) << 8 | UInt32(bytes[at + 3])
    }
    private func be32bytes(_ v: UInt32) -> [UInt8] {
        [UInt8(v >> 24), UInt8(v >> 16), UInt8(v >> 8), UInt8(v & 0xff)]
    }
}

/* ---- helpers ----------------------------------------------------------- */

@MainActor
private func waitUntil(_ what: String, timeout seconds: Double = 10,
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

@MainActor
private func makeController(link: ScriptedDeviceLink,
                            target: CustomFirmwareController.ConnectedTarget? =
                                CustomFirmwareController.ConnectedTarget(
                                    model: "CK-5200", firmwareRevision: "1.2.2",
                                    hardwareRevision: "1.2.0")) -> CustomFirmwareController {
    CustomFirmwareController.resetPendingUpdateForTesting()
    let controller = CustomFirmwareController()
    controller.exchangeTimeoutNanos = 200_000_000    /* 200 ms for the host run */
    controller.rebootConfirmTimeoutNanos = 300_000_000
    controller.connectedTarget = target
    controller.attach(link)
    return controller
}

private func loadFile(_ path: String) -> [UInt8] {
    guard let data = FileManager.default.contents(atPath: path), !data.isEmpty else {
        FileHandle.standardError.write("missing or empty image: \(path)\n".data(using: .utf8)!)
        exit(2)
    }
    return Array(data)
}

/* ---- main -------------------------------------------------------------- */

@main
struct FlashCheck {
    @MainActor
    static func main() async {
        let args = CommandLine.arguments
        guard args.count == 3 else {
            FileHandle.standardError.write("usage: ck5200_flash_check <stock.bin> <custom.bin>\n".data(using: .utf8)!)
            exit(2)
        }
        #if EXTENSION_INSTALLER
        await extensionInstallerCheck(raw: loadFile(args[1]), stock: loadFile(args[2]))
        exit(checkFailures == 0 ? 0 : 1)
        #else
        let stockImage = loadFile(args[1])
        let customImage = loadFile(args[2])
        defer { CustomFirmwareController.resetPendingUpdateForTesting() }

#if STOCK_FLASH_TRIAL
        // Fresh-log discipline (review 2026-10-20): the trial binary starts
        // with an EMPTY audit log so the COMPLETE first transaction can be
        // asserted exactly, including the defect regression that the first
        // event survives the initial write.
        if let storage = ProcessInfo.processInfo.environment["STOCK_MANAGER_TEST_STORAGE"] {
            let auditURL = URL(fileURLWithPath: storage).deletingLastPathComponent()
                .appendingPathComponent("StockUpdateAudit.jsonl")
            try? FileManager.default.removeItem(at: auditURL)
        }
#endif

        /* S1: structural validation still accepts both bundled images and
         * rejects garbage; the GATE is separate and checked below. */
        do { try CustomKeyboardImageValidation.validate(image: stockImage); expect(true, "bundled stock image passes structural validation") }
        catch { expect(false, "bundled stock image passes structural validation: \(error)") }
        expect((try? CustomKeyboardImageValidation.validate(image: [0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x20, 0x00])) == nil,
               "non-JAL first word is rejected structurally")

        /* S2: the authorization gate. Only the exact authorized stock V122
         * artifact may be staged; every other image is refused before any
         * packet is sent (review 2026-10-12). */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (gate run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: customImage, rebootToInstall: true)
            expect(link.a1Count == 0, "the unauthorized custom image was refused before A1")
            if case .failed(let message) = controller.phase {
                expect(message.contains("only the bundled stock V122 artifact"),
                       "refusal names the gate (got: \(message))")
            } else {
                expect(false, "unauthorized image must fail the phase")
            }

            controller.flash(image: [0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x20, 0x00],
                             rebootToInstall: true)
            expect(link.a1Count == 0, "a structurally valid but unauthorized image is also refused")
        }

        /* S3: the target-identity checks (review 2026-10-12). */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            let controller = makeController(link: link, target: nil)
            await waitUntil("identity fails before sending (no-target run)", timeout: 5) {
                if case .failed = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            expect(link.a1Count == 0, "missing target identity refuses to flash")
        }
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            let wrongTarget = CustomFirmwareController.ConnectedTarget(
                model: "CK-9999", firmwareRevision: "1.2.2", hardwareRevision: "1.2.0")
            let controller = makeController(link: link, target: wrongTarget)
            await waitUntil("identity fails before sending (wrong-model run)", timeout: 5) {
                if case .failed = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            expect(link.a1Count == 0, "wrong model refuses to flash")
        }
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            let wrongHw = CustomFirmwareController.ConnectedTarget(
                model: "CK-5200", firmwareRevision: "1.2.2", hardwareRevision: "9.9.9")
            let controller = makeController(link: link, target: wrongHw)
            await waitUntil("identity fails before sending (wrong-hardware run)", timeout: 5) {
                if case .failed = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            expect(link.a1Count == 0, "wrong hardware revision refuses to flash")
        }
        do {
            /* The device reports an unrecognized version payload: flashing
             * must be refused even with the right target strings. */
            let link = ScriptedDeviceLink(image: stockImage)
            link.versionPayload = [0x01, 0x20, 0x01, 0x23]
            let controller = makeController(link: link)
            await waitUntil("identify reports the unrecognized firmware", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            expect(link.a1Count == 0, "flashing refused while the version payload is unrecognized")
        }

        /* S4: the full authorized stock re-flash: every packet byte-checked,
         * A1 maximum honored, staged bytes equal the source, A0 reboots,
         * and the controller ends in the unverified state, cleared only by
         * explicit reconciliation (review 2026-10-12). */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (full run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("stock re-flash ends in the unverified state", timeout: 20) {
                if case .updateUnverified = controller.phase { return link.a0Seen }
                return false
            }
            expect(link.protocolErrors.isEmpty, "no protocol violations (got \(link.protocolErrors))")
            expect(link.stagingVerified, "staged bytes equal the stock image byte-for-byte at A3 (test-only client-output assertion)")
            expect(link.a0Seen, "A0 reboot command was delivered")
            expect(controller.pendingUpdate != nil, "the unresolved-update record exists until reconciled")

            /* Refuse a second flash while unresolved: attach a fresh session
             * (the old one died with the reboot) and try again. */
            let relink = ScriptedDeviceLink(image: stockImage)
            controller.connectedTarget = CustomFirmwareController.ConnectedTarget(
                model: "CK-5200", firmwareRevision: "1.2.2", hardwareRevision: "1.2.0")
            controller.attach(relink)
            await waitUntil("the unresolved update persists across reconnect", timeout: 5) {
                if case .updateUnverified = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            if case .updateUnverified = controller.phase { expect(true, "a second flash is refused while the update is unresolved") }
            else { expect(false, "a second flash is refused while the update is unresolved") }

            /* Manual acknowledgement cannot erase installation uncertainty. */
            controller.markPendingUpdateVerified()
            expect(controller.pendingUpdate != nil, "manual verification cannot clear the record")

            /* Review 2026-10-20: operational closure. Concluding records the
             * observation, keeps the original outcome, never claims
             * installation, and unblocks the next transaction. */
            controller.concludePendingUpdate(observation: "host-check observation")
            expect(controller.pendingUpdate?.isResolved == true,
                   "concluding records a resolution without deleting the record")
            expect(controller.pendingUpdate?.outcome == .sentRebootPending,
                   "concluding preserves the original outcome")
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("a concluded trial no longer blocks a new transaction", timeout: 10) {
                if case .updateUnverified = controller.phase { return relink.a0Seen }
                return false
            }
            expect(relink.a0Seen, "the new transaction runs to its own A0")
            expect(controller.pendingUpdate?.isResolved != true,
                   "the new transaction creates its own fresh unresolved record")
        }

        /* S5: reentrancy: two rapid submissions must start exactly one
         * transaction (review 2026-10-12). */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (double-submit run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            controller.flash(image: stockImage, rebootToInstall: true)
            expect(link.a1Count <= 1, "double submission starts at most one A1 (got \(link.a1Count))")
            controller.detach()
        }

        /* S6: A1's advertised maximum is enforced (review 2026-10-12). */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            link.advertisedMaximum = 0x800
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (max run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("too-large image fails at A1", timeout: 5) {
                if case .failed = controller.phase { return true }
                return false
            }
            expect(link.a2Count == 0, "no A2 was sent after the advertised maximum refused the image")
            if case .failed(let message) = controller.phase {
                expect(message.contains("advertised maximum"), "failure names the advertised maximum (got: \(message))")
            }
        }

        /* S7: A2 acknowledgements must advance exactly by the sent chunk:
         * zero progress aborts the transaction (review 2026-10-12). */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            link.replyStuckOffsetOnce = true
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (stuck-ack run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("a no-progress acknowledgement aborts the update", timeout: 5) {
                if case .failed = controller.phase { return true }
                return false
            }
            expect(link.a2Count == 2, "the transaction aborts right after the stuck acknowledgement (got \(link.a2Count))")
        }

        /* S8: A3's committed size must equal the image size (review
         * 2026-10-12). */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            link.a3CommittedOverride = 0x1234
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (committed-size run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("a mismatched committed size fails the update", timeout: 5) {
                if case .failed = controller.phase { return true }
                return false
            }
            if case .failed(let message) = controller.phase {
                expect(message.contains("committed size"), "failure names the committed size (got: \(message))")
            }
        }

        /* S9: a lost A3 reply is NOT retried: the outcome is reported as
         * unknown and recorded until reconciled (review 2026-10-12). */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            link.closeOnA0 = false
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (commit-unknown run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            link.dropA3ReplyOnce = true /* the A3 reply is lost */
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("a lost A3 reply surfaces commit-unknown", timeout: 10) {
                if case .failed = controller.phase { return true }
                return false
            }
            expect(link.a3Count == 1, "the A3 was not blindly retried (got \(link.a3Count))")
            if case .failed(let message) = controller.phase {
                expect(message.contains("UNKNOWN"), "the failure states the ambiguous outcome (got: \(message))")
            } else {
                expect(false, "commit-unknown must fail the phase")
            }
            expect(controller.pendingUpdate?.outcome == .commitUnknown,
                   "the commit-unknown outcome is recorded")

            /* Relaunch: a fresh controller must NOT silently treat the
             * unresolved update as idle or verified (review 2026-10-12). */
            let link2 = ScriptedDeviceLink(image: stockImage)
            let relaunched = CustomFirmwareController()
            relaunched.exchangeTimeoutNanos = 200_000_000
            relaunched.rebootConfirmTimeoutNanos = 300_000_000
            relaunched.connectedTarget = CustomFirmwareController.ConnectedTarget(
                model: "CK-5200", firmwareRevision: "1.2.2", hardwareRevision: "1.2.0")
            relaunched.attach(link2)
            await waitUntil("a relaunched controller surfaces the unresolved update", timeout: 5) {
                if case .updateUnverified = relaunched.phase { return true }
                return false
            }
            expect(relaunched.pendingUpdate?.outcome == .commitUnknown,
                   "the record survives relaunch")
            relaunched.markPendingUpdateVerified()
            expect(relaunched.pendingUpdate != nil, "manual verification preserves the relaunch record")
        }

        /* S10: no in-session retry after a lost A1 reply. The protocol has no
         * transaction IDs; reconnect before another explicit attempt. */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (retry run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            link.dropNextReplies = 1 /* armed after identify: hits the A1 reply */
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("dropped A1 reply stops the transaction", timeout: 5) {
                if case .failed = controller.phase { return true }
                return false
            }
            expect(link.a1Count == 1, "the dropped A1 reply is not retried")
            expect(link.a2Count == 0 && !link.a0Seen, "no later write or reboot follows the timeout")
            controller.markPendingUpdateVerified()
        }

        /* S11: a session that dies mid-A2 must fail cleanly, not hang. */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            link.closeAfterA2Count = 3
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (death run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("mid-flash session death fails the update", timeout: 10) {
                if case .failed = controller.phase { return true }
                return false
            }
            if case .failed(let message) = controller.phase {
                expect(message.contains("Link closed"), "failure names the closed link")
            }
        }

        /* S12: a device that never answers fails via the timeout. */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            link.muteA2 = true
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (mute run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("silent device fails via timeout", timeout: 10) {
                if case .failed = controller.phase { return true }
                return false
            }
            if case .failed(let message) = controller.phase {
                expect(message.contains("did not answer"), "failure names the timeout (got: \(message))")
            }
        }

        /* S13: honest negative (review 2026-10-12): the stock protocol
         * checks update state and length only; it does NOT verify staged
         * content. The client must not rely on the device catching a
         * corrupted transfer. */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            link.testOnlyIntegrityCheck = false
            link.corruptOneStagedByte = true
            link.closeOnA0 = true
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (corruption run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("a corrupted transfer is NOT detected by the protocol", timeout: 20) {
                if case .updateUnverified = controller.phase { return link.a0Seen }
                return false
            }
            expect(link.corruptedStagingAccepted,
                   "A3 accepted despite corrupted staged bytes: the residual risk is real and documented")
            controller.markPendingUpdateVerified()
        }

        /* S14: A0 answered but the keyboard never reboots: staged, not
         * installed, and the state stays unverified either way. */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            link.closeOnA0 = false
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (A0 run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("missing reboot stays unverified, never success", timeout: 10) {
                if case .updateUnverified = controller.phase { return true }
                return false
            }
            if case .updateUnverified(let message) = controller.phase {
                expect(message.contains("unproven"), "the state does not assert installation")
            }
            controller.markPendingUpdateVerified()
        }

        /* S15 (P0, review 2026-10-13): the keyboard commits A3 and the
         * session disconnects before the acknowledgment reaches the host.
         * The uncertain outcome must be recorded even though the exchange
         * fails with a transport error, not a timeout; it must survive
         * relaunch and refuse further updates until reconciled. */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            link.dieOnA3Commit = true
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (disconnect-at-A3 run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("a mid-A3 disconnection fails the update", timeout: 10) {
                if case .failed = controller.phase { return true }
                return false
            }
            expect(link.a3Count == 1, "A3 was sent exactly once, no blind retry (got \(link.a3Count))")
            expect(controller.pendingUpdate != nil,
                   "the uncertain A3 outcome is recorded after a disconnection")
            expect(controller.pendingUpdate?.outcome == .commitUnknown,
                   "the record says commit-unknown, not sent (got \(String(describing: controller.pendingUpdate?.outcome)))")
            /* Review 2026-10-20: once the commit-unknown record exists, the
             * staging abort must be refused; the commit may already have
             * happened and the record governs that state. */
            expect(!controller.stagingAbortAvailable,
                   "abort is refused after the A3 boundary (record exists)")
            controller.requestStagingAbort()
            expect(controller.pendingUpdate?.outcome == .commitUnknown,
                   "a refused abort does not touch the commit-unknown record")

            /* Relaunch: a fresh controller must surface the unresolved
             * transaction, and a second flash must be refused. */
            let link2 = ScriptedDeviceLink(image: stockImage)
            let relaunched = CustomFirmwareController()
            relaunched.exchangeTimeoutNanos = 200_000_000
            relaunched.rebootConfirmTimeoutNanos = 300_000_000
            relaunched.connectedTarget = CustomFirmwareController.ConnectedTarget(
                model: "CK-5200", firmwareRevision: "1.2.2", hardwareRevision: "1.2.0")
            relaunched.attach(link2)
            await waitUntil("a relaunched controller surfaces the disconnected-A3 record", timeout: 5) {
                if case .updateUnverified = relaunched.phase { return true }
                return false
            }
            expect(relaunched.pendingUpdate?.outcome == .commitUnknown,
                   "the record survives the relaunch")
            relaunched.flash(image: stockImage, rebootToInstall: true)
            if case .updateUnverified = relaunched.phase {
                expect(true, "a second update is refused until the disconnected-A3 record is reconciled")
            } else {
                expect(false, "a second update is refused until the disconnected-A3 record is reconciled")
            }
            relaunched.markPendingUpdateVerified()
            expect(relaunched.pendingUpdate != nil, "manual verification preserves the disconnected-A3 record")
        }

        /* S16 (P0, review 2026-10-13): the A3 acknowledgment is malformed.
         * The accumulator rejects it; the uncertain record must exist
         * anyway, survive relaunch, and block further updates. */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            link.malformedA3Reply = true
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (malformed-A3 run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("a malformed A3 acknowledgment fails the update", timeout: 10) {
                if case .failed = controller.phase { return true }
                return false
            }
            expect(link.a3Count == 1, "the malformed A3 was not blindly retried (got \(link.a3Count))")
            expect(controller.pendingUpdate?.outcome == .commitUnknown,
                   "the uncertain record exists after a malformed acknowledgment")

            let link2 = ScriptedDeviceLink(image: stockImage)
            let relaunched = CustomFirmwareController()
            relaunched.exchangeTimeoutNanos = 200_000_000
            relaunched.rebootConfirmTimeoutNanos = 300_000_000
            relaunched.connectedTarget = CustomFirmwareController.ConnectedTarget(
                model: "CK-5200", firmwareRevision: "1.2.2", hardwareRevision: "1.2.0")
            relaunched.attach(link2)
            await waitUntil("a relaunched controller surfaces the malformed-A3 record", timeout: 5) {
                if case .updateUnverified = relaunched.phase { return true }
                return false
            }
            relaunched.markPendingUpdateVerified()
        }

#if STOCK_FLASH_TRIAL
        /* S17: staging-only abort (review 2026-10-20). While the controller
         * is provably stalled mid-A2, aborting must stop the transfer
         * immediately (no further A2, no A3) and leave no journal record. */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            link.holdA2After = 2
            let controller = makeController(link: link)
            controller.exchangeTimeoutNanos = 2_000_000_000
            await waitUntil("identify reports stock firmware (abort run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("staging stalls mid-transfer", timeout: 10) { link.a2Count >= 3 }
            expect(controller.stagingAbortAvailable,
                   "abort is offered while staging is stalled mid-transaction")
            controller.requestStagingAbort()
            /* Releasing the held reply lets the cancelled task resume and
             * unwind at its next checkpoint, proving the abort, not the
             * timeout, ended the transfer. */
            link.releaseA2Hold()
            await waitUntil("an aborted staging transfer fails without a commit", timeout: 10) {
                if case .failed = controller.phase { return true }
                return false
            }
            expect(link.a3Count == 0, "abort sends no A3 commit (got \(link.a3Count))")
            expect(link.a2Count == 3,
                   "no further A2 frames are dispatched after the abort (got \(link.a2Count))")
            expect(controller.pendingUpdate == nil, "abort leaves no unresolved record")
            expect(!controller.stagingAbortAvailable, "abort is withdrawn once the session has ended")
        }

        /* S18: audit fail-closed (review 2026-10-20). If the audit log cannot
         * be persisted, no update frame may be dispatched. The log written so
         * far is preserved across the check. */
        do {
            if let storage = ProcessInfo.processInfo.environment["STOCK_MANAGER_TEST_STORAGE"] {
                let auditURL = URL(fileURLWithPath: storage).deletingLastPathComponent()
                    .appendingPathComponent("StockUpdateAudit.jsonl")
                let backupURL = auditURL.appendingPathExtension("s17-backup")
                try? FileManager.default.removeItem(at: backupURL)
                var hadLog = false
                if FileManager.default.fileExists(atPath: auditURL.path) {
                    try? FileManager.default.moveItem(at: auditURL, to: backupURL)
                    hadLog = true
                }
                defer {
                    try? FileManager.default.removeItem(at: auditURL)
                    if hadLog { try? FileManager.default.moveItem(at: backupURL, to: auditURL) }
                }
                try? FileManager.default.createDirectory(at: auditURL, withIntermediateDirectories: true)
                let link = ScriptedDeviceLink(image: stockImage)
                let controller = makeController(link: link)
                await waitUntil("identify reports stock firmware (audit-fail run)", timeout: 5) {
                    if case .stockFirmware = controller.phase { return true }
                    return false
                }
                controller.flash(image: stockImage, rebootToInstall: true)
                await waitUntil("an unwritable audit log fails the trial", timeout: 10) {
                    if case .failed = controller.phase { return true }
                    return false
                }
                expect(link.a1Count == 0,
                       "no update frame is dispatched without a persisted audit entry (A1 count \(link.a1Count))")
                expect(controller.pendingUpdate == nil, "an audit failure creates no commit record")
            }
        }

        /* M4 trial audit trail, strengthened (review 2026-10-20): a fresh
         * log must retain the COMPLETE first transaction byte-for-byte and
         * in order, including the defect regression that the very first
         * event survives the initial write. */
        if let storage = ProcessInfo.processInfo.environment["STOCK_MANAGER_TEST_STORAGE"] {
            let auditURL = URL(fileURLWithPath: storage).deletingLastPathComponent()
                .appendingPathComponent("StockUpdateAudit.jsonl")
            if let text = try? String(contentsOf: auditURL, encoding: .utf8) {
                let events = text.split(separator: "\n").filter { !$0.isEmpty }
                    .compactMap { try? JSONDecoder().decode(AuditProbe.self, from: Data($0.utf8)) }
                expect(!events.isEmpty, "trial audit log records update events")
                expect(events.first?.event == "trialStart",
                       "defect regression: the first event of a fresh log is retained")
                var expected: [(String, Int?)] = [("trialStart", nil),
                                                 ("dispatch", 0xa1), ("dispatchAccepted", 0xa1), ("reply", 0xa1)]
                for _ in 0..<583 {
                    expected.append(("dispatch", 0xa2))
                    expected.append(("dispatchAccepted", 0xa2))
                    expected.append(("reply", 0xa2))
                }
                expected += [("commitUnknownRecorded", nil),
                             ("dispatch", 0xa3), ("dispatchAccepted", 0xa3), ("reply", 0xa3),
                             ("commitAcknowledged", 0xa3), ("rebootDispatched", 0xa0),
                             ("dispatch", 0xa0), ("sessionClosed", nil),
                             ("dispatchAccepted", 0xa0), ("reply", 0xa0)]
                expect(events.count >= expected.count,
                       "the first transaction is fully logged (\(events.count) events, expected at least \(expected.count))")
                var prefixOK = events.count >= expected.count
                if prefixOK {
                    for (index, want) in expected.enumerated() {
                        if events[index].event != want.0 || events[index].command != want.1 {
                            prefixOK = false
                            break
                        }
                    }
                }
                expect(prefixOK,
                       "the first transaction appears in exact order: trialStart, A1 pair, 583 ordered A2 pairs, commit point, A3 pair, commit ack, reboot, A0 pair")
                let a2Dispatches = events.prefix(expected.count).filter {
                    $0.event == "dispatch" && $0.command == 0xa2
                }
                expect(a2Dispatches.count == 583,
                       "exactly 583 A2 data frames are dispatched in the first transaction (got \(a2Dispatches.count))")
                let a2Hexes = a2Dispatches.compactMap { $0.requestHex }
                if a2Hexes.count == 583 {
                    var offsetOK = true
                    for (index, hex) in a2Hexes.enumerated() {
                        let sentOffset: UInt32? = UInt32(hex.dropFirst(4).prefix(8), radix: 16)
                        let wantOffset: UInt32 = UInt32(index) * 32
                        if sentOffset != wantOffset {
                            offsetOK = false
                            break
                        }
                    }
                    expect(offsetOK, "A2 offsets advance exactly 0,32,64,... in dispatch order")
                    let expectedLastFrameHexLength = (6 + 24) * 2
                    let actualLastFrameHexLength = a2Hexes.last?.count ?? 0
                    expect(
                        actualLastFrameHexLength == expectedLastFrameHexLength,
                        "final A2 frame: expected 60 hex chars, got \(actualLastFrameHexLength)"
                    )
                }
                expect(events.contains { $0.event == "dispatchAccepted" },
                       "accepted dispatches are marked as sent")
                expect(events.contains { $0.event == "updateFailed" },
                       "failed scenarios are logged")
                expect(events.contains { $0.event == "trialConcluded" },
                       "the concluded trial is logged")
                expect(events.contains { $0.event == "abortRequested" },
                       "the staging abort is logged")
                expect(events.contains { $0.event == "sessionClosed" },
                       "link closure during an update is logged with its cause (review 2026-10-21, P2-2)")
                expect(events.contains { $0.event == "replyRejected" },
                       "rejected reply fragments are persisted byte-for-byte (review 2026-10-21, P3-4)")
                if let a1reply = events.first(where: { $0.event == "reply" && $0.command == 0xa1
                    && !($0.replyHex ?? "").isEmpty }) {
                    expect(a1reply.replyHex == "00006a00",
                           "A1 reply recorded byte-for-byte (advertised maximum)")
                }
            } else {
                expect(false, "the trial audit log exists at \(auditURL.path)")
            }
        }
#endif

        print(checkFailures == 0 ? "ok" : "==== \(checkFailures) failure(s) ====")
        exit(checkFailures == 0 ? 0 : 1)
        #endif
    }
}

#if EXTENSION_INSTALLER
@MainActor
private func extensionInstallerCheck(raw: [UInt8], stock: [UInt8]) async {
    defer { CustomFirmwareController.resetPendingUpdateForTesting() }
    guard let image = try? PreparedFirmwareImage(raw, artifact: .programmable) else {
        expect(false, "pinned extension image accepted"); return
    }
    expect(image.bytes.count == 27_136 && image.sha256 == CustomKeyboardImageAuthorization.authorizedExtensionTransferSHA256,
           "exact full-page transfer size and hash")
    expect(Array(image.bytes.prefix(raw.count)) == raw && image.bytes.dropFirst(raw.count).allSatisfy { $0 == 0xff },
           "accepted executable unchanged; only 78 erased-state padding bytes added")
    expect(!CustomFirmwareController.stockUpdatesEnabled && CustomFirmwareController.extensionUpdatesEnabled,
           "installer build cannot authorize a stock or QMK update")
    do {
        let link = ScriptedDeviceLink(image: image.bytes)
        let c = makeController(link: link)
        await waitUntil("installer identifies stock", timeout: 5) { c.stockMode == .stock && !c.keymapOperationRunning }
        let consent = ExtensionInstallConfirmation(id: c.installationSessionID)
        expect(consent.blockReason(currentSession: c.installationSessionID, preflight: c.extensionInstallBlockReason) == nil,
               "confirmation carries the eligible session without a separate optional capture")
        expect(consent.blockReason(currentSession: c.installationSessionID + 1, preflight: nil)?.contains("connection changed") == true
               && consent.blockReason(currentSession: c.installationSessionID, preflight: "Power check failed") == "Power check failed",
               "stale consent and failed preflight keep an explicit confirmation reason")
        c.flash(image: raw, rebootToInstall: true, artifact: .programmable)
        expect(link.a1Count == 0, "no session consent sends no A1")
        c.flash(image: raw, rebootToInstall: true, artifact: .programmable, authorizedSession: c.installationSessionID - 1)
        expect(link.a1Count == 0, "stale-session consent sends no A1")
        var damaged = raw; damaged[100] ^= 1
        c.flash(image: damaged, rebootToInstall: true, artifact: .programmable, authorizedSession: c.installationSessionID)
        expect(link.a1Count == 0, "modified image sends no A1")
        c.flash(image: stock, rebootToInstall: true)
        expect(link.a1Count == 0, "stock reflash is not enabled by extension permission")
        link.versionPayload = [1, 0x20, 1, 0x23]
        c.flash(image: raw, rebootToInstall: true, artifact: .programmable, authorizedSession: c.installationSessionID)
        await waitUntil("fresh identity failure stops installer", timeout: 5) { !c.flashInFlight }
        expect(link.a1Count == 0, "identity is checked again before staging")
        c.detach()
    }
    // Main path: the existing updater emits every frame, including full-area
    // padding, commit, and reboot. Device byte equality is a TEST assertion.
    do {
        let link = ScriptedDeviceLink(image: image.bytes)
        link.holdA2After = 0
        let c = makeController(link: link)
        await waitUntil("main installer identifies stock", timeout: 5) { c.stockMode == .stock && !c.keymapOperationRunning }
        c.flash(image: raw, rebootToInstall: true, artifact: .programmable, authorizedSession: c.installationSessionID)
        await waitUntil("extension staging is held before commit", timeout: 5) { link.a2Count == 1 }
        c.flash(image: raw, rebootToInstall: true, artifact: .programmable, authorizedSession: c.installationSessionID)
        if case .busy = c.phase { expect(true, "duplicate install request leaves the active exchange intact") }
        else { expect(false, "duplicate install request interrupted staging") }
        link.holdA2After = nil
        link.releaseA2Hold()
        await waitUntil("extension reaches reboot request", timeout: 20) { link.a0Seen }
        expect(link.a1Count == 1 && link.a2Count == 848 && link.a3Count == 1 && link.stagingVerified && link.protocolErrors.isEmpty,
               "one ordered transfer: 848 exact chunks, full-byte comparison, one commit")
        expect(c.pendingUpdate?.imageSHA256 == image.sha256 && c.pendingUpdate?.imageSize == image.bytes.count,
               "journal pins the actual transfer, not just the source executable")
        let fresh = CustomFirmwareController()
        expect(fresh.pendingUpdate == c.pendingUpdate && fresh.extensionInstallBlockReason != nil,
               "relaunch retains installation uncertainty and blocks another flash")
    }
    // Key failure: the device receives A3 but its reply is lost. Never retry,
    // reboot, or erase the commit-unknown journal after a reconnect.
    do {
        let link = ScriptedDeviceLink(image: image.bytes)
        link.dropA3ReplyOnce = true
        let c = makeController(link: link)
        await waitUntil("lost-ack installer identifies stock", timeout: 5) { c.stockMode == .stock && !c.keymapOperationRunning }
        c.flash(image: raw, rebootToInstall: true, artifact: .programmable, authorizedSession: c.installationSessionID)
        await waitUntil("lost extension commit reply stops", timeout: 20) { link.a3Count == 1 && !c.flashInFlight }
        expect(c.pendingUpdate?.outcome == .commitUnknown && !link.a0Seen && link.a3Count == 1,
               "lost A3 reply preserves uncertainty, sends no reboot, and has no retry")
        expect(CustomFirmwareController().pendingUpdate == c.pendingUpdate,
               "extension commit-unknown record survives relaunch")
        c.flash(image: raw, rebootToInstall: true, artifact: .programmable, authorizedSession: c.installationSessionID)
        expect(link.a1Count == 1, "an uncertain installation cannot start another transaction")
    }
    print("Extension installer checks: \(checkFailures) failures. Software model only, not physical flash proof.")
}
#endif
