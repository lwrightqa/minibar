import Foundation
import XCTest
@testable import TinyBarCore

/// A Bonjour browser the test drives.
final class FakeBarDiscovery: BarDiscovery, @unchecked Sendable {
    private struct State {
        var onChange: (@Sendable ([DiscoveredBar]) -> Void)?
        var onError: (@Sendable (DiscoveryError) -> Void)?
        var starts = 0
        var stops = 0
    }

    private let state = Locked(State())

    var starts: Int { state.withLock { $0.starts } }
    var stops: Int { state.withLock { $0.stops } }
    var isRunning: Bool { state.withLock { $0.onChange != nil } }

    func start(onChange: @escaping @Sendable ([DiscoveredBar]) -> Void, onError: @escaping @Sendable (DiscoveryError) -> Void) {
        state.withLock {
            $0.onChange = onChange
            $0.onError = onError
            $0.starts += 1
        }
    }

    func stop() {
        state.withLock {
            $0.onChange = nil
            $0.onError = nil
            $0.stops += 1
        }
    }

    func emit(_ bars: [DiscoveredBar]) {
        state.withLock { $0.onChange }?(bars)
    }

    func fail(_ error: DiscoveryError) {
        state.withLock { $0.onError }?(error)
    }
}

/// A Wi-Fi transport whose pairing answers the test scripts.
final class ScriptedWiFiFactory: TransportFactory, @unchecked Sendable {
    struct Script: Sendable {
        var info: Result<InfoReply, BarError> = .success(InfoReply(deviceID: "f412fa3f2a1c", name: "MiniBar 2A1C", fw: "1.0.0"))
        var pairStart: Result<PairStartReply, BarError> = .success(PairStartReply(pairingID: "d407580a9215e992"))
        /// Answers for each `pair`, in turn; the last repeats.
        var pair: [Result<PairReply, BarError>] = [.success(ScriptedWiFiFactory.paired)]
        /// Answers for each `pair/cancel`, in turn; the last repeats.
        var pairCancel: [Result<PairCancelReply, BarError>] = [.success(PairCancelReply())]
        /// `pair/start`, `pair` or `pair/cancel` wait for the test to open the gate.
        var pairStartGate: Gate?
        var pairGate: Gate?
        var pairCancelGate: Gate?
    }

    static let token = "tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4"
    static let paired = PairReply(token: token, tokenID: "74d8a526", deviceID: "f412fa3f2a1c", name: "MiniBar 2A1C", host: "minibar.local")

    let script: Locked<Script>
    let log = Locked<[String]>([])

    init(_ script: Script = Script()) {
        self.script = Locked(script)
    }

    func openUSB(_ device: SerialDevice) throws -> any USBLinkTransport {
        throw BarError.notATinyBar
    }

    func makeWiFi(endpoint: BarEndpoint, token: String?) -> any WiFiLinkTransport {
        Transport(endpoint: endpoint, factory: self)
    }

    final class Transport: WiFiLinkTransport, @unchecked Sendable {
        let kind = LinkKind.wifi
        let endpoint: BarEndpoint
        let factory: ScriptedWiFiFactory

        init(endpoint: BarEndpoint, factory: ScriptedWiFiFactory) {
            self.endpoint = endpoint
            self.factory = factory
        }

        var endpointDescription: String { endpoint.description }

        func hello(_ request: HelloRequest) async throws -> InfoReply {
            factory.log.withLock { $0.append("info \(endpoint)") }
            return try factory.script.withLock { $0.info }.get()
        }

        func sendCall(_ request: CallRequest) async throws -> CallReply { throw BarError.closed }
        func status() async throws -> StatusReply { throw BarError.closed }

        func pairStart(_ request: PairStartRequest) async throws -> PairStartReply {
            factory.log.withLock { $0.append("pair/start " + ((try? WireJSON.encodeString(request)) ?? "")) }
            await factory.script.withLock { $0.pairStartGate }?.wait()
            return try factory.script.withLock { $0.pairStart }.get()
        }

        func pair(_ request: PairRequest) async throws -> PairReply {
            factory.log.withLock { $0.append("pair " + ((try? WireJSON.encodeString(request)) ?? "")) }
            await factory.script.withLock { $0.pairGate }?.wait()
            return try factory.script.withLock { script in
                script.pair.count > 1 ? script.pair.removeFirst() : script.pair[0]
            }.get()
        }

