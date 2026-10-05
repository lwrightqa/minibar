import Foundation
import XCTest
@testable import TinyBarCore

/// `AppSettings`, `UserDefaultsSettingsStore` and `NameRules` (criteria 7, 9, 27, 32).
final class SettingsTests: XCTestCase {
    private var suiteName = ""
    private var defaults: UserDefaults!

    override func setUp() {
        suiteName = "TinyBarTests.\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suiteName)
    }

    override func tearDown() {
        defaults.removePersistentDomain(forName: suiteName)
    }

    /// Settings with every field changed from its default.
    private func everythingChanged() throws -> AppSettings {
        var catalog = AppCatalog.defaults
        try catalog.rename(callAppID: "zoom", to: "Zm")
        catalog.ignore(catalog.classify(Proc.slack).identity)
        catalog.addIgnoredApp(IgnoredApp(id: "com.apple.garageband10", displayName: "GarageBand",
                                         matchers: [.bundleID("com.apple.garageband10")]))
        return AppSettings(
            installID: "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60",
            detection: DetectionSettings(startDelay: 5, endDelay: 30, countCamera: false, sendAppName: false,
                                         mode: .onlyCallApps, catalog: catalog),
            launchAtLogin: false,
            useWiFi: false,
            manualAddress: "10.0.4.42:8080",
            macName: "Desk Mac",
            bar: KnownBar(deviceID: "f412fa3f2a1c", name: "MiniBar 2A1C", host: "minibar-2.local",
                          lastEndpoint: BarEndpoint(host: "10.0.4.42"), auth: .bearer, tokenID: "74d8a526"),
            pause: .restOfToday(endsAt: try XCTUnwrap(RFC3339.date(from: "2026-10-05T00:00:00-07:00"))),
            didShowWelcome: true,
            didExplainLocalNetwork: true
        )
    }

    // MARK: - Criterion 27: settings survive a relaunch

