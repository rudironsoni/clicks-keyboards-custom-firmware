import Foundation

private final class ProductionLink: KeyboardPacketLink {
    var onFragment: (([UInt8]) -> Void)?
    var onClosed: ((String) -> Void)?
    var packets: [[UInt8]] = []
    let fault: String
    let imageSize: Int
    var injected = false
    var closed = false
    var checkBoundary: (() -> Void)?
    var afterLastDataACK: (() -> Void)?
    init(_ fault: String, imageSize: Int = 18_648) { self.fault = fault; self.imageSize = imageSize }
    func close() { closed = true; onFragment = nil; onClosed = nil }
    func send(_ bytes: [UInt8]) {
        precondition(!closed)
        packets.append(bytes)
        if bytes[1] == 0xa1 { checkBoundary?() }
        // A3 first flushes the final data page, then writes the length marker.
        if bytes[1] == 0xa3 && fault == "commit-failure" { sm_fail_write(2) }
        if bytes[1] == 0xa2 && fault == "interrupted" && sm_write_count() >= 2 {
            sm_disconnect(); onClosed?("modeled staging disconnect"); return
        }
        if bytes[1] == 0xa2 && fault == "expired" { sm_advance_time(2_600) }
        var reply = [UInt8](repeating: 0, count: 8)
        let count = sm_send(bytes, bytes.count, &reply)
        if !injected && ((bytes[1] == 0xa1 && fault == "lost-a1") ||
                        (bytes[1] == 0xa2 && fault == "lost-a2") ||
                        (bytes[1] == 0xa3 && fault == "lost-a3")) {
            injected = true; return
        }
        if !injected && bytes[1] == 0xa2 && fault == "partial" {
            injected = true; onFragment?(Array(reply.prefix(3))); return
        }
        if bytes[1] == 0xa0 {
            // Accepted production M2 requests reset with no ACK, even in the
            // stock-reference identity fixture. Do not invent a C acknowledgement.
            onClosed?("modeled reset request closed the link"); return
        }
        if count > 0 { onFragment?(Array(reply.prefix(count))) }
        if bytes[1] == 0xa2 && reply[4..<8].reduce(0, { ($0 << 8) | Int($1) }) == imageSize {
            afterLastDataACK?()
        }
    }
}

@main
struct StockManagerTests {
    @MainActor static func settle(_ c: CustomFirmwareController) async throws {
        for _ in 0..<2_000 {
            switch c.phase {
            case .idle, .identifying, .busy: try await Task.sleep(for: .milliseconds(2))
            default: return
            }
        }
        fatalError("controller did not finish its bounded operation")
    }

