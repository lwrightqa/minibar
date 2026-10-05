import Foundation
import XCTest
@testable import TinyBarCore

/// Taking a code off the bar when the Wi-Fi page gives it up (`POST
/// /api/v1/pair/cancel`, api.md 4.7; mac-app-ux.md 5.4), and every reply to
/// pairing that the contract has (api.md 4.6, 4.7), in the Mac app's own
/// words as the mock-up's Simulate box shows them (mac-app-ux.md 5.5).
final class PairCancelTests: XCTestCase {
    func test_backTakesTheCodeOffTheBarWithoutWaiting() async throws {
        try await PairCancelScenarios().backTakesTheCodeOffTheBarWithoutWaiting()
    }

    func test_nothingIsCanceledAfterASuccessOrOnceTheCodeEnded() async throws {
        try await PairCancelScenarios().nothingIsCanceledAfterASuccessOrOnceTheCodeEnded()
    }

    func test_aCodeStillOnTheBarIsCanceled() async throws {
        try await PairCancelScenarios().aCodeStillOnTheBarIsCanceled()
    }

    func test_backWhileAskingForACode() async throws {
        try await PairCancelScenarios().backWhileAskingForACode()
    }

    func test_backWhileSendingTheCode() async throws {
        try await PairCancelScenarios().backWhileSendingTheCode()
    }

    func test_anotherBarOrAnAddressGivesUpTheCode() async throws {
        try await PairCancelScenarios().anotherBarOrAnAddressGivesUpTheCode()
    }

    func test_sleepTakesTheCodeOff() async throws {
        try await PairCancelScenarios().sleepTakesTheCodeOff()
    }

    func test_cancelIsBestEffort() async throws {
        try await PairCancelScenarios().cancelIsBestEffort()
    }

    func test_showANewCodeWhileItsOwnCodeShows() async throws {
        try await PairCancelScenarios().showANewCodeWhileItsOwnCodeShows()
    }

    func test_everyPairingReplyInTheMacAppsWords() async throws {
        try await PairCancelScenarios().everyPairingReplyInTheMacAppsWords()
    }

    func test_tokenLimitFromPairOffersShowCodeAgain() async throws {
        try await PairCancelScenarios().tokenLimitFromPairOffersShowCodeAgain()
    }

    func test_waitsAfterBusyAndRateLimited() async throws {
        try await PairCancelScenarios().waitsAfterBusyAndRateLimited()
    }

    // Through the engine: quitting and sleep, as the app does them.

    func test_quitTakesTheCodeOff() async throws {
        try await PairCancelScenarios().quitTakesTheCodeOff()
    }

    func test_sleepThroughTheEngine() async throws {
        try await PairCancelScenarios().sleepThroughTheEngine()
    }

    func test_aClosedWindowsCodeIsCanceledOnce() async throws {
        try await PairCancelScenarios().aClosedWindowsCodeIsCanceledOnce()
    }
}

/// The scenarios, on the main actor like the Connect window.
@MainActor
final class PairCancelScenarios {
    static let bar = PairingScenarios.bar
    static let firstID = "d407580a9215e992"
    /// What the scripted bar logs for `pair/cancel` of the first code: the
    /// exact body sent.
    static let cancelFirst = #"pair/cancel {"pairing_id":"d407580a9215e992"}"#

    let clock = ManualClock()
    let discovery = FakeBarDiscovery()
    let tokens = InMemoryTokenStore()

    static func api(_ code: APIErrorCode, _ status: Int, retryAfter: Int? = nil, attemptsLeft: Int? = nil) -> BarError {
        .api(APIErrorBody(error: code, retryAfterS: retryAfter, attemptsLeft: attemptsLeft), httpStatus: status)
    }

    private func flow(_ factory: ScriptedWiFiFactory) -> WiFiPairingFlow {
        WiFiPairingFlow(clientID: ConnectionRig.client, macName: nil, clock: clock, discovery: discovery,
                        transports: factory, tokens: tokens, needsLocalNetworkExplanation: false, onPaired: { _ in })
    }

