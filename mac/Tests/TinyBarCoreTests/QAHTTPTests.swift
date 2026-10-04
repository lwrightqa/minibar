import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
import XCTest
@testable import TinyBarCore

/// QA: the exact bytes and headers of every Wi-Fi request the app makes, and
/// error statuses without a JSON body.
final class QAHTTPTests: XCTestCase {
    func test_everyRequestOnTheWire() async throws {
        let server = try TestHTTPServer()
        defer { server.stop() }
        server.handle { request in
            switch (request.method, request.path) {
            case ("GET", "/api/v1/info"): return .respond(status: 200, body: FakeBarPTY.infoJSON)
            case ("POST", "/api/v1/call"): return .respond(status: 200, body: HTTPTransportTests.callReply)
            case ("POST", "/api/v1/pair/start"): return .respond(status: 202, body: #"{"ok": true, "pairing_id": "d407580a9215e992", "expires_in_s": 120, "code_length": 6, "attempts": 3}"#)
            case ("POST", "/api/v1/pair"): return .respond(status: 200, body: #"{"ok": true, "token": "tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4", "token_id": "74d8a526", "scope": "call", "device_id": "f412fa3f2a1c", "name": "TinyBar 2A1C", "host": "tinybar.local"}"#)
            case ("DELETE", "/api/v1/clients/self"): return .respond(status: 200, body: #"{"ok": true, "revoked": "74d8a526"}"#)
            default: return .respond(status: 404, body: #"{"ok": false, "error": "not_found", "message": "No.", "field": null}"#)
            }
        }
        let token = HTTPTransportTests.token
        let open = HTTPTransport(endpoint: server.endpoint, token: nil, appVersion: "1.0 (12)", timeout: 2)
        let paired = HTTPTransport(endpoint: server.endpoint, token: token, appVersion: "1.0 (12)", timeout: 2)
        _ = try await open.hello(HelloRequest(client: ConnectionRig.client, name: "Lisa's Mac"))
        _ = try await open.pairStart(PairStartRequest(client: ConnectionRig.client))
        _ = try await open.pair(PairRequest(pairingID: "d407580a9215e992", code: "482913"))
        _ = try await paired.sendCall(CallRequest(client: ConnectionRig.client, session: "q8Zr2Lx0", seq: 7, active: false, leaving: true))
        _ = try await paired.unpairSelf()

        let requests = server.requests
        XCTAssertEqual(requests.map { "\($0.method) \($0.path)" },
                       ["GET /api/v1/info", "POST /api/v1/pair/start", "POST /api/v1/pair", "POST /api/v1/call", "DELETE /api/v1/clients/self"])
        XCTAssertEqual(requests.map(\.bodyText), [
            "",
            #"{"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","kind":"mac","scope":"call"}"#,
            #"{"code":"482913","pairing_id":"d407580a9215e992"}"#,
            #"{"active":false,"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","leaving":true,"seq":7,"session":"q8Zr2Lx0"}"#,
            "",
        ])
        for request in requests {
            let hasBody = !request.body.isEmpty
            XCTAssertEqual(request.headers["content-type"], hasBody ? "application/json" : nil, request.path)
            XCTAssertEqual(request.headers["user-agent"], "TinyBarMac/1.0 (12) (api 1.0)")
            XCTAssertEqual(request.headers["accept-language"], "en", "fixed, never the user's language list")
            XCTAssertNil(request.headers["cookie"])
            let headerBytes = request.headers.reduce(0) { $0 + $1.key.utf8.count + $1.value.utf8.count + 4 }
            XCTAssertLessThan(headerBytes, 2048, "api.md 2.5")
        }
        XCTAssertEqual(requests.map { $0.headers["authorization"] != nil }, [false, false, false, true, true])
        print("QA headers sent on Linux:", requests.map { $0.headers })
        await open.close()
        await paired.close()
    }

    func test_errorStatusesWithoutAJSONBody() async throws {
        let server = try TestHTTPServer()
        defer { server.stop() }
        let wifi = HTTPTransport(endpoint: server.endpoint, token: "x", appVersion: "1.0", timeout: 2)
        server.respond(status: 401, "")
        do {
            _ = try await wifi.sendCall(CallRequest(client: ConnectionRig.client, active: false))
            XCTFail("expected an error")
        } catch let error as BarError {
            XCTAssertTrue(error.isUnauthorized, "a bare 401 still means pair again: \(error)")
        }
        server.respond(status: 503, "<html>busy</html>")
        do {
            _ = try await wifi.sendCall(CallRequest(client: ConnectionRig.client, active: false))
            XCTFail("expected an error")
        } catch let error as BarError {
            XCTAssertFalse(error.isUnauthorized)
        }
        await wifi.close()
    }
}
