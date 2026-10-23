import Foundation

enum StockExtensionError: Error {
    case invalidRecord, identity, framing, stale, rejected(UInt8), readback
    case busy, disconnected, timedOut
}

/// Typed three-layer map. This is not the four-layer QMK profile format.
struct StockExtensionMap: Equatable, Codable {
    struct Action: Equatable, Codable {
        var kind: UInt8 = 0
        var usage: UInt16 = 0
        var mods: UInt8 = 0
        var activation: UInt8 = 0
        var target: UInt8 = 0
        var tapUsage: UInt16 = 0
        var tapMilliseconds: UInt16 = 0
        static let oem = Action(kind: 3)
    }
    var nativeMask: UInt8
    var actions: [Action]

    static var oem: Self {
        // Use the same decoder and validation as a firmware record.
        var record: [UInt8] = [67,75,77,4,108,6]
        for layer in UInt8(0)..<3 { for pos in UInt8(0)..<36 { record += [pos,layer,0x22] } }
        return try! Self(record: record)
    }

    mutating func set(_ action: Action, layer: Int, row: Int, column: Int) throws {
        guard actions.count == 108, (0..<3).contains(layer), (0..<6).contains(row), (0..<6).contains(column) else {
            throw StockExtensionError.invalidRecord
        }
        var candidate = self
        candidate.actions[layer * 36 + row * 6 + column] = action
        _ = try candidate.encode()
        self = candidate
    }

    func encode() throws -> [UInt8] {
        guard actions.count == 108, nativeMask & ~6 == 0 else { throw StockExtensionError.invalidRecord }
        var bytes: [UInt8] = [67, 75, 77, 4, 0, nativeMask]
        for (index, a) in actions.enumerated() {
            guard a.kind <= 3, a.usage <= 255, a.mods & 0xF0 == 0,
                  a.activation <= 4, a.tapUsage <= 255,
                  a.activation == 0 || a.target < 3 else { throw StockExtensionError.invalidRecord }
            guard a.activation == 0 ? (a.target == 0 && a.tapUsage == 0 && a.tapMilliseconds == 0) : a.usage == 0 else {
                throw StockExtensionError.invalidRecord
            }
            if a.kind != 0 {
                guard a.usage == 0, a.mods == 0, a.activation == 0, a.target == 0,
                      a.tapUsage == 0, a.tapMilliseconds == 0 else { throw StockExtensionError.invalidRecord }
            }
            if a == Action() { continue }
            var flags: UInt8 = a.kind == 3 ? 0x22 : a.kind == 2 ? 3 : a.kind == 1 ? 2 : a.activation == 0 ? 0 : 1
            if a.mods != 0 { flags |= 4 }
            if a.activation != 0 && a.tapUsage != 0 { flags |= 8 }
            if a.activation != 0 && a.tapMilliseconds != 0 { flags |= 16 }
            bytes += [UInt8(index % 36), UInt8(index / 36), flags]
            if a.kind == 0 {
                if a.activation == 0 { bytes += Self.le(a.usage) }
                else { bytes += [a.activation << 4 | a.target] }
            }
            if flags & 4 != 0 { bytes += [a.mods] }
            if flags & 8 != 0 { bytes += Self.le(a.tapUsage) }
            if flags & 16 != 0 { bytes += Self.le(a.tapMilliseconds) }
            bytes[4] += 1
        }
        guard bytes.count <= 978 else { throw StockExtensionError.invalidRecord }
        return bytes
    }

    init(record: [UInt8]) throws {
        guard (6...978).contains(record.count), Array(record.prefix(4)) == [67,75,77,4],
              record[4] <= 108, record[5] & ~6 == 0 else { throw StockExtensionError.invalidRecord }
        nativeMask = record[5]
        actions = Array(repeating: Action(), count: 108)
        var cursor = 6
        var seen = Set<Int>()
        func byte() throws -> UInt8 {
            guard cursor < record.count else { throw StockExtensionError.invalidRecord }
            defer { cursor += 1 }
            return record[cursor]
        }
        func word() throws -> UInt16 { let lo = try byte(); return UInt16(lo) | UInt16(try byte()) << 8 }
        for _ in 0..<record[4] {
            let pos = Int(try byte()), layer = Int(try byte()), flags = try byte()
            let index = layer * 36 + pos
            guard pos < 36, layer < 3, seen.insert(index).inserted, flags & ~0x3F == 0 else {
                throw StockExtensionError.invalidRecord
            }
            var a = Action()
            switch flags & 3 {
            case 0:
                guard flags & 0x38 == 0 else { throw StockExtensionError.invalidRecord }
                a.usage = try word()
            case 1:
                guard flags & 0x20 == 0 else { throw StockExtensionError.invalidRecord }
                let packed = try byte(); a.activation = packed >> 4; a.target = packed & 15
                guard (1...4).contains(a.activation) else { throw StockExtensionError.invalidRecord }
            default:
                guard [UInt8(2), 3, 0x22].contains(flags) else { throw StockExtensionError.invalidRecord }
                a.kind = flags == 0x22 ? 3 : flags == 3 ? 2 : 1
            }
            if flags & 4 != 0 { a.mods = try byte() }
            if flags & 8 != 0 { a.tapUsage = try word() }
            if flags & 16 != 0 { a.tapMilliseconds = try word() }
            actions[index] = a
        }
        guard cursor == record.count else { throw StockExtensionError.invalidRecord }
        _ = try encode()
    }

