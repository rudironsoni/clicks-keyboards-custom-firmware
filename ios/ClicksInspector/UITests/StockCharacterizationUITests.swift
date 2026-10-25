import CryptoKit
import XCTest

final class StockCharacterizationUITests: XCTestCase {
    func testStockRecorderWithoutAccessory() throws {
        continueAfterFailure = false
        // Simulator UI test. This proves neither physical key callbacks nor EA transport.
        let app = XCUIApplication()
        app.launchArguments.append("--capture-stock-export")
        app.launch()
        app.buttons["advanced.open"].tap()
        app.buttons["Stock characterization and key recorder"].tap()
        let start = app.buttons["stock.start"]
        XCTAssertTrue(start.waitForExistence(timeout: 5))
        start.tap()
        if app.buttons["Start new session"].waitForExistence(timeout: 1) {
            app.buttons["Start new session"].tap()
        }
        let read = app.buttons["stock.read"]
        reveal(read, in: app, up: true)
        XCTAssertFalse(read.isEnabled, "No accessory must never authorize a getter sweep")
        let begin = app.buttons["stock.trial.begin"]
        reveal(begin, in: app, up: true)
        begin.tap()
        XCTAssertFalse(begin.isEnabled, "A second trial must not overlap the first")
        let text = app.textViews["stock.text"]
        reveal(text, in: app, up: true)
        text.tap()
        text.typeText("abc")
        XCTAssertEqual(text.value as? String, "abc")
        app.buttons["Done typing"].tap()
        let finish = app.buttons["stock.trial.completed"]
        reveal(finish, in: app, up: true)
        finish.tap()
        let stop = app.buttons["stock.stop"]
        reveal(stop, in: app, up: false)
        stop.tap()
        let export = app.buttons["stock.export"]
        reveal(export, in: app, up: true)
        export.tap()
        let share = app.buttons["stock.share"]
        reveal(share, in: app, up: true)
        XCTAssertTrue(share.isEnabled)
        let redacted = try inspectExport(app)
        try verifyExport(redacted, includesText: false)
        let includeText = app.switches["stock.includeText"]
        reveal(includeText, in: app, up: false)
        includeText.coordinate(withNormalizedOffset: CGVector(dx: 0.93, dy: 0.5)).tap()
        let textEnabled = XCTNSPredicateExpectation(predicate: NSPredicate(format: "value == '1'"), object: includeText)
        XCTAssertEqual(XCTWaiter.wait(for: [textEnabled], timeout: 5), .completed)
        reveal(export, in: app, up: true)
        export.tap()
        let withText = try inspectExport(app)
        try verifyExport(withText, includesText: true)
        XCTAssertEqual(redacted["sessionID"] as? String, withText["sessionID"] as? String)
        let screenshot = XCTAttachment(screenshot: app.screenshot())
        screenshot.lifetime = .keepAlways
        add(screenshot)
        reveal(start, in: app, up: false)
        start.tap()
        app.buttons["Start new session"].tap()
        reveal(text, in: app, up: true)
        // An existing empty UITextView can expose nil rather than an empty AX value.
        XCTAssertEqual(text.value as? String ?? "", "", "Replacement must reset UIKit's applied-edit buffer")
        reveal(stop, in: app, up: false)
        stop.tap()
        reveal(export, in: app, up: true)
        export.tap()
        let replacement = try inspectExport(app)
        XCTAssertNotEqual(replacement["sessionID"] as? String, redacted["sessionID"] as? String)
        XCTAssertTrue((replacement["trials"] as? [Any])?.isEmpty == true)
        let freshEvents = try XCTUnwrap(replacement["events"] as? [[String: Any]])
        XCTAssertFalse(freshEvents.contains { $0["kind"] as? String == "text-changed" })
        let freshState = try XCTUnwrap(freshEvents.first?["state"] as? [String: Any])
        XCTAssertEqual(freshState["condition"] as? String, "baseline")
        XCTAssertEqual(freshState["focus"] as? String, "none")
        let clear = app.buttons["stock.clear"]
        reveal(clear, in: app, up: true)
        clear.tap()
        app.buttons["Delete local stock evidence"].tap()
        // Wait for SwiftUI to apply the deletion state after dismissing the confirmation.
        let exportDisabled = XCTNSPredicateExpectation(predicate: NSPredicate(format: "enabled == false"), object: app.buttons["stock.export"])
        XCTAssertEqual(XCTWaiter.wait(for: [exportDisabled], timeout: 5), .completed)
        XCTAssertFalse(app.buttons["stock.share"].exists)
        reveal(app.textViews["stock.text"], in: app, up: false)
        XCTAssertEqual(app.textViews["stock.text"].value as? String ?? "", "")
    }

