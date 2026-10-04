import Foundation
import XCTest
@testable import TinyBarCore

/// The fixes from the QA and macOS review of 2026-10-04 that the QA*Tests
/// files don't already cover.
final class ReviewFixTests: XCTestCase {
    // MARK: - Wi-Fi reach while plugged in (mac-app-ux.md 4.3, 5.2)

    func test_wifiReachableHereIsPublishedWhilePluggedIn() async throws {
        let rig = ConnectionRig.paired()   // nothing answers at the bar's Wi-Fi address
        await rig.start()
        await rig.plugIn()
        var state = await rig.state
        XCTAssertEqual(state.link, .usb)
        XCTAssertEqual(state.wifiReachableHere, false, "the Connect window can say so while it's plugged in")
        await rig.connection.stop()

        let reachable = ConnectionRig.paired()
        reachable.factory.place(reachable.bar, at: ConnectionRig.barAddress)
        await reachable.start()
        await reachable.plugIn()
        state = await reachable.state
        XCTAssertEqual(state.wifiReachableHere, true)
        await reachable.connection.didWake()
        await reachable.settle()
        state = await reachable.state
        XCTAssertNil(state.wifiReachableHere, "another network, perhaps, after a wake")
        await reachable.connection.stop()
    }

    // MARK: - USB handshake (api.md 6.2)

