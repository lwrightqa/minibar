import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
import XCTest
@testable import TinyBarCore

/// `HTTPTransport` against a small HTTP server playing the bar (api.md 2, 4, 5).
final class HTTPTransportTests: XCTestCase {
    static let token = "tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4"
    static let client = "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"
    static let callReply = #"{"ok": true, "device_id": "f412fa3f2a1c", "showing": "call", "screen": "on", "stale": false, "call": {"active": true, "app": "Slack", "inputs": null, "via": "wifi", "since": "2026-10-04T14:12:00-07:00", "aside": false}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:12:00-07:00"}"#

    private var server: TestHTTPServer!

    override func setUpWithError() throws {
        server = try TestHTTPServer()
    }

    override func tearDown() {
        server.stop()
        server = nil
    }

    private func transport(token: String? = HTTPTransportTests.token, timeout: TimeInterval = 2) -> HTTPTransport {
        HTTPTransport(endpoint: server.endpoint, token: token, appVersion: "1.0 (12)", timeout: timeout)
    }

    // MARK: - Building requests

    func test_makeRequestHeaders() throws {
        let bar = BarEndpoint.defaultHost
        let call = HTTPTransport.makeRequest(to: bar, Endpoints.call, body: Data("{}".utf8), token: Self.token, appVersion: "1.0 (12)")
        XCTAssertEqual(call.url?.absoluteString, "http://minibar.local/api/v1/call")
        XCTAssertEqual(call.httpMethod, "POST")
        XCTAssertEqual(call.value(forHTTPHeaderField: "Content-Type"), "application/json")
        XCTAssertEqual(call.value(forHTTPHeaderField: "Accept"), "application/json")
        XCTAssertEqual(call.value(forHTTPHeaderField: "Authorization"), "Bearer \(Self.token)")
        XCTAssertEqual(call.value(forHTTPHeaderField: "User-Agent"), "MiniBarMac/1.0 (12) (api 1.0)")
        XCTAssertEqual(call.timeoutInterval, 5)
        XCTAssertEqual(call.cachePolicy, .reloadIgnoringLocalCacheData)
        XCTAssertFalse(call.httpShouldHandleCookies)

        // No token where the endpoint doesn't take one, even when the app has one.
        for endpoint in [Endpoints.info, Endpoints.pairStart, Endpoints.pair] {
            let request = HTTPTransport.makeRequest(to: bar, endpoint, body: nil, token: Self.token, appVersion: "1.0")
            XCTAssertNil(request.value(forHTTPHeaderField: "Authorization"), "\(endpoint)")
            XCTAssertNil(request.value(forHTTPHeaderField: "Content-Type"), "no body, no Content-Type")
        }
        for endpoint in [Endpoints.status, Endpoints.clientsSelf] {
            let request = HTTPTransport.makeRequest(to: bar, endpoint, body: nil, token: Self.token, appVersion: "1.0")
            XCTAssertEqual(request.value(forHTTPHeaderField: "Authorization"), "Bearer \(Self.token)", "\(endpoint)")
        }
        // No token at all: no header (a bar with auth none, api.md 4.1).
        let open = HTTPTransport.makeRequest(to: bar, Endpoints.call, body: nil, token: nil, appVersion: "1.0")
        XCTAssertNil(open.value(forHTTPHeaderField: "Authorization"))

        let withPort = HTTPTransport.makeRequest(to: BarEndpoint(host: "10.0.4.42", port: 8080), Endpoints.clientsSelf, body: nil, token: nil, appVersion: "1.0")
        XCTAssertEqual(withPort.url?.absoluteString, "http://10.0.4.42:8080/api/v1/clients/self")
        XCTAssertEqual(withPort.httpMethod, "DELETE")
    }

    // MARK: - Against the server