    /// A flow whose bar shows the first code.
    private func showingCode(_ factory: ScriptedWiFiFactory, file: StaticString = #filePath, line: UInt = #line) async -> WiFiPairingFlow {
        let flow = flow(factory)
        flow.choose(Self.bar)
        await flow.requestCode()
        XCTAssertEqual(flow.step, .enterCode(Self.bar, pairingID: Self.firstID, expiresAt: clock.now().addingTimeInterval(120)),
                       file: file, line: line)
        return flow
    }

    private func cancels(_ factory: ScriptedWiFiFactory) -> [String] {
        factory.log.get().filter { $0.hasPrefix("pair/cancel ") }
    }

    /// Waits (in real time, up to 2 seconds) until the scripted bar has logged
    /// `count` requests starting with `prefix`; fails the test if it doesn't.
    private func waitFor(_ count: Int, _ prefix: String, in factory: ScriptedWiFiFactory,
                         file: StaticString = #filePath, line: UInt = #line) async {
        let giveUp = Date().addingTimeInterval(2)
        while factory.log.get().filter({ $0.hasPrefix(prefix) }).count < count {
            if Date() > giveUp { return XCTFail("expected \(count) requests starting \"\(prefix)\": \(factory.log.get())", file: file, line: line) }
            await Task.yield()
            try? await Task.sleep(nanoseconds: 1_000_000)
        }
    }

    /// Waits until a request is held at `gate`; fails the test if none comes.
    private func reach(_ gate: Gate, file: StaticString = #filePath, line: UInt = #line) async {
        let arrived = await gate.waitForArrivals(1)
        XCTAssertTrue(arrived, "the request reached the bar", file: file, line: line)
    }

    /// Lets tasks started in the background run (to show nothing more happens).
    private func flush() async {
        for _ in 0..<20 { await Task.yield() }
        try? await Task.sleep(nanoseconds: 20_000_000)
        for _ in 0..<20 { await Task.yield() }
    }

    // MARK: - Back, the window closed

    /// Back takes the code off the bar at once (api.md 4.7), with the
    /// `pairing_id` from `pair/start`, and never waits for the bar's answer.
    func backTakesTheCodeOffTheBarWithoutWaiting() async throws {
        let gate = Gate()
        let factory = ScriptedWiFiFactory(.init(pairCancelGate: gate))
        let flow = await showingCode(factory)

        // `cancel()` returns while the bar hasn't answered yet: the gate is shut.
        let request = flow.cancel()
        XCTAssertNotNil(request, "a code was on the bar")
        await reach(gate)
        XCTAssertEqual(cancels(factory), [Self.cancelFirst], "only the pairing_id, as api.md 4.7 asks")
        gate.open()
        await request?.value

        XCTAssertNil(flow.cancel(), "once is enough")
        await flow.requestCode()
        await flow.submit(code: "482913")
        XCTAssertEqual(factory.log.get(), ["pair/start " + #"{"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","kind":"mac","scope":"call"}"#,
                                           Self.cancelFirst],
                       "after Back the page asks the bar for nothing more")
    }

    /// Not after a success, and not once the code has ended on the bar.
    func nothingIsCanceledAfterASuccessOrOnceTheCodeEnded() async throws {
        // Paired: the code was used.
        do {
            let factory = ScriptedWiFiFactory()
            let flow = await showingCode(factory)
            await flow.submit(code: "482913")
            guard case .paired = flow.step else { return XCTFail("\(flow.step)") }
            XCTAssertNil(flow.cancel(), "Done, or the window closed, after pairing")
            await flush()
            XCTAssertEqual(cancels(factory), [])
        }
        // Run out: 2 minutes have passed.
        do {
            let factory = ScriptedWiFiFactory()
            let flow = await showingCode(factory)
            clock.advance(by: 120)
            XCTAssertNil(flow.cancel())
            await flush()
            XCTAssertEqual(cancels(factory), [])
        }
        // Answers to `pair` that end the code on the bar.
        let endings: [(BarError, PairingProblem)] = [
            (Self.api(.wrongCode, 403, attemptsLeft: 0), .codeUsedUp),
            (Self.api(.notPairing, 409), .expired),
            (Self.api(.tokenLimit, 409), .tokenLimit),   // the safeguard (api.md 4.7)
            (Self.api(.inSetup, 409), .inSetup),         // setup ends any pairing (api.md 13)
        ]
        for (error, problem) in endings {
            let factory = ScriptedWiFiFactory(.init(pair: [.failure(error)]))
            let flow = await showingCode(factory)
            await flow.submit(code: "482913")
            XCTAssertEqual(flow.step, .failed(problem, bar: Self.bar, retryAt: nil), "\(error)")
            XCTAssertFalse(flow.acceptsCode, "\(problem): the code is gone from the bar")
            XCTAssertNil(flow.cancel(), "\(problem)")
            await flush()
            XCTAssertEqual(cancels(factory), [], "\(problem)")
        }
        // No code was shown: `pair/start` was refused.
        let refusals = [Self.api(.pairingBusy, 409, retryAfter: 45), Self.api(.rateLimited, 429, retryAfter: 30),
                        Self.api(.tokenLimit, 409), Self.api(.inSetup, 409), BarError.timedOut]
        for error in refusals {
            let factory = ScriptedWiFiFactory(.init(pairStart: .failure(error)))
            let flow = flow(factory)
            flow.choose(Self.bar)
            await flow.requestCode()
            XCTAssertNil(flow.cancel(), "\(error)")
            await flush()
            XCTAssertEqual(cancels(factory), [], "\(error)")
        }
    }

