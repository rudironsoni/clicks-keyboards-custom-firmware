import Foundation

/// Sends requests and gathers responses over any byte transport.
/// Implemented by the EASession-backed AccessoryInspector.
protocol KeyboardPacketLink: AnyObject {
    var onFragment: (([UInt8]) -> Void)? { get set }
    var onClosed: ((String) -> Void)? { get set }
    func send(_ bytes: [UInt8])
    func close()
}

/*
 * Custom CK-5200 firmware protocol, same framing as stock
 * ([len, cmd, ...] / [len, 0x02, cmd, status, payload...], big-endian).
 *
 * Commands beyond the stock read set exist only in this project's QMK
 * firmware. Stock CK-5200 firmware 1.2.2 answers the version query with
 * hardware 01 20, never "QM", so every write below is unreachable on stock.
 */

enum CustomKeyboardError: Error, Equatable, CustomStringConvertible {
    case wrongLength(expected: Int, declared: Int)
    case overlength(expected: Int, actual: Int)
    case unexpectedType(UInt8)
    case unexpectedCommand(expected: UInt8, actual: UInt8)
    case rejected(status: UInt8)
    case notCustomFirmware
    case transportUnavailable
    case imageTooLarge(size: Int)
    case updateRejected(at: String, status: UInt8)

    var description: String {
        switch self {
        case .wrongLength(let expected, let declared):
            "Expected length byte \(expected), got \(declared)"
        case .overlength(let expected, let actual):
            "Expected at most \(expected) bytes, got \(actual)"
        case .unexpectedType(let type):
            "Expected response type 0x02, got 0x\(String(type, radix: 16))"
        case .unexpectedCommand(let expected, let actual):
            "Expected command 0x\(String(expected, radix: 16)), got 0x\(String(actual, radix: 16))"
        case .rejected(let status):
            "Device rejected the request; status 0x\(String(status, radix: 16))"
        case .notCustomFirmware:
            "Connected keyboard did not identify as the custom QMK firmware"
        case .transportUnavailable:
            "No usable transport to the keyboard is available"
        case .imageTooLarge(let size):
            "Image is \(size) bytes; the keyboard accepts at most 27136 (0x6a00)"
        case .updateRejected(let at, let status):
            "Update failed at \(at); status 0x\(String(status, radix: 16))"
        }
    }
}

struct KeyboardInfo: Equatable {
    let layers: Int
    let rows: Int
    let columns: Int
}

enum CustomKeyboardCommand {
    static let maximumImageSize = 0x6a00
    static let chunkSize = 32
    static let maximumResponseLength = 8

    // Requests.
    static let versionRequest: [UInt8] = [0x02, 0x03]
    static let infoRequest: [UInt8] = [0x02, 0x90]
    static let resetRequest: [UInt8] = [0x02, 0x93]

    static func getKeyRequest(layer: UInt8, row: UInt8, column: UInt8) -> [UInt8] {
        [0x05, 0x91, layer, row, column]
    }

    static func setKeyRequest(layer: UInt8, row: UInt8, column: UInt8, keycode: UInt16) -> [UInt8] {
        [0x07, 0x92, layer, row, column, UInt8(keycode >> 8), UInt8(keycode & 0xff)]
    }

    static func beginUpdateRequest(size: Int) -> [UInt8] {
        [0x06, 0xa1, UInt8(size >> 24), UInt8((size >> 16) & 0xff), UInt8((size >> 8) & 0xff), UInt8(size & 0xff)]
    }

    static func writeUpdateRequest(offset: Int, chunk: ArraySlice<UInt8>) -> [UInt8] {
        precondition(chunk.count <= 32)
        var request: [UInt8] = [UInt8(6 + chunk.count), 0xa2,
                                 UInt8(offset >> 24), UInt8((offset >> 16) & 0xff),
                                 UInt8((offset >> 8) & 0xff), UInt8(offset & 0xff)]
        request.append(contentsOf: chunk)
        return request
    }

    static func finishUpdateRequest(size: Int) -> [UInt8] {
        [0x06, 0xa3, UInt8(size >> 24), UInt8((size >> 16) & 0xff), UInt8((size >> 8) & 0xff), UInt8(size & 0xff)]
    }

    static let rebootRequest: [UInt8] = [0x02, 0xa0]
}

/// Accumulates one response from stream fragments and validates the header.
struct CustomKeyboardAccumulator {
    let expectedCommand: UInt8
    let expectedLength: Int
    private(set) var bytes: [UInt8] = []

    init(command: UInt8, expectedLength: Int) {
        self.expectedCommand = command
        self.expectedLength = expectedLength
    }

