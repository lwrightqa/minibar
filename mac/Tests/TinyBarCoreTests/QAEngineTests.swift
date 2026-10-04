import Foundation
import XCTest
@testable import TinyBarCore

/// QA: the engine end to end (fake mic, camera, USB and bar).
final class QAEngineTests: XCTestCase {
    func test_flapping() async throws { try await QAEngineScenarios().flapping() }
    func test_twoAppsAtOnce() async throws { try await QAEngineScenarios().twoAppsAtOnce() }
    func test_cameraOnly() async throws { try await QAEngineScenarios().cameraOnly() }
    func test_pauseThenResume() async throws { try await QAEngineScenarios().pauseThenResume() }
    func test_sleepDuringTheStartDelay() async throws { try await QAEngineScenarios().sleepDuringTheStartDelay() }
    func test_testCallWhilePaused() async throws { try await QAEngineScenarios().testCallWhilePaused() }
    func test_wifiOffStopsBonjour() async throws { try await QAEngineScenarios().wifiOffStopsBonjour() }
    func test_wifiBackOnStartsBonjourAgain() async throws { try await QAEngineScenarios().wifiBackOnStartsBonjourAgain() }
    func test_connectWindowBrowsingStopsWhenItCloses() async throws { try await QAEngineScenarios().connectWindowBrowsing() }
}

@MainActor
final class QAEngineScenarios {
    func connected() async -> EngineRig {
        let rig = EngineRig.paired()
        await rig.begin()
        await rig.plugIn()
        return rig
    }

    func flapping() async throws {
        let rig = await connected()
        for _ in 0..<20 {
            await rig.micUse(Proc.zoom)
            await rig.run(for: 1)
            await rig.micUse()
            await rig.run(for: 1)
        }
        XCTAssertEqual(rig.messageLog(), ["0.0 usb idle", "30.0 usb idle"], "40 s of 1-second blips: heartbeats only")
        await rig.engine.shutdown()
    }

    func twoAppsAtOnce() async throws {
        let rig = await connected()
        await rig.micUse(Proc.slack, Proc.zoom)
        await rig.run(for: 5)
        await rig.micUse(Proc.zoom)
        await rig.run(for: 5)
        await rig.micUse(Proc.zoom, Proc.chromeHelper)
        await rig.run(for: 5)
        await rig.micUse()
        await rig.run(for: 12)
        XCTAssertEqual(rig.messageLog(), ["0.0 usb idle", "3.0 usb Slack", "5.0 usb Zoom", "10.0 usb Chrome", "25.0 usb idle"])
        let ids = Set(rig.bar.messages.compactMap(\.request.callID))
        XCTAssertEqual(ids.count, 1, "one call_id across the name changes")
        await rig.engine.shutdown()
    }

    func cameraOnly() async throws {
        let rig = await connected()
        rig.camera.set(true)
        await rig.settle()
        await rig.run(for: 4)
        let started = try XCTUnwrap(rig.bar.messages.last?.request)
        XCTAssertTrue(started.active)
        XCTAssertNil(started.app, "camera calls have no name")
        // Don't Count the Camera ends it at once; Count the Camera Again shows while the camera is on.
        rig.engine.setCountCamera(false)
        await rig.settle()
        XCTAssertEqual(rig.messageLog().last, "4.0 usb idle")
        XCTAssertTrue(rig.engine.state.undo.camera)
        rig.camera.set(false)
        await rig.settle()
        XCTAssertFalse(rig.engine.state.undo.camera)
        await rig.engine.shutdown()
    }

