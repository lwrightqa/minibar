import XCTest
@testable import TinyBarCore

/// Deciding a call: docs/mac-app.md criteria 1 to 11, with `CallDetector`
/// driven like the engine drives it (`DetectorRun`), and `AppCatalog`.
final class DetectionTests: XCTestCase {

    // MARK: - Criterion 1: start delay

    func test_C01_startDelay() {
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.run(until: 2.999)
        XCTAssertEqual(run.log, [], "nothing before 3.0 s")
        run.run(until: 60)
        XCTAssertEqual(run.log, ["3.0 start Slack"], "exactly one active:true, at 3.0 s")
        guard case .started(let call) = run.events.first?.event else { return XCTFail("no start") }
        XCTAssertEqual(call.startedAt, run.date(3), "the call starts when active:true is sent")
        XCTAssertEqual(call.callID, 1)
        XCTAssertEqual(call.elapsedSeconds(at: run.date(33.7)), 30)
    }

    func test_C01_useThatStopsAt2_9SecondsSendsNothing() {
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.observe(.idle, at: 2.9)
        run.run(until: 120)
        XCTAssertEqual(run.log, [])
        XCTAssertNil(run.detector.call)
        XCTAssertNil(run.detector.nextDeadline, "nothing left waiting")
    }

    func test_C01_aLateSnapshotStillCountsTheTimeBeforeIt() {
        // The engine missed the 3.0 s tick and the next snapshot (idle) comes at
        // 3.5 s: the activity lasted 3 s, so the call starts at 3.0 and then
        // ends after the end delay.
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.detector.observe(.idle, at: run.date(3.5)).forEach { _ in }
        XCTAssertNotNil(run.detector.call)
        XCTAssertEqual(run.detector.nextDeadline, run.date(13.5))
    }

    // MARK: - Criterion 2: end delay

    func test_C02_endDelay() {
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.observe(.idle, at: 10)
        run.observe(.mic(Proc.slack), at: 19.9)  // a 9.9 s gap
        run.run(until: 40)
        XCTAssertEqual(run.log, ["3.0 start Slack"], "a 9.9 s gap sends nothing and the call goes on")

        run.observe(.idle, at: 50)
        run.run(until: 59.99)
        XCTAssertEqual(run.log, ["3.0 start Slack"])
        XCTAssertEqual(run.detector.observation.idleSince, run.date(50))
        run.run(until: 200)
        XCTAssertEqual(run.log, ["3.0 start Slack", "60.0 end"], "exactly one active:false, 10.0 s after the activity stopped")
    }

    func test_C02_airPodsGapIsOneCall() {
        // mac-app.md "Timing" examples: a Zoom call where the mic is idle for 2 s.
        var run = DetectorRun()
        run.observe(.mic(Proc.zoom), at: 0)
        run.observe(.idle, at: 600)
        run.observe(.mic(Proc.zoom), at: 602)
        run.observe(.idle, at: 1200)
        run.run(until: 2000)
        XCTAssertEqual(run.log, ["3.0 start Zoom", "1210.0 end"])
    }

    func test_C02_slackHuddleFromTheSpec() {
        // Mic on at 2:04:00 PM, off at 2:31:12 PM → On a call from 2:04:03 to 2:31:22.
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.observe(.idle, at: 27 * 60 + 12)
        run.run(until: 3600)
        XCTAssertEqual(run.log, ["3.0 start Slack", "1642.0 end"])
    }

    // MARK: - Criterion 3: ignored apps

    func test_C03_ignoredAppsNeverCount() {
        var run = DetectorRun()
        run.observe(.mic(Proc.siri), at: 0)
        run.observe(.idle, at: 20)
        run.observe(.mic(Proc.dictation), at: 30)
        run.observe(.idle, at: 90)
        run.observe(.mic(Proc.voiceMemos), at: 100)
        run.observe(.idle, at: 400)
        run.run(until: 1000)
        XCTAssertEqual(run.log, [], "Siri 20 s, Dictation 60 s and Voice Memos 5 minutes")
        XCTAssertEqual(run.detector.observation.micUses, [])
    }

    func test_C03_ignoredAppsDontProlongACall() {
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.observe(.mic(Proc.siri), at: 20)           // Slack stops, Siri starts at once
        run.observe(.mic(Proc.siri, Proc.dictation), at: 22)
        run.observe(.mic(Proc.voiceMemos), at: 25)
        run.run(until: 400)
        XCTAssertEqual(run.log, ["3.0 start Slack", "30.0 end"], "the call ends 10 s after Slack stopped")
    }

