import Foundation
import XCTest
@testable import TinyBarCore

/// QA edge cases for `BarConnection` and the engine: the serial port
/// disappearing mid-call, log lines interleaved with replies, pairing
/// failures, a revoked token, an unreachable bar, clock changes, and power-off.
final class QAConnectionTests: XCTestCase {
    let slack = ReportedCall(active: true, app: "Slack", callID: 1, startedAt: ManualClock.apiExampleStart)
    static let client = ConnectionRig.client

    // MARK: - Clock changes

    /// The Mac's clock is set back an hour while connected. Expected: the
    /// heartbeat keeps going every 30 s of real time (the bar ends a call
    /// after 90 s without one).
    func test_clockSetBackKeepsTheHeartbeatGoing() async throws {
        let clock = JumpClock()
        let factory = FakeTransportFactory()
        let bar = FakeBar(clock: ManualClock())
        factory.plug(bar, registryID: ConnectionRig.espressif.registryID)
        let tokens = InMemoryTokenStore(["f412fa3f2a1c": "tb1_" + String(repeating: "A", count: 43)])
        let known = KnownBar(deviceID: "f412fa3f2a1c", name: "TinyBar 2A1C", host: "tinybar.local", auth: .bearer, tokenID: "1")
        let connection = BarConnection(
            configuration: .init(clientID: Self.client, sessionID: "q8Zr2Lx0", appVersion: "1.0 (12)", useWiFi: false),
            bar: known, clock: clock, transports: factory, tokens: tokens)
        await connection.start()
        await connection.handle(.appeared(ConnectionRig.espressif))
        await settle(connection)
        await connection.report(slack)
        await settle(connection)
        for _ in 0..<80 { clock.advance(by: 0.5); await settle(connection) }   // 40 s
        let before = bar.messages.count
        XCTAssertEqual(before, 3, "idle at 0, Slack at 0, heartbeat at 30")

        clock.jump(by: -3600)
        for _ in 0..<240 { clock.advance(by: 0.5); await settle(connection) }  // 120 s more
        let after = bar.messages.count - before
        XCTAssertGreaterThanOrEqual(after, 3, "heartbeats at about 60, 90 and 120 s of real time; got \(after) (the bar would end the call at 120 s)")
        await connection.stop()
    }

    /// Set forward: nothing notices; elapsed_s counts real time.
    func test_clockSetForwardDuringACall() async throws {
        let clock = JumpClock()
        let factory = FakeTransportFactory()
        let bar = FakeBar(clock: ManualClock())
        factory.plug(bar, registryID: 1)
        let connection = BarConnection(
            configuration: .init(clientID: Self.client, sessionID: "q8Zr2Lx0", appVersion: "1.0 (12)", useWiFi: false),
            bar: nil, clock: clock, transports: factory, tokens: InMemoryTokenStore())
        await connection.start()
        bar.set { $0.auth = .notRequired }
        await connection.handle(.appeared(ConnectionRig.espressif))
        await settle(connection)
        await connection.report(slack)
        await settle(connection)
        clock.jump(by: 7200)
        for _ in 0..<64 { clock.advance(by: 0.5); await settle(connection) }
        let elapsed = bar.messages.last?.request.elapsedS
        XCTAssertEqual(elapsed, 30, "the heartbeat at 30 s of real time says 30, not 7230")
        XCTAssertEqual(bar.messages.filter { $0.request.active }.count, 2, "Slack at 0, one heartbeat at 30: no extra one from the jump")
        await connection.stop()
    }

    // MARK: - Power-off that doesn't happen

    /// Logging out or shutting down sends `leaving`. If the logout is canceled
    /// (another app refuses to quit), there's no wake event. Expected: the
    /// app goes on reporting calls.
    func test_canceledPowerOffDoesntSilenceTheApp() async throws {
        try await QAPowerScenarios().canceledPowerOffDoesntSilenceTheApp()
    }

    // MARK: - 401