        func pairCancel(_ request: PairCancelRequest) async throws -> PairCancelReply {
            factory.log.withLock { $0.append("pair/cancel " + ((try? WireJSON.encodeString(request)) ?? "")) }
            await factory.script.withLock { $0.pairCancelGate }?.wait()
            return try factory.script.withLock { script in
                script.pairCancel.count > 1 ? script.pairCancel.removeFirst() : script.pairCancel[0]
            }.get()
        }

        func unpairSelf() async throws -> RevokeReply { throw BarError.closed }
        func close() async {}
    }
}

/// `PairingCode` and `WiFiPairingFlow` (criteria 22 and 24, mac-app-ux.md 5.4, 5.5).
final class PairingTests: XCTestCase {
    func test_pairingCode() {
        XCTAssertEqual(PairingCode.normalize("482913"), "482913")
        XCTAssertEqual(PairingCode.normalize("482 913"), "482913")
        XCTAssertEqual(PairingCode.normalize(" 482-913 "), "482913")
        XCTAssertEqual(PairingCode.normalize("482–913"), "482913", "an en dash, as pasted")
        XCTAssertEqual(PairingCode.normalize("4 8 2 9 1 3\n"), "482913")
        XCTAssertNil(PairingCode.normalize("48291"))
        XCTAssertNil(PairingCode.normalize("4829134"))
        XCTAssertNil(PairingCode.normalize("48291x"))
        XCTAssertNil(PairingCode.normalize("４８２９１３"), "full-width digits aren't the bar's digits")
        XCTAssertNil(PairingCode.normalize(""))

        XCTAssertEqual(PairingCode.display("482913"), "482 913")
        XCTAssertEqual(PairingCode.display("4829"), "482 9")
        XCTAssertEqual(PairingCode.display("482"), "482")
        XCTAssertEqual(PairingCode.display("48"), "48")
        XCTAssertEqual(PairingCode.display(""), "")
        XCTAssertEqual(PairingCode.display("482-913"), "482 913")
        XCTAssertEqual(PairingCode.display("48291399"), "482 913", "six digits at most")
    }

    func test_problemMapping() {
        XCTAssertEqual(WiFiPairingFlow.problem(for: BarError.api(APIErrorBody(error: .pairingBusy), httpStatus: 409)), .busy(retryAfter: 60))
        XCTAssertEqual(WiFiPairingFlow.problem(for: BarError.api(APIErrorBody(error: .wrongCode), httpStatus: 403)), .codeUsedUp)
        XCTAssertEqual(WiFiPairingFlow.problem(for: BarError.notATinyBar), .notATinyBar)
        XCTAssertEqual(WiFiPairingFlow.problem(for: CancellationError()), .noAnswer)
    }

    func test_C22_codePairing() async throws {
        try await PairingScenarios().test_C22_codePairing()
    }

    func test_C22_wrongCodeThenRight() async throws {
        try await PairingScenarios().test_C22_wrongCodeThenRight()
    }

    func test_C22_eachProblem() async throws {
        try await PairingScenarios().test_C22_eachProblem()
    }

    func test_C22_aCodePastItsTimeOnTheMacsClockIsStillSent() async throws {
        try await PairingScenarios().test_C22_aCodePastItsTimeOnTheMacsClockIsStillSent()
    }

    func test_lookingAndChoosing() async throws {
        try await PairingScenarios().test_lookingAndChoosing()
    }

    func test_localNetworkBlockedWhileLooking() async throws {
        try await PairingScenarios().test_localNetworkBlockedWhileLooking()
    }

    func test_enterAddress() async throws {
        try await PairingScenarios().test_enterAddress()
    }

    func test_C24_aBarWithoutPairing() async throws {
        try await PairingScenarios().test_C24_aBarWithoutPairing()
    }
}

/// The pairing scenarios, run on the main actor like the Connect window.
@MainActor
final class PairingScenarios {
    static let bar = DiscoveredBar(name: "MiniBar 2A1C", deviceID: "f412fa3f2a1c", api: "1.0", fw: "1.0.0", path: "/api/v1",
                                   auth: .bearer, endpoint: BarEndpoint(host: "minibar.local"))
    static let otherBar = DiscoveredBar(name: "MiniBar E4F5", deviceID: "a0b1c2d3e4f5", auth: .bearer,
                                        endpoint: BarEndpoint(host: "minibar-2.local"))