    func test_C03_ignoredAppsShowAsNotCounted() {
        var run = DetectorRun()
        run.observe(.mic(Proc.voiceMemos), at: 0)
        let uses = run.detector.observation.micUses
        XCTAssertEqual(uses.count, 1)
        XCTAssertEqual(uses.first?.counted, false)
        XCTAssertEqual(uses.first?.classification.category, .ignored)
        XCTAssertEqual(uses.first?.classification.identity.displayName, "Voice Memos")
        XCTAssertNil(run.detector.observation.pendingSince)
    }

    // MARK: - Criterion 4: matching processes to apps

    func test_C04_helperProcessesMapToTheirApp() {
        let catalog = AppCatalog.defaults
        let chrome = catalog.classify(Proc.chromeHelper)
        XCTAssertEqual(chrome.category, .callApp(shortName: "Chrome"))
        XCTAssertEqual(chrome.identity.key, "chrome")
        XCTAssertEqual(chrome.identity.displayName, "Google Chrome")

        // Stage 1 alone: the helper's own bundle ID, by prefix and a dot.
        let bareHelper = MicProcess(pid: 1, bundleID: "com.google.Chrome.helper")
        XCTAssertEqual(catalog.classify(bareHelper).category, .callApp(shortName: "Chrome"))

        // Stage 2: an unlisted bundle ID, inside Slack.app.
        let path = Proc.slackHelper.executablePath!
        XCTAssertEqual(AppPaths.outermostAppPath(path), "/Applications/Slack.app")
        let slack = catalog.classify(Proc.slackHelper)
        XCTAssertEqual(slack.category, .callApp(shortName: "Slack"))
        XCTAssertEqual(slack.identity.key, "slack")
        XCTAssertEqual(slack.identity.bundleID, "com.tinyspeck.slackmacgap", "the outer app's bundle ID, for new list entries")

        // Stage 3: a daemon by process name.
        XCTAssertEqual(catalog.classify(Proc.faceTimeAudio).category, .callApp(shortName: "FaceTime"))
        XCTAssertEqual(catalog.classify(Proc.siri).category, .ignored)
    }

    func test_C04_bundleIDMatchingRules() {
        let catalog = AppCatalog.defaults
        XCTAssertEqual(catalog.classify(MicProcess(pid: 1, bundleID: "com.google.Chrome")).category, .callApp(shortName: "Chrome"))
        XCTAssertEqual(catalog.classify(MicProcess(pid: 1, bundleID: "COM.GOOGLE.CHROME.helper")).category,
                       .callApp(shortName: "Chrome"), "bundle IDs ignore case")
        XCTAssertEqual(catalog.classify(MicProcess(pid: 1, bundleID: "com.google.Chromecast")).category, .other,
                       "a prefix must be followed by a dot")
        XCTAssertEqual(catalog.classify(MicProcess(pid: 1, bundleID: "com.google")).category, .other)
        XCTAssertEqual(catalog.classify(MicProcess(pid: 1, bundleID: "", executablePath: "/usr/libexec/avconferenced")).category,
                       .callApp(shortName: "FaceTime"), "an empty bundle ID is no bundle ID")
        XCTAssertEqual(catalog.classify(MicProcess(pid: 1, executablePath: "/usr/libexec/AVConferenced")).category, .other,
                       "process names are case-sensitive")
    }

    func test_C04_outermostAppPath() {
        XCTAssertEqual(AppPaths.outermostAppPath("/Applications/zoom.us.app/Contents/MacOS/zoom.us"), "/Applications/zoom.us.app")
        XCTAssertEqual(AppPaths.outermostAppPath("/Users/me/Applications/Arc.APP/Contents/MacOS/Arc"), "/Users/me/Applications/Arc.APP")
        XCTAssertNil(AppPaths.outermostAppPath("/usr/libexec/avconferenced"))
        XCTAssertNil(AppPaths.outermostAppPath("/Applications/Foo.application/Contents/MacOS/Foo"))
        XCTAssertNil(AppPaths.outermostAppPath("/Applications/.app/x"))
        XCTAssertNil(AppPaths.outermostAppPath(""))
    }

