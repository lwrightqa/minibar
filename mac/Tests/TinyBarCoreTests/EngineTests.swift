import Foundation
import XCTest
@testable import TinyBarCore

/// `TinyBarEngine` with every platform adapter faked: the mic and camera, the
/// USB watcher, Bonjour, the Keychain, login items and sleep, plus a fake bar
/// behind fake transports and a manual clock.
@MainActor
final class EngineRig {
    let clock = ManualClock()
    let mic = FakeMic()
    let camera = FakeCamera()
    let watcher = FakeSerialWatcher()
    let discovery = FakeBarDiscovery()
    let loginItem = FakeLoginItem()
    let power = FakePower()
    let factory = FakeTransportFactory()
    let tokens: InMemoryTokenStore
    let store: InMemorySettingsStore
    let bar: FakeBar
    let engine: TinyBarEngine
    let start: Date

    static let installID = "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"
    static let token = "tb1_" + String(repeating: "C", count: 43)

    init(settings: AppSettings? = nil, tokens: [String: String] = [:], localNetworkPrivacy: Bool = true) {
        bar = FakeBar(clock: clock)
        self.tokens = InMemoryTokenStore(tokens)
        store = InMemorySettingsStore(settings)
        engine = TinyBarEngine(dependencies: TinyBarEngine.Dependencies(
            clock: clock, mic: mic, camera: camera, serialDevices: watcher, discovery: discovery,
            tokens: self.tokens, settings: store, loginItem: loginItem, power: power, transports: factory,
            appVersion: "1.0 (12)", timeZone: TimeZone(identifier: "America/Los_Angeles")!,
            locale: Locale(identifier: "en_US"), hasLocalNetworkPrivacy: localNetworkPrivacy
        ))
        start = clock.now()
    }

    /// A Mac paired with the fake bar earlier.
    static func paired(_ change: ((inout AppSettings) -> Void)? = nil) -> EngineRig {
        var settings = AppSettings(installID: installID)
        settings.bar = KnownBar(deviceID: "f412fa3f2a1c", name: "MiniBar 2A1C", host: "minibar.local",
                                lastEndpoint: ConnectionRig.barAddress, auth: .bearer, tokenID: "74d8a526")
        settings.didShowWelcome = true
        change?(&settings)
        let rig = EngineRig(settings: settings, tokens: ["f412fa3f2a1c": token])
        rig.bar.set {
            $0.tokens.insert(token)
            $0.tokenIDs["74d8a526"] = token
        }
        return rig
    }

    func settle(file: StaticString = #filePath, line: UInt = #line) async {
        let giveUp = Date().addingTimeInterval(3)
        while Date() < giveUp {
            for _ in 0..<10 { await Task.yield() }
            if await engine.isSettled {
                try? await Task.sleep(nanoseconds: 2_000_000)
                for _ in 0..<10 { await Task.yield() }
                if await engine.isSettled { return }
            }
            try? await Task.sleep(nanoseconds: 1_000_000)
        }
        XCTFail("the engine didn't settle", file: file, line: line)
    }

    func begin() async {
        engine.start()
        await settle()
    }

    func run(for seconds: TimeInterval, step: TimeInterval = 0.5) async {
        let end = clock.now().addingTimeInterval(seconds)
        while clock.now() < end {
            clock.advance(to: min(clock.now().addingTimeInterval(step), end))
            await settle()
        }
    }

    func plugIn() async {
        factory.plug(bar, registryID: ConnectionRig.espressif.registryID)
        watcher.send(.appeared(ConnectionRig.espressif))
        await settle()
    }

    func micUse(_ processes: MicProcess...) async {
        let snapshot = processes
        mic.setList(snapshot)
        await settle()
    }

    var elapsed: TimeInterval { (clock.now().timeIntervalSince(start) * 1000).rounded() / 1000 }

    func messageLog() -> [String] {
        bar.messages.map { message in
            let what: String
            if message.request.leaving == true {
                what = "leaving"
            } else if message.request.active {
                what = message.request.app ?? "active"
            } else {
                what = "idle"
            }
            return String(format: "%.1f %@ %@", (message.at.timeIntervalSince(start) * 1000).rounded() / 1000,
                          message.via.rawValue, what)
        }
    }
}

extension FakeMic {
    func setList(_ processes: [MicProcess]) {
        switch processes.count {
        case 0: set()
        case 1: set(processes[0])
        case 2: set(processes[0], processes[1])
        default: set(processes[0], processes[1], processes[2])
        }
    }
}

