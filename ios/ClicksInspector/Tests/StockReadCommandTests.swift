import Foundation

func fragmentedVersionAndAllFixedGetters() throws {
    var version = StockReadAccumulator(command: .version)
    let first = try version.append([0x08, 0x02])
    precondition(first == nil)
    let second = try version.append([0x03, 0x00, 0x01])
    precondition(second == nil)
    let result = try version.append([0x20, 0x01, 0x22])
    precondition(result?.summary == "Version bytes: hardware 01 20, firmware 01 22")

    for command in StockReadCommand.allCases {
        if command == .checksumService || command == .newerUserKeyProbe { continue }
        precondition(command.request == [0x02, command.rawValue])
        let payload = [UInt8](repeating: 0x5a, count: command.expectedResponseLength - 4)
        let response = [UInt8(command.expectedResponseLength), 0x02, command.rawValue, 0x00] + payload
        var accumulator = StockReadAccumulator(command: command)
        let decoded = try accumulator.append(response)
        precondition(decoded?.command == command)
        precondition(decoded?.payload == payload)
    }
    precondition(StockReadCommand.checksumService.request == [5, 8, 0, 0, 0])
    var service = StockReadAccumulator(command: .checksumService)
    let acknowledgement = try service.append([4, 2, 8, 0])
    precondition(acknowledgement?.payload == [])
    precondition(StockReadCommand.newerUserKeyProbe.request == [6, 0x8f, 0, 0, 0, 0])
    var unsupported = StockReadAccumulator(command: .newerUserKeyProbe)
    let rejection = try unsupported.append([8, 2, 0x8f, 0xff, 0, 0, 0, 0])
    precondition(rejection?.summary == "Newer user-key command is unsupported (status ff); no keymap read")
}

func malformedResponsesAreRejected() {
    func expect(_ command: StockReadCommand, _ response: [UInt8], _ expected: StockReadError) {
        var accumulator = StockReadAccumulator(command: command)
        do {
            _ = try accumulator.append(response)
            preconditionFailure("Malformed stock response was accepted")
        } catch let actual as StockReadError {
            precondition(actual == expected)
        } catch {
            preconditionFailure("Unexpected error: \(error)")
        }
    }

    expect(.brightness, [0x04], .wrongLength(expected: 5, declared: 4))
    expect(.brightness, [0x05, 0x03], .unexpectedType(0x03))
    expect(.brightness, [0x05, 0x02, 0x86], .unexpectedCommand(expected: 0x84, actual: 0x86))
    expect(.brightness, [0x05, 0x02, 0x84, 0x01], .rejected(status: 0x01))
    expect(.brightness, [0x05, 0x02, 0x84, 0x00, 0x00, 0x00], .overlength(expected: 5, actual: 6))
    expect(.newerUserKeyProbe, [8, 2, 0x8f, 0], .rejected(status: 0))
    expect(.newerUserKeyProbe, [8, 2, 0x8f, 0xff, 0, 0, 0, 1], .unexpectedProbePayload)
}

@main
struct StockReadCommandTests {
    static func main() throws {
        try fragmentedVersionAndAllFixedGetters()
        malformedResponsesAreRejected()
        print("2 stock read checks passed")
    }
}