    func test_C04_unknownAppsAndIgnoredWins() {
        var catalog = AppCatalog.defaults
        let garage = catalog.classify(Proc.garageBand)
        XCTAssertEqual(garage.category, .other)
        XCTAssertEqual(garage.identity, AppIdentity(key: "com.apple.garageband10", displayName: "GarageBand",
                                                    bundleID: "com.apple.garageband10", processName: "GarageBand"))
        // A daemon with no bundle ID and no display name is named by its process.
        let daemon = catalog.classify(MicProcess(pid: 9, executablePath: "/usr/sbin/mysteryd"))
        XCTAssertEqual(daemon.identity.key, "mysteryd")
        XCTAssertEqual(daemon.identity.displayName, "mysteryd")
        XCTAssertEqual(catalog.classify(MicProcess(pid: 9)).identity.key, "pid:9")

        // On both lists: ignored wins.
        catalog.ignoredApps.append(IgnoredApp(id: "zoom-too", displayName: "zoom.us", matchers: [.bundleID("us.zoom.xos")]))
        XCTAssertEqual(catalog.classify(Proc.zoom).category, .ignored)
    }

    // MARK: - Criterion 5: counting modes

    func test_C05_countingModes() {
        var any = DetectorRun()
        any.observe(.mic(Proc.garageBand), at: 0)
        any.run(until: 10)
        XCTAssertEqual(any.log, ["3.0 start -"], "an unlisted app counts, without a name")
        XCTAssertEqual(any.detector.call?.localName, "GarageBand", "the menu still shows its local name")

        var only = DetectorRun(settings: DetectionSettings(mode: .onlyCallApps))
        only.observe(.mic(Proc.garageBand), at: 0)
        only.observe(ActivitySnapshot(mic: [Proc.garageBand], cameraInUse: true), at: 1)
        only.run(until: 60)
        XCTAssertEqual(only.log, [], "neither the unlisted app nor the camera counts")
        XCTAssertEqual(only.detector.observation.micUses.first?.counted, false)
        XCTAssertFalse(only.detector.observation.cameraCounted)

        only.observe(ActivitySnapshot(mic: [Proc.garageBand, Proc.slack], cameraInUse: true), at: 100)
        only.run(until: 110)
        XCTAssertEqual(only.log, ["103.0 start Slack"], "call apps still count")
    }

    // MARK: - Criterion 6: camera

    func test_C06_camera() {
        var on = DetectorRun()
        on.observe(.camera, at: 0)
        on.run(until: 30)
        XCTAssertEqual(on.log, ["3.0 start -"])
        XCTAssertEqual(on.detector.call?.localName, "Camera")
        XCTAssertNil(on.detector.call?.identity)

        var off = DetectorRun(settings: DetectionSettings(countCamera: false))
        off.observe(.camera, at: 0)
        off.run(until: 30)
        XCTAssertEqual(off.log, [])
        XCTAssertTrue(off.detector.observation.cameraInUse)
        XCTAssertFalse(off.detector.observation.cameraCounted)
    }

    // MARK: - Criterion 7: names

    func test_C07_names() throws {
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.run(until: 5)
        XCTAssertEqual(run.detector.call?.app, "Slack")

        var silent = DetectorRun(settings: DetectionSettings(sendAppName: false))
        silent.observe(.mic(Proc.slack), at: 0)
        silent.observe(.mic(Proc.slack, Proc.zoom), at: 10)
        silent.observe(.camera, at: 20)
        silent.run(until: 60)
        XCTAssertFalse(silent.events.isEmpty)
        for (_, event) in silent.events {
            switch event {
            case .started(let call), .changed(let call), .ended(let call): XCTAssertNil(call.app)
            }
        }
        XCTAssertEqual(silent.detector.call?.localName, "Zoom", "the menu keeps the name; only the bar doesn't get it")

        // And on the wire: no `app` key in any message.
        if case .started(let call) = silent.events.first?.event {
            let request = ReportedCall(call).request(client: "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", session: "q8Zr2Lx0", seq: 1, at: call.startedAt)
            XCTAssertFalse(try WireJSON.encodeString(request).contains("\"app\""))
        } else {
            XCTFail("no call")
        }

        // An edited name over 24 characters is refused.
        var catalog = AppCatalog.defaults
        XCTAssertThrowsError(try catalog.rename(callAppID: "slack", to: String(repeating: "x", count: 25))) {
            XCTAssertEqual($0 as? SettingsError, .badAppName(.tooLong(limit: 24)))
        }
        try catalog.rename(callAppID: "slack", to: String(repeating: "x", count: 24))
        XCTAssertEqual(catalog.callApps.first?.shortName.count, 24)
        var settings = DetectionSettings.defaults
        settings.catalog.callApps[0].shortName = String(repeating: "y", count: 25)
        XCTAssertThrowsError(try settings.validate()) {
            XCTAssertEqual($0 as? SettingsError, .badAppName(.tooLong(limit: 24)))
        }
    }

