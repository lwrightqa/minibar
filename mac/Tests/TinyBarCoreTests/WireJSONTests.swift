import XCTest
@testable import TinyBarCore

/// Decoding replies and errors (api.md 2.3, 2.4).
final class WireJSONTests: XCTestCase {
    private func decode<T: Decodable>(_ type: T.Type, _ text: String, status: Int? = 200) throws(BarError) -> T {
        try WireJSON.decodeReply(T.self, from: Data(text.utf8), httpStatus: status)
    }

    func testErrorRepliesThrowTheErrorBody() {
        let text = #"{"ok": false, "error": "wrong_code", "message": "That code doesn't match. 2 tries left.", "field": "code", "attempts_left": 2}"#
        XCTAssertThrowsError(try decode(PairReply.self, text, status: 403)) { error in
            guard case .api(let body, let status)? = error as? BarError else { return XCTFail("\(error)") }
            XCTAssertEqual(body.error, .wrongCode)
            XCTAssertEqual(body.attemptsLeft, 2)
            XCTAssertEqual(body.field, "code")
            XCTAssertEqual(status, 403)
        }
    }

    func testUSBErrorRepliesHaveNoHTTPStatus() {
        let text = #"{"id": 4, "ok": false, "error": "token_limit", "message": "TinyBar already has 10 paired devices. Remove one on the Remote.", "field": null}"#
        XCTAssertThrowsError(try decode(PairReply.self, text, status: nil)) { error in
            XCTAssertEqual((error as? BarError)?.apiCode, .tokenLimit)
        }
    }

    func testUnauthorized() {
        let text = #"{"ok": false, "error": "unauthorized", "message": "Pair with this TinyBar first.", "field": null}"#
        XCTAssertThrowsError(try decode(CallReply.self, text, status: 401)) { error in
            XCTAssertTrue((error as? BarError)?.isUnauthorized ?? false)
        }
    }

    func testHTTPErrorWithoutAJSONBody() {
        XCTAssertThrowsError(try decode(CallReply.self, "<html>Request Header Fields Too Large</html>", status: 431)) { error in
            guard case .api(let body, let status)? = error as? BarError else { return XCTFail("\(error)") }
            XCTAssertEqual(body.error, .badRequest)
            XCTAssertEqual(status, 431)
        }
    }

    func testGarbageIsMalformed() {
        XCTAssertThrowsError(try decode(CallReply.self, "not json")) { error in
            guard case .malformedReply? = error as? BarError else { return XCTFail("\(error)") }
        }
        XCTAssertThrowsError(try decode(CallReply.self, #"{"ok": true, "device_id": 7}"#)) { error in
            guard case .malformedReply? = error as? BarError else { return XCTFail("\(error)") }
        }
    }

    func testUnknownFieldsAndOpenValuesAreAccepted() throws {
        // A later 1.x bar: a new field, and a new value of the open field `showing`.
        let text = #"{"ok": true, "device_id": "f412fa3f2a1c", "showing": "focus_mode", "screen": "on", "stale": false, "call": {"active": false, "app": null, "inputs": null, "via": null, "since": null, "aside": false}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 20, "timeout_s": 60, "time": "2026-10-04T14:31:40-07:00", "battery": 0.8}"#
        let reply = try decode(CallReply.self, text)
        XCTAssertEqual(reply.showing.rawValue, "focus_mode")
        XCTAssertEqual(reply.showing.normalized, .own)
        XCTAssertEqual(reply.heartbeatS, 20)
        XCTAssertEqual(reply.call, .none)
    }

    func testAnOddlyFormattedTimeDoesntBreakAReply() throws {
        let text = #"{"ok": true, "device_id": "f412fa3f2a1c", "showing": "own", "screen": "dark", "stale": false, "call": {"active": false, "app": null, "inputs": null, "via": null, "since": null, "aside": false}, "sources": {"calendar": true, "mac": false}, "heartbeat_s": 30, "timeout_s": 90, "time": "14:31"}"#
        let reply = try decode(CallReply.self, text)
        XCTAssertNil(reply.time?.date)
        XCTAssertEqual(reply.screen, .dark)
        XCTAssertFalse(reply.sources.mac)
    }

    func testAPIVersion() {
        XCTAssertEqual(APIVersion("1.0"), APIVersion(major: 1, minor: 0))
        XCTAssertTrue(APIVersion("1.7")!.isCompatibleWithApp)
        XCTAssertFalse(APIVersion("2.0")!.isCompatibleWithApp)
        XCTAssertNil(APIVersion("1"))
        XCTAssertNil(APIVersion("1.x"))
        XCTAssertLessThan(APIVersion("1.2")!, APIVersion("1.10")!)
    }

    func testEndpointsAreVersioned() {
        XCTAssertEqual(Endpoints.call.description, "POST /api/v1/call")
        XCTAssertEqual(Endpoints.clientsSelf.description, "DELETE /api/v1/clients/self")
        XCTAssertFalse(Endpoints.info.sendsToken)
        XCTAssertTrue(Endpoints.status.sendsToken)
    }
}
