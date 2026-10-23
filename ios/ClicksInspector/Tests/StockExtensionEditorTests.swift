import Foundation

private func check(_ value: Bool, _ message: String) throws {
    if !value { throw NSError(domain: message, code: 1) }
}

@main enum StockExtensionEditorTests {
    @MainActor static func main() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        let url = directory.appendingPathComponent("ExtensionProfiles.json")
        defer { try? FileManager.default.removeItem(at: directory) }
        let editor = StockExtensionController(profilesURL: url)
        let positions = KeyboardPresentation.rows.flatMap { $0 }.compactMap(\.position) + KeyboardPresentation.unverifiedSystemPositions
        try check(positions.count == 36 && Set(positions) == Set(0..<36), "physical presentation lost or duplicated a matrix position")
        try check(KeyboardPresentation.rows[0].map(\.id).joined() == "QWERTYUIOP", "keyboard uses electrical row order")
        try check(KeyboardPresentation.catalog.first { $0.category == "Modifiers" && $0.name == "Control" }?.action == .init(mods: 1), "modifier picker emitted a usage instead of modifier bits")
        var map = StockExtensionMap.oem
        try map.set(.init(usage: 0x29), layer: 2, row: 0, column: 0)
        try map.set(.init(usage: 0x2b), layer: 1, row: 2, column: 0)
        // The UI edits these same typed fields, with no second keymap model.
        try map.set(.init(mods: 8, activation: 1, target: 0, tapUsage: 0x2b, tapMilliseconds: 200), layer: 1, row: 0, column: 1)
        editor.draft = map
        editor.saveProfile(name: "Daily")
        let reopened = StockExtensionController(profilesURL: url)
        try check(reopened.draft == map, "offline draft did not survive reopening")
        try check(reopened.profiles.count == 1 && reopened.profiles[0].map == map, "phone profile did not survive reopening")
        reopened.loadProfile(reopened.profiles[0])
        try check(reopened.draft == map && !reopened.canApply, "loading a profile sent a command")
        let stored = try Data(contentsOf: url)
        var unknown = try JSONSerialization.jsonObject(with: stored) as! [String: Any]
        unknown["schemaVersion"] = 999
        let unsupported = try JSONSerialization.data(withJSONObject: unknown)
        try unsupported.write(to: url)
        let refused = StockExtensionController(profilesURL: url)
        refused.saveProfile(name: "Must not replace unknown data")
        try check(!refused.storageAvailable && (try Data(contentsOf: url)) == unsupported, "unknown profile file was overwritten")
        try stored.write(to: url)
        let untrusted = UntrustedLink()
        await refused.connect(untrusted, emulated: false)
        try check(untrusted.sends == 0 && untrusted.closed, "unverified accessory received a packet")
        try check(!reopened.ramChangesAuthorized, "RAM consent defaults on")

        if CommandLine.arguments.count == 2 {
            let endpoint = URL(string: CommandLine.arguments[1])!
            var link = V122EmulatorLink(url: endpoint)
            await reopened.connect(link, emulated: false, accessoryIdentityVerified: true)
            try check(reopened.connected, reopened.status)
            try check(!reopened.canApply, "verified identity bypassed RAM consent")
            await reopened.apply()
            reopened.ramChangesAuthorized = true
            try check(reopened.canApply, "RAM consent did not enable a verified extension")
            try await link.control("power")
            reopened.disconnect()
            try check(!reopened.ramChangesAuthorized, "disconnect retained RAM authorization")
            link = V122EmulatorLink(url: endpoint)
            await reopened.connect(link, emulated: true)
            try check(reopened.verified == .oem && reopened.draft == map && !reopened.matchesRAM, "RAM loss silently applied a draft")
            await reopened.apply()
            try check(reopened.matchesRAM, reopened.status)
            let hid = await reopened.verifyEmulator(link)
            try check(hid.hasPrefix("PASS NAV+Q"), hid)
            reopened.disconnect()
            link = V122EmulatorLink(url: endpoint)
            await reopened.connect(link, emulated: true)
            try check(reopened.matchesRAM, "link loss changed RAM")
            try await link.control("lose-ack")
            try reopened.draft.set(.init(usage: 0x2b, mods: 8), layer: 0, row: 0, column: 0)
            let changed = reopened.draft
            await reopened.apply()
            try check(!reopened.connected && reopened.verified == nil && reopened.status.contains("not confirmed"), "lost ack claimed success")
            reopened.disconnect()
            link = V122EmulatorLink(url: endpoint)
            await reopened.connect(link, emulated: true)
            try check(reopened.verified == changed, "lost ack readback did not recover committed RAM")
            reopened.disconnect()
            print("PASS editor -> fragmented async link -> actual V122 -> exact NAV+Q Esc / SYM+E Tab; named profiles, RAM loss, explicit reapply, lost-ack recovery")
        }
        print("PASS typed profiles, durable draft, unknown-file preservation, identity gate, and separate per-session RAM consent")
    }
}

private final class UntrustedLink: KeyboardPacketLink {
    var onFragment: (([UInt8]) -> Void)?
    var onClosed: ((String) -> Void)?
    var sends = 0
    var closed = false
    func send(_ bytes: [UInt8]) { sends += 1 }
    func close() { closed = true }
}
