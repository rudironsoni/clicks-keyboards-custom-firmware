import Foundation

private final class ProductionLink: KeyboardPacketLink {
    var onFragment: (([UInt8]) -> Void)?
    var onClosed: ((String) -> Void)?
    var packets: [[UInt8]] = []
    let fault: String
    var injected = false
    var closed = false
    var checkBoundary: (() -> Void)?
    init(_ fault: String) { self.fault = fault }
    func close() { closed = true; onFragment = nil; onClosed = nil }
    func send(_ bytes: [UInt8]) {
        precondition(!closed)
        packets.append(bytes)
        if bytes[1] == 0xa1 { checkBoundary?() }
        if bytes[1] == 0xa3 && fault == "commit-failure" { sm_fail_write(1) }
        if bytes[1] == 0xa2 && fault == "interrupted" {
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
        let image = [UInt8](try Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[3])))
        precondition(sm_boot(mode))
        if fault != "unauthenticated" { precondition(sm_authenticate()) }
        CustomFirmwareController.resetPendingUpdateForTesting()
        defer { CustomFirmwareController.resetPendingUpdateForTesting() }
        let c = CustomFirmwareController()
        c.exchangeTimeoutNanos = 50_000_000
        c.rebootConfirmTimeoutNanos = 50_000_000
        c.prepareStock(image)
        precondition(c.preparedStock?.sha256 == CustomKeyboardImageAuthorization.authorizedStockSHA256)
        var invalid = image; invalid[100] ^= 1
        precondition((try? PreparedStockImage(invalid)) == nil)
        c.connectedTarget = .init(model: fault == "wrong-model" ? "CK-5200B" : "CK-5200",
                                 firmwareRevision: "1.2.2", hardwareRevision: "1.2.0")
        let link = ProductionLink(fault)
        c.attach(link)
        try await settle(c)
        if fault == "unauthenticated" || fault == "wrong-model" {
            c.flash(image: image, rebootToInstall: true)
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
        c.flash(image: image, rebootToInstall: true)
        try await settle(c)
        precondition(sm_protected_intact())
        if fault == "ok" {
            precondition(sm_staged_matches(image, image.count))
            precondition(sm_marker() == image.count && sm_reboot_count() == 1)
            precondition(c.pendingUpdate?.outcome == .sentRebootPending)
            precondition(c.pendingUpdate?.sourceMode == cMode(mode))
            // Explicit host installer model, not original installer execution.
            precondition(sm_model_install())
            precondition(sm_protected_intact())
        } else if fault == "lost-a3" {
            precondition(sm_marker() == image.count && sm_reboot_count() == 0)
            precondition(c.pendingUpdate?.outcome == .commitUnknown)
            precondition(link.packets.filter { $0[1] == 0xa3 }.count == 1)
        } else if fault == "commit-failure" {
            precondition(sm_marker() == 0 && sm_reboot_count() == 0)
            precondition(c.pendingUpdate?.outcome == .commitUnknown)
        } else {
            precondition(sm_marker() == 0 && sm_reboot_count() == 0 && c.pendingUpdate == nil)
            precondition(!link.packets.contains { $0[1] == 0xa3 || $0[1] == 0xa0 })
        }
        let writes = sm_write_count()
        let requests = link.packets.count
        // No continuation after timeout, no automatic retry, no late reply revival.
        link.onFragment?([8, 2, 0xa2, 0, 0, 0, 0, 32])
        c.flash(image: image, rebootToInstall: true)
        precondition(link.packets.count == requests && sm_write_count() == writes)
        if let record = c.pendingUpdate {
            let fresh = CustomFirmwareController()
            precondition(fresh.pendingUpdate == record)
            fresh.markPendingUpdateVerified()
            precondition(fresh.pendingUpdate == record)
            if fault == "ok" {
                fresh.connectedTarget = .init(model: "CK-5200", firmwareRevision: "1.2.2", hardwareRevision: "1.2.0")
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
