import Foundation

private final class CKeyboardLink: KeyboardPacketLink {
    var onFragment: (([UInt8]) -> Void)?
    var onClosed: ((String) -> Void)?
    var requests: [[UInt8]] = []
    var identity: [UInt8]?
    var loseWriteNumber: Int?
    var partialReplyCommand: UInt8?
    var closeWriteNumber: Int?
    var corruptNextRead = false
    var resetBeforeWriteNumber: Int?
    var ignoreCommand: UInt8?
    var writes = 0

    func close() { onFragment = nil; onClosed = nil }
    func send(_ bytes: [UInt8]) {
        requests.append(bytes)
        var response = [UInt8](repeating: 0, count: 8)
        if bytes[1] == 0x92, writes + 1 == resetBeforeWriteNumber { qmk_bridge_boot() }
        let count: Int
        if bytes[1] == ignoreCommand {
            response = [4, 2, bytes[1], 0, 0, 0, 0, 0]
            count = 4
        } else {
            count = qmk_bridge_send(bytes, bytes.count, &response)
        }
        if bytes[1] == 3, let identity { response = [8, 2, 3, 0] + identity }
        if bytes[1] == 0x91 && corruptNextRead {
            corruptNextRead = false
            response[5] ^= 1
        }
        if bytes[1] == 0x92 || bytes[1] == 0x93 {
            writes += 1
            if writes == closeWriteNumber { onClosed?("test disconnection after C applied write"); return }
            if writes == loseWriteNumber { return }
        }
        guard count > 0 else { return }
        if bytes[1] == partialReplyCommand {
            onFragment?(Array(response.prefix(2)))
            return
        }
        // Exercise fragmented replies without implementing any commands in Swift.
        onFragment?(Array(response.prefix(2)))
        onFragment?(Array(response[2..<count]))
    }
}

@main
struct QMKProfileTests {
    static func report() -> [UInt8] {
        var bytes = [UInt8](repeating: 0, count: 10)
        qmk_bridge_report(&bytes)
        return bytes
    }

    static func key(_ row: UInt8, _ column: UInt8, _ pressed: Bool, _ ms: UInt32 = 12) {
        qmk_bridge_key(row, column, pressed ? 1 : 0)
        qmk_bridge_ticks(ms)
    }

    static func expectKey(_ usage: UInt8) {
        key(0, 0, true)
        precondition(report() == [0, 0, usage, 0, 0, 0, 0, 0, 0, 0], "expected HID usage \(usage), got \(report())")
        print("HID r0c0 press \(report())")
        key(0, 0, false)
        precondition(report() == [UInt8](repeating: 0, count: 10))
        print("HID r0c0 release \(report())")
    }

    static func verifyFactoryBehavior() {
        for layer in 0..<4 {
            for position in 0..<36 {
                qmk_bridge_layer(UInt8(layer))
                qmk_bridge_ticks(300)
                qmk_bridge_clear_reports()
                let row = UInt8(position / 6), column = UInt8(position % 6)
                var code = FactoryLayout.allLayers[layer][position]
                if code == 1 { code = FactoryLayout.layer0[position] }
                var expected = [UInt8](repeating: 0, count: 10)
                switch code {
                case 0x7e40: expected[8] = 0xcf
                case 0x7e41: expected[8] = 0x9d; expected[9] = 2
                case 0x7e42: expected[8] = 0xb8
                case 0xe0...0xe7: expected[0] = 1 << UInt8(code - 0xe0)
                case 0x420d: expected[2] = 0x0d
                case 0x5281: break
                default:
                    expected[0] = UInt8(code >> 8)
                    expected[2] = UInt8(code & 0xff)
                }
                key(row, column, true, 80)
                key(row, column, false)
                var observed = [[UInt8]]()
                for index in 0..<qmk_bridge_report_count() {
                    var bytes = [UInt8](repeating: 0, count: 10)
                    qmk_bridge_report_at(index, &bytes)
                    observed.append(bytes)
                }
                let empty = [UInt8](repeating: 0, count: 10)
                var expectedReports = [expected, empty]
                if code == 0x5281 { expectedReports = [] }
                if (0x0200..<0x0300).contains(code) {
                    var modifierOnly = empty
                    modifierOnly[0] = 2
                    expectedReports = [modifierOnly, expected, modifierOnly, empty]
                }
                precondition(observed == expectedReports, "factory L\(layer) r\(row)c\(column) expected \(expectedReports), got \(observed)")
                precondition(report() == [UInt8](repeating: 0, count: 10), "factory key release must clear HID")
                print("HID factory L\(layer) r\(row)c\(column) code=\(String(code, radix: 16)) expected=\(expected) reports=\(observed)")
            }
        }
        qmk_bridge_layer(0)
    }

