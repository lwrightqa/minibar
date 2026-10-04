import Foundation
import XCTest
@testable import TinyBarCore

/// QA edge cases for `CallDetector`: flapping, several apps, camera only,
/// pause and resume, and wall-clock jumps. Times are seconds from `t0`.
final class QADetectionTests: XCTestCase {

    // MARK: - Rapid on/off flapping

    func test_flappingBeforeTheStartDelayNeverStartsACall() {
        var run = DetectorRun()
        // On for 1 s, off for 1 s, for a minute: never 3 s in a row.
        var t = 0.0
        while t < 60 {
            run.observe(.mic(Proc.zoom), at: t)
            run.observe(.mic(), at: t + 1)
            t += 2
        }
        run.run(until: 120)
        XCTAssertEqual(run.log, [], "blips shorter than the start delay never count")

        // Half-second flapping, 2.9 s on then 0.1 s off, repeatedly.
        var run2 = DetectorRun()
        t = 0
        while t < 30 {
            run2.observe(.mic(Proc.zoom), at: t)
            run2.observe(.mic(), at: t + 2.9)
            t += 3
        }
        run2.run(until: 60)
        XCTAssertEqual(run2.log, [], "2.9 s on, 0.1 s off: no call")
    }

    func test_flappingDuringACallKeepsOneCall() {
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.run(until: 3)
        // Gaps of 9.9 s, then 1 s back on, for two minutes.
        var t = 10.0
        while t < 120 {
            run.observe(.mic(), at: t)
            run.observe(.mic(Proc.slack), at: t + 9.9)
            t += 10.9
        }
        run.observe(.mic(), at: 130)
        run.run(until: 200)
        XCTAssertEqual(run.log, ["3.0 start Slack", "140.0 end"], "one call, ending exactly 10 s after the last use")
    }

    func test_veryFastFlappingWithinOneSecond() {
        // Ten snapshots a second alternating (a noisy property listener): counts as continuous
        // only if no snapshot shows it idle. Each idle snapshot restarts the start delay.
        var run = DetectorRun()
        for i in 0..<100 {
            run.observe(i % 2 == 0 ? .mic(Proc.slack) : .mic(), at: Double(i) * 0.1)
        }
        run.run(until: 20)
        XCTAssertEqual(run.log, [])
    }

    func test_nameFlappingBetweenTwoCallAppsSendsAChangeEachTime() {
        // Slack and Zoom take turns holding the mic every second, never both idle.
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.run(until: 3)
        for i in 0..<10 {
            run.observe(i % 2 == 0 ? .mic(Proc.zoom) : .mic(Proc.slack), at: 4 + Double(i))
        }
        let changes = run.log.filter { $0.contains("change") }.count
        XCTAssertEqual(changes, 10, "every switch is a new active:true message (documented: no debounce on name changes)")
        XCTAssertFalse(run.log.contains { $0.contains("end") })
    }

    // MARK: - Two apps at once

    func test_twoCallAppsStartingTogether() {
        var run = DetectorRun()
        run.observe(.mic(Proc.zoom, Proc.slack), at: 0)
        run.run(until: 5)
        XCTAssertEqual(run.log, ["3.0 start Slack"], "a tie is broken by key order (slack < zoom), deterministic")
    }

    func test_mostRecentCallAppWinsAndTheNameGoesBackWhenItLeaves() {
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.observe(.mic(Proc.slack, Proc.zoom), at: 20)
        run.observe(.mic(Proc.slack), at: 40)
        run.observe(.mic(), at: 60)
        run.run(until: 100)
        XCTAssertEqual(run.log, ["3.0 start Slack", "20.0 change Zoom", "40.0 change Slack", "70.0 end"])
    }

    func test_callAppBeatsOtherAppAndIgnoredAppIsLeftOut() {
        var run = DetectorRun()
        run.observe(.mic(Proc.voiceMemos, Proc.garageBand), at: 0)
        run.run(until: 3)
        run.observe(.mic(Proc.voiceMemos, Proc.garageBand, Proc.chromeHelper), at: 10)
        run.observe(.mic(Proc.voiceMemos, Proc.garageBand), at: 20)
        run.observe(.mic(Proc.voiceMemos), at: 30)
        run.run(until: 80)
        XCTAssertEqual(run.log, ["3.0 start -", "10.0 change Chrome", "20.0 change -", "40.0 end"],
                       "GarageBand counts without a name; Chrome beats it; Voice Memos never prolongs the call")
        XCTAssertEqual(run.detector.call, nil)
    }

