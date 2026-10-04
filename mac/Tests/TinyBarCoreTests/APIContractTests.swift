import XCTest
@testable import TinyBarCore

/// Checks TinyBarCore's message types against every JSON example in
/// docs/api.md: each example the Mac app could send or receive decodes into
/// the right type and encodes back to the same JSON (with `null` equal to a
/// missing key, api.md 2.3). If the contract changes, these fail until the
/// types follow it.
final class APIContractTests: XCTestCase {
    private func loadExamples() throws -> [APIDocExample] {
        guard FileManager.default.fileExists(atPath: APIDoc.url.path) else {
            throw XCTSkip("docs/api.md not found at \(APIDoc.url.path); the package was moved out of the repository")
        }
        return try APIDoc.examples()
    }

    /// The doc's examples all parse, except the one deliberately broken USB line.
    func testEveryExampleIsJSON() throws {
        let examples = try loadExamples()
        XCTAssertGreaterThan(examples.count, 80, "api.md should have about 85 examples")
        var failures: [String] = []
        for example in examples where (try? example.value()) == nil {
            if example.text.contains("\"active\": \"yes\"") && !example.text.hasSuffix("}") { continue }
            failures.append(example.description)
        }
        XCTAssertEqual(failures, [], "Examples that aren't JSON")
    }

    // MARK: - Responses

    private enum ResponseKind: String, CaseIterable {
        case error, ready, status, callReply, info, pairStart, pair, revoke, notUsedByTheMacApp
    }

    private func classifyResponse(_ value: JSONValue) -> ResponseKind {
        if value["ok"]?.boolValue == false { return .error }
        if value["event"] != nil { return .ready }
        if value["rev"] != nil, value["own"] != nil { return .status }
        if value["stale"] != nil { return .callReply }
        if value["device"]?.stringValue == "TinyBar" { return .info }
        if value["pairing_id"] != nil { return .pairStart }
        if value["token_id"] != nil, value.objectValue?.keys.contains("token") == true { return .pair }
        if value["revoked"] != nil { return .revoke }
        // Settings, calendar, paired devices and the setup network are for
        // the Remote and automations; the Mac app's `call` scope can't use them.
        return .notUsedByTheMacApp
    }

    private func roundTrip<T: Codable>(_ type: T.Type, _ value: JSONValue) throws -> JSONValue {
        let decoded = try value.decode(T.self)
        return try JSONValue.from(decoded)
    }

    func testEveryResponseExampleDecodesAndRoundTrips() throws {
        let examples = try loadExamples().filter(\.isResponse)
        var counts: [ResponseKind: Int] = [:]
        var failures: [String] = []
        for example in examples {
            guard let value = try? example.value() else { continue }
            let kind = classifyResponse(value)
            counts[kind, default: 0] += 1
            let original = value.removingKeys(["id", "http_status"])
            do {
                let encoded: JSONValue
                switch kind {
                case .error: encoded = try roundTrip(APIErrorBody.self, original)
                case .ready: encoded = try roundTrip(ReadyEvent.self, original)
                case .status: encoded = try roundTrip(StatusReply.self, original)
                case .callReply: encoded = try roundTrip(CallReply.self, original)
                case .info: encoded = try roundTrip(InfoReply.self, original)
                case .pairStart: encoded = try roundTrip(PairStartReply.self, original)
                case .pair: encoded = try roundTrip(PairReply.self, original)
                case .revoke: encoded = try roundTrip(RevokeReply.self, original)
                case .notUsedByTheMacApp: continue
                }
                if !encoded.isEquivalent(to: original) {
                    failures.append("\(kind): round trip differs\n  doc:  \(original.serialized())\n  ours: \(encoded.serialized())\n  at \(example)")
                }
            } catch {
                failures.append("\(kind): \(error)\n  at \(example)")
            }
        }
        XCTAssertEqual(failures, [], failures.joined(separator: "\n\n"))
        // Make sure the test actually saw each kind (api.md had these counts on 2026-10-04).
        XCTAssertGreaterThanOrEqual(counts[.callReply, default: 0], 8)
        XCTAssertGreaterThanOrEqual(counts[.status, default: 0], 3)
        XCTAssertGreaterThanOrEqual(counts[.info, default: 0], 2)
        XCTAssertGreaterThanOrEqual(counts[.error, default: 0], 10)
        XCTAssertGreaterThanOrEqual(counts[.pair, default: 0], 3)
        XCTAssertGreaterThanOrEqual(counts[.pairStart, default: 0], 1)
        XCTAssertGreaterThanOrEqual(counts[.ready, default: 0], 1)
        XCTAssertGreaterThanOrEqual(counts[.revoke, default: 0], 2)
    }