    let clock = ManualClock()
    let discovery = FakeBarDiscovery()
    let tokens = InMemoryTokenStore()
    let pairedBars = Locked<[KnownBar]>([])
    var paired: [KnownBar] { pairedBars.get() }

    private func flow(_ factory: ScriptedWiFiFactory = ScriptedWiFiFactory(), macName: String? = nil,
                      explain: Bool = false) -> WiFiPairingFlow {
        WiFiPairingFlow(clientID: ConnectionRig.client, macName: macName, clock: clock, discovery: discovery,
                        transports: factory, tokens: tokens, needsLocalNetworkExplanation: explain,
                        onPaired: { [pairedBars] bar in pairedBars.withLock { $0.append(bar) } })
    }

    /// Lets the main actor run the tasks the flow started.
    private func flush() async {
        for _ in 0..<20 { await Task.yield() }
        try? await Task.sleep(nanoseconds: 5_000_000)
        for _ in 0..<20 { await Task.yield() }
    }

    // MARK: - Criterion 22: pairing with the code

    func test_C22_codePairing() async throws {
        let factory = ScriptedWiFiFactory()
        let flow = flow(factory, macName: "Alex's Mac", explain: true)
        XCTAssertEqual(flow.step, .explainLocalNetwork, "macOS 15, the first time: explain before the prompt")
        XCTAssertEqual(discovery.starts, 0, "nothing browses before Continue")

        flow.startLooking()
        XCTAssertEqual(flow.step, .looking)
        XCTAssertEqual(discovery.starts, 1)
        discovery.emit([Self.bar])
        await flush()
        XCTAssertEqual(flow.step, .choose([Self.bar], chosen: Self.bar), "one bar: chosen already")

        await flow.requestCode()
        guard case .enterCode(let bar, let pairingID, let expiresAt) = flow.step else { return XCTFail("\(flow.step)") }
        XCTAssertEqual(bar, Self.bar)
        XCTAssertEqual(pairingID, "d407580a9215e992")
        XCTAssertEqual(expiresAt, clock.now().addingTimeInterval(120))
        XCTAssertEqual(factory.log.get().last,
                       #"pair/start {"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","kind":"mac","name":"Alex's Mac","scope":"call"}"#)

        await flow.submit(code: "482 91")   // not six digits yet: nothing happens
        XCTAssertEqual(factory.log.get().count, 1)
        await flow.submit(code: "482 913")
        let known = KnownBar(deviceID: "f412fa3f2a1c", name: "MiniBar 2A1C", host: "minibar.local",
                             lastEndpoint: BarEndpoint(host: "minibar.local"), auth: .bearer, tokenID: "74d8a526")
        XCTAssertEqual(flow.step, .paired(known))
        XCTAssertEqual(factory.log.get().last, #"pair {"code":"482913","pairing_id":"d407580a9215e992"}"#)
        XCTAssertEqual(try tokens.token(for: "f412fa3f2a1c"), ScriptedWiFiFactory.token, "the token goes to the token store")
        XCTAssertEqual(paired, [known], "the connection adopts the bar")
        XCTAssertFalse(discovery.isRunning, "browsing stops")
    }

    func test_C22_wrongCodeThenRight() async throws {
        let wrong = BarError.api(APIErrorBody(error: .wrongCode, message: "That code doesn't match. 2 tries left.", field: "code",
                                              attemptsLeft: 2), httpStatus: 403)
        let factory = ScriptedWiFiFactory(.init(pair: [.failure(wrong), .success(ScriptedWiFiFactory.paired)]))
        let flow = flow(factory)
        flow.choose(Self.bar)
        await flow.requestCode()
        await flow.submit(code: "111111")
        XCTAssertEqual(flow.step, .failed(.wrongCode(attemptsLeft: 2), bar: Self.bar, retryAt: nil))
        XCTAssertNil(try tokens.token(for: "f412fa3f2a1c"), "pairs nothing")
        await flow.submit(code: "482913")   // the same code is still on the bar
        guard case .paired = flow.step else { return XCTFail("\(flow.step)") }
        XCTAssertEqual(paired.count, 1)
    }

    func test_C22_eachProblem() async throws {
        func api(_ code: APIErrorCode, status: Int, retryAfter: Int? = nil, attemptsLeft: Int? = nil) -> BarError {
            .api(APIErrorBody(error: code, retryAfterS: retryAfter, attemptsLeft: attemptsLeft), httpStatus: status)
        }
        // Asking for a code.
        let startCases: [(BarError, PairingProblem, TimeInterval?)] = [
            (api(.pairingBusy, status: 409, retryAfter: 45), .busy(retryAfter: 45), 45),
            (api(.rateLimited, status: 429, retryAfter: 240), .rateLimited(retryAfter: 240), 240),
            (api(.tokenLimit, status: 409), .tokenLimit, nil),
            (api(.inSetup, status: 409), .inSetup, nil),
            (.timedOut, .noAnswer, nil),
            (.unreachable("refused"), .noAnswer, nil),
            (.localNetworkDenied, .localNetworkBlocked, nil),
        ]
        for (error, problem, retry) in startCases {
            let flow = flow(ScriptedWiFiFactory(.init(pairStart: .failure(error))))
            flow.choose(Self.bar)
            await flow.requestCode()
            XCTAssertEqual(flow.step, .failed(problem, bar: Self.bar, retryAt: retry.map { clock.now().addingTimeInterval($0) }), "\(error)")
        }
        // Sending the code.
        let pairCases: [(BarError, PairingProblem)] = [
            (api(.wrongCode, status: 403, attemptsLeft: 1), .wrongCode(attemptsLeft: 1)),
            (api(.wrongCode, status: 403, attemptsLeft: 0), .codeUsedUp),
            (api(.notPairing, status: 409), .expired),
            // A longer limit than api.md 4.9's one a second (a later bar): shown
            // as a wait. The one-a-second kind is sent once more first
            // (PairCancelTests, rateLimitedPairIsSentOnceMore).
            (api(.rateLimited, status: 429, retryAfter: 30), .rateLimited(retryAfter: 30)),
            (.timedOut, .noAnswer),
        ]
        for (error, problem) in pairCases {
            let factory = ScriptedWiFiFactory(.init(pair: [.failure(error)]))
            let flow = flow(factory)
            flow.choose(Self.bar)
            await flow.requestCode()
            await flow.submit(code: "482913")
            guard case .failed(let got, let bar, _) = flow.step else { return XCTFail("\(flow.step)") }
            XCTAssertEqual(got, problem, "\(error)")
            XCTAssertEqual(bar, Self.bar)
            if problem == .codeUsedUp || problem == .expired {
                let count = factory.log.get().count
                await flow.submit(code: "482913")
                XCTAssertEqual(factory.log.get().count, count, "a used-up code isn't sent again; Show a New Code")
            }
        }
        XCTAssertEqual(paired, [])
        XCTAssertEqual(tokens.deviceIDs, [])
    }

    /// The 2 minutes ran out on the Mac's clock. The bar's 2 minutes start
    /// when the code appears on its screen, which can be later (api.md 4.8),
    /// so the code is still sent, and the bar's `not_pairing` is what says it
    /// has expired ("That code has expired or was canceled on MiniBar").
    func test_C22_aCodePastItsTimeOnTheMacsClockIsStillSent() async throws {
        let factory = ScriptedWiFiFactory(.init(pair: [.failure(.api(APIErrorBody(error: .notPairing), httpStatus: 409)),
                                                      .success(ScriptedWiFiFactory.paired)]))
        let flow = flow(factory)
        flow.choose(Self.bar)
        await flow.requestCode()
        clock.advance(by: 120)
        XCTAssertTrue(flow.acceptsCode, "Pair stays enabled; only the bar knows")
        await flow.submit(code: "482913")
        XCTAssertEqual(flow.step, .failed(.expired, bar: Self.bar, retryAt: nil))
        XCTAssertEqual(factory.log.get().filter { $0.hasPrefix("pair {") }.count, 1, "sent, and the bar decided")
        XCTAssertFalse(flow.acceptsCode, "now the code is known to be gone")
        // Show a New Code, and the new one works.
        await flow.requestCode()
        guard case .enterCode = flow.step else { return XCTFail("\(flow.step)") }
        await flow.submit(code: "482913")
        guard case .paired = flow.step else { return XCTFail("\(flow.step)") }
    }

    func test_lookingAndChoosing() async throws {
        let flow = flow()
        flow.startLooking()
        await clock.waitForSleepers()
        clock.advance(by: 9.9)
        await flush()
        XCTAssertEqual(flow.step, .looking)
        clock.advance(by: 0.1)
        await flush()
        XCTAssertEqual(flow.step, .failed(.noneFound, bar: nil, retryAt: nil), "no MiniBar found after 10 seconds")

        // Try Again, and two bars show up.
        flow.startLooking()
        let unnamed = DiscoveredBar(name: "Something", deviceID: nil, endpoint: BarEndpoint(host: "x.local"))
        discovery.emit([Self.bar, Self.otherBar, unnamed])
        await flush()
        XCTAssertEqual(flow.step, .choose([Self.bar, Self.otherBar], chosen: nil), "several: you choose (only real MiniBars)")
        flow.choose(Self.otherBar)
        XCTAssertEqual(flow.step, .choose([Self.bar, Self.otherBar], chosen: Self.otherBar))
        discovery.emit([Self.otherBar, Self.bar])
        await flush()
        XCTAssertEqual(flow.step, .choose([Self.otherBar, Self.bar], chosen: Self.otherBar), "the choice survives updates")

        flow.cancel()
        XCTAssertFalse(discovery.isRunning)
        XCTAssertEqual(discovery.stops, 2)
    }

    func test_localNetworkBlockedWhileLooking() async throws {
        let flow = flow()
        flow.startLooking()
        discovery.fail(.localNetworkDenied)
        await flush()
        XCTAssertEqual(flow.step, .failed(.localNetworkBlocked, bar: nil, retryAt: nil))
    }

    func test_enterAddress() async throws {
        let factory = ScriptedWiFiFactory()
        let flow = flow(factory)
        await flow.useAddress(" 10.0.4.42 ")
        let bar = DiscoveredBar(name: "MiniBar 2A1C", deviceID: "f412fa3f2a1c", api: "1.0", fw: "1.0.0", path: "/api/v1",
                                auth: .bearer, endpoint: BarEndpoint(host: "10.0.4.42"))
        XCTAssertEqual(flow.step, .choose([bar], chosen: bar), "checked with info, then on to step 3")
        XCTAssertEqual(factory.log.get(), ["info 10.0.4.42"])

        await flow.useAddress("not an address/really")
        XCTAssertEqual(flow.step, .failed(.nothingAt("not an address/really"), bar: nil, retryAt: nil))

        factory.script.withLock { $0.info = .failure(.timedOut) }
        await flow.useAddress("10.0.4.42")
        XCTAssertEqual(flow.step, .failed(.nothingAt("10.0.4.42"), bar: nil, retryAt: nil))

        factory.script.withLock { $0.info = .failure(.malformedReply("<html>")) }
        await flow.useAddress("10.0.4.43")
        XCTAssertEqual(flow.step, .failed(.notATinyBar, bar: nil, retryAt: nil))

        factory.script.withLock { $0.info = .success(InfoReply(device: "Printer", deviceID: "f412fa3f2a1c", name: "x", fw: "1")) }
        await flow.useAddress("10.0.4.44")
        XCTAssertEqual(flow.step, .failed(.notATinyBar, bar: nil, retryAt: nil))
    }

    func test_C24_aBarWithoutPairing() async throws {
        let factory = ScriptedWiFiFactory(.init(info: .success(InfoReply(deviceID: "f412fa3f2a1c", name: "MiniBar 2A1C", fw: "1.0.0",
                                                                         auth: .notRequired))))
        let flow = flow(factory)
        let unknownAuth = DiscoveredBar(name: "MiniBar 2A1C", deviceID: "f412fa3f2a1c", endpoint: BarEndpoint(host: "minibar.local"))
        flow.choose(unknownAuth)
        await flow.requestCode()
        let known = KnownBar(deviceID: "f412fa3f2a1c", name: "MiniBar 2A1C", host: "minibar.local",
                             lastEndpoint: BarEndpoint(host: "minibar.local"), auth: .notRequired)
        XCTAssertEqual(flow.step, .notNeeded(known), "MiniBar 2A1C doesn't need pairing, so you're all set")
        XCTAssertEqual(paired, [known])
        XCTAssertEqual(tokens.deviceIDs, [])
        XCTAssertFalse(factory.log.get().contains { $0.hasPrefix("pair") })
    }

}