    mutating func append(_ fragment: [UInt8]) throws -> [UInt8]? {
        let total = bytes.count + fragment.count
        guard total <= expectedLength else {
            throw CustomKeyboardError.overlength(expected: expectedLength, actual: total)
        }
        guard !fragment.isEmpty else { return nil }
        bytes.append(contentsOf: fragment)

        if let declared = bytes.first, Int(declared) != expectedLength {
            throw CustomKeyboardError.wrongLength(expected: expectedLength, declared: Int(declared))
        }
        if bytes.count >= 2, bytes[1] != 0x02 {
            throw CustomKeyboardError.unexpectedType(bytes[1])
        }
        if bytes.count >= 3, bytes[2] != expectedCommand {
            throw CustomKeyboardError.unexpectedCommand(expected: expectedCommand, actual: bytes[2])
        }
        guard bytes.count == expectedLength else { return nil }
        if bytes[3] != 0x00 {
            throw CustomKeyboardError.rejected(status: bytes[3])
        }
        return Array(bytes.dropFirst(4))
    }
}

/// One request/response exchange against the custom firmware.
struct CustomKeyboardExchange {
    let request: [UInt8]
    let command: UInt8
    let expectedLength: Int

    var accumulator: CustomKeyboardAccumulator

    static func version() -> CustomKeyboardExchange {
        CustomKeyboardExchange(request: CustomKeyboardCommand.versionRequest, command: 0x03, expectedLength: 8)
    }

    static func info() -> CustomKeyboardExchange {
        CustomKeyboardExchange(request: CustomKeyboardCommand.infoRequest, command: 0x90, expectedLength: 8)
    }

    static func getKey(layer: UInt8, row: UInt8, column: UInt8) -> CustomKeyboardExchange {
        CustomKeyboardExchange(request: CustomKeyboardCommand.getKeyRequest(layer: layer, row: row, column: column),
                               command: 0x91, expectedLength: 6)
    }

    static func setKey(layer: UInt8, row: UInt8, column: UInt8, keycode: UInt16) -> CustomKeyboardExchange {
        CustomKeyboardExchange(request: CustomKeyboardCommand.setKeyRequest(layer: layer, row: row, column: column, keycode: keycode),
                               command: 0x92, expectedLength: 4)
    }

    static func resetKeymap() -> CustomKeyboardExchange {
        CustomKeyboardExchange(request: CustomKeyboardCommand.resetRequest, command: 0x93, expectedLength: 4)
    }

    static func beginUpdate(size: Int) -> CustomKeyboardExchange {
        CustomKeyboardExchange(request: CustomKeyboardCommand.beginUpdateRequest(size: size), command: 0xa1, expectedLength: 8)
    }

    static func writeUpdate(offset: Int, chunk: ArraySlice<UInt8>) -> CustomKeyboardExchange {
        CustomKeyboardExchange(request: CustomKeyboardCommand.writeUpdateRequest(offset: offset, chunk: chunk),
                               command: 0xa2, expectedLength: 8)
    }

    static func finishUpdate(size: Int) -> CustomKeyboardExchange {
        CustomKeyboardExchange(request: CustomKeyboardCommand.finishUpdateRequest(size: size), command: 0xa3, expectedLength: 8)
    }

    private init(request: [UInt8], command: UInt8, expectedLength: Int, existing: CustomKeyboardAccumulator? = nil) {
        self.request = request
        self.command = command
        self.expectedLength = expectedLength
        self.accumulator = existing ?? CustomKeyboardAccumulator(command: command, expectedLength: expectedLength)
    }

    /// Copy with an accumulator that already holds received bytes.
    func replacingAccumulator(_ existing: CustomKeyboardAccumulator) -> CustomKeyboardExchange {
        CustomKeyboardExchange(request: request, command: command, expectedLength: expectedLength, existing: existing)
    }
}

enum CustomKeyboardIdentity {
    /// The version reply payload "QM" 00 01 identifies this project's firmware.
    static func isCustomFirmware(_ payload: [UInt8]) -> Bool {
        payload == [UInt8(ascii: "Q"), UInt8(ascii: "M"), 0x00, 0x01]
    }

    static func info(fromInfoPayload payload: [UInt8]) -> KeyboardInfo? {
        guard payload.count == 4 else { return nil }
        return KeyboardInfo(layers: Int(payload[0]), rows: Int(payload[1]), columns: Int(payload[2]))
    }
}

/// Splits an image into the A2 chunk sequence, driving offsets from the
/// device reply as the stock updater does.
struct UpdateChunker {
    let image: [UInt8]
    private(set) var offset: Int = 0

    init(image: [UInt8]) throws {
        guard !image.isEmpty else { throw CustomKeyboardError.imageTooLarge(size: 0) }
        guard image.count <= CustomKeyboardCommand.maximumImageSize else {
            throw CustomKeyboardError.imageTooLarge(size: image.count)
        }
        self.image = image
    }

    var finished: Bool { offset >= image.count }

    var nextExchange: CustomKeyboardExchange? {
        guard !finished else { return nil }
        let end = min(offset + CustomKeyboardCommand.chunkSize, image.count)
        return CustomKeyboardExchange.writeUpdate(offset: offset, chunk: image[offset..<end])
    }

    /// Applies the accepted offset from the A2 reply.
    mutating func applyAcceptedOffset(_ value: UInt32) throws {
        let accepted = Int(value)
        guard accepted >= offset, accepted <= image.count else {
            throw CustomKeyboardError.updateRejected(at: "chunk offset \(accepted)", status: 0x01)
        }
        offset = accepted
    }
}