    /// Error replies only, for the whole 10 seconds: a TinyBar that wasn't
    /// ready. The handshake says so (not "not a TinyBar").
    func test_handshakeWithOnlyErrorRepliesThrowsTheError() async throws {
        let clock = ManualClock()
        let pty = try FakeBarPTY()
        let usb = USBTransport(port: try SerialPort(path: pty.devicePath), device: pty.device(), clock: clock)
        let handshake = Task { try await usb.handshake(USBLinkTests.helloRequest) }
        for _ in 0..<5 {
            let command = try await offPool { try pty.nextCommand() }
            XCTAssertEqual(command?["cmd"], .string("hello"))
            try pty.reply(to: command?["id"], #"{"ok": false, "error": "internal", "message": "Out of memory.", "field": null}"#)
            try await Task.sleep(nanoseconds: 50_000_000)
            await clock.waitForSleepers()
            clock.advance(by: 2)
        }
        do {
            _ = try await handshake.value
            XCTFail("expected an error")
        } catch {
            XCTAssertEqual((error as? BarError)?.apiCode, .internalError)
        }
        await usb.close()
        pty.close()
    }

    /// Such a device isn't left alone until it's replugged: it's greeted
    /// again after 10 seconds.
    func test_aDeviceThatAnsweredWithErrorsIsTriedAgain() async throws {
        let clock = ManualClock()
        let factory = NotReadyFirstFactory()
        let bar = FakeBar(clock: clock)
        factory.inner.plug(bar, registryID: ConnectionRig.espressif.registryID)
        let connection = BarConnection(
            configuration: .init(clientID: ConnectionRig.client, sessionID: "q8Zr2Lx0", appVersion: "1.0 (12)", useWiFi: false),
            bar: nil, clock: clock, transports: factory, tokens: InMemoryTokenStore())
        await connection.start()
        await connection.handle(.appeared(ConnectionRig.espressif))
        await settle(connection)
        var state = await connection.state
        XCTAssertNotEqual(state.phase, .connected)
        for _ in 0..<22 { clock.advance(by: 0.5); await settle(connection) }
        state = await connection.state
        XCTAssertEqual(state.phase, .connected, "greeted again after 10 s")
        XCTAssertEqual(state.link, .usb)
        XCTAssertEqual(factory.opens, 2)
        await connection.stop()
    }

    // MARK: - Leaving

    /// Sleep: nothing is sent between `leaving` and the wake, and the menu
    /// doesn't say connected meanwhile.
    func test_sleepStaysQuietUntilTheWake() async throws {
        let rig = ConnectionRig.paired()
        await rig.start()
        await rig.plugIn()
        await rig.connection.sendLeaving()
        await rig.settle()
        var state = await rig.state
        XCTAssertNotEqual(state.phase, .connected)
        let count = rig.bar.messages.count
        await rig.run(for: 120, step: 5)
        XCTAssertEqual(rig.bar.messages.count, count, "no resume without a wake")
        await rig.connection.didWake()
        await rig.settle()
        state = await rig.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertEqual(rig.bar.messages.count, count + 1)
        await rig.connection.stop()
    }

    // MARK: - Monitors that don't start

    func test_aMicMonitorThatFailsIsShownAndTriedAgain() async throws {
        try await MonitorScenarios().micFailsThenWorks()
    }

    // MARK: - Addresses

    func test_addressesAppTransportSecurityAllows() {
        func allowed(_ text: String) -> Bool? { BarEndpoint(userInput: text)?.isAllowedOverPlainHTTP }
        XCTAssertEqual(allowed("tinybar.local"), true)
        XCTAssertEqual(allowed("TinyBar-2.local"), true)
        XCTAssertEqual(allowed("10.0.4.42"), true)
        XCTAssertEqual(allowed("10.0.4.42:8080"), true)
        XCTAssertEqual(allowed("tinybar"), true, "single-label names count as local")
        XCTAssertEqual(allowed("tinybar.lan"), false)
        XCTAssertEqual(allowed("tinybar.office.example.com"), false)
    }

    func test_enterAddressRefusesNamesThatCantWork() async throws {
        try await PairingFixScenarios().enterAddress()
    }

    func test_noAnswerToPairKeepsTheCode() async throws {
        try await PairingFixScenarios().noAnswerKeepsTheCode()
    }

    func test_rateLimitWaitIsOnTheFlowsClock() async throws {
        try await PairingFixScenarios().rateLimitWait()
    }

    func test_protectedFolders() {
        let home = "/Users/lisa"
        XCTAssertTrue(AppPaths.isInProtectedFolder("/Users/lisa/Downloads/Zoom.app", home: home))
        XCTAssertTrue(AppPaths.isInProtectedFolder("/Users/lisa/Desktop/Build/MyApp.app", home: home + "/"))
        XCTAssertTrue(AppPaths.isInProtectedFolder("/Users/lisa/Library/Mobile Documents/com~apple~CloudDocs/A.app", home: home))
        XCTAssertTrue(AppPaths.isInProtectedFolder("/Volumes/Work/Tools/A.app", home: home))
        XCTAssertFalse(AppPaths.isInProtectedFolder("/Applications/Slack.app", home: home))
        XCTAssertFalse(AppPaths.isInProtectedFolder("/Users/lisa/Applications/Slack.app", home: home))
        XCTAssertFalse(AppPaths.isInProtectedFolder("/Users/lisa/DownloadsExtra/A.app", home: home))
    }

    // MARK: - The real clock

    func test_systemClockIsMonotonicAndAnchoredToTheWallClock() async throws {
        let clock = SystemClock()
        XCTAssertEqual(clock.now().timeIntervalSince(Date()), 0, accuracy: 0.5)
        XCTAssertEqual(clock.wallClockOffset(), 0, accuracy: 0.5)
        let before = clock.now()
        try await clock.sleep(seconds: 0.05)
        let slept = clock.now().timeIntervalSince(before)
        XCTAssertGreaterThanOrEqual(slept, 0.049)
        XCTAssertLessThan(slept, 2)
        try await clock.sleep(until: before)   // in the past: returns at once
        XCTAssertEqual(clock.wallClock(before).timeIntervalSince(before), clock.wallClockOffset(), accuracy: 0.001)
    }
}

/// USB transports whose first handshake gets error replies only.
final class NotReadyFirstFactory: TransportFactory, @unchecked Sendable {
    let inner = FakeTransportFactory()
    private let count = Locked(0)

