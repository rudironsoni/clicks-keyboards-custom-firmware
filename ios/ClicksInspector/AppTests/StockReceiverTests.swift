import XCTest
@testable import ClicksInspector

final class StockReceiverTests: XCTestCase {
    @MainActor func testMismatchedVersionStopsSweepWithoutValidatedObservation() async throws {
        let inspector = AccessoryInspector()
        let mismatch: [UInt8] = [8, 2, 3, 0, 1, 0x20, 1, 0x23]
        inspector.simulateVersionResponse(mismatch)
        XCTAssertEqual(inspector.stockSweepRemaining, 0)
        XCTAssertTrue(inspector.readState.hasPrefix("Failed:"))
        XCTAssertNil(inspector.lastReadResult)
        let read = try XCTUnwrap(inspector.stockEvidence.bundle?.reads.first)
        XCTAssertTrue(read.outcome.hasPrefix("failed:"))
        XCTAssertEqual(read.response, mismatch)
        XCTAssertFalse(inspector.stockEvidence.bundle!.events.contains { $0.fields["outcome"] == "validated" })
        await inspector.stockEvidence.waitForCheckpoints()
    }
}