    func test_C07_renameAndAddRules() throws {
        var catalog = AppCatalog.defaults
        try catalog.rename(callAppID: "zoom", to: "  Zoom call  ")
        XCTAssertEqual(catalog.callApps.first { $0.id == "zoom" }?.shortName, "Zoom call", "trimmed")
        try catalog.rename(callAppID: "zoom", to: "   ")
        XCTAssertEqual(catalog.callApps.first { $0.id == "zoom" }?.shortName, "Zoom", "empty puts back the default")
        XCTAssertThrowsError(try catalog.rename(callAppID: "zoom", to: "Zoöm")) {
            XCTAssertEqual($0 as? SettingsError, .badAppName(.unsupportedCharacters(["ö"])))
        }
        try catalog.rename(callAppID: "nope", to: "Whatever")
        XCTAssertEqual(catalog, {
            var expected = AppCatalog.defaults
            expected.callApps[1].shortName = "Zoom"
            return expected
        }())

        let custom = CallApp(id: "com.example.Talk", displayName: "Example Talk", shortName: " Talk ",
                             matchers: [.bundleID("com.example.Talk")])
        try catalog.addCallApp(custom)
        XCTAssertEqual(catalog.callApps.last?.shortName, "Talk")
        try catalog.rename(callAppID: "com.example.Talk", to: "")
        XCTAssertEqual(catalog.callApps.last?.shortName, "Example Talk", "a user-added app's default is its own name")
        XCTAssertThrowsError(try catalog.addCallApp(CallApp(id: "x", displayName: "X", shortName: "", matchers: []))) {
            XCTAssertEqual($0 as? SettingsError, .badAppName(.empty))
        }
    }

    func test_C07_movingBetweenLists() throws {
        var catalog = AppCatalog.defaults
        // Adding Voice Memos as a call app moves it off the ignored list.
        try catalog.addCallApp(CallApp(id: "com.apple.VoiceMemos", displayName: "Voice Memos", shortName: "Memo",
                                       matchers: [.bundleID("com.apple.VoiceMemos")]))
        XCTAssertFalse(catalog.ignoredApps.contains { $0.id == "voicememos" })
        XCTAssertEqual(catalog.classify(Proc.voiceMemos).category, .callApp(shortName: "Memo"))

        // Adding Zoom to the ignored list moves it off the call apps.
        catalog.addIgnoredApp(IgnoredApp(id: "us.zoom.xos", displayName: "zoom.us", matchers: [.bundleID("us.zoom.xos")]))
        XCTAssertFalse(catalog.callApps.contains { $0.id == "zoom" })
        XCTAssertEqual(catalog.classify(Proc.zoom).category, .ignored)

        catalog.removeIgnoredApp(id: "us.zoom.xos")
        XCTAssertEqual(catalog.classify(Proc.zoom).category, .other, "removed from both lists: an ordinary app")
        catalog.removeCallApp(id: "slack")
        XCTAssertEqual(catalog.classify(Proc.slack).category, .other)
        XCTAssertEqual(catalog.classify(Proc.slack).identity.key, "com.tinyspeck.slackmacgap")
    }

    // MARK: - Criterion 8: several apps, name changes

    func test_C08_nameChangesKeepOneCall() {
        // Meet in Chrome, then straight into a Zoom call with no gap.
        var run = DetectorRun()
        run.observe(.mic(Proc.chromeHelper), at: 0)
        run.observe(.mic(Proc.zoom), at: 300)
        run.run(until: 400)
        XCTAssertEqual(run.log, ["3.0 start Chrome", "300.0 change Zoom"], "one call, one extra active:true, no active:false")
        guard case .started(let first) = run.events[0].event, case .changed(let second) = run.events[1].event else {
            return XCTFail("unexpected events \(run.log)")
        }
        XCTAssertEqual(first.callID, second.callID, "same call_id")
        XCTAssertEqual(first.startedAt, second.startedAt)
    }