    func pauseThenResume() async throws {
        let rig = await connected()
        await rig.micUse(Proc.slack)
        await rig.run(for: 5)
        let firstID = rig.bar.messages.last?.request.callID
        rig.engine.pause(.untilResumed)
        await rig.settle()
        XCTAssertEqual(rig.messageLog().last, "5.0 usb idle", "pausing during a call: active false at once")
        await rig.run(for: 65)
        XCTAssertEqual(rig.messageLog().suffix(2), ["35.0 usb idle", "65.0 usb idle"], "idle heartbeats while paused")
        rig.engine.resume()
        await rig.settle()
        await rig.run(for: 4)
        XCTAssertEqual(rig.messageLog().last, "73.0 usb Slack", "3 s after resuming")
        XCTAssertNotEqual(rig.bar.messages.last?.request.callID, firstID)
        XCTAssertEqual(rig.bar.messages.last?.request.elapsedS, 0)
        await rig.engine.shutdown()
    }

    func sleepDuringTheStartDelay() async throws {
        let rig = await connected()
        await rig.micUse(Proc.slack)
        await rig.run(for: 1)
        await rig.power.send(.willSleep)
        await rig.settle()
        await rig.run(for: 10)
        await rig.power.send(.didWake)
        await rig.settle()
        await rig.run(for: 4)
        XCTAssertEqual(rig.messageLog(), ["0.0 usb idle", "1.0 usb leaving", "11.0 usb idle", "14.0 usb Slack"],
                       "no call before sleeping; the start delay starts over after waking")
        await rig.engine.shutdown()
    }

    /// mac-app-ux.md 6.4: "Use Wi-Fi when TinyBar isn't plugged in" off means
    /// "USB only, no browsing". Expected: the Bonjour browser stops.
    func wifiOffStopsBonjour() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        XCTAssertEqual(rig.discovery.starts, 1)
        try rig.engine.updateSettings { $0.useWiFi = false }
        await rig.settle()
        XCTAssertTrue(rig.discovery.stops >= 1 && !rig.discovery.isRunning,
                      "the platform browser keeps browsing _tinybar._tcp with Wi-Fi off (BarDiscoveryHub never stops it before quit)")
        await rig.engine.shutdown()
    }

    func wifiBackOnStartsBonjourAgain() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        try rig.engine.updateSettings { $0.useWiFi = false }
        await rig.settle()
        try rig.engine.updateSettings { $0.useWiFi = true }
        await rig.settle()
        XCTAssertEqual(rig.discovery.starts, 2)
        XCTAssertTrue(rig.discovery.isRunning)
        // The bar is found at a new address on the new run, and used at once.
        rig.factory.place(rig.bar, at: BarEndpoint(host: "10.0.4.43"))
        rig.discovery.emit([DiscoveredBar(name: "TinyBar 2A1C", deviceID: "f412fa3f2a1c", auth: .bearer,
                                          endpoint: BarEndpoint(host: "10.0.4.43"))])
        await rig.settle()
        XCTAssertEqual(rig.engine.state.connection.phase, .connected)
        await rig.engine.shutdown()
        XCTAssertFalse(rig.discovery.isRunning)
    }

    /// Not set up: nothing browses until the Connect window's Wi-Fi page
    /// does, and closing it stops the browser again.
    func connectWindowBrowsing() async throws {
        let rig = EngineRig()
        await rig.begin()
        XCTAssertEqual(rig.discovery.starts, 0)
        let flow = rig.engine.makeWiFiPairingFlow()
        rig.engine.markLocalNetworkExplained()
        flow.startLooking()
        XCTAssertTrue(rig.discovery.isRunning)
        flow.cancel()
        XCTAssertFalse(rig.discovery.isRunning, "Back, or the window closed: no more browsing")
        await rig.engine.shutdown()
    }

    /// Spec: Send Test Call is dimmed during a real call. Paused isn't
    /// mentioned; a test call while paused shows On a call on the bar.
    func testCallWhilePaused() async throws {
        let rig = await connected()
        rig.engine.pause(.untilResumed)
        await rig.settle()
        rig.engine.sendTestCall()
        await rig.settle()
        XCTAssertEqual(rig.messageLog().last, "0.0 usb Test", "documents: a test call goes out while paused")
        await rig.run(for: 11)
        XCTAssertEqual(rig.messageLog().last, "10.0 usb idle")
        await rig.engine.shutdown()
    }
}
