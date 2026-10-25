import CryptoKit
import Foundation

/// The sheet carries the session being confirmed, not a separate optional
/// state value that can lag behind Boolean sheet presentation.
struct ExtensionInstallConfirmation: Identifiable {
    let id: Int

    func blockReason(currentSession: Int, preflight: String?) -> String? {
        guard id == currentSession else { return "The connection changed. Cancel, open the installer connection, and review again." }
        return preflight
    }
}

/// Artifact approval is separate from permission to write a physical keyboard.
struct PreparedFirmwareImage {
    enum Artifact { case stock, programmable, bootloaderReader }
    let artifact: Artifact
    let bytes: [UInt8]
    let sha256: String

    init(_ bytes: [UInt8], artifact: Artifact = .stock) throws {
        let hash = SHA256.hash(data: Data(bytes)).map { String(format: "%02x", $0) }.joined()
        let expectedSize = artifact == .programmable ? 27_058 : 18_648
        let expectedHash: String
        switch artifact {
        case .stock: expectedHash = CustomKeyboardImageAuthorization.authorizedStockSHA256
        case .programmable: expectedHash = CustomKeyboardImageAuthorization.authorizedExtensionSHA256
        case .bootloaderReader: expectedHash = CustomKeyboardImageAuthorization.authorizedBootloaderReaderSHA256
        }
        guard bytes.count == expectedSize, hash == expectedHash else {
            throw CustomKeyboardError.invalidImage("The image does not match the pinned \(artifact) artifact")
        }
        try CustomKeyboardImageValidation.validate(image: bytes)
        self.artifact = artifact
        // Full application pages avoid an unknown final-page copy tail. The
        // accepted executable is unchanged. Only its 78 unused bytes become FF.
        self.bytes = artifact == .programmable
            ? bytes + Array(repeating: 0xff, count: CustomKeyboardCommand.maximumImageSize - bytes.count) : bytes
        sha256 = SHA256.hash(data: Data(self.bytes)).map { String(format: "%02x", $0) }.joined()
        guard sha256 == (artifact == .programmable ? CustomKeyboardImageAuthorization.authorizedExtensionTransferSHA256 : expectedHash) else {
            throw CustomKeyboardError.invalidImage("The transfer image hash does not match")
        }
        try CustomKeyboardImageValidation.validate(image: self.bytes)
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
    let image: PreparedFirmwareImage
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
