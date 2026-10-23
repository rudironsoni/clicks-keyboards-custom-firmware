import XCTest

final class StockManagerUITests: XCTestCase {
    func testOfficialImagePreparationWithoutAccessory() {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launch()
        app.buttons["advanced.open"].tap()
        let prepare = app.buttons["stockManager.prepare"]
        for _ in 0..<5 {
            if prepare.exists && prepare.isHittable { break }
            app.swipeUp()
        }
        XCTAssertTrue(prepare.exists)
        prepare.tap()
        let status = app.staticTexts["stockManager.preparation"]
        let expected = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label CONTAINS %@",
            "8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8"), object: status)
        XCTAssertEqual(XCTWaiter.wait(for: [expected], timeout: 5), .completed)
        XCTAssertTrue(status.label.contains("Physical installation is disabled"))
        XCTAssertFalse(app.buttons["Mark update verified"].exists)
        let shot = XCTAttachment(screenshot: app.screenshot())
        shot.name = "stock-image-prepared-writes-disabled"
        shot.lifetime = .keepAlways
        add(shot)
    }
}
