import Foundation

/// Share projection v2. Unknown strings and fields fail closed, including recovered checkpoints.
enum StockEvidencePrivacy {
    private static func choice(_ value: String, _ allowed: [String]) -> String {
        allowed.contains(value) ? value : "omitted"
    }

    private static func version(_ value: String?) -> String {
        guard let value, value.range(of: "^[0-9]{1,3}(\\.[0-9]{1,3}){0,3}$", options: .regularExpression) != nil else { return "omitted" }
        return value
    }

    static func outcome(_ value: String) -> String {
        if ["validated", "pending", "user-stop"].contains(value) { return value }
        if value.hasPrefix("failed:") { return "failed; detail omitted" }
        if value.hasPrefix("interrupted") { return "interrupted; detail omitted" }
        if value.hasPrefix("event-limit") { return "event-limit" }
        return "omitted"
    }

    private static func reasonCode(_ value: String) -> String {
        let prefixes = [("Read timed out", "read-timeout"), ("Session streams did not open", "open-timeout"),
            ("Accessory disconnected", "detach"), ("App entered background", "background"),
            ("Input stream error", "input-error"), ("Output stream error", "output-error"),
            ("Output write failed", "write-error"), ("Input read failed", "read-error"),
            ("Input data was already queued", "queued-input"), ("Input arrived before", "early-input"),
            ("Invalid ", "invalid-response"), ("Version payload does not match", "version-mismatch"),
            ("text focus lost", "focus-loss"), ("GCKeyboard connection changed", "keyboard-connection-change"),
            ("user ended trial", "trial-end"), ("Stop stock characterization", "user-stop")]
        return prefixes.first { value.hasPrefix($0.0) }?.1 ?? "other"
    }

    private static func state(_ input: StockObservationState) -> StockObservationState {
        var result = input
        if let identity = input.accessory {
            result.accessory = StockAccessoryIdentity(model: choice(identity.model, ["CK-5200"]),
                firmware: choice(identity.firmware, ["1.2.2"]), hardware: choice(identity.hardware, ["1.2.0"]),
                protocols: identity.protocols.map { choice($0, ["com.clickscompanion.protocol"]) })
        }
        result.session = choice(input.session, ["closed", "opening", "open", "failed"])
        result.app = choice(input.app, ["active", "inactive", "background"])
        result.focus = choice(input.focus, ["none", "test-text-input"])
        result.condition = choice(input.condition, ["baseline", "initial connection", "foreground/background", "disconnect/reconnect",
            "each physical key", "modifiers/layers", "simultaneous keys", "long press/repeat", "sustained typing",
            "backlight timing", "idle/wake", "normal charging", "stock-app settings comparison"])
        result.backlight = choice(input.backlight, ["not-observed", "on", "off", "changed"])
        result.charging = choice(input.charging, ["not-observed", "disconnected", "connected; iOS charging", "connected; iOS not charging"])
        return result
    }

    private static func fields(_ input: [String: String], includeTestText: Bool) -> [String: String] {
        var result: [String: String] = [:]
        let numbers = ["code", "modifiers", "observedHeldMS", "fullUTF16Length", "rangeLocation", "rangeLength",
                       "UTF16Length", "insertedUTF16Length", "location", "length"]
        let booleans = ["repeatedDownWithoutRelease", "releaseWithoutObservedDown", "GCKeyboardAvailable",
                        "truncated", "acceptedByRecorder", "markedText"]
        for (key, value) in input {
            if key == "observedHeldMS", let number = Double(value), number.isFinite, number >= 0 {
                result[key] = String(number)
            } else if numbers.contains(key), let number = UInt64(value) {
                result[key] = String(number)
            } else if booleans.contains(key), ["true", "false"].contains(value) {
                result[key] = value
            } else if key == "phase" {
                result[key] = choice(value, ["down", "up", "cancelled", "changed", "before", "after", "before-key-trial"])
            } else if key == "state" {
                result[key] = choice(value, ["closed", "opening", "open", "failed"])
            } else if key == "outcome" {
                result[key] = outcome(value)
            } else if key == "reason" {
                result["reasonCode"] = reasonCode(value)
            } else if key == "physicalKey" {
                result[key] = choice(value, Array("QWERTYUIOPASDFGHJKLZXCVBNM").map(String.init)
                    + ["Space", "Enter", "Backspace", "Shift", "123/SYM", "Command", "Clicks", "special key 1", "special key 2", "special key 3"])
            } else if key == "combination" {
                result[key] = choice(value, ["single key", "Shift", "123/SYM", "Command", "cursor mode", "two keys", "three keys", "four keys", "long press", "rapid taps"])
            } else if ["heldInSource", "unresolvedPresses"].contains(key),
                      value.isEmpty || value.range(of: "^(GCKeyboard|UIPress):[0-9]{1,5}(,(GCKeyboard|UIPress):[0-9]{1,5})*$", options: .regularExpression) != nil {
                result[key] = value
            } else if ["text", "characters"].contains(key) {
                if includeTestText { result[key] = String(value.prefix(256)) }
                else { result[key + "Length"] = String(value.count) }
            } else {
                // Includes meaning, API uptime, arbitrary error descriptions, and future fields.
                result["detailsOmitted"] = "true"
            }
        }
        return result
    }

