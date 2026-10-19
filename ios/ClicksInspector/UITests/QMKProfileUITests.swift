import XCTest

final class QMKProfileUITests: XCTestCase {
    func testEditSaveReconnectReapplyAndRestore() throws {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchEnvironment["QMK_PROFILE_TEST_ID"] = UUID().uuidString
        app.launch()
        app.buttons["qmk.open"].tap()
        guard app.buttons["qmk.connect"].waitForExistence(timeout: 3) else {
            throw XCTSkip("QMKKeymap simulator configuration is required")
        }
        app.buttons["qmk.connect"].tap()
        waitForLabel(app.staticTexts["qmk.connection"], contains: "All 144")
        if app.switches["qmk.permission"].value as? String != "1" {
            app.switches["qmk.permission"].coordinate(withNormalizedOffset: CGVector(dx: 0.93, dy: 0.5)).tap()
        }
        let cell = app.buttons["qmk.cell.0.0"]
        reveal(cell, in: app)
        cell.tap()
        let hex = app.textFields["qmk.hex"]
        XCTAssertTrue(hex.waitForExistence(timeout: 5))
        hex.tap()
        hex.typeText("0005")
        attach(app, name: "qmk-editing")
        app.buttons["qmk.applyKey"].tap()
        waitForLabel(cell, contains: "0005")
        let name = app.textFields["qmk.profileName"]
        reveal(name, in: app)
        name.tap()
        name.typeText("Daily typing\n")
        let save = app.buttons["qmk.save"]
        reveal(save, in: app)
        save.tap()
        waitForLabel(app.staticTexts["qmk.status"], contains: "Saved Daily typing")

        // Relaunch proves that the actual phone JSON file survives app termination.
        app.terminate()
        app.launch()
        app.buttons["qmk.open"].tap()
        app.buttons["qmk.connect"].tap()
        waitForLabel(app.staticTexts["qmk.connection"], contains: "All 144")
        reveal(app.staticTexts["qmk.comparison"], in: app)
        waitForLabel(app.staticTexts["qmk.comparison"], contains: "differs")
        reveal(app.switches["qmk.permission"], in: app)
        XCTAssertEqual(app.switches["qmk.permission"].value as? String, "0")
        let reapply = app.buttons["qmk.reapply"]
        reveal(reapply, in: app)
        XCTAssertFalse(reapply.isEnabled)
        reveal(app.switches["qmk.permission"], in: app)
        app.switches["qmk.permission"].coordinate(withNormalizedOffset: CGVector(dx: 0.93, dy: 0.5)).tap()
        reveal(reapply, in: app)
        reapply.tap()
        waitForLabel(app.staticTexts["qmk.comparison"], contains: "matches")
        reveal(cell, in: app)
        waitForLabel(cell, contains: "0005")
        attach(app, name: "qmk-reapplied")

        let restore = app.buttons["qmk.restore"]
        reveal(restore, in: app)
        restore.tap()
        waitForLabel(cell, contains: "0014")
        reveal(app.staticTexts["qmk.status"], in: app)
        waitForLabel(app.staticTexts["qmk.status"], contains: "Factory layout restored")
        reveal(cell, in: app)
        attach(app, name: "qmk-restored")
        reveal(app.staticTexts["qmk.comparison"], in: app)
        waitForLabel(app.staticTexts["qmk.comparison"], contains: "differs")
        let reconnect = app.buttons["qmk.reconnect"]
        reveal(reconnect, in: app)
        reconnect.tap()
        waitForLabel(app.staticTexts["qmk.connection"], contains: "All 144")
        XCTAssertEqual(app.switches["qmk.permission"].value as? String, "0")
        reveal(app.staticTexts["qmk.comparison"], in: app)
        waitForLabel(app.staticTexts["qmk.comparison"], contains: "differs")
    }

    private func waitForLabel(_ element: XCUIElement, contains text: String) {
        let match = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label CONTAINS %@", text), object: element)
        XCTAssertEqual(XCTWaiter.wait(for: [match], timeout: 10), .completed)
    }

    private func attach(_ app: XCUIApplication, name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private func reveal(_ element: XCUIElement, in app: XCUIApplication) {
        for direction in [true, false] {
            for _ in 0..<25 {
                // Lazy rows can report hittable even when their tap point is at
                // the home indicator. Check the center before querying hittability.
                if element.exists, !element.frame.isEmpty,
                   element.frame.midY > app.navigationBars.firstMatch.frame.maxY + 8,
                   element.frame.midY < app.frame.maxY - 44,
                   element.isHittable { return }
                let up = element.exists && !element.frame.isEmpty ? element.frame.midY > app.frame.midY : direction
                let start = app.coordinate(withNormalizedOffset: CGVector(dx: 0.96, dy: 0.55))
                let end = app.coordinate(withNormalizedOffset: CGVector(dx: 0.96, dy: up ? 0.35 : 0.75))
                start.press(forDuration: 0.05, thenDragTo: end)
            }
        }
        XCTFail("Cannot reveal \(element)")
    }
}