    /// A wrong code with tries left, `rate_limited` and no answer leave the
    /// code on the bar, so Back takes it off.
    func aCodeStillOnTheBarIsCanceled() async throws {
        let outlives: [(BarError, PairingProblem)] = [
            (Self.api(.wrongCode, 403, attemptsLeft: 2), .wrongCode(attemptsLeft: 2)),
            (Self.api(.rateLimited, 429, retryAfter: 1), .rateLimited(retryAfter: 1)),
            (.timedOut, .noAnswer),
        ]
        for (error, problem) in outlives {
            let factory = ScriptedWiFiFactory(.init(pair: [.failure(error)]))
            let flow = await showingCode(factory)
            await flow.submit(code: "111111")
            guard case .failed(let got, _, _) = flow.step, got == problem else { return XCTFail("\(flow.step)") }
            XCTAssertTrue(flow.acceptsCode, "\(problem): the same code can still be sent")
            await flow.cancel()?.value
            XCTAssertEqual(cancels(factory), [Self.cancelFirst], "\(problem)")
        }
    }

    /// Back while `pair/start` is on its way: the code the bar then shows
    /// comes off again at once, and the page that has gone never shows it.
    func backWhileAskingForACode() async throws {
        let gate = Gate()
        let factory = ScriptedWiFiFactory(.init(pairStartGate: gate))
        let flow = flow(factory)
        flow.choose(Self.bar)
        let asking = Task { await flow.requestCode() }
        await reach(gate)
        XCTAssertEqual(flow.step, .requestingCode(Self.bar))
        XCTAssertNil(flow.cancel(), "no pairing_id yet: the bar's answer decides")
        XCTAssertEqual(cancels(factory), [])
        gate.open()
        await asking.value
        await waitFor(1, "pair/cancel ", in: factory)
        XCTAssertEqual(cancels(factory), [Self.cancelFirst])
        XCTAssertEqual(flow.step, .requestingCode(Self.bar), "never .enterCode")
        XCTAssertFalse(flow.acceptsCode)

        // The bar refused instead: nothing to take off.
        let refusingGate = Gate()
        let refusing = ScriptedWiFiFactory(.init(pairStart: .failure(Self.api(.pairingBusy, 409, retryAfter: 60)),
                                                 pairStartGate: refusingGate))
        let flow2 = self.flow(refusing)
        flow2.choose(Self.bar)
        let asking2 = Task { await flow2.requestCode() }
        await reach(refusingGate)
        flow2.cancel()
        refusingGate.open()
        await asking2.value
        await flush()
        XCTAssertEqual(cancels(refusing), [])
    }