    func test_C08_slackThenZoomJoining() {
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.observe(.mic(Proc.slack, Proc.zoom), at: 10)        // Zoom starts using the mic last
        run.observe(.mic(Proc.slack, Proc.zoom, Proc.garageBand), at: 20)  // a call app beats others
        run.observe(.mic(Proc.zoom, Proc.garageBand), at: 30)
        run.run(until: 40)
        XCTAssertEqual(run.log, ["3.0 start Slack", "10.0 change Zoom"])
        XCTAssertEqual(run.detector.observation.micUses.map(\.classification.identity.displayName),
                       ["GarageBand", "zoom.us"], "most recent first")
    }

    func test_C08_cameraAfterTheMicKeepsTheLastName() {
        var run = DetectorRun()
        run.observe(ActivitySnapshot(mic: [Proc.slack], cameraInUse: true), at: 0)
        run.observe(.camera, at: 60)        // muted: Slack let go of the mic, the camera is on
        run.run(until: 200)
        XCTAssertEqual(run.log, ["3.0 start Slack"])
        XCTAssertEqual(run.detector.call?.app, "Slack")
        XCTAssertEqual(run.detector.call?.identity?.key, "slack")
        run.observe(.idle, at: 300)
        run.run(until: 400)
        XCTAssertEqual(run.log, ["3.0 start Slack", "310.0 end"])
    }

    func test_C08_otherAppReplacingACallAppLosesTheName() {
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.observe(.mic(Proc.garageBand), at: 20)
        run.run(until: 30)
        XCTAssertEqual(run.log, ["3.0 start Slack", "20.0 change -"])
        XCTAssertEqual(run.detector.call?.localName, "GarageBand")
    }

    // MARK: - Criterion 9: settings apply at once

    func test_C09_settingsApplyAtOnce() throws {
        // Ignoring the call's app.
        var ignore = DetectorRun()
        ignore.observe(.mic(Proc.slack), at: 0)
        ignore.run(until: 20)
        let slack = try XCTUnwrap(ignore.detector.call?.identity)
        ignore.update({ $0.catalog.ignore(slack) }, at: 20)
        XCTAssertEqual(ignore.log, ["3.0 start Slack", "20.0 end"], "active:false at once")
        ignore.run(until: 100)
        XCTAssertEqual(ignore.log.count, 2, "and Slack doesn't start a new call")

        // Turning off the camera during a camera-only call.
        var camera = DetectorRun()
        camera.observe(.camera, at: 0)
        camera.update({ $0.countCamera = false }, at: 15)
        XCTAssertEqual(camera.log, ["3.0 start -", "15.0 end"])

        // "Only call apps" during a call from an unlisted app.
        var mode = DetectorRun()
        mode.observe(.mic(Proc.garageBand), at: 0)
        mode.update({ $0.mode = .onlyCallApps }, at: 8)
        XCTAssertEqual(mode.log, ["3.0 start -", "8.0 end"])
    }

    func test_C09_aChangeDuringTheEndDelayDoesntEndTheCallEarly() {
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.observe(.idle, at: 20)
        run.update({ $0.sendAppName = true; $0.startDelay = 5 }, at: 22)
        run.run(until: 29.9)
        XCTAssertEqual(run.log, ["3.0 start Slack"])
        run.update({ $0.endDelay = 3 }, at: 25)  // shorter: already over
        XCTAssertEqual(run.log, ["3.0 start Slack", "25.0 end"])
    }

    func test_C09_ignoringTheAppKeepsACameraCallWithoutItsName() throws {
        var run = DetectorRun()
        run.observe(ActivitySnapshot(mic: [Proc.zoom], cameraInUse: true), at: 0)
        run.run(until: 10)
        let zoom = try XCTUnwrap(run.detector.call?.identity)
        run.update({ $0.catalog.ignore(zoom) }, at: 10)
        XCTAssertEqual(run.log, ["3.0 start Zoom", "10.0 change -"], "the camera still counts, so the call goes on")
        XCTAssertEqual(run.detector.call?.localName, "Camera")
    }

