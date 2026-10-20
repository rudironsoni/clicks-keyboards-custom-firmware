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
 * The keymap commands 0x90..0x93 exist only in this project's QMK
 * firmware; stock CK-5200 firmware 1.2.2 answers the version query with
 * hardware 01 20, never "QM", so those are unreachable on stock. The
 * A1/A2/A3/A0 update dialect, however, IS implemented by stock: the
 * first flash from this app runs through the stock handler (see
 * docs/FLASHING.md).
 */

enum CustomKeyboardError: Error, Equatable, CustomStringConvertible {
    case wrongLength(expected: Int, declared: Int)
    case overlength(expected: Int, actual: Int)
    case unexpectedType(UInt8)
    case unexpectedCommand(expected: UInt8, actual: UInt8)
    case rejected(status: UInt8)
    case notCustomFirmware
    case transportUnavailable
    case diagnosticsOnly
    case imageTooLarge(size: Int)
    case updateRejected(at: String, status: UInt8)
    case timedOut
    case invalidImage(String)
    case invalidProfile
    case readbackMismatch
    case ramWritesNotAllowed
    case auditUnavailable

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
        case .diagnosticsOnly:
            "Diagnostics-only build: firmware, reset, and settings writes are disabled"
        case .imageTooLarge(let size):
            "Image is \(size) bytes; the keyboard accepts at most 27136 (0x6a00)"
        case .updateRejected(let at, let status):
            "Update failed at \(at); status 0x\(String(status, radix: 16))"
        case .timedOut:
            "The keyboard did not answer in time"
        case .invalidImage(let reason):
            "Firmware image rejected: \(reason)"
        case .invalidProfile:
            "Profile must have a name, schema version 1, and four 6x6 CK-5200 layers"
        case .readbackMismatch:
            "Keyboard readback does not match the requested layout"
        case .ramWritesNotAllowed:
            "Read the keyboard layout and enable RAM writes for this session first"
        case .auditUnavailable:
            "The update audit log could not be persisted; the trial stops before any further frame"
        }
    }
}

struct KeyboardInfo: Equatable {
    let layers: Int
    let rows: Int
    let columns: Int
}

/// Which images this build is allowed to flash (review 2026-10-12).
///
/// The ONLY authorized artifact is the official stock V122 application image,
/// identified by its recorded SHA-256 (the same hash pinned by
/// keyboards/ck5200/tools/preflight_check.sh). Custom QMK flashing is closed
/// until a separately reviewed CH32X035 image is authorized: the bundled QMK
/// image targets the correct family now, but it has no independent recovery
/// evidence and no review authorization. Enforcement is structural and
/// independent of any UI state: `PreparedStockImage` refuses every image whose
/// hash is not `authorizedStockSHA256`, and the per-frame dispatch permit
/// re-checks the accepted transaction's image hash.
enum CustomKeyboardImageAuthorization {
    static let authorizedStockSHA256 = "8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8"
}

/// Structural validation of a CK-5200 application image, the same checks
/// as the Mac tool's validate_binary (keyboards/ck5200/tools/validate_image.py):
/// a bootable image starts with a JAL to its reset handler inside the image,
/// and declares the vector-table base at word 1. Stops the app from staging
/// an arbitrary imported file into the one state the project calls
/// unrecoverable (review 2026-10-11, F-2).
enum CustomKeyboardImageValidation {
    static let appBase = 0x2000
    static let maxImageSize = 0x6A00

