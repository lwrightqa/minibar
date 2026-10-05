import Foundation
import XCTest
@testable import TinyBarCore

/// QA: pairing over Wi-Fi when things go wrong.
final class QAPairingTests: XCTestCase {
    func test_rateLimitedPairKeepsTheCodeUsable() async throws {
        try await QAPairingScenarios().rateLimitedPairKeepsTheCodeUsable()
    }

    func test_keychainFailureIsReportedAsNoAnswer() async throws {
        try await QAPairingScenarios().keychainFailureIsReportedAsNoAnswer()
    }

    func test_pairReplyForAnotherBarIsRefused() async throws {
        try await QAPairingScenarios().pairReplyForAnotherBarIsRefused()
    }

    func test_doubleSubmitSendsOnePair() async throws {
        try await QAPairingScenarios().doubleSubmitSendsOnePair()
    }
}

final class FailingTokenStore: TokenStore, @unchecked Sendable {
    struct Denied: Error {}
    func token(for deviceID: String) throws -> String? { nil }
    func setToken(_ token: String, for deviceID: String) throws { throw Denied() }
    func removeToken(for deviceID: String) throws {}
}

@MainActor
final class QAPairingScenarios {
    let clock = ManualClock()
    let discovery = FakeBarDiscovery()

    private func flow(_ factory: ScriptedWiFiFactory, tokens: any TokenStore = InMemoryTokenStore()) -> WiFiPairingFlow {
        WiFiPairingFlow(clientID: ConnectionRig.client, macName: nil, clock: clock, discovery: discovery,
                        transports: factory, tokens: tokens, needsLocalNetworkExplanation: false, onPaired: { _ in })
    }

    /// A wrong code, then the right one typed again within a second: the bar
    /// answers 429 (`POST /pair` is limited to one a second, api.md 4.9). Its
    /// code is still on screen. The app waits that second itself and sends
    /// the same code once more, with no message in between (the review of
    /// 2026-10-05), so the pairing goes through.
    func rateLimitedPairKeepsTheCodeUsable() async throws {
        let wrong = BarError.api(APIErrorBody(error: .wrongCode, attemptsLeft: 2), httpStatus: 403)
        let limited = BarError.api(APIErrorBody(error: .rateLimited, retryAfterS: 1), httpStatus: 429)
        let factory = ScriptedWiFiFactory(.init(pair: [.failure(wrong), .failure(limited), .success(ScriptedWiFiFactory.paired)]))
        let flow = flow(factory)
        flow.choose(PairingScenarios.bar)
        await flow.requestCode()
        await flow.submit(code: "111111")
        XCTAssertEqual(flow.step, .failed(.wrongCode(attemptsLeft: 2), bar: PairingScenarios.bar, retryAt: nil))
        let sending = Task { await flow.submit(code: "482913") }
        guard await clock.waitForSleepers(1) else { return XCTFail("the app should wait the second before sending again") }
        XCTAssertEqual(flow.step, .pairing(PairingScenarios.bar), "no message: the refused request had no effect")
        clock.advance(by: 1)
        await sending.value
        guard case .paired = flow.step else {
            return XCTFail("after a 1-second rate limit the code on the bar can't be sent any more (pairing_id forgotten); step: \(flow.step), requests: \(factory.log.get())")
        }
        XCTAssertEqual(factory.log.get().filter { $0.hasPrefix("pair {") }.count, 3, "wrong, refused, sent again")
    }

    /// The Keychain refuses to store the token. The message shown is the one
    /// for "TinyBar didn't answer", which sends the user to the wrong fix.
    func keychainFailureIsReportedAsNoAnswer() async throws {
        let factory = ScriptedWiFiFactory()
        let flow = flow(factory, tokens: FailingTokenStore())
        flow.choose(PairingScenarios.bar)
        await flow.requestCode()
        await flow.submit(code: "482913")
        XCTAssertEqual(flow.step, .failed(.noAnswer, bar: PairingScenarios.bar, retryAt: nil),
                       "documents the mapping: a Keychain failure shows as no answer")
    }

    func pairReplyForAnotherBarIsRefused() async throws {
        let other = PairReply(token: ScriptedWiFiFactory.token, tokenID: "1", deviceID: "a0b1c2d3e4f5", name: "TinyBar E4F5", host: nil)
        let factory = ScriptedWiFiFactory(.init(pair: [.success(other)]))
        let tokens = InMemoryTokenStore()
        let flow = flow(factory, tokens: tokens)
        flow.choose(PairingScenarios.bar)
        await flow.requestCode()
        await flow.submit(code: "482913")
        XCTAssertEqual(tokens.deviceIDs, [], "a token for another bar isn't kept")
        guard case .failed = flow.step else { return XCTFail("\(flow.step)") }
    }

    /// The Pair button and the sixth digit both submit: only one POST /pair.
    func doubleSubmitSendsOnePair() async throws {
        let factory = ScriptedWiFiFactory()
        let flow = flow(factory)
        flow.choose(PairingScenarios.bar)
        await flow.requestCode()
        async let first: Void = flow.submit(code: "482913")
        async let second: Void = flow.submit(code: "482913")
        _ = await (first, second)
        XCTAssertEqual(factory.log.get().filter { $0.hasPrefix("pair {") }.count, 1)
    }
}
