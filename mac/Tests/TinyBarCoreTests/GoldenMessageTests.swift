import XCTest
@testable import TinyBarCore

/// The exact bytes the app sends (docs/mac-app.md criterion 12, as updated by
/// api.md 14.5): compact JSON, keys in byte order, `cmd` and `id` first on
/// USB, nothing else. Each golden message is also checked against the
/// matching example in api.md.
final class GoldenMessageTests: XCTestCase {
    let client = "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"
    let session = "q8Zr2Lx0"
    let start = ManualClock.apiExampleStart

    private func json(_ text: String) throws -> JSONValue {
        try JSONValue.parse(text)
    }

    // MARK: call

    func testCallStartsOverWiFi() throws {
        let call = ReportedCall(active: true, app: "Slack", callID: 1, startedAt: start)
        let request = call.request(client: client, session: session, seq: 1, at: start)
        XCTAssertEqual(
            try WireJSON.encodeString(request),
            #"{"active":true,"app":"Slack","call_id":1,"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","elapsed_s":0,"seq":1,"session":"q8Zr2Lx0"}"#
        )
        // api.md section 1, the minimal Wi-Fi session.
        XCTAssertTrue(try JSONValue.from(request).isEquivalent(to: json(
            #"{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "session": "q8Zr2Lx0", "seq": 1, "active": true, "app": "Slack", "call_id": 1, "elapsed_s": 0}"#
        )))
        XCTAssertEqual(request.problems, [])
    }

    func testHeartbeatDuringCallCarriesElapsedSeconds() throws {
        let call = ReportedCall(active: true, app: "Slack", callID: 7, startedAt: start)
        let request = call.request(client: client, session: session, seq: 42, at: start.addingTimeInterval(30.9))
        // api.md 5.4, the heartbeat 30 seconds later.
        XCTAssertTrue(try JSONValue.from(request).isEquivalent(to: json(
            #"{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "session": "q8Zr2Lx0", "seq": 42, "active": true, "app": "Slack", "call_id": 7, "elapsed_s": 30}"#
        )))
    }

    func testIdleHeartbeatHasOnlyTheRequiredFields() throws {
        let request = ReportedCall.idle.request(client: client, session: session, seq: 44, at: start)
        XCTAssertEqual(
            try WireJSON.encodeString(request),
            #"{"active":false,"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","seq":44,"session":"q8Zr2Lx0"}"#
        )
    }

    func testLeavingSendsActiveFalseEvenDuringACall() throws {
        let call = ReportedCall(active: true, app: "Slack", callID: 7, startedAt: start)
        let request = call.request(client: client, session: session, seq: 47, at: start, leaving: true)
        XCTAssertEqual(
            try WireJSON.encodeString(request),
            #"{"active":false,"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","leaving":true,"seq":47,"session":"q8Zr2Lx0"}"#
        )
        XCTAssertEqual(request.problems, [])
    }

    func testCallWithoutANameHasNoAppKey() throws {
        let call = ReportedCall(active: true, app: nil, callID: 8, startedAt: start)
        let text = try WireJSON.encodeString(call.request(client: client, session: session, seq: 45, at: start))
        XCTAssertFalse(text.contains("\"app\""))
        XCTAssertFalse(text.contains("inputs"), "inputs is Proposed and not sent (api.md 14.2)")
    }

    func testCallOverUSB() throws {
        let call = ReportedCall(active: true, app: "Slack", callID: 1, startedAt: start)
        let line = try USBFraming.commandLine(.call, id: 2, body: call.request(client: client, session: session, seq: 1, at: start))
        XCTAssertEqual(
            String(decoding: line, as: UTF8.self),
            "@tb {\"cmd\":\"call\",\"id\":2,\"active\":true,\"app\":\"Slack\",\"call_id\":1,"
                + "\"client\":\"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60\",\"elapsed_s\":0,\"seq\":1,\"session\":\"q8Zr2Lx0\"}\n"
        )
        // api.md section 1, the same over USB.
        guard case .reply(_, let body) = USBFraming.classify(line.dropLast()) else { return XCTFail("not a protocol line") }
        XCTAssertTrue(try JSONValue.parse(body).isEquivalent(to: json(
            #"{"cmd": "call", "id": 2, "client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "session": "q8Zr2Lx0", "seq": 1, "active": true, "app": "Slack", "call_id": 1, "elapsed_s": 0}"#
        )))
    }

    func testProblemsCatchContractViolations() {
        var request = CallRequest(client: "short", session: "has space", seq: 0, active: true,
                                  app: String(repeating: "x", count: 65), callID: 0, elapsedS: 90_000, leaving: true)
        XCTAssertEqual(request.problems.count, 7)
        request = CallRequest(client: client, session: session, seq: 1, active: false)
        XCTAssertEqual(request.problems, [])
    }

    // MARK: hello, status, pair

    func testHelloOverUSB() throws {
        let hello = HelloRequest(client: client, appVersion: "1.0 (12)")
        let line = try USBFraming.commandLine(.hello, id: 1, body: hello)
        XCTAssertEqual(
            String(decoding: line, as: UTF8.self),
            "@tb {\"cmd\":\"hello\",\"id\":1,\"api\":\"1.0\",\"app_version\":\"1.0 (12)\",\"client\":\"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60\"}\n"
        )
    }

    func testHelloWithTheMacsClockMatchesTheDoc() throws {
        // Proposed (api.md 14.2): sent only once the user agrees.
        let losAngeles = try XCTUnwrap(TimeZone(identifier: "America/Los_Angeles"))
        let hello = HelloRequest(client: client, appVersion: "1.0 (12)",
                                 time: BarTime(start.addingTimeInterval(-2), timeZone: losAngeles),
                                 timeZone: losAngeles.identifier)
        XCTAssertTrue(try JSONValue.from(hello).isEquivalent(to: json(
            #"{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "app_version": "1.0 (12)", "api": "1.0", "time": "2026-10-04T14:11:58-07:00", "time_zone": "America/Los_Angeles"}"#
        )))
    }

    func testStatusCommandOverUSB() throws {
        XCTAssertEqual(String(decoding: try USBFraming.commandLine(.status, id: 3), as: UTF8.self),
                       "@tb {\"cmd\":\"status\",\"id\":3}\n")
    }

    func testPairOverUSB() throws {
        let line = try USBFraming.commandLine(.pair, id: 4, body: USBPairRequest(client: client))
        XCTAssertEqual(String(decoding: line, as: UTF8.self),
                       "@tb {\"cmd\":\"pair\",\"id\":4,\"client\":\"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60\"}\n")
    }

    func testPairStartAndPairOverWiFi() throws {
        XCTAssertEqual(
            try WireJSON.encodeString(PairStartRequest(client: client)),
            #"{"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","kind":"mac","scope":"call"}"#
        )
        XCTAssertEqual(
            try WireJSON.encodeString(PairRequest(pairingID: "d407580a9215e992", code: "482913")),
            #"{"code":"482913","pairing_id":"d407580a9215e992"}"#
        )
    }

    func testUSBLineOverTheLimitIsRefused() {
        let request = USBRequestCommand(method: .post, path: "/api/v1/message",
                                        body: .object(["text": .string(String(repeating: "a", count: 2100))]))
        XCTAssertThrowsError(try USBFraming.commandLine(.request, id: 9, body: request)) { error in
            XCTAssertEqual(error as? BarError, .tooLarge)
        }
    }
}