    static func validate(image: [UInt8]) throws {
        guard !image.isEmpty else { throw CustomKeyboardError.invalidImage("empty file") }
        guard image.count <= CustomKeyboardCommand.maximumImageSize else {
            throw CustomKeyboardError.imageTooLarge(size: image.count)
        }
        guard image.count >= 8 else {
            throw CustomKeyboardError.invalidImage("too short for a reset jump and first vector entry")
        }

        func word(_ offset: Int) -> UInt32 {
            UInt32(image[offset]) | (UInt32(image[offset + 1]) << 8)
                | (UInt32(image[offset + 2]) << 16) | (UInt32(image[offset + 3]) << 24)
        }

        let instruction = word(0)
        guard instruction & 0x7F == 0x6F else {
            throw CustomKeyboardError.invalidImage("first instruction is not a JAL reset jump")
        }
        guard (instruction >> 7) & 0x1F == 0 else {
            throw CustomKeyboardError.invalidImage("reset JAL must use x0 as its destination")
        }

        var immediate = ((instruction >> 31) & 1) << 20
            | ((instruction >> 21) & 0x3FF) << 1
            | ((instruction >> 20) & 1) << 11
            | ((instruction >> 12) & 0xFF) << 12
        if immediate & (1 << 20) != 0 { immediate -= 1 << 21 }
        let target = appBase + Int(Int32(bitPattern: immediate))
        guard target >= appBase, target < appBase + image.count else {
            throw CustomKeyboardError.invalidImage("reset target 0x\(String(target, radix: 16)) is outside the image")
        }
        guard target & 1 == 0 else {
            throw CustomKeyboardError.invalidImage("reset target is not 2-byte aligned")
        }
        guard word(4) == appBase else {
            throw CustomKeyboardError.invalidImage("first vector target is 0x\(String(word(4), radix: 16)), expected 0x\(String(appBase, radix: 16))")
        }
    }
}

enum CustomKeyboardCommand {
    static let maximumImageSize = 0x6a00
    static let chunkSize = 32
    static let maximumResponseLength = 8

    // Requests.
    static let versionRequest: [UInt8] = [0x02, 0x03]

    /// The stock firmware identity the connected keyboard must report
    /// before the stock-reflash path may run (live iPhone read, recorded in
    /// keyboards/ck5200/docs/USB_AND_IPHONE.md).
    static let stockVersionPayload: [UInt8] = [0x01, 0x20, 0x01, 0x22]
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

    static func reboot() -> CustomKeyboardExchange {
        CustomKeyboardExchange(request: CustomKeyboardCommand.rebootRequest, command: 0xa0, expectedLength: 4)
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

/// Splits an image into the A2 chunk sequence. The stock handler
/// acknowledges a complete chunk by returning the new cumulative offset
/// (stock 0x3810..0x3812); any other acknowledgement value aborts the
/// transaction: zero progress, partial acknowledgement, backward movement,
/// and forward jumps are all rejections (review 2026-10-12).
struct UpdateChunker {
    let image: [UInt8]
    private(set) var offset: Int = 0
    private var lastChunkCount = 0

    init(image: [UInt8]) throws {
        guard !image.isEmpty else { throw CustomKeyboardError.imageTooLarge(size: 0) }
        guard image.count <= CustomKeyboardCommand.maximumImageSize else {
            throw CustomKeyboardError.imageTooLarge(size: image.count)
        }
        self.image = image
    }

    var finished: Bool { offset >= image.count }

    mutating func nextExchange() -> CustomKeyboardExchange? {
        guard !finished else { return nil }
        let end = min(offset + CustomKeyboardCommand.chunkSize, image.count)
        lastChunkCount = end - offset
        return CustomKeyboardExchange.writeUpdate(offset: offset, chunk: image[offset..<end])
    }

    /// Applies the accepted offset from the A2 reply: it must equal exactly
    /// the end of the chunk that was just sent.
    mutating func applyAcceptedOffset(_ value: UInt32) throws {
        let accepted = Int(value)
        guard lastChunkCount > 0, accepted == offset + lastChunkCount else {
            throw CustomKeyboardError.updateRejected(
                at: "chunk acknowledgement \(accepted), expected \(offset + lastChunkCount)",
                status: 0x01)
        }
        offset = accepted
        lastChunkCount = 0
    }
}
