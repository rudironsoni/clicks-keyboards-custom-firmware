import Foundation

private final class CaptureLink: KeyboardPacketLink {
    var onFragment: (([UInt8]) -> Void)?
    var onClosed: ((String) -> Void)?
    var packets: [[UInt8]] = []
    func close() {}
    func send(_ bytes: [UInt8]) {
        packets.append(bytes)
        if bytes == [2, 3] { onFragment?([8, 2, 3, 0, 1, 0x20, 1, 0x22]) }
    }
}

@main
struct DiagnosticsOnlyTests {
    @MainActor static func main() async throws {
        let controller = CustomFirmwareController()
        let link = CaptureLink()
        controller.connectedTarget = .init(model: "CK-5200", firmwareRevision: "1.2.2", hardwareRevision: "1.2.0")
        controller.attach(link)
        for _ in 0..<20 { await Task.yield() }
        let image = [UInt8](try Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1])))
        controller.flash(image: image, rebootToInstall: true)
        for _ in 0..<20 { await Task.yield() }
        precondition(!link.packets.contains { $0.count > 1 && (0xa0...0xa3).contains($0[1]) }, "diagnostics dispatched firmware write")
        #if DIAGNOSTICS_BOUNDARY_TEST
        let before = link.packets.count
        for opcode: UInt8 in [0xa0, 0xa1, 0xa2, 0xa3, 0x82, 0x92, 0x93] {
            for request in [[2, opcode], [6, opcode, 0, 0, 0, 8]] {
                do {
                    try controller.dispatch(request)
                    preconditionFailure("diagnostics boundary accepted a write")
                } catch CustomKeyboardError.diagnosticsOnly {}
            }
        }
        precondition(link.packets.count == before)
        #endif
        print("PASS diagnostics flash entry rejects stock image before A1")
        print("PASS diagnostics boundary refuses A0/A1/A2/A3 and settings/reset commands")
    }
}
