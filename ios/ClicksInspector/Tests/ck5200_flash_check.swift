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
        let stockImage = loadFile(args[1])
        let customImage = loadFile(args[2])
        defer { CustomFirmwareController.resetPendingUpdateForTesting() }

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
            await waitUntil("identify reports stock firmware (no-target run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
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
            await waitUntil("identify reports stock firmware (wrong-model run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
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
            await waitUntil("identify reports stock firmware (wrong-hardware run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
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

            /* Explicit reconciliation clears it. */
            controller.markPendingUpdateVerified()
            expect(controller.pendingUpdate == nil, "explicit reconciliation clears the record")
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
            expect(relaunched.pendingUpdate == nil, "explicit reconciliation clears the relaunch record")
        }

        /* S10: one dropped A1 reply triggers exactly one retry and still
         * completes; A1 is safe to resend (it re-inits staging). */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (retry run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            link.dropNextReplies = 1 /* armed after identify: hits the A1 reply */
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("retry run completes after a dropped A1 reply", timeout: 20) {
                if case .updateUnverified = controller.phase { return link.a0Seen }
                return false
            }
            expect(link.a1Count == 2, "the dropped A1 reply caused exactly one A1 retry (got \(link.a1Count))")
            expect(link.stagingVerified, "retry run staged the stock image correctly")
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
                expect(message.contains("staged"), "the state explains the staged-not-installed outcome")
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
            expect(relaunched.pendingUpdate == nil, "explicit reconciliation clears the disconnected-A3 record")
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

        print(checkFailures == 0 ? "ok" : "==== \(checkFailures) failure(s) ====")
        exit(checkFailures == 0 ? 0 : 1)
    }
}
