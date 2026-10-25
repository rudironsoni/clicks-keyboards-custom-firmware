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
    var writes = 0

    func close() { onFragment = nil; onClosed = nil }
    func send(_ bytes: [UInt8]) {
        requests.append(bytes)
        var response = [UInt8](repeating: 0, count: 8)
        let count = qmk_bridge_send(bytes, bytes.count, &response)
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
        CustomFirmwareController.resetPendingUpdateForTesting()
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: folder) }
        let url = folder.appendingPathComponent("profiles.json")
        let controller = CustomFirmwareController(profilesURL: url)
        controller.exchangeTimeoutNanos = 20_000_000
        qmk_bridge_boot()
        var link = try await connect(controller)
        precondition(controller.keymap == FactoryLayout.asRowMajor, "Swift factory map must match pinned production QMK")
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
        for layer in 0..<4 {
            controller.setKey(layer: layer, row: 0, column: 0, keycode: UInt16(0x04 + layer))
            try await wait(controller)
            precondition(controller.keymap[layer][0][0] == UInt16(0x04 + layer))
            precondition(link.requests.suffix(2).map { $0[1] } == [0x92, 0x91])
        }
        controller.saveProfile(name: "All four layers")
        let desired = controller.desiredProfile!
        let saved = try KeymapProfileLibrary.load(from: url)
        precondition(saved.profiles == [desired])
        controller.resetKeymap()
        try await wait(controller)
        precondition(controller.keymap == FactoryLayout.asRowMajor && controller.needsReapply)
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
        precondition(!link.requests.contains { [0x92, 0x93].contains($0[1]) })
        relaunched.ramWritesAllowed = true
        relaunched.applyDesiredProfile()
        // A second submission must not overlap the transaction.
        relaunched.resetKeymap()
        try await wait(relaunched)
        precondition(relaunched.keymap == desired.keymap && !relaunched.needsReapply)
        precondition(relaunched.verifiedWrites == 4)
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
    }
}