    private static func le(_ value: UInt16) -> [UInt8] { [UInt8(value & 255), UInt8(value >> 8)] }
}

/// One complete operation owns the session, including its readback.
@MainActor
final class StockExtensionSession {
    private let transfer: ([UInt8]) async throws -> [UInt8]
    private let nonce: UInt32
    private var generation: UInt16 = 0
    private var recordLength = 0
    private(set) var identified = false
    private(set) var remappingEnabled = false
    private var busy = false
    private var epoch = 0

    init(nonce: UInt32, transfer: @escaping ([UInt8]) async throws -> [UInt8]) {
        self.nonce = nonce
        self.transfer = transfer
    }

    func open() async throws {
        guard !busy else { throw StockExtensionError.busy }
        busy = true
        defer { busy = false }
        try await handshake()
    }

    private func handshake() async throws {
        identified = false
        let caps = try await exchange(0)
        guard nonce != 0, caps.count == 15, caps[14] <= 1,
              Array(caps.prefix(12)) == [67,75,88,49,4,3,6,6,0xD2,3,24,1] else {
            throw StockExtensionError.identity
        }
        recordLength = Int(caps[12]) | Int(caps[13]) << 8
        remappingEnabled = caps[14] != 0
        guard (6...978).contains(recordLength) else { throw StockExtensionError.identity }
        identified = true
    }

    func read() async throws -> StockExtensionMap {
        guard !busy else { throw StockExtensionError.busy }
        busy = true
        defer { busy = false }
        return try await readMap()
    }

    private func readMap() async throws -> StockExtensionMap {
        guard identified else { throw StockExtensionError.identity }
        var record: [UInt8] = []
        while record.count < recordLength {
            let count = min(24, recordLength - record.count)
            let chunk = try await exchange(1, offset: record.count, payload: [UInt8(count)])
            guard chunk.count == count else { identified = false; throw StockExtensionError.framing }
            record += chunk
        }
        do { return try StockExtensionMap(record: record) }
        catch { identified = false; throw error }
    }

    func apply(_ map: StockExtensionMap, enabled: Bool) async throws {
        guard !busy else { throw StockExtensionError.busy }
        busy = true
        defer { busy = false }
        guard identified else { throw StockExtensionError.identity }
        let record = try map.encode()
        _ = try await exchange(2, offset: record.count)
        for offset in stride(from: 0, to: record.count, by: 24) {
            _ = try await exchange(3, offset: offset, payload: Array(record[offset..<min(offset + 24, record.count)]))
        }
        _ = try await exchange(4, payload: [enabled ? 1 : 0])
        try await handshake()
        let readback = try await readMap()
        guard remappingEnabled == enabled, readback == map else {
            identified = false; throw StockExtensionError.readback
        }
    }

    func abort() async throws {
        guard !busy else { throw StockExtensionError.busy }
        busy = true
        defer { busy = false }
        _ = try await exchange(5)
    }
    func close() { epoch += 1; identified = false }

    private func exchange(_ op: UInt8, offset: Int = 0, payload: [UInt8] = []) async throws -> [UInt8] {
        guard op == 0 || identified else { throw StockExtensionError.identity }
        let token = (0..<4).map { UInt8((nonce >> ($0 * 8)) & 255) }
        let request: [UInt8] = [UInt8(12 + payload.count), 0x94, 1, op] + token
            + [UInt8(generation & 255), UInt8(generation >> 8), UInt8(offset & 255), UInt8(offset >> 8)] + payload
        do {
            try Task.checkCancellation()
            let current = epoch
            let reply = try await transfer(request)
            try Task.checkCancellation()
            guard current == epoch else { throw StockExtensionError.disconnected }
            // Stock rejects unknown commands with a short reply, without an
            // extension nonce/header. Treat this as rejection, never identity.
            guard (4...36).contains(reply.count), Int(reply[0]) == reply.count,
                  Array(reply[1..<3]) == [2, 0x94] else { throw StockExtensionError.framing }
            guard reply[3] == 0 else { throw StockExtensionError.rejected(reply[3]) }
            guard (12...36).contains(reply.count), Int(reply[0]) == reply.count,
                  Array(reply[1..<3]) == [2,0x94], reply[4] == 1, reply[5] == op,
                  Array(reply[6..<10]) == token else { throw StockExtensionError.framing }
            guard op < 2 || reply.count == 12 else { throw StockExtensionError.framing }
            let next = UInt16(reply[10]) | UInt16(reply[11]) << 8
            if op != 0 {
                guard generation < UInt16.max, next == generation + (op == 4 ? 1 : 0) else {
                    throw StockExtensionError.stale
                }
            }
            generation = next
            return Array(reply.dropFirst(12))
        } catch {
            identified = false // Never retry a mutation after a lost acknowledgement.
            throw error
        }
    }
}