    func test_C09_delaysOutOfRangeAreRefused() {
        func check(start: Int, end: Int) -> SettingsError? {
            do {
                try DetectionSettings(startDelay: start, endDelay: end).validate()
                return nil
            } catch {
                return error
            }
        }
        XCTAssertNil(check(start: 0, end: 3))
        XCTAssertNil(check(start: 30, end: 60))
        XCTAssertEqual(check(start: -1, end: 10), .startDelayOutOfRange(-1))
        XCTAssertEqual(check(start: 31, end: 10), .startDelayOutOfRange(31))
        XCTAssertEqual(check(start: 3, end: 2), .endDelayOutOfRange(2))
        XCTAssertEqual(check(start: 3, end: 61), .endDelayOutOfRange(61))
        XCTAssertNoThrow(try DetectionSettings.defaults.validate())
    }

    func test_C09_settingsTurningAnAppOnStartAfterTheStartDelay() {
        var run = DetectorRun()
        run.observe(.mic(Proc.voiceMemos), at: 0)
        run.update({ $0.catalog.removeIgnoredApp(id: "voicememos") }, at: 30)
        run.run(until: 32.9)
        XCTAssertEqual(run.log, [])
        run.run(until: 40)
        XCTAssertEqual(run.log, ["33.0 start -"], "counted from the change, not from when it started")
    }

    // MARK: - Criterion 10: pause

    func test_C10_pause() {
        let tz = TimeZone(identifier: "America/Los_Angeles")!
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.run(until: 100)
        let pause = PauseChoice.oneHour.pauseState(at: run.date(100), timeZone: tz)
        XCTAssertEqual(pause, .until(run.date(3700)))
        run.setPause(pause, at: 100)
        XCTAssertEqual(run.log, ["3.0 start Slack", "100.0 end"], "active:false at once")
        XCTAssertTrue(run.detector.pause.isPaused(at: run.date(100)))

        // The mic stays in use all through the pause; snapshots keep coming.
        for second in stride(from: 200.0, to: 3700, by: 500) {
            run.observe(.mic(Proc.slack), at: second)
        }
        XCTAssertEqual(run.log.count, 2, "no call starts while paused")
        XCTAssertNil(run.detector.observation.pendingSince)
        XCTAssertEqual(run.detector.nextDeadline, run.date(3700), "the pause ends by itself")

        run.run(until: 3702.9)
        XCTAssertEqual(run.detector.pause, .notPaused)
        XCTAssertEqual(run.log.count, 2)
        run.run(until: 3800)
        XCTAssertEqual(run.log, ["3.0 start Slack", "100.0 end", "3703.0 start Slack"],
                       "a mic already in use starts a call 3 s after the pause ended")
        guard case .started(let second) = run.events.last?.event else { return XCTFail() }
        XCTAssertEqual(second.callID, 2, "a new call_id")
    }

    func test_C10_resumeByHand() {
        var run = DetectorRun(pause: .untilResumed)
        run.observe(.mic(Proc.zoom), at: 0)
        run.run(until: 1000)
        XCTAssertEqual(run.log, [])
        XCTAssertNil(run.detector.nextDeadline)
        run.setPause(.notPaused, at: 1000)
        run.run(until: 1010)
        XCTAssertEqual(run.log, ["1003.0 start Zoom"])
    }