/// The engine scenarios, on the main actor like the app.
@MainActor
final class EngineScenarios {
    func test_endToEnd() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        await rig.plugIn()
        XCTAssertEqual(rig.messageLog(), ["0.0 usb idle"])
        XCTAssertEqual(rig.engine.state.connection.phase, .connected)

        await rig.run(for: 5)
        await rig.micUse(Proc.slack)
        XCTAssertEqual(rig.engine.state.observation.pendingSince, rig.start.addingTimeInterval(5), "Mic in use by Slack")
        await rig.run(for: 3)
        XCTAssertEqual(rig.messageLog().last, "8.0 usb Slack", "a call after the 3 s start delay")
        XCTAssertEqual(rig.engine.state.call?.app, "Slack")

        await rig.run(for: 12)
        await rig.micUse()
        await rig.run(for: 9.5)
        XCTAssertNotNil(rig.engine.state.call)
        await rig.run(for: 1)
        XCTAssertEqual(rig.messageLog().last, "30.0 usb idle", "ended 10 s after the mic stopped")
        XCTAssertNil(rig.engine.state.call)
        XCTAssertEqual(rig.engine.state.seenApps.map(\.identity.displayName), ["Slack"])
        let requests = rig.bar.messages.map(\.request)
        XCTAssertEqual(requests.compactMap(\.seq), Array(1...UInt32(requests.count)))
        XCTAssertTrue(requests.allSatisfy { $0.client == EngineRig.installID && $0.problems.isEmpty })
        await rig.engine.shutdown()
    }

    func test_C10_pauseFromTheMenu() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        await rig.plugIn()
        await rig.micUse(Proc.zoom)
        await rig.run(for: 4)
        XCTAssertEqual(rig.messageLog().last, "3.0 usb Zoom")

        rig.engine.pause(.oneHour)
        await rig.settle()
        XCTAssertEqual(rig.messageLog().last, "4.0 usb idle", "active: false at once")
        XCTAssertEqual(rig.store.load()?.pause, .until(rig.start.addingTimeInterval(3604)), "the pause survives a relaunch")
        XCTAssertTrue(rig.engine.state.isPaused(at: rig.clock.now()))

        await rig.run(for: 60, step: 5)
        XCTAssertEqual(rig.messageLog().suffix(3), ["4.0 usb idle", "34.0 usb idle", "64.0 usb idle"],
                       "heartbeats go on with active: false")
        XCTAssertNil(rig.engine.state.call)

        await rig.run(for: 3530, step: 30)
        await rig.run(for: 20)
        XCTAssertEqual(rig.engine.state.settings.pause, .notPaused, "the pause ends by itself")
        XCTAssertEqual(rig.store.load()?.pause, .notPaused)
        XCTAssertTrue(rig.messageLog().contains("3607.0 usb Zoom"), "Zoom, still on the mic, counts 3 s after the pause")
        await rig.engine.shutdown()
    }

    func test_C26_dontCountAndCountAgain() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        await rig.plugIn()
        await rig.micUse(Proc.slack)
        await rig.run(for: 4)
        let slack = try XCTUnwrap(rig.engine.state.observation.micUses.first?.classification.identity)

        rig.engine.dontCount(slack)
        await rig.settle()
        XCTAssertEqual(rig.messageLog().last, "4.0 usb idle", "the call ends at once")
        XCTAssertEqual(rig.engine.state.undo.app?.app, slack, "Count Slack Again is offered")
        XCTAssertTrue(rig.store.load()?.detection.catalog.ignoredApps.contains { $0.id == "slack" } ?? false)

        await rig.run(for: 2)
        rig.engine.countAgain(slack)
        await rig.settle()
        XCTAssertNil(rig.engine.state.undo.app)
        XCTAssertEqual(rig.store.load()?.detection.catalog, .defaults)
        await rig.run(for: 3)
        XCTAssertEqual(rig.messageLog().last, "9.0 usb Slack", "counted again, from when it was put back")

        // The undo goes away once the app stops using the mic.
        rig.engine.dontCount(slack)
        await rig.settle()
        XCTAssertNotNil(rig.engine.state.undo.app)
        await rig.micUse()
        XCTAssertNil(rig.engine.state.undo.app)

        // The camera.
        rig.camera.set(true)
        await rig.settle()
        await rig.run(for: 3)
        XCTAssertEqual(rig.messageLog().last, "12.0 usb active", "the camera alone: a call with no name")
        rig.engine.setCountCamera(false)
        await rig.settle()
        XCTAssertEqual(rig.messageLog().last, "12.0 usb idle")
        XCTAssertTrue(rig.engine.state.undo.camera, "Count the Camera Again")
        rig.camera.set(false)
        await rig.settle()
        XCTAssertFalse(rig.engine.state.undo.camera)
        XCTAssertFalse(rig.engine.state.settings.detection.countCamera)
        await rig.engine.shutdown()
    }

    func test_settingsChanges() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        XCTAssertThrowsError(try rig.engine.updateSettings { $0.detection.startDelay = 31 }) {
            XCTAssertEqual($0 as? SettingsError, .startDelayOutOfRange(31))
        }
        XCTAssertEqual(rig.engine.state.settings.detection.startDelay, 3, "nothing changed")
        try rig.engine.updateSettings { $0.detection.startDelay = 0; $0.macName = "Desk Mac" }
        XCTAssertEqual(rig.store.load()?.detection.startDelay, 0, "saved at once")
        await rig.plugIn()
        await rig.micUse(Proc.slack)
        XCTAssertEqual(rig.messageLog().last, "0.0 usb Slack", "start delay 0 applies at once")

        try rig.engine.updateSettings { $0.detection.sendAppName = false }
        await rig.settle()
        XCTAssertEqual(rig.messageLog().last, "0.0 usb active", "the name is withdrawn at once, same call")
        XCTAssertEqual(rig.bar.messages.last?.request.callID, rig.bar.messages.dropLast().last?.request.callID)
        await rig.engine.shutdown()
    }

    func test_C29_launchAtLogin() async throws {
        let rig = EngineRig()
        await rig.begin()
        XCTAssertTrue(rig.engine.shouldShowWelcome)
        XCTAssertTrue(rig.engine.state.settings.launchAtLogin, "checked by default")

        rig.loginItem.willRegister(as: .requiresApproval)
        rig.engine.setLaunchAtLogin(true)
        XCTAssertEqual(rig.engine.state.loginItem, .requiresApproval)
        XCTAssertFalse(rig.engine.state.settings.launchAtLogin, "the switch turns itself off")

        rig.loginItem.willRegister(as: .enabled)
        rig.engine.setLaunchAtLogin(true)
        XCTAssertTrue(rig.engine.state.settings.launchAtLogin)
        rig.engine.setLaunchAtLogin(false)
        XCTAssertEqual(rig.engine.state.loginItem, .notRegistered)
        XCTAssertFalse(rig.store.load()?.launchAtLogin ?? true)

        rig.engine.markWelcomeShown()
        XCTAssertFalse(rig.engine.shouldShowWelcome)
        XCTAssertEqual(rig.store.load()?.didShowWelcome, true)
        rig.engine.markLocalNetworkExplained()
        XCTAssertEqual(rig.store.load()?.didExplainLocalNetwork, true)
        await rig.engine.shutdown()
    }

    func test_firstLaunchKeepsItsInstallID() async throws {
        let rig = EngineRig()
        let saved = try XCTUnwrap(rig.store.load(), "saved at the first launch")
        XCTAssertEqual(saved.installID, rig.engine.state.settings.installID)
        XCTAssertTrue(Identifiers.isValidClientID(saved.installID))
        await rig.begin()
        XCTAssertEqual(rig.engine.state.connection.phase, .notSetUp)
        XCTAssertEqual(rig.discovery.starts, 0, "no Bonjour (and no Local Network prompt) before a bar is set up")
        await rig.engine.shutdown()
    }

    func test_sendTestCall() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        rig.engine.sendTestCall()
        await rig.settle()
        XCTAssertNil(rig.engine.state.testCall, "not connected: nothing")

        await rig.plugIn()
        rig.engine.sendTestCall()
        await rig.settle()
        XCTAssertEqual(rig.messageLog().last, "0.0 usb Test")
        XCTAssertNotNil(rig.engine.state.testCall)
        await rig.run(for: 9.5)
        XCTAssertNotNil(rig.engine.state.testCall)
        await rig.run(for: 1)
        XCTAssertEqual(rig.messageLog().last, "10.0 usb idle", "10 seconds, then it ends")
        XCTAssertNil(rig.engine.state.testCall)

        await rig.micUse(Proc.slack)
        await rig.run(for: 3)
        rig.engine.sendTestCall()
        await rig.settle()
        XCTAssertEqual(rig.messageLog().last, "13.5 usb Slack", "not during a real call")
        let ids = rig.bar.messages.compactMap(\.request.callID)
        XCTAssertEqual(Set(ids).count, 2, "the test call and the real call have different call_ids")
        await rig.engine.shutdown()
    }

    func test_C19_sleepAndWake() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        await rig.plugIn()
        await rig.micUse(Proc.slack)
        await rig.run(for: 4)
        await rig.power.send(.willSleep)
        let last = try XCTUnwrap(rig.bar.messages.last?.request)
        XCTAssertEqual(last.leaving, true, "leaving before the Mac sleeps")
        XCTAssertFalse(last.active)
        let count = rig.bar.messages.count
        await rig.run(for: 60, step: 5)
        XCTAssertEqual(rig.bar.messages.count, count, "nothing while asleep")

        await rig.power.send(.didWake)
        await rig.settle()
        XCTAssertEqual(rig.messageLog().last, "64.0 usb idle", "a fresh start, sent at once")
        await rig.run(for: 3)
        XCTAssertEqual(rig.messageLog().last, "67.0 usb Slack", "the call that's still going counts again after the start delay")
        let callIDs = rig.bar.messages.compactMap(\.request.callID)
        XCTAssertEqual(callIDs.first, 1)
        XCTAssertEqual(callIDs.last, 2, "a new call_id")
        XCTAssertEqual(rig.engine.state.seenApps.count, 1, "the apps seen this session are kept")
        await rig.engine.shutdown()
    }

    func test_C19_quitDuringACall() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        await rig.plugIn()
        await rig.micUse(Proc.slack)
        await rig.run(for: 4)
        await rig.engine.shutdown()
        XCTAssertEqual(rig.bar.messages.last?.request.leaving, true)
        XCTAssertTrue(rig.mic.isStopped)
        XCTAssertTrue(rig.watcher.isStopped)
        XCTAssertEqual(rig.factory.opened.first?.isClosed, true)
    }

    func test_C20_C32_pairsOverUSBAndSavesNoHistory() async throws {
        let rig = EngineRig()
        await rig.begin()
        await rig.micUse(Proc.garageBand, Proc.voiceMemos)
        await rig.plugIn()
        XCTAssertEqual(rig.engine.state.settings.bar?.name, "MiniBar 2A1C", "paired over the cable")
        XCTAssertEqual(rig.store.load()?.bar?.deviceID, "f412fa3f2a1c", "and the bar is saved")
        XCTAssertEqual(rig.tokens.deviceIDs, ["f412fa3f2a1c"], "the token is in the token store")
        XCTAssertEqual(rig.discovery.starts, 1, "now Bonjour looks for it, for Wi-Fi")

        let saved = try WireJSON.encodeString(try XCTUnwrap(rig.store.load()))
        XCTAssertFalse(saved.contains("tb1_"), "never the token")
        XCTAssertFalse(saved.contains("GarageBand"), "no record of which apps used the mic")
        XCTAssertFalse(saved.contains("garageband"))
        XCTAssertEqual(rig.engine.state.seenApps.count, 2, "only in memory")
        await rig.engine.shutdown()
    }

    func test_forgetBar() async throws {
        let rig = EngineRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.begin()
        XCTAssertEqual(rig.messageLog(), ["0.0 wifi idle"])
        await rig.engine.forgetBar()
        await rig.settle()
        XCTAssertNil(rig.engine.state.settings.bar)
        XCTAssertNil(rig.store.load()?.bar)
        XCTAssertEqual(rig.tokens.deviceIDs, [])
        XCTAssertEqual(rig.engine.state.connection.phase, .notSetUp)
        await rig.run(for: 60, step: 5)
        XCTAssertEqual(rig.messageLog(), ["0.0 wifi idle"], "no more messages")
        await rig.engine.shutdown()
    }

    func test_C22_wifiPairingThroughTheEngine() async throws {
        let rig = EngineRig()
        let address = BarEndpoint(host: "10.0.4.42")
        rig.factory.place(rig.bar, at: address)
        await rig.begin()
        let flow = rig.engine.makeWiFiPairingFlow()
        XCTAssertEqual(flow.step, .explainLocalNetwork)
        rig.engine.markLocalNetworkExplained()
        flow.startLooking()
        let found = DiscoveredBar(name: "MiniBar 2A1C", deviceID: "f412fa3f2a1c", auth: .bearer, endpoint: address)
        rig.discovery.emit([found])
        await rig.settle()
        await flow.requestCode()
        await flow.submit(code: "482913")
        guard case .paired(let known) = flow.step else { return XCTFail("\(flow.step)") }
        await rig.settle()
        XCTAssertEqual(rig.engine.state.settings.bar?.deviceID, known.deviceID)
        XCTAssertEqual(rig.store.load()?.bar?.tokenID, known.tokenID)
        XCTAssertEqual(rig.engine.state.connection.phase, .connected, "Wi-Fi with the new token")
        XCTAssertEqual(rig.messageLog().last, "0.0 wifi idle")
        XCTAssertNotNil(rig.bar.messages.last?.token)
        XCTAssertEqual(rig.discovery.starts, 1, "one Bonjour browser, shared")
        await rig.engine.shutdown()
        XCTAssertEqual(rig.discovery.stops, 1, "stopped on quit")
    }

    func test_usbPauseFromTheMenu() async throws {
        let rig = EngineRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.begin()
        await rig.plugIn()
        rig.engine.setUSBPaused(true)
        await rig.settle()
        XCTAssertTrue(rig.engine.state.connection.usbPaused)
        XCTAssertEqual(rig.engine.state.connection.link, .wifi)
        rig.engine.setUSBPaused(false)
        await rig.settle()
        XCTAssertEqual(rig.engine.state.connection.link, .usb)
        await rig.engine.shutdown()
    }

    func test_localNetworkBlockedFromBonjour() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        rig.discovery.fail(.localNetworkDenied)
        await rig.settle()
        XCTAssertTrue(rig.engine.state.localNetworkBlocked)
        XCTAssertEqual(rig.engine.state.connection.phase, .localNetworkBlocked)
        rig.discovery.emit([DiscoveredBar(name: "MiniBar 2A1C", deviceID: "f412fa3f2a1c", endpoint: ConnectionRig.barAddress)])
        await rig.settle()
        XCTAssertFalse(rig.engine.state.localNetworkBlocked)
        await rig.engine.shutdown()
    }

    func test_wifiSwitchStopsBrowsing() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        XCTAssertEqual(rig.discovery.starts, 1)
        try rig.engine.updateSettings { $0.useWiFi = false }
        await rig.settle()
        XCTAssertEqual(rig.engine.state.connection.phase, .notPluggedIn)
        await rig.engine.shutdown()
    }
}