    @MainActor static func main() async throws {
        let mode = Int32(CommandLine.arguments[1])!
        let fault = CommandLine.arguments[2]
        let raw = [UInt8](try Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[3])))
        let artifact: PreparedFirmwareImage.Artifact = CustomFirmwareController.extensionUpdatesEnabled ? .programmable : .stock
        let image = try PreparedFirmwareImage(raw, artifact: artifact).bytes
        precondition(sm_boot(mode))
        if fault != "unauthenticated" { precondition(sm_authenticate()) }
        CustomFirmwareController.resetPendingUpdateForTesting()
        defer { CustomFirmwareController.resetPendingUpdateForTesting() }
        let c = CustomFirmwareController()
        c.exchangeTimeoutNanos = 50_000_000
        c.rebootConfirmTimeoutNanos = 50_000_000
        if artifact == .stock {
            c.prepareStock(raw)
            precondition(c.preparedStock?.sha256 == CustomKeyboardImageAuthorization.authorizedStockSHA256)
        }
        var invalid = raw; invalid[100] ^= 1
        precondition((try? PreparedFirmwareImage(invalid, artifact: artifact)) == nil)
        c.connectedTarget = .init(model: fault == "wrong-model" ? "CK-5200B" : "CK-5200",
                                 firmwareRevision: "1.2.2", hardwareRevision: "1.2.0")
        let link = ProductionLink(fault, imageSize: image.count)
        func flash() {
            c.flash(image: raw, rebootToInstall: true, artifact: artifact,
                    authorizedSession: artifact == .programmable ? c.installationSessionID : nil)
        }
        c.attach(link)
        try await settle(c)
        if fault == "unauthenticated" || fault == "wrong-model" {
            flash()
            precondition(sm_write_count() == 0 && sm_reboot_count() == 0)
            precondition(!link.packets.contains { (0xa0...0xa3).contains($0[1]) })
            print("PASS \(mode) \(fault): zero writes")
            return
        }
        precondition(c.stockMode == (mode == 2 ? .rescue : mode == 1 ? .qmk : .stock))
        // All commands except the exact read-only version request are refused
        // outside a transaction, including setting, keymap and rescue-entry writes.
        let before = link.packets.count
        for op in UInt8.min...UInt8.max where op != 3 {
            do { try c.dispatch([2, op]); fatalError("unexpected command allowed: \(op)") }
            catch {}
        }
        precondition(link.packets.count == before)
        link.checkBoundary = {
            for request in [CustomKeyboardCommand.beginUpdateRequest(size: image.count),
                            CustomKeyboardCommand.finishUpdateRequest(size: image.count), [2, 0xa0], [2, 0x94]] {
                do { try c.dispatch(request); fatalError("direct dispatch escaped transaction ownership") }
                catch {}
            }
        }
        if fault == "session-swap" {
            link.afterLastDataACK = {
                // The continuation has already succeeded, but its Task has not resumed.
                c.detach()
                sm_disconnect()
                precondition(sm_authenticate())
                c.attach(ProductionLink("read-only"))
            }
        }
        flash()
        try await settle(c)
        precondition(sm_protected_intact())
        if fault == "ok" {
            precondition(sm_staged_matches(image, image.count))
            precondition(sm_marker() == image.count && sm_reboot_count() == 1)
            precondition(c.pendingUpdate?.outcome == .sentRebootPending)
            precondition(c.pendingUpdate?.sourceMode == cMode(mode))
            // This host copy model is stock-only. Extension checks stop at
            // staging/reboot and make no claim about destination flash.
            if artifact == .stock { precondition(sm_model_install()) }
            precondition(sm_protected_intact())
        } else if fault == "lost-a3" {
            precondition(sm_marker() == image.count && sm_reboot_count() == 0)
            precondition(c.pendingUpdate?.outcome == .commitUnknown)
            precondition(link.packets.filter { $0[1] == 0xa3 }.count == 1)
        } else if fault == "commit-failure" {
            precondition(sm_marker() == UInt32.max && sm_reboot_count() == 0)
            precondition(sm_write_count() == (image.count + 4 + 255) / 256 + 2 && sm_staged_matches(image, image.count))
            precondition(c.pendingUpdate?.outcome == .commitUnknown)
        } else {
            precondition((sm_marker() == 0 || sm_marker() == UInt32.max) && sm_reboot_count() == 0 && c.pendingUpdate == nil)
            precondition(!link.packets.contains { $0[1] == 0xa3 || $0[1] == 0xa0 })
            if fault == "interrupted" || fault == "session-swap" {
                precondition(sm_write_count() >= 2 && sm_staged_matches(image, 252))
                precondition(!sm_staged_matches(image, image.count))
            }
        }
        let writes = sm_write_count()
        let requests = link.packets.count
        // No continuation after timeout, no automatic retry, no late reply revival.
        link.onFragment?([8, 2, 0xa2, 0, 0, 0, 0, 32])
        if fault != "session-swap" { flash() }
        precondition(link.packets.count == requests && sm_write_count() == writes)
        if let record = c.pendingUpdate {
            let fresh = CustomFirmwareController()
            precondition(fresh.pendingUpdate == record)
            fresh.markPendingUpdateVerified()
            precondition(fresh.pendingUpdate == record)
            if fault == "ok" {
                sm_disconnect()
                fresh.connectedTarget = .init(model: "CK-5200", firmwareRevision: "1.2.2", hardwareRevision: "1.2.0")
                fresh.exchangeTimeoutNanos = 50_000_000
                fresh.attach(ProductionLink("read-only"))
                try await settle(fresh)
                precondition(fresh.stockMode == nil && sm_write_count() == writes)
                precondition(sm_authenticate())
                fresh.attach(ProductionLink("read-only"))
                try await settle(fresh)
                precondition(fresh.stockMode == .stock)
                precondition(fresh.pendingUpdate == record && fresh.stockObservation.contains("does not prove"))
            }
        }
        print("PASS Swift + production C: mode \(mode), \(fault); writes \(writes), no physical operations")
    }

    static func cMode(_ mode: Int32) -> StockFirmwareMode { mode == 2 ? .rescue : mode == 1 ? .qmk : .stock }
}