    func test_C27_settingsPersist() throws {
        let store = UserDefaultsSettingsStore(defaults: defaults)
        XCTAssertNil(store.load(), "first launch")

        let settings = try everythingChanged()
        try store.save(settings)
        XCTAssertEqual(UserDefaultsSettingsStore(defaults: defaults).load(), settings, "every field round-trips")

        for pause in [PauseState.notPaused, .untilResumed, .until(ManualClock.apiExampleStart)] {
            var paused = settings
            paused.pause = pause
            try store.save(paused)
            XCTAssertEqual(store.load()?.pause, pause)
        }

        // Unreadable: start fresh rather than crash.
        defaults.set(Data("{not json".utf8), forKey: UserDefaultsSettingsStore.defaultKey)
        XCTAssertNil(store.load())
        defaults.set("a string", forKey: UserDefaultsSettingsStore.defaultKey)
        XCTAssertNil(store.load())

        // A newer version's fields are optional: only the install ID is needed.
        defaults.set(Data(#"{"installID": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "detection": {"startDelay": 7}}"#.utf8),
                     forKey: UserDefaultsSettingsStore.defaultKey)
        let partial = try XCTUnwrap(store.load())
        XCTAssertEqual(partial.detection.startDelay, 7)
        XCTAssertEqual(partial.detection.endDelay, 10)
        XCTAssertEqual(partial.detection.catalog, .defaults)
        XCTAssertTrue(partial.launchAtLogin)
        XCTAssertTrue(partial.useWiFi)
        XCTAssertEqual(partial.pause, .notPaused)
        XCTAssertNil(partial.bar)
    }

    func test_C27_restoreDefaultsPerTab() throws {
        let changed = try everythingChanged()
        let fresh = AppSettings(installID: changed.installID)

        var general = changed
        general.restoreGeneralDefaults()
        XCTAssertTrue(general.launchAtLogin)
        XCTAssertEqual(general.detection.startDelay, 3)
        XCTAssertEqual(general.detection.endDelay, 10)
        XCTAssertTrue(general.detection.countCamera)
        XCTAssertTrue(general.detection.sendAppName)
        XCTAssertEqual(general.detection.catalog, changed.detection.catalog, "lists stay")
        XCTAssertEqual(general.detection.mode, changed.detection.mode)
        XCTAssertEqual(general.bar, changed.bar)

        var apps = changed
        apps.restoreAppsDefaults()
        XCTAssertEqual(apps.detection.catalog, .defaults)
        XCTAssertEqual(apps.detection.mode, .anyExceptIgnored)
        XCTAssertEqual(apps.detection.startDelay, 5, "General stays")

        var connection = changed
        connection.restoreConnectionDefaults()
        XCTAssertTrue(connection.useWiFi)
        XCTAssertNil(connection.manualAddress)
        XCTAssertNil(connection.macName)
        XCTAssertEqual(connection.bar, changed.bar, "the bar isn't forgotten")
        XCTAssertEqual(connection.installID, changed.installID)

        // All three together give first-launch settings, apart from the bar and what's remembered.
        var all = changed
        all.restoreGeneralDefaults()
        all.restoreAppsDefaults()
        all.restoreConnectionDefaults()
        all.bar = nil
        all.pause = .notPaused
        all.didShowWelcome = false
        all.didExplainLocalNetwork = false
        XCTAssertEqual(all, fresh)
    }

    func test_firstLaunchDefaults() {
        let first = AppSettings.firstLaunch()
        XCTAssertTrue(Identifiers.isValidClientID(first.installID))
        XCTAssertNotEqual(first.installID, AppSettings.firstLaunch().installID, "random per install")
        XCTAssertEqual(first.detection, .defaults)
        XCTAssertTrue(first.launchAtLogin)
        XCTAssertTrue(first.useWiFi)
        XCTAssertNil(first.bar)
        XCTAssertFalse(first.didShowWelcome)
        XCTAssertNoThrow(try first.validate())
    }

    // MARK: - Criterion 32: no history on disk

    func test_C32_noHistoryOnDisk() throws {
        // Use the Mac for a while: several apps, a call, the camera.
        var run = DetectorRun()
        run.observe(.mic(Proc.garageBand), at: 0)
        run.observe(.mic(Proc.slack, Proc.voiceMemos), at: 30)
        run.observe(ActivitySnapshot(mic: [Proc.zoom], cameraInUse: true), at: 100)
        run.observe(.idle, at: 400)
        run.run(until: 500)
        XCTAssertEqual(run.detector.seenApps.count, 4)

        var settings = AppSettings(installID: Identifiers.newInstallID(), detection: run.detector.settings)
        settings.bar = KnownBar(deviceID: "f412fa3f2a1c", name: "MiniBar 2A1C")
        let store = UserDefaultsSettingsStore(defaults: defaults)
        try store.save(settings)
        let saved = String(decoding: try XCTUnwrap(defaults.data(forKey: UserDefaultsSettingsStore.defaultKey)), as: UTF8.self)
        // Only list entries are there; nothing about which apps used the mic, or when.
        XCTAssertFalse(saved.contains("GarageBand"))
        XCTAssertFalse(saved.contains("garageband"))
        XCTAssertFalse(saved.contains("lastSeen"))
        XCTAssertFalse(saved.contains("seen"))
        XCTAssertFalse(saved.contains("since"))
        XCTAssertFalse(saved.contains("tb1_"), "no token")
        let keys = try XCTUnwrap(JSONValue.parse(saved).objectValue?.keys.sorted())
        XCTAssertEqual(keys, ["bar", "detection", "didExplainLocalNetwork", "didShowWelcome", "installID", "launchAtLogin",
                              "pause", "useWiFi"])
    }

    // MARK: - Names and validation

    func test_nameRules() {
        XCTAssertNil(NameRules.appNameProblem("Slack"))
        XCTAssertNil(NameRules.appNameProblem("  Slack  "), "trimmed first")
        XCTAssertNil(NameRules.appNameProblem(String(repeating: "x", count: 24)))
        XCTAssertNil(NameRules.appNameProblem("Zoom (work) #2 ~!"))
        XCTAssertEqual(NameRules.appNameProblem(""), .empty)
        XCTAssertEqual(NameRules.appNameProblem("   "), .empty)
        XCTAssertEqual(NameRules.appNameProblem(String(repeating: "x", count: 25)), .tooLong(limit: 24))
        XCTAssertEqual(NameRules.appNameProblem("Zoöm"), .unsupportedCharacters(["ö"]))
        XCTAssertEqual(NameRules.appNameProblem("Café Café"), .unsupportedCharacters(["é"]), "each character once")
        XCTAssertEqual(NameRules.appNameProblem("Alex’s 📞"), .unsupportedCharacters(["’", "📞"]))
        XCTAssertEqual(NameRules.appNameProblem("a\tb"), .unsupportedCharacters(["\t"]))

        XCTAssertNil(NameRules.macNameProblem(""), "empty: send none, which is fine")
        XCTAssertNil(NameRules.macNameProblem("  "))
        XCTAssertNil(NameRules.macNameProblem("Desk Mac"))
        XCTAssertNil(NameRules.macNameProblem(String(repeating: "m", count: 32)))
        XCTAssertEqual(NameRules.macNameProblem(String(repeating: "m", count: 33)), .tooLong(limit: 32))
        XCTAssertEqual(NameRules.macNameProblem("Alex’s Mac"), .unsupportedCharacters(["’"]))
    }

    func test_validate() throws {
        var settings = AppSettings(installID: "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60")
        settings.macName = "  "
        settings.manualAddress = ""
        XCTAssertNoThrow(try settings.validate())
        XCTAssertNil(settings.macNameToSend)
        XCTAssertNil(settings.manualEndpoint)

        settings.macName = " Desk Mac "
        settings.manualAddress = "http://10.0.4.42:8080/"
        XCTAssertNoThrow(try settings.validate())
        XCTAssertEqual(settings.macNameToSend, "Desk Mac")
        XCTAssertEqual(settings.manualEndpoint, BarEndpoint(host: "10.0.4.42", port: 8080))

        settings.macName = "Alex’s Mac"
        XCTAssertThrowsError(try settings.validate()) { XCTAssertEqual($0 as? SettingsError, .badMacName(.unsupportedCharacters(["’"]))) }
        settings.macName = nil
        settings.manualAddress = "minibar.local/api"
        XCTAssertThrowsError(try settings.validate()) { XCTAssertEqual($0 as? SettingsError, .badAddress("minibar.local/api")) }
        settings.manualAddress = nil
        settings.detection.endDelay = 2
        XCTAssertThrowsError(try settings.validate()) { XCTAssertEqual($0 as? SettingsError, .endDelayOutOfRange(2)) }
    }

    func test_inMemoryStore() throws {
        let store = InMemorySettingsStore()
        XCTAssertNil(store.load())
        let settings = AppSettings(installID: "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60")
        try store.save(settings)
        XCTAssertEqual(store.load(), settings)
    }
}