    static func project(_ input: StockEvidenceBundle, includeTestText: Bool = false) -> StockEvidenceBundle {
        let environment = ["iOS": version(input.environment["iOS"]), "appVersion": version(input.environment["appVersion"]),
            "appBuild": version(input.environment["appBuild"]), "source": choice(input.environment["source"] ?? "unknown", [
                "physical iOS API observations; not a firmware-image fixture",
                "simulator iOS callbacks; no physical accessory evidence", "host simulation"])]
        var result = StockEvidenceBundle(schemaVersion: 1, sessionID: input.sessionID, startedAt: input.startedAt,
                                         endedAt: input.endedAt, endReason: input.endReason.map(outcome), environment: environment)
        result.privacy = "shareable-v2: allowlisted fields; wall times and key codes retained; test text \(includeTestText ? "included by user" : "omitted")"
        var permittedRequests: [Int: [UInt8]] = [:]
        var permittedResponses: [Int: [UInt8]] = [:]
        for read in input.reads {
            guard let command = StockReadCommand(rawValue: read.command), StockReadPolicy.demonstrated.contains(command),
                  read.request == command.request else { continue }
            permittedRequests[read.id] = read.request
            var copy = StockReadObservation(id: read.id, command: read.command, request: read.request,
                                            startedMS: read.startedMS, before: state(read.before))
            copy.sentMS = read.sentMS
            copy.endedMS = read.endedMS
            copy.responseLatencyMS = read.responseLatencyMS
            copy.after = read.after.map(state)
            copy.outcome = outcome(read.outcome)
            var parser = StockReadAccumulator(command: command)
            if read.outcome == "validated", (try? parser.append(read.response)) != nil {
                copy.response = read.response
                copy.sameAsPrevious = read.sameAsPrevious
                permittedResponses[read.id] = read.response
            } else if read.outcome == "validated" { copy.outcome = "failed; invalid saved response" }
            result.reads.append(copy)
        }
        let tx = Dictionary(grouping: input.events.filter { $0.kind == "tx-fragment" }, by: { $0.readID ?? -1 })
        let rx = Dictionary(grouping: input.events.filter { $0.kind == "rx-fragment" }, by: { $0.readID ?? -1 })
        for event in input.events {
            let id = event.readID ?? -1
            var bytes: [UInt8]?
            if event.kind == "request", let request = permittedRequests[id], event.bytes == request { bytes = request }
            if event.kind == "tx-fragment", let request = permittedRequests[id],
               tx[id]?.flatMap({ $0.bytes ?? [] }) == request { bytes = event.bytes }
            if event.kind == "rx-fragment", let response = permittedResponses[id],
               rx[id]?.flatMap({ $0.bytes ?? [] }) == response { bytes = event.bytes }
            var details = fields(event.fields, includeTestText: includeTestText)
            if event.bytes != nil, bytes == nil { details["redaction"] = "unvalidated stream payload" }
            result.events.append(StockEvidenceEvent(sequence: event.sequence, timestamp: event.timestamp, elapsedMS: event.elapsedMS,
                source: choice(event.source, ["EA", "GCKeyboard", "UIPress", "UIKit", "text-input", "app", "user"]),
                kind: choice(event.kind, ["session-start", "request", "tx-fragment", "rx-fragment", "read-finished", "key",
                    "key-observation-gap", "attach", "detach", "accessory-snapshot", "session-close", "foreground",
                    "background; callbacks may stop", "inactive", "read-error", "error", "session-state", "three-pass-baseline-completed",
                    "key-trial-start", "key-trial-end", "unobservable", "state-mark", "focus-gained", "replacement-proposed",
                    "text-changed", "selection-changed"]),
                state: state(event.state), fields: details, bytes: bytes, byteCount: event.byteCount, readID: event.readID))
        }
        return result
    }
}
