import XCTest

final class StockCharacterizationUITests: XCTestCase {
    func testStockRecorderWithoutAccessory() {
        // Simulator UI test. This proves neither physical key callbacks nor EA transport.
        let app = XCUIApplication()
        app.launch()
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
        let text = app.textViews["stock.text"]
        reveal(text, in: app, up: true)
        text.tap()
        text.typeText("abc")
        XCTAssertEqual(text.value as? String, "abc")
        let stop = app.buttons["stock.stop"]
        reveal(stop, in: app, up: false)
        stop.tap()
        let export = app.buttons["stock.export"]
        reveal(export, in: app, up: true)
        export.tap()
        let share = app.buttons["stock.share"]
        reveal(share, in: app, up: true)
        XCTAssertTrue(share.isEnabled)
        let screenshot = XCTAttachment(screenshot: app.screenshot())
        screenshot.lifetime = .keepAlways
        add(screenshot)
    }

    private func reveal(_ element: XCUIElement, in app: XCUIApplication, up: Bool) {
        for _ in 0..<12 {
            if element.exists && element.isHittable { return }
            if up { app.swipeUp() } else { app.swipeDown() }
        }
        XCTAssertTrue(element.isHittable)
    }
}
