import XCTest
@testable import ClicksInspector

final class StockReceiverTests: XCTestCase {
    @MainActor func testStockVersionAfterCoalescedNotification() async {
        let inspector = AccessoryInspector()
        inspector.simulateVersionResponse([4, 4, 1, 0, 8, 2, 3, 0, 1, 0x20, 1, 0x22])
        XCTAssertTrue(inspector.stockVersionVerified)
        XCTAssertEqual(inspector.readState, "Completed")
        inspector.background()
        await inspector.stockEvidence.waitForCheckpoints()
    }

    @MainActor func testExtensionOwnershipAndBackgroundCancellation() async {
        let inspector = AccessoryInspector()
        let management = SilentLink()
        inspector.customFirmware.attach(management)
        XCTAssertFalse(inspector.canOpenExtensionEditor)
        XCTAssertFalse(inspector.claimExtensionEmulator())
        inspector.customFirmware.detach()
        XCTAssertTrue(inspector.claimExtensionEmulator())
        let extensionLink = SilentLink()
        let opening = Task { await inspector.extensionEditor.connect(extensionLink, emulated: true) }
        while !inspector.extensionEditor.busy { await Task.yield() }
        XCTAssertFalse(inspector.canReadSelectedCommand)
        XCTAssertFalse(inspector.canOpenExtensionEditor)
        XCTAssertFalse(inspector.claimFirmwareEmulator())
        inspector.startStockRecording()
        XCTAssertFalse(inspector.stockEvidence.isRecording)
        inspector.background()
        await opening.value
        XCTAssertFalse(inspector.extensionEditor.connected)
        XCTAssertFalse(inspector.extensionEditor.ownsSession)
        XCTAssertTrue(extensionLink.closed)
        XCTAssertTrue(management.closed)
    }

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

private final class SilentLink: KeyboardPacketLink {
    var onFragment: (([UInt8]) -> Void)?
    var onClosed: ((String) -> Void)?
    var closed = false
    func send(_ bytes: [UInt8]) {}
    func close() { closed = true; onFragment = nil; onClosed = nil }
}