    func test_pauseChoices() throws {
        let la = try XCTUnwrap(TimeZone(identifier: "America/Los_Angeles"))
        let now = ManualClock.apiExampleStart  // 2026-10-04T14:12:00-07:00
        XCTAssertEqual(PauseChoice.oneHour.pauseState(at: now, timeZone: la), .until(now.addingTimeInterval(3600)))
        XCTAssertEqual(PauseChoice.untilResumed.pauseState(at: now, timeZone: la), .untilResumed)
        XCTAssertEqual(PauseChoice.restOfToday.pauseState(at: now, timeZone: la),
                       .restOfToday(endsAt: try XCTUnwrap(RFC3339.date(from: "2026-10-05T00:00:00-07:00"))))

        // The day the clocks go back (1 November 2026 in Los Angeles) is 25 hours long.
        let beforeFallBack = try XCTUnwrap(RFC3339.date(from: "2026-11-01T23:30:00-08:00"))
        XCTAssertEqual(PauseChoice.restOfToday.pauseState(at: beforeFallBack, timeZone: la).endsAt,
                       RFC3339.date(from: "2026-11-02T00:00:00-08:00"))
        let earlyFallBack = try XCTUnwrap(RFC3339.date(from: "2026-11-01T00:30:00-07:00"))
        XCTAssertEqual(PauseChoice.restOfToday.pauseState(at: earlyFallBack, timeZone: la).endsAt,
                       RFC3339.date(from: "2026-11-02T00:00:00-08:00"))
        // Spring forward (8 March 2026): 23 hours.
        let spring = try XCTUnwrap(RFC3339.date(from: "2026-03-08T01:30:00-08:00"))
        XCTAssertEqual(PauseChoice.restOfToday.pauseState(at: spring, timeZone: la).endsAt,
                       RFC3339.date(from: "2026-03-09T00:00:00-07:00"))

        // Where the clock change skips midnight itself (Santiago, first Sunday
        // of September 2026), the pause ends at the first moment of the day.
        if let santiago = TimeZone(identifier: "America/Santiago"),
           santiago.secondsFromGMT(for: try XCTUnwrap(RFC3339.date(from: "2026-09-06T06:00:00Z"))) == -3 * 3600 {
            let evening = try XCTUnwrap(RFC3339.date(from: "2026-09-05T22:00:00-04:00"))
            XCTAssertEqual(PauseChoice.restOfToday.pauseState(at: evening, timeZone: santiago).endsAt,
                           RFC3339.date(from: "2026-09-06T01:00:00-03:00"))
        }

        // Exactly at midnight: the rest of today is a whole day.
        let midnight = try XCTUnwrap(RFC3339.date(from: "2026-10-05T00:00:00-07:00"))
        XCTAssertEqual(PauseChoice.restOfToday.pauseState(at: midnight, timeZone: la).endsAt,
                       RFC3339.date(from: "2026-10-06T00:00:00-07:00"))

        // isPaused and endsAt.
        let state = PauseChoice.oneHour.pauseState(at: now, timeZone: la)
        XCTAssertTrue(state.isPaused(at: now.addingTimeInterval(3599)))
        XCTAssertFalse(state.isPaused(at: now.addingTimeInterval(3600)))
        XCTAssertFalse(PauseState.notPaused.isPaused(at: now))
        XCTAssertTrue(PauseState.untilResumed.isPaused(at: .distantFuture))
    }

    // MARK: - Criterion 11: start delay 0

    func test_C11_zeroStartDelay() {
        var run = DetectorRun(settings: DetectionSettings(startDelay: 0))
        run.observe(.mic(Proc.zoom), at: 5)
        XCTAssertEqual(run.log, ["5.0 start Zoom"], "the first snapshot with activity starts it")
        XCTAssertEqual(run.detector.call?.startedAt, run.date(5))

        var camera = DetectorRun(settings: DetectionSettings(startDelay: 0))
        camera.observe(.camera, at: 1)
        XCTAssertEqual(camera.log, ["1.0 start -"])
    }

    // MARK: - What the menu reads

    func test_observationWhileWaitingForTheStartDelay() {
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.observe(.mic(Proc.slack, Proc.voiceMemos), at: 1)
        let observation = run.detector.observation
        XCTAssertEqual(observation.pendingSince, run.date(0))
        XCTAssertNil(observation.idleSince)
        XCTAssertEqual(observation.micUses.map(\.counted), [false, true], "Voice Memos (most recent) isn't counted")
        XCTAssertEqual(observation.micUses.last?.since, run.date(0))
        XCTAssertNil(run.detector.call)
    }

    func test_severalProcessesOfOneAppAreOneUse() {
        var run = DetectorRun()
        run.observe(.mic(Proc.slack), at: 0)
        run.observe(.mic(Proc.slack, Proc.slackHelper), at: 2)
        XCTAssertEqual(run.detector.observation.micUses.count, 1)
        XCTAssertEqual(run.detector.observation.micUses.first?.since, run.date(0))
    }

    // MARK: - Criterion 26 (core part): Don't Count and Count Again

