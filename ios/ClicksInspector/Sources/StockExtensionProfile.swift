import Foundation

struct StockExtensionProfile: Codable, Identifiable, Equatable {
    var id = UUID()
    let name: String
    let map: StockExtensionMap
    let enabled: Bool

    func validate() throws {
        guard !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty, name.count <= 80 else {
            throw StockExtensionError.invalidRecord
        }
        _ = try map.encode()
    }
}

/// Separate from QMKProfiles.json. Invalid or unknown files are never replaced.
struct StockExtensionProfiles: Codable {
    var schemaVersion = 1
    var profiles: [StockExtensionProfile] = []
    var draft: StockExtensionProfile?

    static var fileURL: URL {
        let directory = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        #if targetEnvironment(simulator)
        if let id = ProcessInfo.processInfo.environment["EXTENSION_PROFILE_TEST_ID"], UUID(uuidString: id) != nil {
            return directory.appendingPathComponent("ExtensionProfiles-test-\(id).json")
        }
        #endif
        return directory.appendingPathComponent("ExtensionProfiles.json")
    }

    private func validate() throws {
        guard schemaVersion == 1, Set(profiles.map(\.id)).count == profiles.count else {
            throw StockExtensionError.invalidRecord
        }
        try profiles.forEach { try $0.validate() }
        try draft?.validate()
    }

    static func load(from url: URL) throws -> Self {
        guard FileManager.default.fileExists(atPath: url.path) else { return Self() }
        let library = try JSONDecoder().decode(Self.self, from: Data(contentsOf: url))
        try library.validate()
        return library
    }

    func save(to url: URL) throws {
        try validate()
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try JSONEncoder().encode(self).write(to: url, options: .atomic)
    }
}
