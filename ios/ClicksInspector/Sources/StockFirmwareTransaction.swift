import CryptoKit
import Foundation

/// Artifact approval is separate from permission to write a physical keyboard.
struct PreparedStockImage {
    let bytes: [UInt8]
    let sha256: String

    init(_ bytes: [UInt8]) throws {
        let hash = SHA256.hash(data: Data(bytes)).map { String(format: "%02x", $0) }.joined()
        guard bytes.count == 18_648,
              hash == CustomKeyboardImageAuthorization.authorizedStockSHA256 else {
            throw CustomKeyboardError.invalidImage("Only exact CK-5200 V122 is accepted")
        }
        try CustomKeyboardImageValidation.validate(image: bytes)
        self.bytes = bytes
        sha256 = hash
    }
}

enum StockFirmwareMode: String, Codable {
    case stock, qmk, rescue

    init?(payload: [UInt8]) {
        switch payload {
        case CustomKeyboardCommand.stockVersionPayload: self = .stock
        case [0x51, 0x4d, 0, 1]: self = .qmk
        case [0x52, 0x53, 0, 1]: self = .rescue
        default: return nil
        }
    }
}

/// Only the next exact packet of this pinned-image transaction can reach transport.
/// State advances only after its complete, validated acknowledgement.
struct StockFirmwareTransaction {
    enum Step { case begin, data, commit, reboot, finished }
    let image: PreparedStockImage
    let source: StockFirmwareMode
    let target: CustomFirmwareController.ConnectedTarget
    let generation: Int
    private(set) var step = Step.begin
    private(set) var offset = 0

    var request: [UInt8]? {
        switch step {
        case .begin: return CustomKeyboardCommand.beginUpdateRequest(size: image.bytes.count)
        case .data:
            return CustomKeyboardCommand.writeUpdateRequest(offset: offset,
                chunk: image.bytes[offset..<min(offset + 32, image.bytes.count)])
        case .commit: return CustomKeyboardCommand.finishUpdateRequest(size: image.bytes.count)
        case .reboot: return CustomKeyboardCommand.rebootRequest
        case .finished: return nil
        }
    }

    mutating func accept(_ payload: [UInt8]) throws {
        let value = payload.reduce(UInt32(0)) { ($0 << 8) | UInt32($1) }
        switch step {
        case .begin:
            guard payload.count == 4, value == UInt32(CustomKeyboardCommand.maximumImageSize) else {
                throw CustomKeyboardError.updateRejected(at: "advertised maximum", status: 1)
            }
            step = .data
        case .data:
            let next = min(offset + 32, image.bytes.count)
            guard payload.count == 4, value == next else {
                throw CustomKeyboardError.updateRejected(at: "chunk acknowledgement", status: 1)
            }
            offset = next
            if offset == image.bytes.count { step = .commit }
        case .commit:
            guard payload.count == 4, value == image.bytes.count else {
                throw CustomKeyboardError.updateRejected(at: "committed size", status: 1)
            }
            step = .reboot
        case .reboot:
            guard payload.isEmpty else { throw CustomKeyboardError.rejected(status: 255) }
            step = .finished
        case .finished: throw CustomKeyboardError.transportUnavailable
        }
    }
}