    func test_handOverFromOneAppToAnotherBeforeTheStartDelay() {
        // Slack opens the mic at 0, Zoom at 2, Slack lets go at 2.5: counted use is continuous.
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.observe(.mic(Proc.slack, Proc.zoom), at: 2)
        run.observe(.mic(Proc.zoom), at: 2.5)
        run.run(until: 10)
        XCTAssertEqual(run.log, ["3.0 start Zoom"])
    }

    // MARK: - Camera only

    func test_cameraOnlyCallLifecycle() {
        var run = DetectorRun()
        run.observe(.camera, at: 0)
        run.run(until: 3)
        XCTAssertEqual(run.detector.call?.localName, "Camera")
        XCTAssertNil(run.detector.call?.identity)
        run.observe(ActivitySnapshot(mic: [Proc.slack], cameraInUse: true), at: 10)
        run.observe(.camera, at: 20)
        run.observe(.mic(), at: 30)
        run.run(until: 60)
        XCTAssertEqual(run.log, ["3.0 start -", "10.0 change Slack", "40.0 end"],
                       "the camera call takes Slack's name, keeps it after the mic stops, and ends 10 s after the camera")
    }

    func test_cameraOnlyEndsAtOnceWhenTurnedOffOrModeChanges() {
        var run = DetectorRun()
        run.observe(.camera, at: 0)
        run.run(until: 5)
        run.update({ $0.countCamera = false }, at: 6)
        XCTAssertEqual(run.log, ["3.0 start -", "6.0 end"])

        var run2 = DetectorRun()
        run2.observe(.camera, at: 0)
        run2.run(until: 5)
        run2.update({ $0.mode = .onlyCallApps }, at: 6)
        XCTAssertEqual(run2.log, ["3.0 start -", "6.0 end"])
        // Back to Any app: the camera counts again after the start delay from the change.
        run2.update({ $0.mode = .anyExceptIgnored }, at: 10)
        run2.run(until: 20)
        XCTAssertEqual(run2.log, ["3.0 start -", "6.0 end", "13.0 start -"])
    }

    func test_cameraOnlyWithNamesOffNeverSendsAName() {
        var settings = DetectionSettings.defaults
        settings.sendAppName = false
        var run = DetectorRun(settings: settings)
        run.observe(ActivitySnapshot(mic: [Proc.zoom], cameraInUse: true), at: 0)
        run.observe(.camera, at: 10)
        run.run(until: 15)
        XCTAssertEqual(run.log, ["3.0 start -"])
        XCTAssertEqual(run.detector.call?.localName, "Zoom", "the menu still says Zoom")
    }

    // MARK: - Pause then resume

    func test_pauseDuringACallThenResumeWhileTheMicIsStillOn() {
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.run(until: 3)
        let firstID = run.detector.call?.callID
        run.setPause(.untilResumed, at: 10)
        run.observe(.mic(Proc.slack, Proc.zoom), at: 20)
        run.run(until: 100)
        run.setPause(.notPaused, at: 100)
        run.run(until: 110)
        XCTAssertEqual(run.log, ["3.0 start Slack", "10.0 end", "103.0 start Zoom"])
        XCTAssertNotEqual(run.detector.call?.callID, firstID, "a new call after the pause")
    }

    func test_timedPauseEndsByItselfAndAPendingStartIsMeasuredFromThere() {
        var run = DetectorRun()
        run.setPause(.until(run.date(3600)), at: 0)
        run.observe(.mic(Proc.zoom), at: 1)
        run.run(until: 3599)
        XCTAssertEqual(run.log, [])
        run.run(until: 3700)
        XCTAssertEqual(run.log, ["3603.0 start Zoom"])
        XCTAssertEqual(run.detector.pause, .notPaused)
    }

    func test_pauseInsideTheStartDelayCancelsThePendingStart() {
        var run = DetectorRun()
        run.observe(.mic(Proc.zoom), at: 0)
        run.setPause(.untilResumed, at: 2)
        run.setPause(.notPaused, at: 2.5)
        run.run(until: 10)
        XCTAssertEqual(run.log, ["5.5 start Zoom"], "the start delay starts over at the resume")
    }