    /// Back while the code is on its way (`pair`): the answer decides. A
    /// success keeps the pairing; a code still on the bar is taken off.
    func backWhileSendingTheCode() async throws {
        func backWhileSending(_ answer: Result<PairReply, BarError>) async -> (WiFiPairingFlow, ScriptedWiFiFactory) {
            let gate = Gate()
            let factory = ScriptedWiFiFactory(.init(pair: [answer], pairGate: gate))
            let flow = await showingCode(factory)
            let sending = Task { await flow.submit(code: "482913") }
            await reach(gate)
            XCTAssertEqual(flow.step, .pairing(Self.bar))
            XCTAssertNil(flow.cancel(), "the code is on its way: its answer decides")
            gate.open()
            await sending.value
            return (flow, factory)
        }

        // The bar took the code: paired, the token is kept, nothing is canceled.
        let (paired, pairedFactory) = await backWhileSending(.success(ScriptedWiFiFactory.paired))
        guard case .paired = paired.step else { return XCTFail("\(paired.step)") }
        XCTAssertEqual(try tokens.token(for: "f412fa3f2a1c"), ScriptedWiFiFactory.token)
        await flush()
        XCTAssertEqual(cancels(pairedFactory), [])

        // A wrong code with tries left: the code is still on the bar.
        let (_, wrongFactory) = await backWhileSending(.failure(Self.api(.wrongCode, 403, attemptsLeft: 2)))
        await waitFor(1, "pair/cancel ", in: wrongFactory)
        XCTAssertEqual(cancels(wrongFactory), [Self.cancelFirst])

        // The third wrong code: the bar ended it already.
        let (_, usedUpFactory) = await backWhileSending(.failure(Self.api(.wrongCode, 403, attemptsLeft: 0)))
        await flush()
        XCTAssertEqual(cancels(usedUpFactory), [])
    }

    /// "Didn't see a code? Choose another TinyBar." and Enter Address… give
    /// up the code on the bar chosen before.
    func anotherBarOrAnAddressGivesUpTheCode() async throws {
        let factory = ScriptedWiFiFactory()
        let flow = flow(factory)
        flow.startLooking()
        discovery.emit([Self.bar, PairingScenarios.otherBar])
        await flush()
        flow.choose(Self.bar)
        await flow.requestCode()
        guard case .enterCode = flow.step else { return XCTFail("\(flow.step)") }

        flow.startLooking()   // Choose another TinyBar
        await waitFor(1, "pair/cancel ", in: factory)
        XCTAssertEqual(flow.step, .looking)
        XCTAssertTrue(discovery.isRunning, "browsing again")

        discovery.emit([Self.bar, PairingScenarios.otherBar])
        await flush()
        flow.choose(Self.bar)
        await flow.requestCode()
        guard case .enterCode = flow.step else { return XCTFail("\(flow.step)") }
        await flow.useAddress("10.0.4.42")
        await waitFor(2, "pair/cancel ", in: factory)
        XCTAssertEqual(cancels(factory), [Self.cancelFirst, Self.cancelFirst])
        flow.cancel()
    }

    // MARK: - Sleep

    /// Nobody types a code while the Mac sleeps: it comes off the bar, and
    /// after the wake the page offers a new one.
    func sleepTakesTheCodeOff() async throws {
        let factory = ScriptedWiFiFactory()
        let flow = await showingCode(factory)
        let request = flow.macWillSleep()
        XCTAssertNotNil(request)
        XCTAssertEqual(flow.step, .failed(.expired, bar: Self.bar, retryAt: nil),
                       "after the wake: That code has expired or was canceled on TinyBar. Show a new code to try again.")
        XCTAssertFalse(flow.acceptsCode)
        await request?.value
        XCTAssertEqual(cancels(factory), [Self.cancelFirst])
        await flow.requestCode()   // Show a New Code
        guard case .enterCode = flow.step else { return XCTFail("\(flow.step)") }

        // No code on the bar: sleep changes nothing.
        let idle = self.flow(ScriptedWiFiFactory())
        idle.choose(Self.bar)
        XCTAssertNil(idle.macWillSleep())
        XCTAssertEqual(idle.step, .choose([Self.bar], chosen: Self.bar))

        // Sleep while the code is on its way, and the bar takes it: paired after all.
        let gate = Gate()
        let sending = ScriptedWiFiFactory(.init(pairGate: gate))
        let flow2 = await showingCode(sending)
        let submitting = Task { await flow2.submit(code: "482913") }
        await reach(gate)
        XCTAssertNil(flow2.macWillSleep(), "the answer decides")
        gate.open()
        await submitting.value
        guard case .paired = flow2.step else { return XCTFail("\(flow2.step)") }
        await flush()
        XCTAssertEqual(cancels(sending), [])
    }