    func test_callOverWiFi() async throws {
        server.respond(Self.callReply)
        let wifi = transport()
        let request = CallRequest(client: Self.client, session: "q8Zr2Lx0", seq: 1, active: true, app: "Slack", callID: 1, elapsedS: 0)
        let reply = try await wifi.sendCall(request)
        XCTAssertEqual(reply.showing, .call)
        XCTAssertEqual(reply.call.app, "Slack")
        XCTAssertEqual(reply.call.via, .wifi)

        let received = try XCTUnwrap(server.requests.first)
        XCTAssertEqual(received.method, "POST")
        XCTAssertEqual(received.path, "/api/v1/call")
        XCTAssertEqual(received.bodyText,
                       #"{"active":true,"app":"Slack","call_id":1,"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","elapsed_s":0,"seq":1,"session":"q8Zr2Lx0"}"#,
                       "the golden bytes (criterion 12)")
        XCTAssertEqual(received.headers["content-type"], "application/json")
        XCTAssertEqual(received.headers["authorization"], "Bearer \(Self.token)")
        XCTAssertEqual(received.headers["user-agent"], "MiniBarMac/1.0 (12) (api 1.0)")
        XCTAssertEqual(received.headers["host"], "127.0.0.1:\(server.port)", "the bar checks Host (api.md 2.2)")
        XCTAssertNil(received.headers["cookie"])
        await wifi.close()
    }