final class EngineTests: XCTestCase {
    func test_endToEnd() async throws { try await EngineScenarios().test_endToEnd() }
    func test_C10_pauseFromTheMenu() async throws { try await EngineScenarios().test_C10_pauseFromTheMenu() }
    func test_C26_dontCountAndCountAgain() async throws { try await EngineScenarios().test_C26_dontCountAndCountAgain() }
    func test_settingsChanges() async throws { try await EngineScenarios().test_settingsChanges() }
    func test_C29_launchAtLogin() async throws { try await EngineScenarios().test_C29_launchAtLogin() }
    func test_firstLaunchKeepsItsInstallID() async throws { try await EngineScenarios().test_firstLaunchKeepsItsInstallID() }
    func test_sendTestCall() async throws { try await EngineScenarios().test_sendTestCall() }
    func test_C19_sleepAndWake() async throws { try await EngineScenarios().test_C19_sleepAndWake() }
    func test_C19_quitDuringACall() async throws { try await EngineScenarios().test_C19_quitDuringACall() }
    func test_C20_C32_pairsOverUSBAndSavesNoHistory() async throws { try await EngineScenarios().test_C20_C32_pairsOverUSBAndSavesNoHistory() }
    func test_forgetBar() async throws { try await EngineScenarios().test_forgetBar() }
    func test_C22_wifiPairingThroughTheEngine() async throws { try await EngineScenarios().test_C22_wifiPairingThroughTheEngine() }
    func test_usbPauseFromTheMenu() async throws { try await EngineScenarios().test_usbPauseFromTheMenu() }
    func test_localNetworkBlockedFromBonjour() async throws { try await EngineScenarios().test_localNetworkBlockedFromBonjour() }
    func test_wifiSwitchStopsBrowsing() async throws { try await EngineScenarios().test_wifiSwitchStopsBrowsing() }
}
