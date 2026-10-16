import Foundation

/// Phone storage is durable. The keyboard's copy exists only in RAM.
struct KeymapProfile: Codable, Equatable, Identifiable {
    let schemaVersion: Int
    let id: UUID
    let name: String
    let model: String
    let hardwareRevision: String
    let layers: Int
    let rows: Int
    let columns: Int
    let keymap: [[[UInt16]]]

    init(name: String, keymap: [[[UInt16]]]) throws {
        schemaVersion = 1
        id = UUID()
        self.name = name.trimmingCharacters(in: .whitespacesAndNewlines)
        model = "CK-5200"
        hardwareRevision = "1.2.0"
        layers = 4
        rows = 6
        columns = 6
        self.keymap = keymap
        try validate()
    }

    func validate() throws {
        guard schemaVersion == 1, !name.isEmpty, name.count <= 80,
              model == "CK-5200", hardwareRevision == "1.2.0",
              layers == 4, rows == 6, columns == 6,
              keymap.count == layers,
              keymap.allSatisfy({ $0.count == rows && $0.allSatisfy { $0.count == columns } }) else {
            throw CustomKeyboardError.invalidProfile
        }
    }
}

struct KeymapProfileLibrary: Codable {
    var schemaVersion = 1
    var profiles: [KeymapProfile] = []
    var desiredID: UUID?

    static var fileURL: URL {
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        #if targetEnvironment(simulator)
        if let id = ProcessInfo.processInfo.environment["QMK_PROFILE_TEST_ID"], UUID(uuidString: id) != nil {
            return documents.appendingPathComponent("QMKProfiles-test-\(id).json")
        }
        #endif
        return documents.appendingPathComponent("QMKProfiles.json")
    }

    static func load(from url: URL) throws -> Self {
        guard FileManager.default.fileExists(atPath: url.path) else { return Self() }
        let value = try JSONDecoder().decode(Self.self, from: Data(contentsOf: url))
        guard value.schemaVersion == 1,
              Set(value.profiles.map(\.id)).count == value.profiles.count,
              value.desiredID == nil || value.profiles.contains(where: { $0.id == value.desiredID }) else {
            throw CustomKeyboardError.invalidProfile
        }
        try value.profiles.forEach { try $0.validate() }
        return value
    }

    func save(to url: URL) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(self).write(to: url, options: .atomic)
    }
}
