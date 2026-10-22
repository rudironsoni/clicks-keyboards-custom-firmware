import Foundation
import Combine

@MainActor
final class StockExtensionController: ObservableObject {
    // R3 and physical extension-command authorization are not met.
    static let physicalCommandsAllowed = false
    @Published var draft = StockExtensionMap.oem
    @Published var enabled = true
    @Published private(set) var verified: StockExtensionMap?
    @Published private(set) var connected = false
    @Published private(set) var busy = false
    @Published private(set) var status = "Offline draft. Physical commands are blocked."
    @Published private(set) var profiles: [StockExtensionProfile] = []
    @Published private(set) var storageAvailable = true
    private var library = StockExtensionProfiles()
    private let profilesURL: URL
    private var transport: StockExtensionTransport?
    private var session: StockExtensionSession?
    private var generation = 0

    init(profilesURL: URL = StockExtensionProfiles.fileURL) {
        self.profilesURL = profilesURL
        do {
            library = try StockExtensionProfiles.load(from: profilesURL)
            profiles = library.profiles
        } catch {
            storageAvailable = false
            status = "Cannot read phone profiles. The file is preserved. \(error)"
        }
    }

    var ownsSession: Bool { transport != nil }
    var canApply: Bool { connected && !busy && session?.identified == true }
    var matchesRAM: Bool { verified == draft && session?.remappingEnabled == enabled }

    func connect(_ link: KeyboardPacketLink, emulated: Bool) async {
        #if os(macOS) || targetEnvironment(simulator)
        let allowed = emulated
        #else
        let allowed = false
        #endif
        guard allowed || Self.physicalCommandsAllowed else {
            link.close()
            status = "Physical extension commands are blocked."
            return
        }
        guard !busy else { link.close(); return }
        disconnect()
        let token = generation
        let adapter = StockExtensionTransport(link)
        transport = adapter
        adapter.onLoss = { [weak self] in
            self?.session?.close()
            self?.connected = false
            self?.verified = nil
            if self?.busy == false { self?.status = "Connection lost. Reconnect to read RAM. Apply is always manual." }
        }
        let client = StockExtensionSession(nonce: UInt32.random(in: 1...UInt32.max), transfer: adapter.exchange)
        session = client
        busy = true
        defer { if token == generation { busy = false } }
        do {
            try await client.open()
            let map = try await client.read()
            guard token == generation else { return }
            verified = map
            connected = true
            // Reconnect reads RAM only. It never replaces or applies the draft.
            status = "Read all 108 positions. Apply is always manual."
        } catch {
            guard token == generation else { return }
            adapter.close()
            status = "Connection failed. Stock V122 cannot be edited. \(error)"
        }
    }

    func disconnect() {
        generation += 1
        session?.close()
        transport?.close()
        transport = nil
        session = nil
        connected = false
        verified = nil
        busy = false
        status = "Disconnected. Draft and phone profiles are kept."
    }

    func apply() async {
        guard canApply, let session else { return }
        let map = draft, applyEnabled = enabled, token = generation
        busy = true
        status = "Applying the complete RAM map."
        defer { if token == generation { busy = false } }
        do {
            try await session.apply(map, enabled: applyEnabled)
            guard token == generation else { return }
            verified = map
            status = "Applied and read back all 108 positions in RAM."
        } catch {
            guard token == generation else { return }
            transport?.close()
            status = "Apply not confirmed. RAM may contain the new map. Reconnect and read. No automatic retry. \(error)"
        }
    }

    func useReadback() {
        guard !busy, let verified else { return }
        draft = verified
        enabled = session?.remappingEnabled ?? false
    }

    #if os(macOS) || targetEnvironment(simulator)
    func verifyEmulator(_ link: V122EmulatorLink) async -> String {
        guard canApply else { return "The editor is not ready." }
        busy = true
        let token = generation
        defer { if token == generation { busy = false } }
        do { return try await link.verifyHID() }
        catch { return "HID check failed: \(error)" }
    }
    #endif

    func saveProfile(name: String) {
        guard !busy, storageAvailable else { return }
        do {
            let profile = StockExtensionProfile(name: name.trimmingCharacters(in: .whitespacesAndNewlines), map: draft, enabled: enabled)
            try profile.validate()
            var next = library
            next.profiles.append(profile)
            try next.save(to: profilesURL)
            library = next
            profiles = next.profiles
            status = "Saved \(profile.name) on this iPhone."
        } catch { status = "Profile was not saved. \(error)" }
    }

    func loadProfile(_ profile: StockExtensionProfile) {
        guard !busy else { return }
        do {
            try profile.validate()
            draft = profile.map
            enabled = profile.enabled
            status = "Loaded \(profile.name) into the draft. Apply is always manual."
        } catch { status = "Invalid profile. \(error)" }
    }
}