    func test_infoStatusAndUnpair() async throws {
        server.handle { request in
            switch (request.method, request.path) {
            case ("GET", "/api/v1/info"):
                return .respond(status: 200, body: FakeBarPTY.infoJSON)
            case ("GET", "/api/v1/status"):
                return .respond(status: 200, body: USBLinkTests.statusJSON)
            case ("DELETE", "/api/v1/clients/self"):
                return .respond(status: 200, body: #"{"ok": true, "revoked": "74d8a526"}"#)
            default:
                return .respond(status: 404, body: #"{"ok": false, "error": "not_found", "message": "No.", "field": null}"#)
            }
        }
        let wifi = transport()
        let info = try await wifi.hello(HelloRequest(client: Self.client))
        XCTAssertEqual(info.deviceID, "f412fa3f2a1c")
        XCTAssertEqual(info.auth, .bearer)
        let status = try await wifi.status()
        XCTAssertEqual(status.rev, 1843)
        let revoked = try await wifi.unpairSelf()
        XCTAssertEqual(revoked.revoked, "74d8a526")

        let requests = server.requests
        XCTAssertEqual(requests.map { "\($0.method) \($0.path)" }, ["GET /api/v1/info", "GET /api/v1/status", "DELETE /api/v1/clients/self"])
        XCTAssertNil(requests[0].headers["authorization"], "info needs no token")
        XCTAssertEqual(requests[1].headers["authorization"], "Bearer \(Self.token)")
        XCTAssertEqual(requests[2].headers["authorization"], "Bearer \(Self.token)")
        XCTAssertEqual(requests[0].body, Data(), "GET has no body")
        XCTAssertNil(requests[0].headers["content-type"])
        await wifi.close()
    }

    func test_pairingExchange() async throws {
        server.handle { request in
            switch request.path {
            case "/api/v1/pair/start":
                return .respond(status: 202, body: #"{"ok": true, "pairing_id": "d407580a9215e992", "expires_in_s": 120, "code_length": 6, "attempts": 3}"#)
            case "/api/v1/pair":
                if request.bodyText.contains("482913") {
                    return .respond(status: 200, body: #"{"ok": true, "token": "tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4", "token_id": "74d8a526", "scope": "call", "device_id": "f412fa3f2a1c", "name": "MiniBar 2A1C", "host": "minibar.local"}"#)
                }
                return .respond(status: 403, body: #"{"ok": false, "error": "wrong_code", "message": "That code doesn't match. 2 tries left.", "field": "code", "attempts_left": 2}"#)
            default:
                return .respond(status: 404, body: "")
            }
        }
        let wifi = transport(token: nil)
        let started = try await wifi.pairStart(PairStartRequest(client: Self.client))
        XCTAssertEqual(started.pairingID, "d407580a9215e992")
        XCTAssertEqual(started.expiresInS, 120)
        XCTAssertEqual(server.requests.last?.bodyText, #"{"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","kind":"mac","scope":"call"}"#)

        do {
            _ = try await wifi.pair(PairRequest(pairingID: started.pairingID, code: "111111"))
            XCTFail("expected wrong_code")
        } catch let error as BarError {
            guard case .api(let body, let status) = error else { return XCTFail("\(error)") }
            XCTAssertEqual(body.error, .wrongCode)
            XCTAssertEqual(body.attemptsLeft, 2)
            XCTAssertEqual(body.field, "code")
            XCTAssertEqual(status, 403)
        }

        let paired = try await wifi.pair(PairRequest(pairingID: started.pairingID, code: "482913"))
        XCTAssertEqual(paired.token, Self.token)
        XCTAssertEqual(paired.deviceID, "f412fa3f2a1c")
        XCTAssertEqual(server.requests.last?.bodyText, #"{"code":"482913","pairing_id":"d407580a9215e992"}"#)
        XCTAssertEqual(server.requests.count, 3, "pairing requests aren't repeated")
        await wifi.close()
    }

    func test_errorReplies() async throws {
        let wifi = transport()
        let idle = CallRequest(client: Self.client, session: "q8Zr2Lx0", seq: 1, active: false)

        server.respond(status: 401, #"{"ok": false, "error": "unauthorized", "message": "Pair with this MiniBar first.", "field": null}"#)
        do {
            _ = try await wifi.sendCall(idle)
            XCTFail("expected unauthorized")
        } catch let error as BarError {
            XCTAssertTrue(error.isUnauthorized)
            XCTAssertEqual(error, .api(APIErrorBody(error: .unauthorized, message: "Pair with this MiniBar first.", field: nil), httpStatus: 401))
        }

        // A status from the HTTP server itself, without a JSON body (api.md 2.5).
        server.handle { _ in .respond(status: 431, body: "") }
        do {
            _ = try await wifi.sendCall(idle)
            XCTFail("expected an error")
        } catch let error as BarError {
            XCTAssertEqual(error.apiCode, .badRequest)
            guard case .api(_, let status) = error else { return XCTFail() }
            XCTAssertEqual(status, 431)
        }

        // 200 with something that isn't the reply.
        server.respond(status: 200, "<html>captive portal</html>")
        await XCTAssertThrowsBarError(try await wifi.sendCall(idle), .malformedReply("Not a JSON object: x"), matchingCase: true)

        server.respond(status: 503, #"{"ok": false, "error": "busy", "message": "Starting up.", "field": null}"#)
        do {
            _ = try await wifi.status()
            XCTFail("expected busy")
        } catch let error as BarError {
            XCTAssertTrue(error.isConnectionProblem, "busy is retried like a connection problem")
        }
        await wifi.close()
    }

    func test_timeoutAndRefused() async throws {
        server.handle { _ in .noResponse }
        let slow = transport(timeout: 1)
        let start = Date()
        await XCTAssertThrowsBarError(try await slow.status(), .timedOut)
        XCTAssertGreaterThanOrEqual(Date().timeIntervalSince(start), 0.9)
        XCTAssertLessThan(Date().timeIntervalSince(start), 4)
        XCTAssertEqual(server.requests.count, 1, "the request reached the bar, which never answered")
        await slow.close()

        // Nothing listening there: refused.
        let closedPort = try TestHTTPServer()
        let endpoint = closedPort.endpoint
        closedPort.stop()
        let nobody = HTTPTransport(endpoint: endpoint, token: nil, appVersion: "1.0", timeout: 2)
        do {
            _ = try await nobody.hello(HelloRequest(client: Self.client))
            XCTFail("expected unreachable")
        } catch let error as BarError {
            guard case .unreachable = error else { return XCTFail("expected unreachable, got \(error)") }
            XCTAssertTrue(error.isConnectionProblem)
        }
        await nobody.close()
    }

    func test_aDroppedConnectionIsRetriedOnceForRepeatableRequests() async throws {
        let attempts = Locked(0)
        server.handle { request in
            let attempt = attempts.withLock { count -> Int in count += 1; return count }
            return attempt == 1 ? .closeWithoutResponse : .respond(status: 200, body: HTTPTransportTests.callReply)
        }
        let wifi = transport()
        let reply = try await wifi.sendCall(CallRequest(client: Self.client, session: "q8Zr2Lx0", seq: 3, active: true, app: "Slack", callID: 1, elapsedS: 5))
        XCTAssertEqual(reply.showing, .call)
        XCTAssertEqual(server.requests.count, 2, "sent once more on a new connection")
        XCTAssertEqual(server.requests[0].bodyText, server.requests[1].bodyText)

        // pair/start isn't safe to repeat.
        attempts.set(0)
        do {
            _ = try await wifi.pairStart(PairStartRequest(client: Self.client))
            XCTFail("expected a failure")
        } catch let error as BarError {
            XCTAssertTrue(error.isConnectionProblem || { if case .malformedReply = error { return true }; return false }(), "\(error)")
        }
        XCTAssertEqual(server.requests.count, 3, "not repeated")
        await wifi.close()
    }

    func test_closedTransport() async throws {
        server.respond(Self.callReply)
        let wifi = transport()
        await wifi.close()
        await wifi.close()
        await XCTAssertThrowsBarError(try await wifi.status(), .closed)
        XCTAssertEqual(server.requests.count, 0)
        XCTAssertEqual(wifi.endpointDescription, "127.0.0.1:\(server.port)")
        XCTAssertEqual(HTTPTransport(endpoint: .defaultHost, token: nil, appVersion: "1").endpointDescription, "minibar.local")
    }

    func test_cancellingTheTaskCancelsTheRequest() async throws {
        server.handle { _ in .noResponse }
        let wifi = transport(timeout: 10)
        let task = Task { try await wifi.status() }
        try await Task.sleep(nanoseconds: 200_000_000)
        task.cancel()
        let start = Date()
        do {
            _ = try await task.value
            XCTFail("expected cancelled")
        } catch {
            XCTAssertEqual(error as? BarError, .cancelled)
        }
        XCTAssertLessThan(Date().timeIntervalSince(start), 3)
        await wifi.close()
    }

    func test_errorMapping() {
        XCTAssertEqual(HTTPTransport.barError(for: URLError(.timedOut)), .timedOut)
        XCTAssertEqual(HTTPTransport.barError(for: URLError(.cancelled)), .cancelled)
        guard case .unreachable = HTTPTransport.barError(for: URLError(.cannotFindHost)) else { return XCTFail() }
        guard case .unreachable = HTTPTransport.barError(for: URLError(.notConnectedToInternet)) else { return XCTFail() }
        let denied = URLError(.notConnectedToInternet, userInfo: ["_NSURLErrorNWPathKey": "unsatisfied (Local network prohibited), interface: en0[802.11]"])
        XCTAssertEqual(HTTPTransport.barError(for: denied), .localNetworkDenied)
        XCTAssertTrue(HTTPTransport.isStaleConnection(URLError(.networkConnectionLost)))
        XCTAssertTrue(HTTPTransport.isStaleConnection(URLError(.badServerResponse)))
        XCTAssertFalse(HTTPTransport.isStaleConnection(URLError(.timedOut)))
        XCTAssertTrue(HTTPTransport.isRepeatable(Endpoints.call))
        XCTAssertTrue(HTTPTransport.isRepeatable(Endpoints.status))
        XCTAssertTrue(HTTPTransport.isRepeatable(Endpoints.clientsSelf))
        XCTAssertFalse(HTTPTransport.isRepeatable(Endpoints.pair))
        XCTAssertFalse(HTTPTransport.isRepeatable(Endpoints.pairStart))
    }

    func test_defaultFactory() throws {
        let factory = DefaultTransportFactory(clock: ManualClock(), appVersion: "1.0 (12)")
        let wifi = factory.makeWiFi(endpoint: .defaultHost, token: nil)
        XCTAssertEqual(wifi.kind, .wifi)
        XCTAssertEqual(wifi.endpoint, .defaultHost)
        // Never anything but Espressif's USB Serial/JTAG (criterion 16).
        let printer = SerialDevice(calloutPath: "/dev/cu.usbserial-1410", vendorID: 0x1A86, productID: 0x7523, registryID: 9)
        XCTAssertThrowsError(try factory.openUSB(printer)) { XCTAssertEqual($0 as? BarError, .notATinyBar) }
        let bar = try FakeBarPTY()
        let usb = try factory.openUSB(bar.device())
        XCTAssertEqual(usb.kind, .usb)
        XCTAssertEqual(usb.device, bar.device())
    }
}

/// Like `XCTAssertThrowsBarError`, but only the case has to match.
func XCTAssertThrowsBarError<T>(
    _ expression: @autoclosure () async throws -> T,
    _ expected: BarError,
    matchingCase: Bool,
    file: StaticString = #filePath,
    line: UInt = #line
) async {
    do {
        _ = try await expression()
        XCTFail("expected \(expected)", file: file, line: line)
    } catch {
        guard let error = error as? BarError else { return XCTFail("not a BarError: \(error)", file: file, line: line) }
        XCTAssertEqual(String(describing: error).prefix(while: { $0 != "(" }),
                       String(describing: expected).prefix(while: { $0 != "(" }), file: file, line: line)
    }
}