    /// api.md 16: on 401 check info first; delete the token only when the
    /// paired bar itself refuses it. Here the address now answers 401 to
    /// everything, info included (another device took the bar's address),
    /// so it isn't known to be the paired bar. Expected: the token is kept.
    func test_401FromSomethingThatIsntThePairedBarKeepsTheToken() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        XCTAssertEqual(rig.messageLog(), ["0.0 wifi idle"])
        rig.bar.set { $0.wifiFailure = .api(APIErrorBody(error: .unauthorized), httpStatus: 401) }
        await rig.connection.report(slack)
        await rig.settle()
        XCTAssertNotNil(try rig.tokens.token(for: "f412fa3f2a1c"), "token deleted although info never confirmed the paired bar refused it")
        let state = await rig.state
        XCTAssertNotEqual(state.phase, .unrecognized)
        await rig.connection.stop()
    }

    /// The paired bar itself refuses: token deleted, Pair Again, nothing more
    /// sent, and a call in progress is simply not delivered (the bar ends it
    /// after 90 s). Then USB re-pairs at once.
    func test_tokenRevokedDuringACall() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        await rig.connection.report(slack)
        await rig.settle()
        XCTAssertEqual(rig.messageLog(), ["0.0 wifi idle", "0.0 wifi Slack"])
        rig.bar.set { $0.tokens.removeAll() }
        await rig.run(for: 31)
        var state = await rig.state
        XCTAssertEqual(state.phase, .unrecognized)
        let refusedAt = rig.bar.log.filter { $0 == "wifi call refused" }.count
        XCTAssertEqual(refusedAt, 1)
        await rig.run(for: 120)
        XCTAssertEqual(rig.bar.log.filter { $0 == "wifi call refused" }.count, 1, "no retries without a token")
        await rig.plugIn()
        state = await rig.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertEqual(rig.messageLog().last, "151.0 usb Slack", "the call reaches the bar again over USB")
        await rig.connection.stop()
    }

    // MARK: - Pairing failures over USB

    /// The bar answers `pair` with `busy` (saving) or not at all. Expected:
    /// the app tries pairing again while the bar stays plugged in, so Wi-Fi
    /// works once it's unplugged.
    func test_usbPairingIsRetriedAfterATransientFailure() async throws {
        let clock = ManualClock()
        let wifi = FakeTransportFactory()
        let flaky = FlakyPairFactory(clock: clock, wifi: wifi)
        let bar = flaky.bar
        let connection = BarConnection(
            configuration: .init(clientID: Self.client, sessionID: "q8Zr2Lx0", appVersion: "1.0 (12)"),
            bar: nil, clock: clock, transports: flaky, tokens: flaky.tokens)
        await connection.start()
        await connection.handle(.appeared(ConnectionRig.espressif))
        await settle(connection)
        var state = await connection.state
        XCTAssertEqual(state.phase, .connected, "USB works without a token")
        for _ in 0..<240 { clock.advance(by: 0.5); await settle(connection) }   // two minutes plugged in
        XCTAssertGreaterThan(flaky.pairAttempts, 1, "pair was tried once (busy) and never again")
        await connection.handle(.disappeared(ConnectionRig.espressif))
        await settle(connection)
        state = await connection.state
        XCTAssertNotEqual(state.phase, .unrecognized, "after unplugging, the menu says the bar doesn't recognize this Mac, though it was never paired")
        _ = bar
        await connection.stop()
    }

    /// mac-app-ux.md 5.2: at the token limit the window says "To use Wi-Fi too,
    /// remove a device on TinyBar's Remote". The user does, with the bar still
    /// plugged in. Expected: the app pairs on its own soon after.
    func test_tokenLimitClearedWhilePluggedIn() async throws {
        let rig = ConnectionRig()
        rig.bar.set { $0.tokenLimit = true }
        await rig.start()
        await rig.plugIn()
        var state = await rig.state
        XCTAssertTrue(state.tokenLimitReached)
        rig.bar.set { $0.tokenLimit = false }      // a device removed on the Remote
        await rig.run(for: 120)
        state = await rig.state
        XCTAssertEqual(rig.bar.log.filter { $0 == "usb pair" }.count, 2, "pair is never asked again while plugged in")
        XCTAssertFalse(state.tokenLimitReached)
        XCTAssertNotNil(try rig.tokens.token(for: "f412fa3f2a1c"))
        await rig.connection.stop()
    }

    // MARK: - Unreachable

    func test_aCallThatStartsWhileUnreachableGoesOutAtOnceAndAgainOnRecovery() async throws {
        let rig = ConnectionRig.paired()
        await rig.start()                 // nothing answers at the address
        await rig.run(for: 20)
        var state = await rig.state
        XCTAssertEqual(state.phase, .unreachable)
        let tries = rig.factory.made.count
        await rig.connection.report(slack)
        await rig.settle()
        XCTAssertEqual(rig.factory.made.count, tries + 1, "a change is tried at once, even during the back-off")
        await rig.run(for: 5)
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.run(for: 40)
        state = await rig.state
        XCTAssertEqual(state.phase, .connected)
        let first = try XCTUnwrap(rig.bar.messages.first?.request)
        XCTAssertTrue(first.active)
        XCTAssertEqual(first.elapsedS, Int(rig.seconds(rig.bar.messages.first!.at)) , "elapsed_s counts from the call's start")
        await rig.connection.stop()
    }

    // MARK: - Real USB over a pseudo-terminal

    private func ptyRig(_ options: PTYBarResponder.Options = .init(), known: KnownBar? = nil, tokens: [String: String] = [:])
        throws -> (ManualClock, PTYBarResponder, PTYTransportFactory, BarConnection, InMemoryTokenStore, FakeBar) {
        let clock = ManualClock()
        let responder = try PTYBarResponder(options: options)
        let wifi = FakeTransportFactory()
        let wifiBar = FakeBar(clock: clock)
        let factory = PTYTransportFactory(clock: clock, wifi: wifi)
        let store = InMemoryTokenStore(tokens)
        let connection = BarConnection(
            configuration: .init(clientID: Self.client, sessionID: "q8Zr2Lx0", appVersion: "1.0 (12)"),
            bar: known, clock: clock, transports: factory, tokens: store)
        wifi.place(wifiBar, at: ConnectionRig.barAddress)
        return (clock, responder, factory, connection, store, wifiBar)
    }

    /// Every byte the app writes on the port in a first session: hello, pair, call.
    func test_wireBytesOverUSBMatchTheContract() async throws {
        let (clock, responder, _, connection, store, _) = try ptyRig()
        await connection.start()
        await connection.handle(.appeared(responder.device()))
        await settle(connection)
        await connection.report(slack)
        await settle(connection)
        clock.advance(by: 4)
        await settle(connection)
        let lines = responder.received
        XCTAssertEqual(lines, [
            #"@tb {"cmd":"hello","id":1,"api":"1.0","app_version":"1.0 (12)","client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"}"#,
            #"@tb {"cmd":"pair","id":2,"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"}"#,
            #"@tb {"cmd":"call","id":3,"active":false,"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","seq":1,"session":"q8Zr2Lx0"}"#,
            #"@tb {"cmd":"call","id":4,"active":true,"app":"Slack","call_id":1,"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","elapsed_s":0,"seq":2,"session":"q8Zr2Lx0"}"#,
        ])
        XCTAssertFalse(lines.contains { $0.hasSuffix("\r") }, "lines end in LF only")
        XCTAssertNotNil(try store.token(for: "f412fa3f2a1c"))
        let state = await connection.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertEqual(state.link, .usb)
        await connection.stop()
        responder.vanish()
    }

    /// Log output before, between and after replies, replies written in two
    /// pieces, ANSI colors, binary garbage and "@tb" in the middle of a log line.
    func test_logLinesInterleavedWithReplies() async throws {
        var options = PTYBarResponder.Options()
        options.logNoise = true
        options.splitReplies = true
        let (clock, responder, _, connection, _, _) = try ptyRig(options)
        try responder.pty.write("ESP-ROM:esp32s3-20210327\r\nBuild:Mar 27 2021\r\n")   // boot output waiting before the port opens
        await connection.start()
        await connection.handle(.appeared(responder.device()))
        await settle(connection)
        // A crash dump with no line breaks, longer than any protocol line, then a reply.
        try responder.pty.write(String(repeating: "x", count: 10_000) + "\r\n")
        await connection.report(slack)
        await settle(connection)
        for _ in 0..<130 { clock.advance(by: 0.5); await settle(connection) }   // heartbeats at 30, 60
        let calls = responder.received.filter { $0.contains(#""cmd":"call""#) }
        XCTAssertEqual(calls.count, 4, "idle, Slack, two heartbeats")
        let state = await connection.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertEqual(state.link, .usb)
        XCTAssertEqual(state.lastReply?.call.app, "Slack")
        await connection.stop()
        responder.vanish()
    }

    /// Breaks the firmware's promise (6.4): a log line without its line ending
    /// right before a reply. The reply is lost in the log line. Documents how the
    /// app recovers: a missed reply, then the next heartbeat.
    func test_unterminatedLogLineBeforeAReply() async throws {
        let (clock, responder, _, connection, _, _) = try ptyRig()
        await connection.start()
        await connection.handle(.appeared(responder.device()))
        await settle(connection)
        responder.set { $0.unterminatedLogBeforeReply = true }
        await connection.report(slack)
        // The reply is lost: 3 s later it times out.
        try await Task.sleep(nanoseconds: 150_000_000)
        responder.set { $0.unterminatedLogBeforeReply = false }
        clock.advance(by: 3.1)
        await settle(connection)
        var state = await connection.state
        XCTAssertNotEqual(state.lastReply?.call.app, "Slack", "the reply was lost")
        for _ in 0..<10 { clock.advance(by: 0.5); await settle(connection) }
        state = await connection.state
        XCTAssertEqual(state.lastReply?.call.app, "Slack", "recovered on the retry")
        await connection.stop()
        responder.vanish()
    }

    /// The port disappears while a call message waits for its reply: the state
    /// goes over Wi-Fi at once, before IOKit even reports the removal.
    func test_serialPortDisappearsMidCall() async throws {
        let token = "tb1_" + String(repeating: "A", count: 43)
        let known = KnownBar(deviceID: "f412fa3f2a1c", name: "TinyBar 2A1C", host: "tinybar.local",
                             lastEndpoint: ConnectionRig.barAddress, auth: .bearer, tokenID: "1")
        let (clock, responder, _, connection, _, wifiBar) = try ptyRig(known: known, tokens: ["f412fa3f2a1c": token])
        wifiBar.set { $0.tokens.insert(token) }
        await connection.start()
        await connection.handle(.appeared(responder.device()))
        await settle(connection)
        await connection.report(slack)
        await settle(connection)
        var state = await connection.state
        XCTAssertEqual(state.link, .usb)
        let wifiBefore = wifiBar.messages.count

        // The bar goes quiet, a name change is sent, and the port vanishes mid-request.
        responder.set { $0.muted = true }
        await connection.report(ReportedCall(active: true, app: "Zoom", callID: 1, startedAt: ManualClock.apiExampleStart))
        try await Task.sleep(nanoseconds: 50_000_000)
        responder.vanish()
        let giveUp = Date().addingTimeInterval(3)
        while wifiBar.messages.count == wifiBefore, Date() < giveUp {
            try await Task.sleep(nanoseconds: 5_000_000)
        }
        await settle(connection)
        let wifiMessages = wifiBar.messages.dropFirst(wifiBefore)
        XCTAssertEqual(wifiMessages.first?.request.app, "Zoom", "the current state over Wi-Fi at once")
        XCTAssertEqual(wifiMessages.first.map { $0.at.timeIntervalSince(ManualClock.apiExampleStart) }, 0, "no clock time passed")
        state = await connection.state
        XCTAssertEqual(state.link, .wifi)
        XCTAssertEqual(state.phase, .connected)

        // IOKit reports the removal afterwards; nothing changes, nothing breaks.
        await connection.handle(.disappeared(responder.device()))
        await settle(connection)
        for _ in 0..<70 { clock.advance(by: 0.5); await settle(connection) }
        state = await connection.state
        XCTAssertEqual(state.link, .wifi)
        XCTAssertEqual(wifiBar.messages.last?.request.app, "Zoom")
        await connection.stop()
    }

    /// The port vanishes during the handshake, before IOKit reports it. When
    /// a new port appears (the bar restarted), it's used.
    func test_portVanishesDuringTheHandshakeThenComesBack() async throws {
        var options = PTYBarResponder.Options()
        options.muted = true
        let (clock, responder, _, connection, _, _) = try ptyRig(options)
        await connection.start()
        await connection.handle(.appeared(responder.device(registryID: 7)))
        try await Task.sleep(nanoseconds: 100_000_000)
        responder.vanish()
        try await Task.sleep(nanoseconds: 100_000_000)
        await settle(connection)
        await connection.handle(.disappeared(responder.device(registryID: 7)))
        await settle(connection)

        let again = try PTYBarResponder()
        await connection.handle(.appeared(again.device(registryID: 8)))
        await settle(connection)
        clock.advance(by: 1)
        await settle(connection)
        let state = await connection.state
        XCTAssertEqual(state.link, .usb)
        XCTAssertEqual(state.phase, .connected)
        await connection.stop()
        again.vanish()
    }

    /// A line of garbage starting with the marker was left in the bar's input
    /// (a previous app was killed mid-write). The bar answers the first hello
    /// with `bad_json` and `"id": null`. Expected: the app keeps trying, since
    /// it is a TinyBar.
    func test_badJSONReplyToTheFirstHello() async throws {
        let clock = ManualClock()
        let pty = try FakeBarPTY()
        let port = try SerialPort(path: pty.devicePath)
        let usb = USBTransport(port: port, device: pty.device(), clock: clock)
        let handshake = Task { try await usb.handshake(USBLinkTests.helloRequest) }
        _ = try await offPool { try pty.nextCommand() }
        try pty.send(#"{"id": null, "ok": false, "error": "bad_json", "message": "That line isn't a JSON object.", "field": null}"#)
        try await Task.sleep(nanoseconds: 100_000_000)
        await clock.waitForSleepers()
        clock.advance(by: 2)
        if let second = try await offPool({ try pty.nextCommand(timeout: 1) }) {
            try pty.reply(to: second["id"], FakeBarPTY.infoJSON)
        }
        do {
            let info = try await handshake.value
            XCTAssertTrue(info.isTinyBar)
        } catch {
            XCTFail("handshake gave up on a TinyBar after one bad_json reply: \(error)")
        }
        await usb.close()
    }
}

/// USB transports whose bar answers `pair` with `busy` the first time.
final class FlakyPairFactory: TransportFactory, @unchecked Sendable {
    let clock: ManualClock
    let wifi: FakeTransportFactory
    let bar: FakeBar
    let tokens = InMemoryTokenStore()
    private let attempts = Locked(0)

    init(clock: ManualClock, wifi: FakeTransportFactory) {
        self.clock = clock
        self.wifi = wifi
        bar = FakeBar(clock: clock)
    }

    var pairAttempts: Int { attempts.get() }

    func openUSB(_ device: SerialDevice) throws -> any USBLinkTransport {
        FlakyUSB(inner: FakeUSBTransport(device: device, bar: bar), attempts: attempts)
    }

    func makeWiFi(endpoint: BarEndpoint, token: String?) -> any WiFiLinkTransport {
        wifi.makeWiFi(endpoint: endpoint, token: token)
    }
}

final class FlakyUSB: USBLinkTransport, @unchecked Sendable {
    let inner: FakeUSBTransport
    let attempts: Locked<Int>
    init(inner: FakeUSBTransport, attempts: Locked<Int>) { self.inner = inner; self.attempts = attempts }
    var kind: LinkKind { .usb }
    var device: SerialDevice { inner.device }
    var events: AsyncStream<USBEvent> { inner.events }
    var endpointDescription: String { inner.endpointDescription }
    func handshake(_ request: HelloRequest) async throws -> InfoReply { try await inner.handshake(request) }
    func hello(_ request: HelloRequest) async throws -> InfoReply { try await inner.hello(request) }
    func sendCall(_ request: CallRequest) async throws -> CallReply { try await inner.sendCall(request) }
    func status() async throws -> StatusReply { try await inner.status() }
    func pair(_ request: USBPairRequest) async throws -> PairReply {
        let n = attempts.withLock { $0 += 1; return $0 }
        if n == 1 { throw BarError.api(APIErrorBody(error: .busy, message: "Saving."), httpStatus: nil) }
        return try await inner.pair(request)
    }
    func close() async { await inner.close() }
}

@MainActor
final class QAPowerScenarios {
    func canceledPowerOffDoesntSilenceTheApp() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        await rig.plugIn()
        await rig.micUse(Proc.slack)
        await rig.run(for: 5)
        XCTAssertEqual(rig.messageLog().last, "3.0 usb Slack")
        await rig.power.send(.willPowerOff)
        await rig.settle()
        XCTAssertEqual(rig.messageLog().last, "5.0 usb leaving")
        XCTAssertNotEqual(rig.engine.state.connection.phase, .connected,
                          "after leaving, nothing is being sent, so the menu mustn't say connected")
        // The logout was canceled; the user carries on, and a Zoom call starts.
        await rig.micUse()
        await rig.run(for: 20)
        await rig.micUse(Proc.zoom)
        await rig.run(for: 100)
        let after = rig.messageLog().filter { Double($0.split(separator: " ")[0])! > 5 }
        XCTAssertEqual(after.first?.hasPrefix("15.0 usb"), true, "10 s after the power-off that didn't happen, the app carries on: \(after)")
        XCTAssertTrue(after.contains("15.0 usb idle"), "the Slack call's end (at 15 s) is sent")
        XCTAssertTrue(after.contains("28.0 usb Zoom"), "Zoom reaches the bar after the start delay: \(after)")
        XCTAssertTrue(after.contains("58.0 usb Zoom"), "and heartbeats continue")
        XCTAssertEqual(rig.engine.state.connection.phase, .connected)
        await rig.engine.shutdown()
    }
}
