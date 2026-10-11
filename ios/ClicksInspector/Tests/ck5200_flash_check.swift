/*
 * Host check for the flash path of the REAL CustomFirmwareController.
 *
 * A scripted device implementing the decoded stock A1/A2/A3/A0 handler
 * semantics (dispatcher at stock 0x3216; the APK updater flow) receives the
 * bundled images through the real controller and asserts every outgoing
 * packet byte-for-byte, staging equality at A3, the timeout+retry path,
 * session death mid-flash, A0 reboot confirmation, and image validation.
 * This is the host-side mirror of the firmware's ck5200_restore_test.c
 * (review 2026-10-11, findings F-2/F-7).
 *
 * Compile and run:
 *   swiftc Sources/CustomKeyboardProtocol.swift Sources/CustomFirmwareController.swift \
 *     Tests/ck5200_flash_check.swift -o /tmp/ck5200_flash_check
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
    private var a1Size: UInt32?     /* size announced by A1 */

    /* Test controls. */
    var dropNextReplies = 0        /* swallow the next N replies */
    var muteA2 = false             /* never answer A2 (hard timeout path) */
    var closeAfterA2Count: Int?    /* simulate session death mid-A2 */
    var closeOnA0 = true           /* reboot after A0 kills the session */
    private var a2Answers = 0

    /* Observed by the checks. */
    private(set) var a1Count = 0
    private(set) var a2Count = 0
    private(set) var a3Count = 0
    private(set) var a0Seen = false
    private(set) var duplicateOffsets: [UInt32] = []
    private(set) var stagingVerified = false
    private(set) var protocolErrors: [String] = []

    init(image: [UInt8]) { self.image = image }

    func send(_ bytes: [UInt8]) {
        guard !closed else { return }
        guard bytes.count >= 2, bytes[0] == bytes.count else {
            protocolErrors.append("bad frame length byte")
            return
        }
        switch bytes[1] {
        case 0x03: /* version: stock firmware identity */
            reply([0x08, 0x02, 0x03, 0x00, 0x01, 0x20, 0x01, 0x22])

        case 0xa1:
            a1Count += 1
            guard bytes.count == 6 else { protocolErrors.append("A1 length"); return }
            let size = be32(bytes, 2)
            a1Size = size
            staged = []
            stagedOffset = 0
            let ok = size >= 1 && size <= 0x6A00
            reply([0x08, 0x02, 0xa1, ok ? 0x00 : 0x01, 0x00, 0x00, 0x6a, 0x00])

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
            if lastA2Offset == offset && a2Count > 1 {
                duplicateOffsets.append(offset) /* same offset twice = retry */
            }
            lastA2Offset = offset
            let chunk = Array(bytes[6...])
            staged.append(contentsOf: chunk)
            stagedOffset += UInt32(chunk.count)
            a2Answers += 1
            if let die = closeAfterA2Count, a2Answers >= die {
                dieMidFlash()
                return
            }
            reply([0x08, 0x02, 0xa2, 0x00] + be32bytes(stagedOffset))

        case 0xa3:
            a3Count += 1
            guard bytes.count == 6 else { protocolErrors.append("A3 length"); return }
            let size = be32(bytes, 2)
            var ok = size == stagedOffset && size == a1Size
            if ok {
                /* The whole point: the staged bytes equal the source image. */
                stagingVerified = staged == image
                ok = stagingVerified
            }
            reply([0x08, 0x02, 0xa3, ok ? 0x00 : 0x01] + be32bytes(stagedOffset))

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

    private var lastA2Offset: UInt32 = 0xFFFF_FFFF

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
private func makeController(link: ScriptedDeviceLink) -> CustomFirmwareController {
    let controller = CustomFirmwareController()
    controller.exchangeTimeoutNanos = 200_000_000    /* 200 ms for the host run */
    controller.rebootConfirmTimeoutNanos = 300_000_000
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

        /* S1: validation accepts both bundled images and rejects garbage. */
        do { try CustomKeyboardImageValidation.validate(image: stockImage); expect(true, "bundled stock image passes validation") }
        catch { expect(false, "bundled stock image passes validation: \(error)") }
        do { try CustomKeyboardImageValidation.validate(image: customImage); expect(true, "bundled custom image passes validation") }
        catch { expect(false, "bundled custom image passes validation: \(error)") }
        expect((try? CustomKeyboardImageValidation.validate(image: [0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x20, 0x00])) == nil,
               "non-JAL first word is rejected")
        expect((try? CustomKeyboardImageValidation.validate(image: [UInt8](repeating: 0xFF, count: 0x6A01))) == nil,
               "oversized image is rejected")
        expect((try? CustomKeyboardImageValidation.validate(image: [])) == nil,
               "empty image is rejected")

        /* S2: the full stock image goes through the real controller, every
         * packet byte-checked, staged bytes equal the source, A0 reboots. */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("stock re-flash completes (A0 + session death)", timeout: 20) {
                if case .idle = controller.phase { return link.a0Seen }
                return false
            }
            expect(link.protocolErrors.isEmpty, "no protocol violations (got \(link.protocolErrors))")
            expect(link.stagingVerified, "staged bytes equal the stock image byte-for-byte at A3")
            expect(link.a0Seen, "A0 reboot command was delivered")
        }

        /* S3: a dropped A1 reply must trigger exactly one retry and still
         * complete; A1 is idempotent (it re-inits staging, which is what a
         * fresh cycle does anyway). */
        do {
            let link = ScriptedDeviceLink(image: customImage)
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (retry run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            link.dropNextReplies = 1 /* arms after identify, so it hits the A1 reply */
            controller.flash(image: customImage, rebootToInstall: true)
            await waitUntil("retry run completes after a dropped reply", timeout: 20) {
                if case .idle = controller.phase { return link.a0Seen }
                return false
            }
            expect(link.a1Count == 2, "the dropped A1 reply caused exactly one A1 retry (got \(link.a1Count))")
            expect(link.stagingVerified, "retry run staged the custom image correctly")
        }

        /* S4: a session that dies mid-A2 must fail cleanly, not hang. */
        do {
            let link = ScriptedDeviceLink(image: customImage)
            link.closeAfterA2Count = 3
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (death run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: customImage, rebootToInstall: true)
            await waitUntil("mid-flash session death fails the update", timeout: 10) {
                if case .failed = controller.phase { return true }
                return false
            }
            if case .failed(let message) = controller.phase {
                expect(message.contains("Link closed"), "failure names the closed link")
            }
        }

        /* S5: a device that never answers must fail via the timeout, not
         * hang in .busy forever. */
        do {
            let link = ScriptedDeviceLink(image: customImage)
            link.muteA2 = true
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (mute run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: customImage, rebootToInstall: true)
            await waitUntil("silent device fails via timeout", timeout: 10) {
                if case .failed = controller.phase { return true }
                return false
            }
            if case .failed(let message) = controller.phase {
                expect(message.contains("did not answer"), "failure names the timeout (got: \(message))")
            }
        }

        /* S6: a garbage image is refused before any packet is sent. */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (garbage run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: [0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x20, 0x00], rebootToInstall: true)
            expect(link.a1Count == 0, "no A1 was sent for a rejected image")
            if case .failed(let message) = controller.phase {
                expect(message.contains("Rejected image"), "phase names the rejection")
            } else {
                expect(false, "garbage image must fail the phase")
            }
        }

        /* S7: A0 answered but the keyboard never reboots: the app must say
         * the image is staged, not report success. */
        do {
            let link = ScriptedDeviceLink(image: stockImage)
            link.closeOnA0 = false
            let controller = makeController(link: link)
            await waitUntil("identify reports stock firmware (A0 run)", timeout: 5) {
                if case .stockFirmware = controller.phase { return true }
                return false
            }
            controller.flash(image: stockImage, rebootToInstall: true)
            await waitUntil("missing reboot is reported as staged, not success", timeout: 10) {
                if case .failed = controller.phase { return true }
                return false
            }
            if case .failed(let message) = controller.phase {
                expect(message.contains("staged"), "the failure explains the staged state")
            }
        }

        print(checkFailures == 0 ? "ok" : "==== \(checkFailures) failure(s) ====")
        exit(checkFailures == 0 ? 0 : 1)
    }
}
