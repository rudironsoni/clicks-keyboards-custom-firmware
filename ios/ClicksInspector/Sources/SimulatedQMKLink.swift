#if targetEnvironment(simulator) && QMK_RAM_KEYMAP
import Foundation

/// Simulator-only transport to the same production C/QMK bridge as host tests.
/// No command implementation is duplicated in Swift.
final class SimulatedQMKLink: KeyboardPacketLink {
    var onFragment: (([UInt8]) -> Void)?
    var onClosed: ((String) -> Void)?
    init() { qmk_bridge_boot() }

    func close() { onFragment = nil; onClosed = nil }

    func send(_ bytes: [UInt8]) {
        var response = [UInt8](repeating: 0, count: 8)
        let count = qmk_bridge_send(bytes, bytes.count, &response)
        guard count > 0 else { return }
        onFragment?(Array(response.prefix(count)))
    }
}
#endif