    // MARK: - Best effort

    /// `pair/cancel` never throws and gives up after 2 seconds. It shares
    /// `pair`'s limit of one request a second (api.md 4.9), so after
    /// `rate_limited` it tries once more a second later.
    func cancelIsBestEffort() async throws {
        let id = Self.firstID
        let endpoint = Self.bar.endpoint
        let clock = self.clock

        let limited = ScriptedWiFiFactory(.init(pairCancel: [.failure(Self.api(.rateLimited, 429, retryAfter: 1)),
                                                             .success(PairCancelReply())]))
        let retrying = Task.detached {
            await WiFiPairingFlow.cancelCode(pairingID: id, at: endpoint, transports: limited, clock: clock)
        }
        await waitFor(1, "pair/cancel ", in: limited)
        // Both sleepers must be waiting before the clock moves, or the retry would never wake.
        guard await clock.waitForSleepers(2) else { return XCTFail("the time-out and the second's wait aren't both waiting") }
        clock.advance(by: 1)
        await retrying.value
        XCTAssertEqual(cancels(limited), [Self.cancelFirst, Self.cancelFirst])

        // `not_pairing`: the code had ended already. Nothing more to do.
        let ended = ScriptedWiFiFactory(.init(pairCancel: [.failure(Self.api(.notPairing, 409))]))
        await WiFiPairingFlow.cancelCode(pairingID: id, at: endpoint, transports: ended, clock: clock)
        XCTAssertEqual(cancels(ended), [Self.cancelFirst])

        // No answer: it gives up after 2 seconds on the clock.
        let gate = Gate()
        let silent = ScriptedWiFiFactory(.init(pairCancelGate: gate))
        let waiting = Task.detached {
            await WiFiPairingFlow.cancelCode(pairingID: id, at: endpoint, transports: silent, clock: clock)
        }
        await reach(gate)
        guard await clock.waitForSleepers(1) else { return XCTFail("the time-out isn't waiting") }
        clock.advance(by: WiFiPairingFlow.cancelTimeout)
        await waiting.value
        XCTAssertEqual(cancels(silent).count, 1)
    }

    // MARK: - Every reply