    var opens: Int { count.get() }

    func openUSB(_ device: SerialDevice) throws -> any USBLinkTransport {
        let n = count.withLock { $0 += 1; return $0 }
        let transport = try inner.openUSB(device)
        return n == 1 ? NotReadyUSB(inner: transport) : transport
    }

    func makeWiFi(endpoint: BarEndpoint, token: String?) -> any WiFiLinkTransport {
        inner.makeWiFi(endpoint: endpoint, token: token)
    }
}

final class NotReadyUSB: USBLinkTransport, @unchecked Sendable {
    let inner: any USBLinkTransport
    init(inner: any USBLinkTransport) { self.inner = inner }
    var kind: LinkKind { .usb }
    var device: SerialDevice { inner.device }
    var events: AsyncStream<USBEvent> { inner.events }
    var endpointDescription: String { inner.endpointDescription }
    func handshake(_ request: HelloRequest) async throws -> InfoReply {
        throw BarError.api(APIErrorBody(error: .internalError, message: "Out of memory."), httpStatus: nil)
    }
    func hello(_ request: HelloRequest) async throws -> InfoReply { try await inner.hello(request) }
    func sendCall(_ request: CallRequest) async throws -> CallReply { try await inner.sendCall(request) }
    func status() async throws -> StatusReply { try await inner.status() }
    func pair(_ request: USBPairRequest) async throws -> PairReply { try await inner.pair(request) }
    func close() async { await inner.close() }
}

/// A mic monitor that fails to start the first time.
final class FailingOnceMic: MicActivitySource, @unchecked Sendable {
    struct Unavailable: Error {}
    let inner = FakeMic()
    private let attempts = Locked(0)

    var starts: Int { attempts.get() }

    func start(onChange: @escaping @Sendable ([MicProcess]) -> Void) throws {
        let n = attempts.withLock { $0 += 1; return $0 }
        if n == 1 { throw Unavailable() }
        try inner.start(onChange: onChange)
    }

    func stop() { inner.stop() }
    func current() -> [MicProcess] { inner.current() }
}

@MainActor
final class MonitorScenarios {
    func micFailsThenWorks() async throws {
        let clock = ManualClock()
        let mic = FailingOnceMic()
        var settings = AppSettings(installID: ConnectionRig.client)
        settings.didShowWelcome = true
        let engine = TinyBarEngine(dependencies: TinyBarEngine.Dependencies(
            clock: clock, mic: mic, camera: FakeCamera(), serialDevices: FakeSerialWatcher(), discovery: FakeBarDiscovery(),
            tokens: InMemoryTokenStore(), settings: InMemorySettingsStore(settings), loginItem: FakeLoginItem(),
            power: FakePower(), transports: FakeTransportFactory(), appVersion: "1.0 (12)",
            timeZone: TimeZone(identifier: "America/Los_Angeles")!, locale: Locale(identifier: "en_US"),
            hasLocalNetworkPrivacy: false))
        engine.start()
        XCTAssertEqual(engine.state.monitorProblems, [.mic])
        var content = engine.menuContent(showDetails: false)
        XCTAssertEqual(content.icon, .needsYou)
        XCTAssertEqual(content.line1, "Can\u{2019}t tell when the mic is in use")
        // A minute later it's tried again, and works.
        for _ in 0..<130 where mic.starts < 2 {
            clock.advance(by: 0.5)
            try await Task.sleep(nanoseconds: 2_000_000)
        }
        try await Task.sleep(nanoseconds: 20_000_000)
        XCTAssertEqual(mic.starts, 2)
        XCTAssertEqual(engine.state.monitorProblems, [])
        content = engine.menuContent(showDetails: false)
        XCTAssertEqual(content.line1, "Not set up yet")
        mic.inner.set(Proc.slack)
        try await Task.sleep(nanoseconds: 20_000_000)
        XCTAssertEqual(engine.state.observation.micUses.first?.classification.identity.key, "slack", "the mic is watched now")
        await engine.shutdown()
    }
}

@MainActor
final class PairingFixScenarios {
    func enterAddress() async throws {
        let flow = WiFiPairingFlow(clientID: ConnectionRig.client, macName: nil, clock: ManualClock(),
                                   discovery: FakeBarDiscovery(), transports: FakeTransportFactory(),
                                   tokens: InMemoryTokenStore(), needsLocalNetworkExplanation: false, onPaired: { _ in })
        await flow.useAddress("tinybar.office.example.com")
        XCTAssertEqual(flow.step, .failed(.addressNotAllowed, bar: nil, retryAt: nil))
    }

