import Foundation

enum StockReadCommand: UInt8, CaseIterable, Identifiable, Hashable {
    case version = 0x03
    case brightness = 0x84
    case backlightDelay = 0x86
    case batteryIdleInterval = 0x88
    case rawSetting8A = 0x8a
    case rawSetting8C = 0x8c
    case statusFlags = 0x8d
    case checksumService = 0x08
    case newerUserKeyProbe = 0x8f

    var id: UInt8 { rawValue }

    var title: String {
        switch self {
        case .version: "Version"
        case .brightness: "Brightness"
        case .backlightDelay: "Backlight delay (ms)"
        case .batteryIdleInterval: "Battery idle interval (raw)"
        case .rawSetting8A: "Raw setting 0x8A"
        case .rawSetting8C: "Raw setting 0x8C"
        case .statusFlags: "Status flags"
        case .checksumService: "Probe checksum service 0x08"
        case .newerUserKeyProbe: "Probe newer user-key read 0x8F"
        }
    }

    var request: [UInt8] {
        switch self {
        case .checksumService: [0x05, 0x08, 0x00, 0x00, 0x00]
        // The default handler loads four argument bytes even for unknown commands.
        case .newerUserKeyProbe: [0x06, 0x8f, 0x00, 0x00, 0x00, 0x00]
        default: [0x02, rawValue]
        }
    }

    var expectedStatus: UInt8 { self == .newerUserKeyProbe ? 0xff : 0x00 }

    var expectedResponseLength: Int {
        switch self {
        case .version, .newerUserKeyProbe: 8
        case .checksumService: 4
        case .brightness, .rawSetting8A, .rawSetting8C, .statusFlags: 5
        case .backlightDelay, .batteryIdleInterval: 6
        }
    }

    static var maximumResponseLength: Int {
        allCases.map(\.expectedResponseLength).max() ?? 0
    }
}

enum StockReadError: Error, Equatable, CustomStringConvertible {
    case wrongLength(expected: Int, declared: Int)
    case overlength(expected: Int, actual: Int)
    case unexpectedType(UInt8)
    case unexpectedCommand(expected: UInt8, actual: UInt8)
    case rejected(status: UInt8)
    case unexpectedProbePayload

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
            "Unexpected device status 0x\(String(status, radix: 16))"
        case .unexpectedProbePayload:
            "Unsupported-command reply contained unexpected data"
        }
    }
}

struct StockReadResult: Equatable {
    let command: StockReadCommand
    let payload: [UInt8]

    var summary: String {
        if command == .checksumService { return "Checksum service acknowledged; no checksum or memory bytes returned" }
        if command == .newerUserKeyProbe { return "Newer user-key command is unsupported (status ff); no keymap read" }
        let raw = payload.map { String(format: "%02x", $0) }.joined(separator: " ")
        if command == .version, payload.count == 4 {
            let hardware = payload.prefix(2).map { String(format: "%02x", $0) }.joined(separator: " ")
            let firmware = payload.suffix(2).map { String(format: "%02x", $0) }.joined(separator: " ")
            return "Version bytes: hardware \(hardware), firmware \(firmware)"
        }
        return "\(command.title) payload: \(raw)"
    }
}

struct StockReadAccumulator {
    let command: StockReadCommand
    private(set) var bytes: [UInt8] = []

    var remainingCapacity: Int { command.expectedResponseLength - bytes.count }

    mutating func append(_ fragment: [UInt8]) throws -> StockReadResult? {
        let total = bytes.count + fragment.count
        guard total <= command.expectedResponseLength else {
            throw StockReadError.overlength(expected: command.expectedResponseLength, actual: total)
        }
        guard !fragment.isEmpty else { return nil }
        bytes.append(contentsOf: fragment)

        if let declared = bytes.first, Int(declared) != command.expectedResponseLength {
            throw StockReadError.wrongLength(expected: command.expectedResponseLength, declared: Int(declared))
        }
        if bytes.count >= 2, bytes[1] != 0x02 {
            throw StockReadError.unexpectedType(bytes[1])
        }
        if bytes.count >= 3, bytes[2] != command.rawValue {
            throw StockReadError.unexpectedCommand(expected: command.rawValue, actual: bytes[2])
        }
        if bytes.count >= 4, bytes[3] != command.expectedStatus {
            throw StockReadError.rejected(status: bytes[3])
        }
        guard bytes.count == command.expectedResponseLength else { return nil }
        if command == .newerUserKeyProbe, bytes.dropFirst(4).contains(where: { $0 != 0 }) {
            throw StockReadError.unexpectedProbePayload
        }
        return StockReadResult(command: command, payload: Array(bytes.dropFirst(4)))
    }
}