    func test_C26_dontCountAndCountAgain() throws {
        var catalog = AppCatalog.defaults
        let slack = catalog.classify(Proc.slack).identity

        let edit = catalog.ignore(slack)
        XCTAssertEqual(edit.catalogBefore, .defaults)
        XCTAssertFalse(catalog.callApps.contains { $0.id == "slack" })
        XCTAssertEqual(catalog.ignoredApps.last?.id, "slack")
        XCTAssertEqual(catalog.classify(Proc.slack).category, .ignored)
        XCTAssertEqual(catalog.classify(Proc.slack).identity.key, "slack", "the same key, so undo can find it")

        let afterOnce = catalog
        catalog.ignore(catalog.classify(Proc.slack).identity)
        XCTAssertEqual(catalog, afterOnce, "doing it twice changes nothing the second time")

        // Another edit made since stays when Slack is put back.
        try catalog.rename(callAppID: "zoom", to: "Zm")
        catalog.undo(edit)
        XCTAssertEqual(catalog.callApps.map(\.id), AppCatalog.defaults.callApps.map(\.id), "back in its old place")
        XCTAssertEqual(catalog.ignoredApps, AppCatalog.defaults.ignoredApps)
        XCTAssertEqual(catalog.callApps.first { $0.id == "zoom" }?.shortName, "Zm")
        XCTAssertEqual(catalog.classify(Proc.slack).category, .callApp(shortName: "Slack"))
    }

    func test_C26_dontCountAnOtherApp() {
        var catalog = AppCatalog.defaults
        let garage = catalog.classify(Proc.garageBand).identity
        let edit = catalog.ignore(garage)
        XCTAssertEqual(catalog.ignoredApps.last,
                       IgnoredApp(id: "com.apple.garageband10", displayName: "GarageBand",
                                  matchers: [.bundleID("com.apple.garageband10")]))
        XCTAssertEqual(catalog.classify(Proc.garageBand).category, .ignored)
        catalog.undo(edit)
        XCTAssertEqual(catalog, .defaults)

        // A daemon with no bundle ID is listed by its process name.
        let daemon = catalog.classify(MicProcess(pid: 5, executablePath: "/usr/sbin/mysteryd")).identity
        catalog.ignore(daemon)
        XCTAssertEqual(catalog.ignoredApps.last?.matchers, [.processName("mysteryd")])
        // Nothing to list it by: nothing changes.
        var unchanged = AppCatalog.defaults
        unchanged.ignore(AppIdentity(key: "pid:7", displayName: "Process 7"))
        XCTAssertEqual(unchanged, .defaults)
    }

    // MARK: - Criterion 28 (core part): seen apps, in memory only

    func test_C28_seenAppsInMemory() {
        var run = DetectorRun()
        XCTAssertEqual(run.detector.seenApps, [])
        run.observe(.mic(Proc.slack), at: 0)
        run.observe(.mic(Proc.slack, Proc.voiceMemos), at: 5)
        run.observe(.idle, at: 10)
        run.observe(.mic(Proc.garageBand), at: 20)
        XCTAssertEqual(run.detector.seenApps.map(\.identity.displayName), ["GarageBand", "Voice Memos", "Slack"])
        XCTAssertEqual(run.detector.seenApps.map(\.category), [.other, .ignored, .callApp(shortName: "Slack")])
        XCTAssertEqual(run.detector.seenApps.last?.lastSeen, run.date(5))

        // Slack again: it moves to the top.
        run.observe(.mic(Proc.slack), at: 30)
        XCTAssertEqual(run.detector.seenApps.first?.identity.key, "slack")

        // Categories follow the lists.
        run.update({ $0.catalog.removeIgnoredApp(id: "voicememos") }, at: 40)
        XCTAssertEqual(run.detector.seenApps.first { $0.identity.displayName == "Voice Memos" }?.category, .other)
        XCTAssertEqual(run.detector.seenApps.count, 3)

        XCTAssertEqual(CallDetector().seenApps, [], "a new launch starts empty")
    }

    func test_callIDsAndReservedIDs() {
        var detector = CallDetector(firstCallID: 41)
        XCTAssertEqual(detector.reserveCallID(), 41)
        let t = ManualClock.apiExampleStart
        detector.observe(.mic(Proc.slack), at: t)
        let events = detector.tick(at: t.addingTimeInterval(3))
        guard case .started(let call) = events.first else { return XCTFail() }
        XCTAssertEqual(call.callID, 42)
        XCTAssertEqual(CallDetector(firstCallID: 0).call, nil)
        var zero = CallDetector(firstCallID: 0)
        XCTAssertEqual(zero.reserveCallID(), 1, "call_id is never 0")
    }
}
