/*
 * Runnable host check for the custom-firmware protocol layer.
 * Compiled and executed by ios/ClicksInspector/check.sh:
 *   swiftc Sources/CustomKeyboardProtocol.swift Tests/ck5200_protocol_check.swift -o /tmp/ck5200_protocol_check
 */

import Foundation

private func expectEqual<T: Equatable>(_ what: String, _ actual: T, _ expected: T) {
    precondition(actual == expected, "\(what): expected \(expected), got \(actual)")
}


@main
struct ProtocolCheck {
    static func main() {
    // Requests.
    expectEqual("version request", CustomKeyboardCommand.versionRequest, [0x02, 0x03])
    expectEqual("info request", CustomKeyboardCommand.infoRequest, [0x02, 0x90])
    expectEqual("reset request", CustomKeyboardCommand.resetRequest, [0x02, 0x93])
    expectEqual("get key request",
                CustomKeyboardCommand.getKeyRequest(layer: 1, row: 2, column: 3),
                [0x05, 0x91, 0x01, 0x02, 0x03])
    expectEqual("set key request",
                CustomKeyboardCommand.setKeyRequest(layer: 1, row: 2, column: 3, keycode: 0x1234),
                [0x07, 0x92, 0x01, 0x02, 0x03, 0x12, 0x34])
    expectEqual("begin request", CustomKeyboardCommand.beginUpdateRequest(size: 0x1234), [0x06, 0xa1, 0x00, 0x00, 0x12, 0x34])
        expectEqual("write request",
                    CustomKeyboardCommand.writeUpdateRequest(offset: 0x20, chunk: [UInt8](repeating: 0xaa, count: 32)[...]),
                    [0x26, 0xa2, 0x00, 0x00, 0x00, 0x20] + [UInt8](repeating: 0xaa, count: 32))
    expectEqual("finish request", CustomKeyboardCommand.finishUpdateRequest(size: 0x1234), [0x06, 0xa3, 0x00, 0x00, 0x12, 0x34])

    // Identity.
    expectEqual("custom firmware payload recognized",
                CustomKeyboardIdentity.isCustomFirmware([0x51, 0x4d, 0x00, 0x01]), true)
    expectEqual("stock version payload not custom",
                CustomKeyboardIdentity.isCustomFirmware([0x01, 0x20, 0x01, 0x22]), false)
    expectEqual("info parsed",
                CustomKeyboardIdentity.info(fromInfoPayload: [4, 6, 6, 0]),
                KeyboardInfo(layers: 4, rows: 6, columns: 6))

    // Accumulator: happy path in fragments.
    var getExchange = CustomKeyboardExchange.getKey(layer: 0, row: 1, column: 2)
    var payload: [UInt8]? = try! getExchange.accumulator.append([0x06, 0x02, 0x91, 0x00])
    expectEqual("partial append yields nothing", payload == nil, true)
    getExchange = getExchange.replacingAccumulator(getExchange.accumulator)
    payload = try! getExchange.accumulator.append([0x12, 0x34])
    expectEqual("completed payload", payload ?? [], [0x12, 0x34])

    // Accumulator: rejection cases on reply packets [len, type, cmd, status].
    func rejected(command: UInt8, expectedLength: Int, reply: [UInt8]) -> CustomKeyboardError? {
        var accumulator = CustomKeyboardAccumulator(command: command, expectedLength: expectedLength)
        do {
            _ = try accumulator.append(reply)
            return nil
        } catch let error as CustomKeyboardError {
            return error
        } catch {
            return nil
        }
    }
    expectEqual("wrong length byte rejected",
                rejected(command: 0x92, expectedLength: 4, reply: [0x05, 0x02, 0x92, 0x00]),
                CustomKeyboardError.wrongLength(expected: 4, declared: 5))
    expectEqual("wrong type rejected",
                rejected(command: 0x92, expectedLength: 4, reply: [0x04, 0x03, 0x92, 0x00]),
                CustomKeyboardError.unexpectedType(0x03))
    expectEqual("wrong command rejected",
                rejected(command: 0x92, expectedLength: 4, reply: [0x04, 0x02, 0x91, 0x00]),
                CustomKeyboardError.unexpectedCommand(expected: 0x92, actual: 0x91))
    expectEqual("nonzero status rejected",
                rejected(command: 0x92, expectedLength: 4, reply: [0x04, 0x02, 0x92, 0x01]),
                CustomKeyboardError.rejected(status: 0x01))

    // Update chunker.
    do {
        let image = [UInt8](0..<100)
        var chunker = try UpdateChunker(image: image)
        expectEqual("chunker starts unfinished", chunker.finished, false)
        var chunks: [[UInt8]] = []
        var accepted = 0
        while let next = chunker.nextExchange {
            chunks.append(next.request)
            accepted = min(accepted + (next.request.count - 6), 100)
            try chunker.applyAcceptedOffset(UInt32(accepted))
        }
        expectEqual("chunk count", chunks.count, 4)
        expectEqual("finished", chunker.finished, true)
        expectEqual("first chunk size", chunks[0].count, 38)
        expectEqual("last chunk size", chunks[3].count, 10)
    } catch {
        preconditionFailure("chunker check threw: \(error)")
    }
    do {
        _ = try UpdateChunker(image: [UInt8](repeating: 0, count: CustomKeyboardCommand.maximumImageSize + 1))
        preconditionFailure("oversized image must throw")
    } catch let error as CustomKeyboardError {
        expectEqual("oversized image error", error, CustomKeyboardError.imageTooLarge(size: CustomKeyboardCommand.maximumImageSize + 1))
    } catch {
        preconditionFailure("unexpected error: \(error)")
    }

    print("ok")
    }
}