    /// No reply to `pair`: the code is still on the bar, so Pair can send it
    /// again (until it expires).
    func noAnswerKeepsTheCode() async throws {
        let clock = ManualClock()
        let factory = ScriptedWiFiFactory(.init(pair: [.failure(.timedOut), .success(ScriptedWiFiFactory.paired)]))
        let flow = WiFiPairingFlow(clientID: ConnectionRig.client, macName: nil, clock: clock, discovery: FakeBarDiscovery(),
                                   transports: factory, tokens: InMemoryTokenStore(), needsLocalNetworkExplanation: false,
                                   onPaired: { _ in })
        flow.choose(PairingScenarios.bar)
        await flow.requestCode()
        await flow.submit(code: "482913")
        XCTAssertEqual(flow.step, .failed(.noAnswer, bar: PairingScenarios.bar, retryAt: nil))
        XCTAssertTrue(flow.acceptsCode)
        XCTAssertTrue(flow.canRetryNow())
        await flow.submit(code: "482913")
        guard case .paired = flow.step else { return XCTFail("\(flow.step)") }

        // After the code expires, it isn't sent again.
        let late = WiFiPairingFlow(clientID: ConnectionRig.client, macName: nil, clock: clock, discovery: FakeBarDiscovery(),
                                   transports: ScriptedWiFiFactory(.init(pair: [.failure(.timedOut)])), tokens: InMemoryTokenStore(),
                                   needsLocalNetworkExplanation: false, onPaired: { _ in })
        late.choose(PairingScenarios.bar)
        await late.requestCode()
        clock.advance(by: 121)
        await late.submit(code: "482913")
        XCTAssertEqual(late.step, .failed(.expired, bar: PairingScenarios.bar, retryAt: nil))
        XCTAssertFalse(late.acceptsCode)
    }

    /// The wait after `rate_limited` is on the flow's clock.
    func rateLimitWait() async throws {
        let clock = ManualClock()
        let limited = BarError.api(APIErrorBody(error: .rateLimited, retryAfterS: 5), httpStatus: 429)
        let factory = ScriptedWiFiFactory(.init(pair: [.failure(limited), .success(ScriptedWiFiFactory.paired)]))
        let flow = WiFiPairingFlow(clientID: ConnectionRig.client, macName: nil, clock: clock, discovery: FakeBarDiscovery(),
                                   transports: factory, tokens: InMemoryTokenStore(), needsLocalNetworkExplanation: false,
                                   onPaired: { _ in })
        flow.choose(PairingScenarios.bar)
        await flow.requestCode()
        await flow.submit(code: "482913")
        XCTAssertFalse(flow.canRetryNow())
        await flow.submit(code: "482913")
        guard case .failed(.rateLimited, _, _) = flow.step else { return XCTFail("sent during the wait: \(flow.step)") }
        clock.changeWallClock(by: 3600)
        XCTAssertFalse(flow.canRetryNow(), "setting the Mac's clock doesn't end the wait")
        clock.advance(by: 5)
        XCTAssertTrue(flow.canRetryNow())
        await flow.submit(code: "482913")
        guard case .paired = flow.step else { return XCTFail("\(flow.step)") }
    }
}
