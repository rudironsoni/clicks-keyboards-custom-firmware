import Foundation

enum StockLabelEvidence: String, Codable {
    case inferredFromStaticDecode, unresolved, conflictingSources
}

struct StockPhysicalKey: Codable, Identifiable {
    let id: String
    let row: Int?
    let column: Int?
    let physicalLabel: String
    let labelEvidence: StockLabelEvidence
    let evidence: String
    let base: String
    let sym: String
    let cursor: String
}

extension FactoryLayout {
    /// Immutable historical M1 v1 catalogue from 246f2b8. These arrays and labels
    /// preserve the meaning of accepted captures when coverage is regenerated.
    /// The Q conflict and unmapped Shift statements below are historical, not
    /// current matrix claims. Current M3 firmware uses FactoryLayout's own arrays.
    static let stockPhysicalKeys: [StockPhysicalKey] = {
        let layer0: [UInt16] = [
            0x0004, 0x0018, 0x000e, 0x002a, 0x0028, 0x5da1,
            0x001a, 0x000c, 0x0007, 0x000f, 0x0019, 0x5da2,
            0x0008, 0x0012, 0x0009, 0x0000, 0x0005, 0xe3,
            0x0015, 0x0013, 0x000a, 0x001d, 0x0011, 0x002c,
            0x0017, 0x0004, 0x000b, 0x001b, 0x0010, 0xe0,
            0x001c, 0x0016, 0x420d, 0x0006, 0x5281, 0x5da3,
        ]
        let layer1: [UInt16] = [
            0x001e, 0x0024, 0x0034, 0x0001, 0x0001, 0x0001,
            0x001f, 0x0025, 0x0033, 0x0034, 0x0038, 0x0001,
            0x0020, 0x0026, 0x0033, 0x0000, 0x001e, 0x0001,
            0x0021, 0x0027, 0x0026, 0x0001, 0x0036, 0x0001,
            0x0022, 0x002d, 0x0027, 0x0024, 0x0037, 0x0001,
            0x0023, 0x0038, 0x0001, 0x001f, 0x0001, 0x0001,
        ]
        let layer2: [UInt16] = [
            0x0001, 0x0001, 0x0051, 0x0001, 0x0001, 0x0001,
            0x0052, 0x0052, 0x004f, 0x004f, 0x0001, 0x0001,
            0x0001, 0x0001, 0x0001, 0x0000, 0x0001, 0x0001,
            0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
            0x0001, 0x0050, 0x0001, 0x0001, 0x0001, 0x0001,
            0x0001, 0x0051, 0x0001, 0x0001, 0x0001, 0x0001,
        ]
        var keys = layer0.indices.compactMap { index -> StockPhysicalKey? in
            guard layer0[index] != 0 else { return nil } // Historical v1 omits r2c3.
            let row = index / 6, col = index % 6
            var base = KeycodeNames.name(for: layer0[index])
            var label = base
            var confidence = StockLabelEvidence.inferredFromStaticDecode
            if index == 0 {
                label = "Q (mapping conflict)"
                base = "Static decode says Q; custom layer0 encodes HID A. Physical check required."
                confidence = .conflictingSources
            } else if index == 32 {
                label = "J"
                base = "J; stock mode logic is not the custom LT(2,J) behavior"
            } else if index == 34 {
                label = "123/SYM (candidate)"
                base = "Sticky companion; one-shot SYM mechanics at stock 0x2876"
            } else if [5, 11, 35].contains(index) {
                label = "Unknown consumer cap r\(row)c\(col)"
                base = "Consumer bit \([5: 2, 11: 1, 35: 0][index]!), printed label and usage unresolved"
                confidence = .unresolved
            }
            func layerValue(_ values: [UInt16]) -> String {
                values[index] == 1 ? "No separate mapping in static decode" : KeycodeNames.name(for: values[index])
            }
            let sym = index == 21 ? "Configurable stock byte 0x11e; shift interplay at 0x2df4"
                : index == 32 ? "Clicks key behavior from stock byte 0x11f; semantics unverified"
                : layerValue(layer1)
            let cursor = index == 32 ? "Left" : layerValue(layer2)
            return StockPhysicalKey(id: "r\(row)c\(col)", row: row, column: col, physicalLabel: label,
                labelEvidence: confidence, evidence: "STOCK_DECODE.md#keymap-stock-0x2b1c0x3010; FactoryLayout.swift (custom adaptations distinguished)",
                base: base, sym: sym, cursor: cursor)
        }
        keys.append(StockPhysicalKey(id: "unmapped-shift", row: nil, column: nil,
            physicalLabel: "Shift (matrix location unknown)", labelEvidence: .unresolved,
            evidence: "Shift interplay mentioned at stock 0x2df4; no verified cap-to-matrix mapping",
            base: "Shift behavior requires observation", sym: "Unknown", cursor: "Unknown"))
        return keys
    }()
}

