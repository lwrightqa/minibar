import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
import XCTest
@testable import TinyBarCore

/// The Mac app's pairing messages on the wire, field by field against
/// api.md 4.6, 4.7 (with `pair/cancel`) and 5.2, and every pairing reply the
/// contract has, as the fake HTTP server serves them through the real
/// `HTTPTransport`.
final class PairingWireTests: XCTestCase {
    static let client = ConnectionRig.client
    static let token = "tb1_" + String(repeating: "B", count: 43)

    // The replies, as api.md writes them.
    static let started = #"{"ok": true, "pairing_id": "d407580a9215e992", "expires_in_s": 120, "code_length": 6, "attempts": 3}"#
    static let paired = #"{"ok": true, "token": "tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4", "token_id": "74d8a526", "scope": "call", "device_id": "f412fa3f2a1c", "name": "TinyBar 2A1C", "host": "tinybar.local"}"#
    static let canceled = #"{"ok": true}"#
    static let busy = #"{"ok": false, "error": "pairing_busy", "message": "Another device is pairing. Try again in 74 seconds.", "field": null, "retry_after_s": 74}"#
    static let backOff = #"{"ok": false, "error": "rate_limited", "message": "Too many pairings failed. Try again in 240 seconds.", "field": null, "retry_after_s": 240}"#
    static let oneASecond = #"{"ok": false, "error": "rate_limited", "message": "One pairing request a second.", "field": null, "retry_after_s": 1}"#
    static let tokenLimit = #"{"ok": false, "error": "token_limit", "message": "TinyBar already has 10 paired devices. Remove one on the Remote.", "field": null}"#
    static let inSetup = #"{"ok": false, "error": "in_setup", "message": "TinyBar is setting up Wi-Fi.", "field": null}"#
    static let notPairing = #"{"ok": false, "error": "not_pairing", "message": "No code is on the screen for that pairing_id.", "field": "pairing_id"}"#
    static let wrongClient = #"{"ok": false, "error": "wrong_client", "message": "This token can't report calls for that client.", "field": "client"}"#
    static func wrongCode(_ left: Int) -> String {
        #"{"ok": false, "error": "wrong_code", "message": "That code doesn't match.", "field": "code", "attempts_left": \#(left)}"#
    }

    private var server: TestHTTPServer!

    override func setUpWithError() throws {
        server = try TestHTTPServer()
    }

    override func tearDown() {
        server.stop()
        server = nil
    }

    // MARK: - Requests, field by field

    func test_pairStartFields() throws {
        // api.md 4.6: name (optional, only if the user named the Mac), kind, scope, client.
        let named = try JSONValue.parse(WireJSON.encodeString(PairStartRequest(name: "Alex's Mac", client: Self.client)))
        XCTAssertEqual(named.keySet, ["name", "kind", "scope", "client"])
        XCTAssertEqual(named["kind"]?.stringValue, "mac")
        XCTAssertEqual(named["scope"]?.stringValue, "call", "the Mac app's call scope (api.md 4.4)")
        XCTAssertEqual(named["client"]?.stringValue, Self.client)
        let unnamed = try JSONValue.parse(WireJSON.encodeString(PairStartRequest(client: Self.client)))
        XCTAssertEqual(unnamed.keySet, ["kind", "scope", "client"], "no name is left out, not sent as null")
        // `client`: 8 to 64 of A-Z a-z 0-9 - (the install ID).
        XCTAssertTrue((8...64).contains(Self.client.count))
        XCTAssertTrue(Self.client.allSatisfy { $0.isASCII && ($0.isLetter || $0.isNumber || $0 == "-") })
    }

