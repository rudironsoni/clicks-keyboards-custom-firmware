import XCTest

final class StockExtensionUITests: XCTestCase {
    func testEditApplyProfilesAndRecoverAgainstV122() throws {
        guard ProcessInfo.processInfo.environment["V122_EDITOR_UI_TEST"] == "1" else {
            throw XCTSkip("Start the actual V122 editor bridge and set V122_EDITOR_UI_TEST=1.")
        }
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchEnvironment["EXTENSION_PROFILE_TEST_ID"] = UUID().uuidString
        app.launch()
        app.buttons["extension.open"].tap()
        app.buttons["extension.connect"].tap()
        wait(app.staticTexts["extension.status"], "Read all 108")
        tap("extension.power", app)
        wait(app.staticTexts["extension.hidStatus"], "RAM lost")
        tap("extension.connect", app)
        wait(app.staticTexts["extension.status"], "Read all 108")

        reveal(app.segmentedControls["extension.layer"], app)
        app.segmentedControls["extension.layer"].buttons["Navigation"].tap()
        edit(0, key: "extension.esc", app)
        app.segmentedControls["extension.layer"].buttons["Symbols"].tap()
        edit(12, key: "extension.tab", app)
        capture(app, "extension-three-layer-draft")
        tap("extension.apply", app)
        wait(app.staticTexts["extension.status"], "Applied and read back all 108")
        tap("extension.hid", app)
        wait(app.staticTexts["extension.hidStatus"], "PASS NAV+Q")
        capture(app, "extension-exact-hid")

        let name = app.textFields["extension.profileName"]
        reveal(name, app)
        name.tap()
        name.typeText("Daily extension\n")
        tap("extension.save", app)
        wait(app.staticTexts["extension.status"], "Saved Daily extension")
        tap("extension.loseAck", app)
        wait(app.staticTexts["extension.hidStatus"], "Next APPLY reply")
        tap("extension.apply", app)
        wait(app.staticTexts["extension.status"], "Apply not confirmed")
        XCTAssertFalse(app.buttons["extension.apply"].isEnabled)
        capture(app, "extension-lost-ack")
        tap("extension.disconnect", app)
        tap("extension.connect", app)
        wait(app.staticTexts["extension.status"], "Read all 108")
        wait(app.staticTexts["extension.comparison"], "matches verified RAM")

        tap("extension.power", app)
        wait(app.staticTexts["extension.hidStatus"], "RAM lost")
        app.terminate()
        app.launch()
        app.buttons["extension.open"].tap()
        app.buttons["extension.connect"].tap()
        wait(app.staticTexts["extension.status"], "Read all 108")
        tap("extension.profile.Daily extension", app)
        wait(app.staticTexts["extension.comparison"], "differs from verified RAM")
        capture(app, "extension-profile-needs-explicit-reapply")
        tap("extension.apply", app)
        wait(app.staticTexts["extension.status"], "Applied and read back all 108")
        tap("extension.hid", app)
        wait(app.staticTexts["extension.hidStatus"], "PASS NAV+Q")
        capture(app, "extension-profile-reapplied")
    }

    private func edit(_ position: Int, key: String, _ app: XCUIApplication) {
        tap("extension.cell.\(position)", app)
        app.buttons["extension.kind"].tap()
        app.buttons["Key / shortcut / activation"].tap()
        app.buttons[key].tap()
        capture(app, "extension-action-\(position)")
        app.buttons["extension.savePosition"].tap()
    }

    private func tap(_ id: String, _ app: XCUIApplication) {
        let button = app.buttons[id]
        reveal(button, app)
        button.tap()
    }

    private func wait(_ element: XCUIElement, _ text: String) {
        let expected = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label CONTAINS %@", text), object: element)
        XCTAssertEqual(XCTWaiter.wait(for: [expected], timeout: 15), .completed)
    }

    private func capture(_ app: XCUIApplication, _ name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private func reveal(_ element: XCUIElement, _ app: XCUIApplication) {
        for direction in [true, false] {
            for _ in 0..<20 {
                if element.exists, !element.frame.isEmpty, element.isHittable,
                   element.frame.midY > app.navigationBars.firstMatch.frame.maxY + 8,
                   element.frame.midY < app.frame.maxY - 50 { return }
                let up = element.exists && !element.frame.isEmpty ? element.frame.midY > app.frame.midY : direction
                let start = app.coordinate(withNormalizedOffset: CGVector(dx: 0.97, dy: 0.55))
                let end = app.coordinate(withNormalizedOffset: CGVector(dx: 0.97, dy: up ? 0.3 : 0.8))
                start.press(forDuration: 0.05, thenDragTo: end)
            }
        }
        XCTFail("Cannot reveal \(element)")
    }
}