    func test_rapidPauseResumeToggling() {
        var run = DetectorRun()
        run.observe(.mic(Proc.zoom), at: 0)
        run.run(until: 3)
        for i in 0..<5 {
            run.setPause(.untilResumed, at: 4 + Double(i) * 2)
            run.setPause(.notPaused, at: 5 + Double(i) * 2)
        }
        run.run(until: 30)
        XCTAssertEqual(run.log, ["3.0 start Zoom", "4.0 end", "16.0 start Zoom"],
                       "each resume restarts the start delay; no start while resumes last under 3 s")
    }

    // MARK: - Wall-clock jumps

    // QA first showed these on the detector fed wall-clock dates: set back
    // during the end delay, the call stayed on for the size of the jump; set
    // forward, elapsed_s grew by it. The engine now feeds the detector its
    // clock's monotonic time (`TinyClock.now()`), so the jumps below change
    // only `wallClockOffset()`, as on a Mac.

    func test_clockSetBackDuringTheEndDelayEndsTheCallOnTime() async throws {
        try await QAClockScenarios().setBackDuringTheEndDelay()
    }

    func test_clockSetForwardDoesntInflateElapsedSeconds() async throws {
        try await QAClockScenarios().setForwardDuringACall()
    }

    func test_pausesAndTimesFollowTheMacsClock() async throws {
        try await QAClockScenarios().pausesAndTimes()
    }
}

@MainActor
final class QAClockScenarios {
    func setBackDuringTheEndDelay() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        await rig.plugIn()
        await rig.micUse(Proc.slack)
        await rig.run(for: 20)
        await rig.micUse()                       // hung up at 20: ends at 30
        await rig.run(for: 5)
        rig.clock.changeWallClock(by: -3600)     // set back an hour at 25
        await rig.run(for: 10)
        XCTAssertNil(rig.engine.state.call, "the call ends 10 s after the hang-up")
        XCTAssertEqual(rig.messageLog().last, "30.0 usb idle")
        await rig.engine.shutdown()
    }

    func setForwardDuringACall() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        await rig.plugIn()
        await rig.micUse(Proc.slack)
        await rig.run(for: 3)                    // the call starts at 3
        rig.clock.changeWallClock(by: 2 * 3600)
        await rig.run(for: 30)                   // heartbeat at 33
        XCTAssertEqual(rig.messageLog().suffix(2), ["3.0 usb Slack", "33.0 usb Slack"])
        XCTAssertEqual(rig.bar.messages.last?.request.elapsedS, 30)
        XCTAssertEqual(rig.engine.menuContent(showDetails: false).line1, "On a call · Slack · 1m")
        await rig.engine.shutdown()
    }

    /// "Paused until" is shown on the Mac's clock and lasts an hour of real
    /// time; "For the Rest of Today" ends at the Mac's midnight.
    func pausesAndTimes() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        rig.engine.pause(.oneHour)               // 2:12 PM
        XCTAssertEqual(plain(rig.engine.menuContent(showDetails: false).line1), "Paused until 3:12 PM")
        rig.clock.changeWallClock(by: -3600)
        XCTAssertEqual(plain(rig.engine.menuContent(showDetails: false).line1), "Paused until 2:12 PM",
                       "the same moment, on the Mac's new clock")
        await rig.run(for: 3599, step: 60)
        XCTAssertTrue(rig.engine.state.isPaused(at: rig.clock.now()))
        await rig.run(for: 1)
        XCTAssertFalse(rig.engine.state.isPaused(at: rig.clock.now()), "an hour of real time")
        // Saved on the Mac's clock.
        rig.engine.pause(.oneHour)
        let saved = try XCTUnwrap(rig.store.load()?.pause.endsAt)
        XCTAssertEqual(saved, rig.clock.wallNow().addingTimeInterval(3600))

        // Now 2:12 PM + 1 h on the clock = 2:12 PM wall time again (set back
        // an hour). Set it forward 10 hours: 12:12 AM on October 5.
        rig.engine.resume()
        rig.clock.changeWallClock(by: 10 * 3600)
        rig.engine.pause(.restOfToday)
        let end = try XCTUnwrap(rig.engine.state.settings.pause.endsAt)
        XCTAssertEqual(end.timeIntervalSince(rig.clock.now()), 23 * 3600 + 48 * 60, accuracy: 0.001,
                       "midnight on the Mac's clock, 23 h 48 min away")
        await rig.engine.shutdown()
    }
}

/// Clock times with an ordinary space (ICU puts U+202F before AM and PM).
func plain(_ text: String) -> String {
    text.replacingOccurrences(of: "\u{202F}", with: " ")
}