    @MainActor static func wait(_ controller: CustomFirmwareController) async throws {
        for _ in 0..<1000 {
            if !controller.keymapOperationRunning { return }
            try await Task.sleep(for: .milliseconds(2))
        }
        fatalError("controller did not finish")
    }

    @MainActor private static func connect(_ controller: CustomFirmwareController, _ link: CKeyboardLink = CKeyboardLink()) async throws -> CKeyboardLink {
        controller.connectedTarget = .init(model: "CK-5200", firmwareRevision: "QMK", hardwareRevision: "1.2.0")
        controller.attach(link)
        try await wait(controller)
        return link
    }

    @MainActor static func main() async throws {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: folder) }
        let url = folder.appendingPathComponent("profiles.json")
        let controller = CustomFirmwareController(profilesURL: url)
        controller.exchangeTimeoutNanos = 20_000_000
        qmk_bridge_boot()
        var link = try await connect(controller)
        precondition(controller.keymap == FactoryLayout.asRowMajor, "Swift factory map must match pinned production QMK")
        verifyFactoryBehavior()
        expectKey(0x14)
        precondition(controller.keymapIsVerified && !controller.ramWritesAllowed)
        for command: UInt8 in [0xa0, 0xa1, 0xa2, 0xa3, 0x82, 0x92, 0x93] {
            let before = link.requests.count
            do { try controller.dispatch([2, command]); fatalError("boundary permitted \(command)") }
            catch {}
            precondition(link.requests.count == before)
        }
        controller.ramWritesAllowed = true
        controller.runSelfTest()
        try await wait(controller)
        precondition(controller.selfTestReport?.passed == true)
        precondition(controller.keymap[0][2][3] == 0x00e1, "self-test must restore Left Shift")
        // A0..A3 stay blocked even with the runtime RAM switch enabled.
        for request in [CustomKeyboardCommand.rebootRequest,
                        CustomKeyboardCommand.beginUpdateRequest(size: 16),
                        CustomKeyboardCommand.writeUpdateRequest(offset: 0, chunk: [1, 2][...]),
                        CustomKeyboardCommand.finishUpdateRequest(size: 16)] {
            do { try controller.dispatch(request); fatalError("RAM gate authorized flashing") } catch {}
        }
        key(0, 0, true)
        controller.setKey(layer: 0, row: 0, column: 0, keycode: 4)
        try await wait(controller)
        key(0, 0, false)
        precondition(report() == [UInt8](repeating: 0, count: 10), "editing a held key must not leave its original HID usage pressed")
        precondition(!controller.keymapIsVerified && controller.verifiedWrites == 0, "held-key write must be rejected")
        link = try await connect(controller)
        precondition(controller.keymap == FactoryLayout.asRowMajor, "busy rejection must leave mappings unchanged")
        controller.ramWritesAllowed = true
        print("PASS held-key write rejected without changing RAM or stranding a HID usage")
        for (row, column): (UInt8, UInt8) in [(2, 3), (5, 2), (0, 5)] {
            key(row, column, true, 220)
            controller.resetKeymap()
            try await wait(controller)
            precondition(!controller.keymapIsVerified && controller.verifiedWrites == 0)
            key(row, column, false)
            precondition(report() == [UInt8](repeating: 0, count: 10))
            link = try await connect(controller)
            precondition(controller.keymap == FactoryLayout.asRowMajor)
            controller.ramWritesAllowed = true
        }
        expectKey(0x14)
        print("PASS held modifier, layer-tap, and consumer key reject reset and release cleanly")
        for layer in 0..<4 {
            controller.setKey(layer: layer, row: 0, column: 0, keycode: UInt16(0x04 + layer))
            try await wait(controller)
            precondition(controller.keymap[layer][0][0] == UInt16(0x04 + layer))
            precondition(link.requests.suffix(2).map { $0[1] } == [0x92, 0x91])
            qmk_bridge_layer(UInt8(layer))
            expectKey(UInt8(0x04 + layer))
        }
        qmk_bridge_layer(0)
        var wire = [UInt8](repeating: 0, count: 10)
        qmk_bridge_key(0, 0, 1)
        qmk_bridge_ticks(12)
        qmk_bridge_report(&wire)
        precondition(wire == [0, 0, 4, 0, 0, 0, 0, 0, 0, 0], "Swift edit must change the production QMK HID report")
        qmk_bridge_key(0, 0, 0)
        qmk_bridge_ticks(12)
        qmk_bridge_report(&wire)
        precondition(wire == [UInt8](repeating: 0, count: 10), "release must reach HID")
        controller.saveProfile(name: "All four layers")
        let desired = controller.desiredProfile!
        let saved = try KeymapProfileLibrary.load(from: url)
        precondition(saved.profiles == [desired])
        for layer in 0..<4 {
            for row in 0..<6 {
                for column in 0..<6 {
                    let replacement: UInt16 = FactoryLayout.asRowMajor[layer][row][column] == 4 ? 5 : 4
                    controller.setKey(layer: layer, row: row, column: column, keycode: replacement)
                    try await wait(controller)
                    precondition(controller.keymap[layer][row][column] == replacement)
                }
            }
        }
        precondition(controller.desiredProfile == desired, "RAM edits must not alter the saved phone profile")
        controller.resetKeymap()
        try await wait(controller)
        precondition(controller.keymap == FactoryLayout.asRowMajor && controller.needsReapply)
        verifyFactoryBehavior()
        expectKey(0x14)
        precondition(link.requests.filter { $0[1] == 0x93 }.count == 1)
        precondition(controller.desiredProfile == desired)
        print("PASS real Swift controller + production C/QMK: edit four layers, atomic JSON save, 0x93 restore and 144-key readback")

        // Relaunch + RAM loss: desired persists, device returns factory, no auto writes.
        controller.detach()
        qmk_bridge_boot()
        let relaunched = CustomFirmwareController(profilesURL: url)
        relaunched.exchangeTimeoutNanos = 20_000_000
        link = try await connect(relaunched)
        precondition(relaunched.needsReapply && relaunched.desiredProfile == desired)
        expectKey(0x14)
        precondition(!link.requests.contains { [0x92, 0x93].contains($0[1]) })
        relaunched.ramWritesAllowed = true
        relaunched.applyDesiredProfile()
        // A second submission must not overlap the transaction.
        relaunched.resetKeymap()
        try await wait(relaunched)
        precondition(relaunched.keymap == desired.keymap && !relaunched.needsReapply)
        precondition(relaunched.verifiedWrites == 4)
        expectKey(0x04)
        precondition(!link.requests.contains { $0[1] == 0x93 })
        print("PASS relaunch/reconnect compares durable desired profile, never auto-applies, manually reapplies with per-write readback")
        let staleClosure = link.onClosed
        let staleFragment = link.onFragment
        link = try await connect(relaunched)
        staleClosure?("late close from an old session")
        staleFragment?([4, 2, 0x92, 0])
        precondition(relaunched.keymapIsVerified && relaunched.hasQMKIdentity)

        // Lost second write reply: C did apply it, but Swift can only claim one.
        qmk_bridge_boot()
        link = try await connect(relaunched)
        link.loseWriteNumber = 2
        relaunched.ramWritesAllowed = true
        relaunched.applyDesiredProfile()
        try await wait(relaunched)
        precondition(relaunched.verifiedWrites == 1 && !relaunched.keymapIsVerified)
        precondition(relaunched.profileMessage.contains("1/4"))
        let before = link.requests.count
        relaunched.identify()
        precondition(link.requests.count == before, "ambiguous session must require reconnect")
        link = try await connect(relaunched)
        precondition(relaunched.keymap[0][0][0] == 4 && relaunched.keymap[1][0][0] == 5)
        precondition(relaunched.needsReapply && !relaunched.ramWritesAllowed)
        relaunched.ramWritesAllowed = true
        relaunched.applyDesiredProfile()
        try await wait(relaunched)
        precondition(relaunched.verifiedWrites == 2 && relaunched.keymap == desired.keymap)
        print("PASS lost reply: partial progress retained, no retry on ambiguous stream, reconnect reads actual RAM, manual resume applies only missing keys")

        qmk_bridge_boot()
        link = try await connect(relaunched)
        link.resetBeforeWriteNumber = 3
        relaunched.ramWritesAllowed = true
        relaunched.applyDesiredProfile()
        try await wait(relaunched)
        precondition(!relaunched.keymapIsVerified, "reset during apply must not report a complete profile")
        link = try await connect(relaunched)
        precondition(relaunched.needsReapply)
        precondition(relaunched.keymap[0][0][0] == 0x14 && relaunched.keymap[2][0][0] == 6)
        expectKey(0x14)
        relaunched.ramWritesAllowed = true
        relaunched.applyDesiredProfile()
        try await wait(relaunched)
        precondition(relaunched.keymapIsVerified && !relaunched.needsReapply)
        expectKey(4)
        print("PASS unexpected reset during profile apply cannot claim success; reconnect exposes actual partial RAM")

        link.ignoreCommand = 0x93
        relaunched.resetKeymap()
        try await wait(relaunched)
        precondition(!relaunched.keymapIsVerified && relaunched.verifiedWrites == 0)
        link = try await connect(relaunched)
        precondition(relaunched.keymap == desired.keymap)
        expectKey(4)
        relaunched.ramWritesAllowed = true
        print("PASS reset acknowledgment without restoration fails full-layout verification")

        // A fragmented read reply must still time out, rather than cancel its timer.
        link.partialReplyCommand = 0x91
        relaunched.setKey(layer: 0, row: 0, column: 1, keycode: 0x30)
        try await wait(relaunched)
        precondition(!relaunched.keymapIsVerified && relaunched.verifiedWrites == 0)
        link = try await connect(relaunched)
        precondition(relaunched.keymap[0][0][1] == 0x30)
        relaunched.ramWritesAllowed = true
        link.closeWriteNumber = 1
        relaunched.resetKeymap()
        try await wait(relaunched)
        precondition(!relaunched.hasQMKIdentity && !relaunched.ramWritesAllowed)
        precondition(relaunched.profileMessage.contains("last write may have reached RAM"))
        link = try await connect(relaunched)
        precondition(relaunched.keymap == FactoryLayout.asRowMajor && relaunched.needsReapply)
        print("PASS truncated reply timeout and disconnection after reset; reconnect verifies the real C result")
        relaunched.ramWritesAllowed = true
        link.corruptNextRead = true
        relaunched.setKey(layer: 0, row: 0, column: 1, keycode: 0x30)
        try await wait(relaunched)
        precondition(!relaunched.keymapIsVerified && relaunched.verifiedWrites == 0)
        precondition(relaunched.profileMessage.contains("readback does not match"))
        print("PASS mismatched readback cannot report write success")

        // Self-test must retain the first confirmed write if restoration loses the link.
        qmk_bridge_boot()
        link = try await connect(relaunched)
        relaunched.ramWritesAllowed = true
        link.closeWriteNumber = 2
        relaunched.runSelfTest()
        try await wait(relaunched)
        precondition(relaunched.verifiedWrites == 1 && !relaunched.keymapIsVerified)
        precondition(relaunched.profileMessage.contains("1/2"))
        precondition(link.writes == 2)
        print("PASS self-test retains confirmed progress when restoration loses its reply")

        for identity: [UInt8] in [[1, 0x20, 1, 0x22], [0x52, 0x53, 0, 1], [0x51, 0x4d, 0, 2]] {
            let wrong = CKeyboardLink()
            wrong.identity = identity
            _ = try await connect(relaunched, wrong)
            relaunched.ramWritesAllowed = true
            for request in [CustomKeyboardCommand.infoRequest, CustomKeyboardCommand.getKeyRequest(layer: 0, row: 0, column: 0),
                            CustomKeyboardCommand.setKeyRequest(layer: 0, row: 0, column: 0, keycode: 4), CustomKeyboardCommand.resetRequest] {
                do { try relaunched.dispatch(request); fatalError("wrong identity authorized custom command") } catch {}
            }
            precondition(wrong.requests.allSatisfy { $0[1] == 3 })
        }
        for target in [CustomFirmwareController.ConnectedTarget(model: "CK-5200", firmwareRevision: "QMK", hardwareRevision: "1.3.0"),
                       .init(model: "Other", firmwareRevision: "QMK", hardwareRevision: "1.2.0")] {
            relaunched.connectedTarget = target
            let wrong = CKeyboardLink()
            relaunched.attach(wrong)
            try await wait(relaunched)
            precondition(wrong.requests.isEmpty)
        }
        print("PASS transport boundary blocks stock, RS rescue, unknown QM version, wrong hardware/model, and A0-A3")

        var invalidProfile = try JSONSerialization.jsonObject(with: JSONEncoder().encode(desired)) as! [String: Any]
        invalidProfile["rows"] = 5
        let invalid = try JSONDecoder().decode(KeymapProfile.self, from: JSONSerialization.data(withJSONObject: invalidProfile))
        do { try invalid.validate(); fatalError("wrong dimensions accepted") } catch {}

        var malformed = try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as! [String: Any]
        malformed["schemaVersion"] = 99
        try JSONSerialization.data(withJSONObject: malformed).write(to: url)
        do { _ = try KeymapProfileLibrary.load(from: url); fatalError("unknown schema accepted") } catch {}
        let damaged = CustomFirmwareController(profilesURL: url)
        _ = try await connect(damaged)
        damaged.saveProfile(name: "Do not overwrite damaged file")
        precondition(damaged.profiles.isEmpty)
        let preserved = try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as! [String: Any]
        precondition(preserved["schemaVersion"] as? Int == 99)
        print("PASS unsupported JSON version is reported and preserved")
        let corruptBytes = Data("not a JSON profile".utf8)
        try corruptBytes.write(to: url)
        let corrupt = CustomFirmwareController(profilesURL: url)
        _ = try await connect(corrupt)
        corrupt.saveProfile(name: "Cannot replace corrupt bytes")
        precondition(corrupt.profiles.isEmpty && corrupt.desiredProfile == nil)
        let preservedCorruptBytes = try Data(contentsOf: url)
        precondition(preservedCorruptBytes == corruptBytes)
        print("PASS corrupt saved profiles remain unchanged and cannot report save success")
    }
}