    func test_pairAndCancelFields() throws {
        // api.md 4.7: pairing_id and code; cookie is the Remote's, never sent.
        let pair = try JSONValue.parse(WireJSON.encodeString(PairRequest(pairingID: "d407580a9215e992", code: "482913")))
        XCTAssertEqual(pair.keySet, ["pairing_id", "code"])
        // pair/cancel: pairing_id only.
        XCTAssertEqual(try WireJSON.encodeString(PairCancelRequest(pairingID: "d407580a9215e992")),
                       #"{"pairing_id":"d407580a9215e992"}"#)
        XCTAssertEqual(Endpoints.pairCancel.method, .post)
        XCTAssertEqual(Endpoints.pairCancel.path, "/api/v1/pair/cancel")
        XCTAssertFalse(Endpoints.pairCancel.sendsToken, "no token needed (api.md 4.7)")
        XCTAssertTrue(HTTPTransport.isRepeatable(Endpoints.pairCancel), "safe to repeat (api.md 4.7)")
        XCTAssertFalse(HTTPTransport.isRepeatable(Endpoints.pair))
        XCTAssertFalse(HTTPTransport.isRepeatable(Endpoints.pairStart))
        // Replies: `{"ok": true}` decodes, and so does pair/start's 202.
        XCTAssertEqual(try WireJSON.decodeReply(PairCancelReply.self, from: Data(Self.canceled.utf8), httpStatus: 200).ok, true)
        let started = try WireJSON.decodeReply(PairStartReply.self, from: Data(Self.started.utf8), httpStatus: 202)
        XCTAssertEqual(started, PairStartReply(pairingID: "d407580a9215e992", expiresInS: 120, codeLength: 6, attempts: 3))
    }

    func test_pairCancelOverHTTP() async throws {
        let answers = Locked([(200, Self.canceled), (409, Self.notPairing), (429, Self.oneASecond)])
        server.handle { request in
            guard request.method == "POST", request.path == "/api/v1/pair/cancel" else {
                return .respond(status: 404, body: #"{"ok": false, "error": "not_found", "message": "No.", "field": null}"#)
            }
            let (status, body) = answers.withLock { $0.removeFirst() }
            return .respond(status: status, body: body, headers: status == 429 ? ["Retry-After": "1"] : [:])
        }
        // Even a transport that has a token sends none to pair/cancel.
        let wifi = HTTPTransport(endpoint: server.endpoint, token: Self.token, appVersion: "1.0 (12)", timeout: 2)
        let request = PairCancelRequest(pairingID: "d407580a9215e992")

        let reply = try await wifi.pairCancel(request)
        XCTAssertTrue(reply.ok)
        let sent = try XCTUnwrap(server.requests.first)
        XCTAssertEqual(sent.method, "POST")
        XCTAssertEqual(sent.path, "/api/v1/pair/cancel")
        XCTAssertEqual(sent.bodyText, #"{"pairing_id":"d407580a9215e992"}"#)
        XCTAssertEqual(sent.headers["content-type"], "application/json")
        XCTAssertNil(sent.headers["authorization"])
        XCTAssertEqual(sent.headers["user-agent"], "TinyBarMac/1.0 (12) (api 1.0)")
        XCTAssertEqual(sent.headers["accept-language"], "en")
        XCTAssertNil(sent.headers["cookie"])

        // 409 not_pairing: the code had ended already.
        do {
            _ = try await wifi.pairCancel(request)
            XCTFail("expected not_pairing")
        } catch let error as BarError {
            XCTAssertEqual(error, .api(APIErrorBody(error: .notPairing, message: "No code is on the screen for that pairing_id.",
                                                    field: "pairing_id"), httpStatus: 409))
        }
        // 429 rate_limited: shares pair's one request a second.
        do {
            _ = try await wifi.pairCancel(request)
            XCTFail("expected rate_limited")
        } catch let error as BarError {
            guard case .api(let body, let status) = error else { return XCTFail("\(error)") }
            XCTAssertEqual(body.error, .rateLimited)
            XCTAssertEqual(body.retryAfterS, 1)
            XCTAssertEqual(status, 429)
        }
        XCTAssertEqual(server.requests.count, 3)
        await wifi.close()
    }

    /// A pair/cancel that meets a connection the bar had closed is sent once
    /// more (api.md 2.6, "safe to repeat").
    func test_pairCancelIsRetriedOnADroppedConnection() async throws {
        let attempts = Locked(0)
        server.handle { _ in
            let attempt = attempts.withLock { count -> Int in count += 1; return count }
            return attempt == 1 ? .closeWithoutResponse : .respond(status: 200, body: PairingWireTests.canceled)
        }
        let wifi = HTTPTransport(endpoint: server.endpoint, token: nil, appVersion: "1.0 (12)", timeout: 2)
        let reply = try await wifi.pairCancel(PairCancelRequest(pairingID: "d407580a9215e992"))
        XCTAssertTrue(reply.ok)
        XCTAssertEqual(server.requests.count, 2)
        XCTAssertEqual(server.requests[0].bodyText, server.requests[1].bodyText)
        await wifi.close()
    }

    func test_wrongClientOnACall() async throws {
        server.respond(status: 403, Self.wrongClient)
        let wifi = HTTPTransport(endpoint: server.endpoint, token: Self.token, appVersion: "1.0 (12)", timeout: 2)
        do {
            _ = try await wifi.sendCall(CallRequest(client: Self.client, session: "q8Zr2Lx0", seq: 1, active: false))
            XCTFail("expected wrong_client")
        } catch let error as BarError {
            XCTAssertEqual(error.apiCode, .wrongClient)
            XCTAssertTrue(error.refusesToken, "this Mac's token doesn't match: pair again")
            XCTAssertFalse(error.isUnauthorized)
            guard case .api(let body, let status) = error else { return XCTFail("\(error)") }
            XCTAssertEqual(body.field, "client")
            XCTAssertEqual(status, 403)
        }
        XCTAssertEqual(server.requests.first?.headers["authorization"], "Bearer \(Self.token)")
        await wifi.close()
    }

    /// The whole `wrong_client` path over HTTP: the call is refused, `info`
    /// shows it's the paired bar, the token is dropped, and the bar is told to
    /// forget it with that token.
    func test_wrongClientThroughTheConnection() async throws {
        server.handle { request in
            switch (request.method, request.path) {
            case ("GET", "/api/v1/info"): return .respond(status: 200, body: FakeBarPTY.infoJSON)
            case ("POST", "/api/v1/call"): return .respond(status: 403, body: PairingWireTests.wrongClient)
            case ("DELETE", "/api/v1/clients/self"): return .respond(status: 200, body: #"{"ok": true, "revoked": "74d8a526"}"#)
            default: return .respond(status: 404, body: #"{"ok": false, "error": "not_found", "message": "No.", "field": null}"#)
            }
        }
        let clock = ManualClock()
        let tokens = InMemoryTokenStore(["f412fa3f2a1c": Self.token])
        let known = KnownBar(deviceID: "f412fa3f2a1c", name: "TinyBar 2A1C", host: "tinybar.local",
                             lastEndpoint: server.endpoint, auth: .bearer, tokenID: "74d8a526")
        let connection = BarConnection(
            configuration: .init(clientID: Self.client, sessionID: "q8Zr2Lx0", appVersion: "1.0 (12)"),
            bar: known, clock: clock, transports: DefaultTransportFactory(clock: clock, appVersion: "1.0 (12)"), tokens: tokens)
        await connection.start()
        await settle(connection)
        let state = await connection.state
        XCTAssertEqual(state.phase, .unrecognized, "TinyBar 2A1C doesn't recognize this Mac · Pair Again…")
        XCTAssertNil(try tokens.token(for: "f412fa3f2a1c"))
        XCTAssertEqual(server.requests.map { "\($0.method) \($0.path) \($0.headers["authorization"] ?? "no token")" }, [
            "GET /api/v1/info no token",
            "POST /api/v1/call Bearer \(Self.token)",
            "GET /api/v1/info no token",
            "DELETE /api/v1/clients/self Bearer \(Self.token)",
        ])
        await connection.stop()
    }

    // MARK: - Every pairing reply, through the flow

    func test_everyPairingReplyOverHTTP() async throws {
        try await PairingWireScenarios(server: server).everyPairingReply()
    }
}

/// The Pair Over Wi-Fi flow against the fake HTTP server, through the real
/// transports.
@MainActor
final class PairingWireScenarios {
    let server: TestHTTPServer
    let clock = ManualClock()
    let tokens = InMemoryTokenStore()
    /// Answers for each path, in turn; the last repeats.
    let answers = Locked<[String: [(Int, String)]]>([:])

    init(server: TestHTTPServer) {
        self.server = server
        server.handle { [answers] request in
            answers.withLock { all in
                guard var queue = all[request.path], !queue.isEmpty else {
                    return .respond(status: 404, body: #"{"ok": false, "error": "not_found", "message": "No.", "field": null}"#)
                }
                let (status, body) = queue.count > 1 ? queue.removeFirst() : queue[0]
                all[request.path] = queue
                var headers: [String: String] = [:]
                if let retry = (try? JSONValue.parse(body))?["retry_after_s"]?.intValue { headers["Retry-After"] = "\(retry)" }
                return .respond(status: status, body: body, headers: headers)
            }
        }
    }

    private var bar: DiscoveredBar {
        DiscoveredBar(name: "TinyBar 2A1C", deviceID: "f412fa3f2a1c", auth: .bearer, endpoint: server.endpoint)
    }

    private func flow() -> WiFiPairingFlow {
        WiFiPairingFlow(clientID: ConnectionRig.client, macName: nil, clock: clock, discovery: FakeBarDiscovery(),
                        transports: DefaultTransportFactory(clock: clock, appVersion: "1.0 (12)"), tokens: tokens,
                        needsLocalNetworkExplanation: false, onPaired: { _ in })
    }

    private func answer(_ path: String, _ replies: (Int, String)...) {
        answers.withLock { $0["/api/v1/" + path] = replies }
    }

    private func cancelBodies() -> [String] {
        server.requests.filter { $0.path == "/api/v1/pair/cancel" }.map(\.bodyText)
    }

    func everyPairingReply() async throws {
        typealias W = PairingWireTests
        // Asking for a code (api.md 4.6).
        let startReplies: [((Int, String), PairingProblem, TimeInterval?)] = [
            ((409, W.busy), .busy(retryAfter: 74), 74),
            ((429, W.backOff), .rateLimited(retryAfter: 240), 240),
            ((409, W.tokenLimit), .tokenLimit, nil),
            ((409, W.inSetup), .inSetup, nil),
        ]
        for (reply, problem, wait) in startReplies {
            answer("pair/start", reply)
            let flow = flow()
            flow.choose(bar)
            await flow.requestCode()
            XCTAssertEqual(flow.step, .failed(problem, bar: bar, retryAt: wait.map { clock.now().addingTimeInterval($0) }), reply.1)
            XCTAssertNil(flow.cancel(), "no code was shown")
        }

        // Sending the code (api.md 4.7). `outlives`: the code is still on the
        // bar, so Back takes it off with pair/cancel.
        let pairReplies: [((Int, String), PairingProblem, Bool)] = [
            ((403, W.wrongCode(2)), .wrongCode(attemptsLeft: 2), true),
            ((403, W.wrongCode(1)), .wrongCode(attemptsLeft: 1), true),
            ((403, W.wrongCode(0)), .codeUsedUp, false),
            ((409, W.notPairing), .expired, false),
            ((409, W.tokenLimit), .tokenLimit, false),
            ((409, W.inSetup), .inSetup, false),
            ((429, W.oneASecond), .rateLimited(retryAfter: 1), true),
        ]
        for (reply, problem, outlives) in pairReplies {
            answer("pair/start", (202, W.started))
            answer("pair", reply)
            answer("pair/cancel", (200, W.canceled))
            let before = cancelBodies().count
            let flow = flow()
            flow.choose(bar)
            await flow.requestCode()
            guard case .enterCode(_, "d407580a9215e992", _) = flow.step else { return XCTFail("\(flow.step)") }
            await flow.submit(code: "482 913")
            guard case .failed(let got, _, _) = flow.step else { return XCTFail("\(reply.1): \(flow.step)") }
            XCTAssertEqual(got, problem, reply.1)
            XCTAssertEqual(server.requests.last { $0.path == "/api/v1/pair" }?.bodyText,
                           #"{"code":"482913","pairing_id":"d407580a9215e992"}"#)
            let request = flow.cancel()   // Back
            XCTAssertEqual(request != nil, outlives, reply.1)
            await request?.value
            XCTAssertEqual(cancelBodies().count, before + (outlives ? 1 : 0), reply.1)
            if outlives {
                XCTAssertEqual(cancelBodies().last, #"{"pairing_id":"d407580a9215e992"}"#)
            }
        }

        // And the code that works.
        answer("pair/start", (202, W.started))
        answer("pair", (200, W.paired))
        let flow = flow()
        flow.choose(bar)
        await flow.requestCode()
        await flow.submit(code: "482913")
        guard case .paired(let known) = flow.step else { return XCTFail("\(flow.step)") }
        XCTAssertEqual(known.tokenID, "74d8a526")
        XCTAssertEqual(try tokens.token(for: "f412fa3f2a1c"), ScriptedWiFiFactory.token)
        let cancels = cancelBodies().count
        XCTAssertNil(flow.cancel(), "nothing to cancel after pairing")
        XCTAssertEqual(cancelBodies().count, cancels)
        XCTAssertTrue(server.requests.allSatisfy { $0.headers["authorization"] == nil }, "pairing never sends a token")
    }
}