    private func inspectExport(_ app: XCUIApplication) throws -> [String: Any] {
        let inspect = app.buttons["stock.inspectExport"]
        reveal(inspect, in: app, up: true)
        inspect.tap()
        let json = app.textViews["stock.export.json"]
        XCTAssertTrue(json.waitForExistence(timeout: 5))
        var value = ""
        var manifest: [String] = []
        defer {
            let attachment = XCTAttachment(string: value)
            attachment.name = "UI-captured-export.json"
            attachment.lifetime = .keepAlways
            add(attachment)
            let pages = XCTAttachment(string: manifest.joined(separator: "\n"))
            pages.name = "UI-captured-export-pages.txt"
            pages.lifetime = .keepAlways
            add(pages)
        }
        let first = try capturePage(app, expectedPage: 1)
        value += first.text
        manifest.append(first.metadata)
        let count = try XCTUnwrap(Int(first.metadata.split(separator: ":")[1]))
        let next = app.buttons["stock.export.next"]
        if count > 1 {
            for page in 2...count {
                XCTAssertTrue(next.isEnabled)
                next.tap()
                let captured = try capturePage(app, expectedPage: page, pageCount: count)
                value += captured.text
                manifest.append(captured.metadata)
            }
        }
        XCTAssertFalse(next.isEnabled)
        let result = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(value.utf8)) as? [String: Any])
        app.navigationBars.buttons.element(boundBy: 0).tap()
        return result
    }

    private func capturePage(_ app: XCUIApplication, expectedPage: Int, pageCount: Int? = nil) throws -> (text: String, metadata: String) {
        let header = app.staticTexts["stock.export.page"]
        let json = app.textViews["stock.export.json"]
        var captured: (text: String, metadata: String)?
        let ready = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            guard header.exists, json.exists,
                  let metadata = header.value as? String, let text = json.value as? String,
                  self.pageMatches(text, metadata: metadata, expectedPage: expectedPage, pageCount: pageCount) else { return false }
            captured = (text, metadata)
            return true
        }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [ready], timeout: 5), .completed,
                       "Expected page \(expectedPage) and its actual TextEditor bytes")
        // Append the validated snapshot, not another AX read that could return different text.
        return try XCTUnwrap(captured)
    }

    private func pageMatches(_ text: String, metadata: String, expectedPage: Int, pageCount: Int? = nil) -> Bool {
        let fields = metadata.split(separator: ":")
        guard fields.count == 4, Int(fields[0]) == expectedPage,
              let count = Int(fields[1]), count >= expectedPage,
              pageCount == nil || count == pageCount,
              Int(fields[2]) == text.utf8.count else { return false }
        return fields[3] == SHA256.hash(data: Data(text.utf8)).map { String(format: "%02x", $0) }.joined()
    }

    func testExportPageCaptureRejectsStaleText() {
        let old = "previous page"
        let current = "expected page" // Same byte count, so length alone cannot pass this check.
        let digest = SHA256.hash(data: Data(current.utf8)).map { String(format: "%02x", $0) }.joined()
        let metadata = "2:3:\(current.utf8.count):\(digest)"
        XCTAssertFalse(pageMatches(old, metadata: metadata, expectedPage: 2))
        XCTAssertFalse(pageMatches(current, metadata: metadata, expectedPage: 3))
        XCTAssertTrue(pageMatches(current, metadata: metadata, expectedPage: 2))
        // Identical adjacent pages are valid once their page identity advances.
        XCTAssertTrue(pageMatches(current, metadata: "3:3:\(current.utf8.count):\(digest)", expectedPage: 3))
    }

    private func verifyExport(_ json: [String: Any], includesText: Bool) throws {
        XCTAssertEqual(json["schemaVersion"] as? Int, 2)
        XCTAssertTrue((json["reads"] as? [Any])?.isEmpty == true)
        let events = try XCTUnwrap(json["events"] as? [[String: Any]])
        let edits = events.filter { $0["kind"] as? String == "text-changed" }
        XCTAssertFalse(edits.isEmpty, "UIKit must report applied edits, not just proposed edits")
        XCTAssertTrue(events.contains { $0["source"] as? String == "UIKit" && $0["kind"] as? String == "focus-gained" })
        let trialID = "CK-5200-V122-H120-trials-v1/r0c0/single"
        var text = ""
        var previous = -1.0
        for event in events {
            let elapsed = try XCTUnwrap(event["elapsedMS"] as? Double)
            XCTAssertGreaterThanOrEqual(elapsed, previous)
            previous = elapsed
            XCTAssertNotNil(event["timestamp"] as? String)
            XCTAssertNotNil(event["sequence"] as? Int)
            let fields = try XCTUnwrap(event["fields"] as? [String: String])
            if !includesText { XCTAssertNil(fields["text"]); XCTAssertNil(fields["characters"]) }
            XCTAssertNil(fields["apiTimestamp"])
        }
        for edit in edits {
            XCTAssertEqual(edit["source"] as? String, "text-input")
            XCTAssertEqual(edit["trialID"] as? String, trialID)
            XCTAssertEqual(edit["trialAttempt"] as? Int, 0)
            let fields = try XCTUnwrap(edit["fields"] as? [String: String])
            XCTAssertNotNil(fields["rangeLocation"])
            XCTAssertNotNil(fields["rangeLength"])
            XCTAssertNotNil(fields["insertedUTF16Length"])
            XCTAssertEqual(fields["truncated"], "false")
            text += fields["text"] ?? ""
        }
        XCTAssertEqual((edits.last?["fields"] as? [String: String])?["UTF16Length"], "3")
        XCTAssertEqual(text, includesText ? "abc" : "")
        let coverage = try XCTUnwrap(json["coverage"] as? [[String: Any]])
        let definition = try XCTUnwrap(coverage.first?["definition"] as? [String: Any])
        let key = try XCTUnwrap(definition["key"] as? [String: Any])
        XCTAssertEqual(definition["id"] as? String, trialID)
        XCTAssertEqual(key["row"] as? Int, 0)
        XCTAssertEqual(key["column"] as? Int, 0)
        XCTAssertEqual(key["labelEvidence"] as? String, "conflictingSources")
        XCTAssertEqual(key["physicalLabel"] as? String, "Q (mapping conflict)")
        XCTAssertFalse((coverage.first?["appliedTextEvents"] as? [Int])?.isEmpty ?? true)
        let attempts = try XCTUnwrap(json["trials"] as? [[String: Any]])
        XCTAssertNotEqual(attempts.first?["status"] as? String, "active")
        // Software keyboard typing is not evidence of a physical key callback.
        XCTAssertTrue((coverage.first?["physicalValidation"] as? String)?.hasPrefix("unverified") == true)
    }

    private func reveal(_ element: XCUIElement, in app: XCUIApplication, up: Bool) {
        // Lazy list rows can disappear after an insertion moves them above the viewport.
        // Search both directions rather than continuing into the same list boundary.
        for direction in [up, !up] {
            for _ in 0..<30 {
                if element.exists, !element.frame.isEmpty,
                   element.frame.midY > app.navigationBars.firstMatch.frame.maxY + 8,
                   element.frame.midY < app.frame.maxY - 44,
                   element.isHittable { return }
                var moveUp = direction
                if element.exists, !element.frame.isEmpty, element.frame.minY.isFinite {
                    moveUp = element.frame.midY > app.frame.midY
                }
                // Short drags in the list gutter avoid the nested text view and overshoot.
                let start = app.coordinate(withNormalizedOffset: CGVector(dx: 0.96, dy: 0.55))
                let end = app.coordinate(withNormalizedOffset: CGVector(dx: 0.96, dy: moveUp ? 0.35 : 0.75))
                start.press(forDuration: 0.05, thenDragTo: end)
            }
        }
        XCTFail("Cannot reveal \(element) inside the scrollable viewport")
    }
}
