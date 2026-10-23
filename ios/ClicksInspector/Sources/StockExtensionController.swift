import Foundation
import Combine

@MainActor
final class StockExtensionController: ObservableObject {
    @Published var draft = StockExtensionMap.oem { didSet { storeDraft() } }
    @Published var enabled = true { didSet { storeDraft() } }
    @Published var profileName = "My Custom Layout" { didSet { storeDraft() } }
    @Published var ramChangesAuthorized = false
    @Published private(set) var isEmulated = false
    @Published private(set) var verified: StockExtensionMap?
    @Published private(set) var connected = false
    @Published private(set) var busy = false
    @Published private(set) var applyUncertain = false
    @Published private(set) var status = "Edit offline. Your draft stays on this iPhone."
    @Published private(set) var profileStatus = "Draft on iPhone"
    @Published private(set) var profiles: [StockExtensionProfile] = []
    @Published private(set) var storageAvailable = true
    private var library = StockExtensionProfiles()
    private let profilesURL: URL
    private var transport: StockExtensionTransport?
    private var session: StockExtensionSession?
    private var generation = 0
    private var storageReady = false

    init(profilesURL: URL = StockExtensionProfiles.fileURL) {
        self.profilesURL = profilesURL
        do {
            library = try StockExtensionProfiles.load(from: profilesURL)
            profiles = library.profiles
            if let saved = library.draft {
                draft = saved.map
                enabled = saved.enabled
                profileName = saved.name
            }
            storageReady = true
        } catch {
            storageAvailable = false
            status = "Cannot read phone profiles. The file is preserved. \(error)"
            profileStatus = status
        }
    }

    var ownsSession: Bool { transport != nil }
    var canApply: Bool { connected && !busy && session?.identified == true && (isEmulated || ramChangesAuthorized) }
    var matchesRAM: Bool { verified == draft && session?.remappingEnabled == enabled }

    /// Identity permits capability reads only. RAM apply needs separate consent.
    /// This policy never grants permission to flash firmware.
    func connect(_ link: KeyboardPacketLink, emulated: Bool, accessoryIdentityVerified: Bool = false,
                 incomingFramer: KeyboardPacketFramer = .init(maximumLength: 36),
                 diagnostic: ((String, [UInt8], String?) -> Void)? = nil) async {
        #if os(macOS) || targetEnvironment(simulator)
        let allowed = emulated
        #else
        let allowed = false
        #endif
        guard allowed || (!emulated && accessoryIdentityVerified) else {
            link.close()
            status = "Identify an eligible CK-5200 before checking its programmable extension."
            return
        }
        guard !busy else { link.close(); return }
        disconnect()
        isEmulated = allowed
        let token = generation
        let adapter = StockExtensionTransport(link, incomingFramer: incomingFramer, diagnostic: diagnostic)
        transport = adapter
        adapter.onLoss = { [weak self] in
            self?.session?.close()
            self?.connected = false
            self?.verified = nil
            self?.ramChangesAuthorized = false
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
            applyUncertain = false
            // Reconnect reads RAM only. It never replaces or applies the draft.
            status = "Read all 108 positions. Apply is always manual."
        } catch {
            guard token == generation else { return }
            adapter.close()
            switch error {
            case StockExtensionError.framing:
                status = "The extension reply could not be validated. This does not prove stock firmware is running. Installation is not verified. Your phone draft is kept."
            case StockExtensionError.rejected(let code):
                status = String(format: "The running firmware rejected extension command 0x94 (status %02x). Installation is not verified. Do not flash again. Your phone draft is kept.", Int(code))
            case StockExtensionError.identity:
                status = "The reply did not match the required extension capabilities. Installation is not verified. Your phone draft is kept."
            default:
                status = "Could not verify the programmable extension. Your phone draft is kept. \(error)"
            }
        }
    }

    func disconnect() {
        generation += 1
        session?.close()
        transport?.close()
        transport = nil
        session = nil
        ramChangesAuthorized = false
        isEmulated = false
        connected = false
        verified = nil
        busy = false
        status = "Disconnected. Draft and phone profiles are kept."
    }

    func apply() async {
        guard canApply, let session else { return }
        let map = draft, applyEnabled = enabled, token = generation
        busy = true
        applyUncertain = false
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
            applyUncertain = true
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
            if let index = next.profiles.firstIndex(where: { $0.name == profile.name }) {
                next.profiles[index] = profile
            } else { next.profiles.append(profile) }
            next.draft = profile
            try next.save(to: profilesURL)
            library = next
            profiles = next.profiles
            profileName = profile.name
            profileStatus = "Saved on iPhone"
            status = "Saved \(profile.name) on this iPhone."
        } catch { status = "Profile was not saved. \(error)" }
    }

    func loadProfile(_ profile: StockExtensionProfile) {
        guard !busy else { return }
        do {
            try profile.validate()
            draft = profile.map
            enabled = profile.enabled
            profileName = profile.name
            profileStatus = "Saved profile loaded"
            status = "Loaded \(profile.name) into the draft. Apply is always manual."
        } catch { status = "Invalid profile. \(error)" }
    }

    private func storeDraft() {
        guard storageReady, storageAvailable else { return }
        do {
            let value = StockExtensionProfile(name: profileName.trimmingCharacters(in: .whitespacesAndNewlines), map: draft, enabled: enabled)
            try value.validate()
            var next = library
            next.draft = value
            try next.save(to: profilesURL)
            library = next
            profileStatus = "Draft saved on iPhone"
        } catch { profileStatus = "Draft not saved: \(error)" }
    }
}