    /// Every response object has every field (api.md 2.3), so the examples'
    /// key sets are the contract's. These are the sets the types model.
    func testResponseExamplesHaveExactlyTheModeledFields() throws {
        let callReply: Set = ["ok", "device_id", "showing", "screen", "stale", "call", "sources", "heartbeat_s", "timeout_s", "time"]
        let barCall: Set = ["active", "app", "inputs", "via", "since", "aside"]
        let info: Set = ["ok", "device", "device_id", "name", "fw", "api", "host", "auth", "pairing", "heartbeat_s",
                         "timeout_s", "time", "time_source", "wifi"]
        let status: Set = ["ok", "device_id", "rev", "time", "time_source", "showing", "screen", "own", "message", "away",
                           "call", "meeting", "pomodoro", "sources", "macs", "calendar", "wifi"]
        let pomodoro: Set = ["state", "phase", "round", "rounds", "length_s", "remaining_s", "ends_at", "paused_by",
                             "ringing", "done_today", "focused_today_s"]
        let mac: Set = ["client", "name", "via", "connected", "active", "last_heard"]
        let pair: Set = ["ok", "token", "token_id", "scope", "device_id", "name", "host"]
        var failures: [String] = []
        func check(_ name: String, _ value: JSONValue?, _ expected: Set<String>, _ example: APIDocExample) {
            guard let value else { return failures.append("\(name) missing at \(example)") }
            let keys = value.removingKeys(["id", "http_status"]).keySet
            if keys != expected {
                failures.append("\(name): missing \(expected.subtracting(keys).sorted()), extra \(keys.subtracting(expected).sorted()) at \(example)")
            }
        }
        for example in try loadExamples().filter(\.isResponse) {
            guard let value = try? example.value() else { continue }
            switch classifyResponse(value) {
            case .callReply:
                check("call reply", value, callReply, example)
                check("call", value["call"], barCall, example)
            case .info:
                check("info", value, info, example)
            case .status:
                check("status", value, status, example)
                check("status.call", value["call"], barCall, example)
                check("status.pomodoro", value["pomodoro"], pomodoro, example)
                if case .array(let macs)? = value["macs"] {
                    for entry in macs { check("status.macs[]", entry, mac, example) }
                }
            case .pair:
                check("pair", value, pair, example)
            default:
                break
            }
        }
        XCTAssertEqual(failures, [], failures.joined(separator: "\n"))
    }

    // MARK: - Requests

    private enum RequestKind {
        case call, hello, pairStart, pair, usbPair, usbStatus, usbRequest, notSentByTheMacApp
    }

    private func classifyRequest(_ value: JSONValue) -> RequestKind {
        switch value["cmd"]?.stringValue {
        case "hello": return .hello
        case "call": return .call
        case "pair": return .usbPair
        case "status": return .usbStatus
        case "request": return .usbRequest
        case .some: return .notSentByTheMacApp  // the unknown-command example
        case .none: break
        }
        if value["client"] != nil, value["active"] != nil { return .call }
        if value["kind"] != nil, value["scope"] != nil { return .pairStart }
        if value["pairing_id"] != nil, value["code"] != nil { return .pair }
        return .notSentByTheMacApp
    }

    func testEveryRequestExampleTheAppCouldSendRoundTrips() throws {
        let examples = try loadExamples().filter { !$0.isResponse }
        var seen: [String: Int] = [:]
        var failures: [String] = []
        for example in examples {
            guard let value = try? example.value() else { continue }
            let kind = classifyRequest(value)
            seen["\(kind)", default: 0] += 1
            let original = value.removingKeys(["cmd", "id"])
            do {
                let encoded: JSONValue
                switch kind {
                case .call: encoded = try roundTrip(CallRequest.self, original)
                case .hello: encoded = try roundTrip(HelloRequest.self, original)
                case .pairStart: encoded = try roundTrip(PairStartRequest.self, original)
                case .pair: encoded = try roundTrip(PairRequest.self, original)
                case .usbPair: encoded = try roundTrip(USBPairRequest.self, original)
                case .usbStatus: encoded = try roundTrip(EmptyBody.self, original)
                case .usbRequest: encoded = try roundTrip(USBRequestCommand.self, original)
                case .notSentByTheMacApp: continue
                }
                if !encoded.isEquivalent(to: original) {
                    failures.append("\(kind): round trip differs\n  doc:  \(original.serialized())\n  ours: \(encoded.serialized())\n  at \(example)")
                }
            } catch {
                failures.append("\(kind): \(error)\n  at \(example)")
            }
        }
        XCTAssertEqual(failures, [], failures.joined(separator: "\n\n"))
        XCTAssertGreaterThanOrEqual(seen["call", default: 0], 10)
        XCTAssertGreaterThanOrEqual(seen["hello", default: 0], 2)
        XCTAssertGreaterThanOrEqual(seen["pairStart", default: 0], 2)
        XCTAssertGreaterThanOrEqual(seen["pair", default: 0], 1)
        XCTAssertGreaterThanOrEqual(seen["usbPair", default: 0], 1)
    }

    /// The call examples the app itself would produce all follow api.md 5.1's rules.
    func testCallRequestExamplesHaveNoProblems() throws {
        for example in try loadExamples() where !example.isResponse {
            guard let value = try? example.value(), classifyRequest(value) == .call,
                  value["active"]?.boolValue != nil, value["client"] != nil
            else { continue }
            let request = try value.removingKeys(["cmd", "id"]).decode(CallRequest.self)
            XCTAssertEqual(request.problems, [], "\(example)")
        }
    }
}