    /// Show a New Code while its own code is still on the bar: the bar says
    /// it's busy (one code at a time), and the page goes back to typing that
    /// code, as the mock-up's Mac app does. Once it has run out, busy means
    /// someone else.
    func showANewCodeWhileItsOwnCodeShows() async throws {
        let factory = ScriptedWiFiFactory()
        let flow = await showingCode(factory)
        let expiresAt = clock.now().addingTimeInterval(120)
        clock.advance(by: 30)
        factory.script.withLock { $0.pairStart = .failure(Self.api(.pairingBusy, 409, retryAfter: 90)) }
        await flow.requestCode()
        XCTAssertEqual(flow.step, .enterCode(Self.bar, pairingID: Self.firstID, expiresAt: expiresAt),
                       "back to typing its own code, not \"Someone else is pairing\"")
        XCTAssertTrue(flow.acceptsCode)
        XCTAssertEqual(cancels(factory), [], "its own code stays")

        // That code was canceled on the bar (a tap): the bar shows a new one.
        factory.script.withLock { $0.pairStart = .success(PairStartReply(pairingID: "5c0e7a91b2d34f60")) }
        await flow.requestCode()
        XCTAssertEqual(flow.step, .enterCode(Self.bar, pairingID: "5c0e7a91b2d34f60", expiresAt: clock.now().addingTimeInterval(120)))
        await flow.submit(code: "482913")
        XCTAssertEqual(factory.log.get().last, #"pair {"code":"482913","pairing_id":"5c0e7a91b2d34f60"}"#)

        // Its own code has run out: busy is someone else's code.
        let other = ScriptedWiFiFactory()
        let flow2 = await showingCode(other)
        clock.advance(by: 120)
        other.script.withLock { $0.pairStart = .failure(Self.api(.pairingBusy, 409, retryAfter: 74)) }
        await flow2.requestCode()
        XCTAssertEqual(flow2.step, .failed(.busy(retryAfter: 74), bar: Self.bar, retryAt: clock.now().addingTimeInterval(74)))
        XCTAssertEqual(PairingProblem.busy(retryAfter: 74).message(barName: "TinyBar 2A1C"),
                       "Someone else is pairing with this TinyBar. Try again in 2 minutes.")
    }

    /// Each reply's problem and message. The words are the mock-up's
    /// (`macErrText` in its Simulate box), with the app's curly apostrophes.
    func everyPairingReplyInTheMacAppsWords() async throws {
        let cases: [(BarError, PairingProblem, String)] = [
            (Self.api(.pairingBusy, 409, retryAfter: 45), .busy(retryAfter: 45),
             "Someone else is pairing with this TinyBar. Try again in 45 seconds."),
            (Self.api(.pairingBusy, 409, retryAfter: 74), .busy(retryAfter: 74),
             "Someone else is pairing with this TinyBar. Try again in 2 minutes."),
            (Self.api(.rateLimited, 429, retryAfter: 1), .rateLimited(retryAfter: 1),
             "Too many tries. You can try again in 1 second."),
            (Self.api(.rateLimited, 429, retryAfter: 240), .rateLimited(retryAfter: 240),
             "Too many tries. You can try again in 4 minutes."),
            (Self.api(.tokenLimit, 409), .tokenLimit,
             "TinyBar 2A1C already has 10 paired devices. Remove one on its Remote, then try again."),
            (Self.api(.inSetup, 409), .inSetup,
             "TinyBar 2A1C is setting up Wi-Fi. Finish setup on the bar, then try again."),
            (Self.api(.wrongCode, 403, attemptsLeft: 2), .wrongCode(attemptsLeft: 2),
             "That code didn't match. 2 tries left."),
            (Self.api(.wrongCode, 403, attemptsLeft: 1), .wrongCode(attemptsLeft: 1),
             "That code didn't match. 1 try left."),
            (Self.api(.wrongCode, 403, attemptsLeft: 0), .codeUsedUp,
             "That code didn't match, so TinyBar canceled pairing. Show a new code to try again."),
            (Self.api(.notPairing, 409), .expired,
             "That code has expired or was canceled on TinyBar. Show a new code to try again."),
            (.timedOut, .noAnswer,
             "TinyBar 2A1C didn't answer. Make sure it's on, then try again."),
        ]
        for (error, problem, words) in cases {
            XCTAssertEqual(WiFiPairingFlow.problem(for: error), problem, "\(error)")
            let shown = problem.message(barName: "TinyBar 2A1C")
            XCTAssertEqual(shown.replacingOccurrences(of: "\u{2019}", with: "'"), words)
            XCTAssertFalse(shown.contains("'"), "curly apostrophes in the app (mac-app-ux.md 2): \(shown)")
        }
        // A wait the bar left out counts as a minute; never less than a second.
        XCTAssertEqual(WiFiPairingFlow.problem(for: Self.api(.pairingBusy, 409)), .busy(retryAfter: 60))
        XCTAssertEqual(WiFiPairingFlow.problem(for: Self.api(.rateLimited, 429, retryAfter: 0)), .rateLimited(retryAfter: 1))
        // A refused call token is never a pairing problem the page could fix.
        XCTAssertEqual(WiFiPairingFlow.problem(for: Self.api(.wrongClient, 403)), .noAnswer)
    }

    /// `token_limit` from `pair` (the safeguard, api.md 4.7): the code has
    /// ended. Once a device is removed on the Remote, Show Code on TinyBar
    /// asks again.
    func tokenLimitFromPairOffersShowCodeAgain() async throws {
        let factory = ScriptedWiFiFactory(.init(pair: [.failure(Self.api(.tokenLimit, 409)), .success(ScriptedWiFiFactory.paired)]))
        let flow = await showingCode(factory)
        await flow.submit(code: "482913")
        XCTAssertEqual(flow.step, .failed(.tokenLimit, bar: Self.bar, retryAt: nil))
        await flow.submit(code: "482913")
        XCTAssertEqual(factory.log.get().filter { $0.hasPrefix("pair {") }.count, 1, "the code ended: not sent again")
        XCTAssertTrue(flow.canRetryNow())
        await flow.requestCode()
        guard case .enterCode = flow.step else { return XCTFail("\(flow.step)") }
        await flow.submit(code: "482913")
        guard case .paired = flow.step else { return XCTFail("\(flow.step)") }
    }

    /// `pairing_busy` and `rate_limited` carry `retry_after_s`: Show Code on
    /// TinyBar comes back when that wait is over, on the flow's clock.
    func waitsAfterBusyAndRateLimited() async throws {
        let waits: [(BarError, TimeInterval)] = [(Self.api(.pairingBusy, 409, retryAfter: 45), 45),
                                                 (Self.api(.rateLimited, 429, retryAfter: 240), 240)]
        for (error, seconds) in waits {
            let factory = ScriptedWiFiFactory(.init(pairStart: .failure(error)))
            let flow = flow(factory)
            flow.choose(Self.bar)
            await flow.requestCode()
            XCTAssertFalse(flow.canRetryNow(), "\(error)")
            clock.advance(by: seconds - 1)
            XCTAssertFalse(flow.canRetryNow(), "\(error)")
            clock.advance(by: 1)
            XCTAssertTrue(flow.canRetryNow(), "\(error)")
            factory.script.withLock { $0.pairStart = .success(PairStartReply(pairingID: Self.firstID)) }
            await flow.requestCode()
            guard case .enterCode = flow.step else { return XCTFail("\(flow.step)") }
        }
    }

    // MARK: - Through the engine

    private static let found = DiscoveredBar(name: "TinyBar 2A1C", deviceID: "f412fa3f2a1c", auth: .bearer,
                                             endpoint: ConnectionRig.barAddress)

    private func cancelLines(_ rig: EngineRig) -> [String] {
        rig.bar.log.filter { $0.hasPrefix("wifi pair/cancel") }
    }

    /// Quitting with a code on the bar: `leaving` and `pair/cancel` both go
    /// out before the app quits.
    func quitTakesTheCodeOff() async throws {
        let rig = EngineRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.begin()
        await rig.plugIn()
        let flow = rig.engine.makeWiFiPairingFlow()   // Pair Over Wi-Fi…, with the window open
        flow.choose(Self.found)
        await flow.requestCode()
        guard case .enterCode = flow.step else { return XCTFail("\(flow.step)") }
        await rig.engine.shutdown()
        // The window keeps its flow; the engine only holds it weakly.
        withExtendedLifetime(flow) {}
        XCTAssertEqual(cancelLines(rig), ["wifi pair/cancel d407580a9215e992"])
        XCTAssertEqual(rig.bar.messages.last?.request.leaving, true)
    }

    /// Sleep with a code on the bar: it comes off while `leaving` goes out.
    func sleepThroughTheEngine() async throws {
        let rig = EngineRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.begin()
        await rig.plugIn()
        let flow = rig.engine.makeWiFiPairingFlow()
        flow.choose(Self.found)
        await flow.requestCode()
        await rig.power.send(.willSleep)
        XCTAssertEqual(cancelLines(rig), ["wifi pair/cancel d407580a9215e992"])
        XCTAssertEqual(rig.bar.messages.last?.request.leaving, true)
        XCTAssertEqual(flow.step, .failed(.expired, bar: Self.found, retryAt: nil))
        await rig.power.send(.didWake)
        await rig.settle()
        await flow.requestCode()   // Show a New Code
        guard case .enterCode = flow.step else { return XCTFail("\(flow.step)") }
        await rig.engine.shutdown()
        withExtendedLifetime(flow) {}
        XCTAssertEqual(cancelLines(rig).count, 2, "quitting takes the new code off too")
    }

    /// The window closed (its flow canceled the code), then the app quits:
    /// the code was taken off once, and quitting sends nothing more.
    func aClosedWindowsCodeIsCanceledOnce() async throws {
        let rig = EngineRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.begin()
        var flow: WiFiPairingFlow? = rig.engine.makeWiFiPairingFlow()
        flow?.choose(Self.found)
        await flow?.requestCode()
        let request = flow?.cancel()
        flow = nil
        await request?.value
        await rig.engine.shutdown()
        XCTAssertEqual(cancelLines(rig), ["wifi pair/cancel d407580a9215e992"])

        // Quitting without the Wi-Fi page: nothing to cancel.
        let plain = EngineRig.paired()
        plain.factory.place(plain.bar, at: ConnectionRig.barAddress)
        await plain.begin()
        await plain.engine.shutdown()
        XCTAssertEqual(cancelLines(plain), [])
    }
}