enum StockTrialAction: String, Codable, CaseIterable {
    case single, shift, command, control, sym, cursor, hold, consumerChord
}

struct StockTrialDefinition: Codable, Identifiable {
    let id: String
    let catalogVersion: String
    let key: StockPhysicalKey
    let action: StockTrialAction
    let expectedAction: String
    let expectedBehavior: String
}

enum StockTrialCatalog {
    static let version = "CK-5200-V122-H120-trials-v1"
    static let all: [StockTrialDefinition] = FactoryLayout.stockPhysicalKeys.flatMap { key in
        var actions: [StockTrialAction] = key.row == nil ? [.single] : [.single, .shift, .command, .control, .sym, .cursor, .hold]
        if key.id == "r1c5" { actions.append(.consumerChord) }
        return actions.map { action in
            let instruction: String
            let expected: String
            switch action {
            case .single:
                instruction = "Press and release \(key.physicalLabel) once."
                expected = key.base
            case .hold:
                instruction = "Hold \(key.physicalLabel) for two seconds, then release."
                expected = "Record repeat and release; firmware repeat timing is unverified."
            case .sym:
                instruction = "Activate stock 123/SYM, then press and release \(key.physicalLabel)."
                expected = key.sym
            case .cursor:
                instruction = "Activate stock cursor mode if its control is known, then press and release \(key.physicalLabel). Otherwise mark unobservable. Do not assume custom hold-J activation."
                expected = key.cursor
            case .consumerChord:
                instruction = "Only after identifying both caps, press r1c5 with r5c4, then release both. Otherwise mark unobservable."
                expected = "Stock decode associates this chord with consumer bit 1; usage unresolved."
            case .shift, .command, .control:
                instruction = "Hold \(action.rawValue), press and release \(key.physicalLabel), then release the modifier."
                expected = "Observe modifier interaction; iOS may consume the shortcut. Base candidate: \(key.base)"
            }
            return StockTrialDefinition(id: "\(version)/\(key.id)/\(action.rawValue)", catalogVersion: version,
                key: key, action: action, expectedAction: instruction + " Use test text only. If the cap is unknown, do not guess.",
                expectedBehavior: expected)
        }
    }
    static func find(_ id: String) -> StockTrialDefinition? { all.first { $0.id == id } }
}

enum StockTrialStatus: String, Codable, CaseIterable {
    case active, completed, failed, ambiguous, unobservable
}

struct StockTrialRun: Codable {
    let id: Int
    let trialID: String
    let startedMS: Double
    var endedMS: Double?
    var status: StockTrialStatus = .active
}

struct StockTrialCoverage: Codable {
    let definition: StockTrialDefinition
    let attempts: [StockTrialRun]
    let callbackEvents: [Int]
    let appliedTextEvents: [Int]
    let gapEvents: [Int]
    let status: String
    // A completed trial means the user finished observation, never physical validation.
    var physicalValidation = "unverified; requires human comparison with the actual CK-5200"

    static func build(events: [StockEvidenceEvent], trials: [StockTrialRun]) -> [StockTrialCoverage] {
        StockTrialCatalog.all.map { definition in
            let matching = events.filter { $0.trialID == definition.id }
            let attempts = trials.filter { $0.trialID == definition.id }
            return Self(definition: definition, attempts: attempts,
                callbackEvents: matching.filter { $0.kind == "key" }.map(\.sequence),
                appliedTextEvents: matching.filter { $0.kind == "text-changed" }.map(\.sequence),
                gapEvents: matching.filter { $0.kind == "key-observation-gap" }.map(\.sequence),
                status: attempts.last?.status.rawValue ?? "not-started")
        }
    }
}
