import XCTest

final class StockExtensionUITests: XCTestCase {
    func testEditApplyProfilesAndRecoverAgainstV122() throws {
        guard ProcessInfo.processInfo.environment["V122_EDITOR_UI_TEST"] == "1" else {
            throw XCTSkip("Start the actual V122 editor bridge and set V122_EDITOR_UI_TEST=1.")
        }
        continueAfterFailure = false
        let app = XCUIApplication()
        for mode in ["eligible", "blocked"] {
            app.launchEnvironment["FIRMWARE_CONFIRMATION_UI_TEST"] = mode
            app.launch()
            let flash = app.buttons["firmware.confirm"]
            XCTAssertTrue(flash.waitForExistence(timeout: 5))
            XCTAssertFalse(flash.isEnabled)
            let risk = app.switches["firmware.risk"]
            risk.coordinate(withNormalizedOffset: CGVector(dx: 0.93, dy: 0.5)).tap()
            let acknowledged = XCTNSPredicateExpectation(predicate: NSPredicate(format: "value == '1'"), object: risk)
            XCTAssertEqual(XCTWaiter.wait(for: [acknowledged], timeout: 5), .completed)
            let readiness = XCTNSPredicateExpectation(predicate: NSPredicate(format: "enabled == %@", NSNumber(value: mode == "eligible")), object: flash)
            XCTAssertEqual(XCTWaiter.wait(for: [readiness], timeout: 5), .completed)
            if mode == "blocked" {
                XCTAssertTrue(app.staticTexts["firmware.blockReason"].label.contains("Test preflight blocked"))
            }
            capture(app, "firmware-confirmation-\(mode)")
            app.terminate()
        }
        app.launchEnvironment.removeValue(forKey: "FIRMWARE_CONFIRMATION_UI_TEST")
        app.launchEnvironment["EXTENSION_PROFILE_TEST_ID"] = UUID().uuidString
        app.launch()
        XCTAssertTrue(app.navigationBars["My Clicks"].exists)
        XCTAssertTrue(app.buttons["extension.save"].isHittable)
        XCTAssertTrue(app.buttons["extension.apply"].isHittable)
        XCTAssertFalse(app.buttons["extension.apply"].isEnabled)
        XCTAssertFalse(app.buttons["qmk.open"].exists)
        capture(app, "my-clicks-offline-keyboard")
        openConnection(app)
        app.buttons["extension.installer"].tap()
        XCTAssertTrue(app.staticTexts["installer.image"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts["installer.image"].label.contains("27,058"))
        reveal(app.buttons["installer.review"], app)
        XCTAssertFalse(app.buttons["installer.review"].isEnabled, "Offline installation must not be armed")
        capture(app, "my-clicks-extension-installer-offline")
        app.navigationBars.buttons.element(boundBy: 0).tap()
        closeConnection(app)
        app.segmentedControls["extension.layer"].buttons["Navigation"].tap()
        edit(0, action: "Escape", app)
        app.segmentedControls["extension.layer"].buttons["Symbols"].tap()
        edit(12, action: "Tab", app)
        let name = app.textFields["extension.profileName"]
        reveal(name, app)
        name.tap()
        name.typeText(String(repeating: XCUIKeyboardKey.delete.rawValue, count: (name.value as? String ?? "").count))
        name.typeText("Daily extension\n")
        tap("extension.save", app)
        wait(app.staticTexts["extension.profileStatus"], "Saved on iPhone")
        capture(app, "my-clicks-named-draft")

        openConnection(app)
        tap("extension.connect", app)
        wait(app.staticTexts["keyboard.connectionStatus"], "Read all 108")
        tap("extension.power", app)
        wait(app.staticTexts["extension.hidStatus"], "RAM lost")
        tap("extension.connect", app)
        wait(app.staticTexts["keyboard.connectionStatus"], "Read all 108")
        closeConnection(app)
        wait(app.staticTexts["extension.comparison"], "Not synchronized")
        tap("extension.apply", app)
        wait(app.staticTexts["extension.comparison"], "Verified in emulator")
        capture(app, "my-clicks-verified-map")
        openConnection(app)
        tap("extension.hid", app)
        wait(app.staticTexts["extension.hidStatus"], "PASS NAV+Q")
        capture(app, "my-clicks-exact-hid")
        tap("extension.loseAck", app)
        wait(app.staticTexts["extension.hidStatus"], "Next APPLY reply")
        closeConnection(app)
        tap("extension.apply", app)
        wait(app.staticTexts["extension.comparison"], "Apply not confirmed")
        XCTAssertFalse(app.buttons["extension.apply"].isEnabled)
        capture(app, "my-clicks-uncertain-apply")
        openConnection(app)
        tap("extension.disconnect", app)
        tap("extension.connect", app)
        wait(app.staticTexts["keyboard.connectionStatus"], "Read all 108")
        tap("extension.power", app)
        wait(app.staticTexts["extension.hidStatus"], "RAM lost")
        app.terminate()
        app.launch()
        XCTAssertFalse(app.buttons["extension.apply"].isEnabled)
        openConnection(app)
        tap("extension.connect", app)
        wait(app.staticTexts["keyboard.connectionStatus"], "Read all 108")
        closeConnection(app)
        tap("keyboard.profiles", app)
        tap("extension.profile.Daily extension", app)
        wait(app.staticTexts["extension.comparison"], "Not synchronized")
        capture(app, "my-clicks-manual-reapply")
        tap("extension.apply", app)
        wait(app.staticTexts["extension.comparison"], "Verified in emulator")
        openConnection(app)
        tap("extension.hid", app)
        wait(app.staticTexts["extension.hidStatus"], "PASS NAV+Q")
        closeConnection(app)
        capture(app, "my-clicks-profile-reapplied")

        app.terminate()
        app.launchEnvironment["MY_CLICKS_DARK_TEST"] = "1"
        app.launchArguments += ["-UIPreferredContentSizeCategoryName", "UICTContentSizeCategoryAccessibilityXXXL"]
        app.launch()
        XCTAssertTrue(app.buttons["extension.save"].isHittable)
        XCTAssertTrue(app.buttons["extension.apply"].isHittable)
        XCTAssertTrue(app.otherElements["keyboard.actionBar"].exists)
        XCTAssertLessThan(app.otherElements["keyboard.actionBar"].frame.height, app.frame.height / 3)
        capture(app, "my-clicks-dark-large-text")
        reveal(app.buttons["extension.cell.0"], app)
        capture(app, "my-clicks-dark-large-key-list")
    }

    private func openConnection(_ app: XCUIApplication) { app.buttons["keyboard.connection"].tap() }
    private func closeConnection(_ app: XCUIApplication) { app.buttons["Done"].tap() }

    private func edit(_ position: Int, action: String, _ app: XCUIApplication) {
        tap("extension.cell.\(position)", app)
        let search = app.searchFields.firstMatch
        XCTAssertTrue(search.waitForExistence(timeout: 5))
        search.tap()
        search.typeText(action)
        app.buttons["action.Keys.\(action)"].tap()
        capture(app, "my-clicks-action-\(position)")
        app.buttons["extension.savePosition"].tap()
    }

    private func tap(_ id: String, _ app: XCUIApplication) {
        let button = app.buttons[id]
        if !["extension.save", "extension.apply"].contains(id) { reveal(button, app) }
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
                let footer = app.otherElements["keyboard.actionBar"]
                let bottom = footer.exists && footer.isHittable ? footer.frame.minY - 12 : app.frame.maxY - 50
                if element.exists, !element.frame.isEmpty, element.isHittable,
                   element.frame.midY > app.navigationBars.firstMatch.frame.maxY + 8,
                   element.frame.midY < bottom { return }
                let up = element.exists && !element.frame.isEmpty ? element.frame.midY > app.frame.midY : direction
                let start = app.coordinate(withNormalizedOffset: CGVector(dx: 0.97, dy: 0.55))
                let end = app.coordinate(withNormalizedOffset: CGVector(dx: 0.97, dy: up ? 0.3 : 0.8))
                start.press(forDuration: 0.05, thenDragTo: end)
            }
        }
        XCTFail("Cannot reveal \(element)")
    }
}
