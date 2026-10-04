import XCTest
@testable import TinyBarCore

final class RFC3339Tests: XCTestCase {
    let losAngeles = TimeZone(identifier: "America/Los_Angeles")!

    func testFormatsWithTheOffset() {
        XCTAssertEqual(RFC3339.string(from: ManualClock.apiExampleStart, timeZone: losAngeles), "2026-10-04T14:12:00-07:00")
        XCTAssertEqual(RFC3339.string(from: ManualClock.apiExampleStart, timeZone: TimeZone(identifier: "UTC")!),
                       "2026-10-04T21:12:00+00:00")
        XCTAssertEqual(RFC3339.string(from: ManualClock.apiExampleStart, timeZone: TimeZone(identifier: "Asia/Kolkata")!),
                       "2026-10-05T02:42:00+05:30")
        // Winter time, and fractions of a second rounded down.
        let january = Date(timeIntervalSince1970: 1_767_225_600.9)  // 2026-01-01T00:00:00Z
        XCTAssertEqual(RFC3339.string(from: january, timeZone: losAngeles), "2025-12-31T16:00:00-08:00")
        XCTAssertEqual(RFC3339.string(from: Date(timeIntervalSince1970: -1), timeZone: TimeZone(identifier: "UTC")!),
                       "1969-12-31T23:59:59+00:00")
    }

    func testParses() {
        XCTAssertEqual(RFC3339.date(from: "2026-10-04T14:12:00-07:00"), ManualClock.apiExampleStart)
        XCTAssertEqual(RFC3339.date(from: "2026-10-04T21:12:00Z"), ManualClock.apiExampleStart)
        XCTAssertEqual(RFC3339.date(from: "2026-10-04t21:12:00.250z"), ManualClock.apiExampleStart)
        XCTAssertEqual(RFC3339.date(from: "2026-10-05T02:42:00+05:30"), ManualClock.apiExampleStart)
        XCTAssertEqual(RFC3339.date(from: "2024-02-29T12:00:00Z"), Date(timeIntervalSince1970: 1_709_208_000))
    }

    func testRefusesOtherFormats() {
        for text in ["", "14:12", "2026-10-04", "2026-10-04T14:12:00", "2026-10-04T14:12-07:00", "2026-13-04T14:12:00Z",
                     "2026-10-04T24:00:00Z", "2026-10-04T14:12:00+0700", "2026-10-04T14:12:00Z ", "2026-10-04T14:12:00.Z"] {
            XCTAssertNil(RFC3339.date(from: text), text)
        }
    }

    func testRoundTripsEveryHourOfAYear() {
        var date = Date(timeIntervalSince1970: 1_767_225_600)
        for _ in 0..<(24 * 366) {
            XCTAssertEqual(RFC3339.date(from: RFC3339.string(from: date, timeZone: losAngeles)), date)
            date.addTimeInterval(3600)
        }
    }
}

final class JSONValueTests: XCTestCase {
    func testSerializesCompactlyWithSortedKeys() throws {
        let value = try JSONValue.parse(#"{"b": [1, 2.5, true, null], "a": "x/y \"q\" \\ é\n\u0001", "id": 3, "cmd": "call"}"#)
        XCTAssertEqual(value.serialized(),
                       #"{"a":"x/y \"q\" \\ é\n\u0001","b":[1,2.5,true,null],"cmd":"call","id":3}"#)
        XCTAssertEqual(value.serialized(leadingKeys: ["cmd", "id", "missing"]),
                       #"{"cmd":"call","id":3,"a":"x/y \"q\" \\ é\n\u0001","b":[1,2.5,true,null]}"#)
    }

    func testEquivalenceTreatsNullAsMissingAndComparesNumbersByValue() throws {
        let a = try JSONValue.parse(#"{"x": 1, "y": null, "z": {"w": null}}"#)
        let b = try JSONValue.parse(#"{"x": 1.0, "z": {}}"#)
        XCTAssertTrue(a.isEquivalent(to: b))
        XCTAssertFalse(a.isEquivalent(to: try JSONValue.parse(#"{"x": 2}"#)))
        XCTAssertFalse(try JSONValue.parse("[1, 2]").isEquivalent(to: try JSONValue.parse("[2, 1]")))
        XCTAssertFalse(try JSONValue.parse("true").isEquivalent(to: try JSONValue.parse("1")))
    }

    func testBooleansStayBooleans() throws {
        XCTAssertEqual(try JSONValue.parse("[true, 1, 0, false]"), .array([.bool(true), .int(1), .int(0), .bool(false)]))
    }
}

final class IdentifiersTests: XCTestCase {
    func testNewIDsFollowTheContract() {
        for _ in 0..<100 {
            XCTAssertTrue(Identifiers.isValidClientID(Identifiers.newInstallID()))
            XCTAssertTrue(Identifiers.isValidSessionID(Identifiers.newSessionID()))
        }
    }

    func testValidation() {
        XCTAssertTrue(Identifiers.isValidClientID("6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"))
        XCTAssertFalse(Identifiers.isValidClientID("short"))
        XCTAssertFalse(Identifiers.isValidClientID("has_underscore_12"))
        XCTAssertTrue(Identifiers.isValidSessionID("q8Zr2Lx0"))
        XCTAssertFalse(Identifiers.isValidSessionID(""))
        XCTAssertFalse(Identifiers.isValidSessionID("q8Zr2Lx0q8Zr2Lx0q"))
        XCTAssertTrue(Identifiers.isValidDeviceID("f412fa3f2a1c"))
        XCTAssertFalse(Identifiers.isValidDeviceID("F412FA3F2A1C"))
        XCTAssertTrue(Identifiers.looksLikeToken("tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4"))
        XCTAssertFalse(Identifiers.looksLikeToken("tb1_short"))
    }
}

final class ManualClockTests: XCTestCase {
    func testSleepersWakeWhenTheClockPassesTheirDeadline() async throws {
        let clock = ManualClock()
        let start = clock.now()
        let woke = Locked<Set<Int>>([])
        var tasks: [Int: Task<Void, any Error>] = [:]
        for seconds in [5, 1, 3] {
            tasks[seconds] = Task {
                try await clock.sleep(until: start.addingTimeInterval(TimeInterval(seconds)))
                woke.withLock { _ = $0.insert(seconds) }
            }
        }
        let allAsleep = await clock.waitForSleepers(3)
        XCTAssertTrue(allAsleep)
        XCTAssertEqual(clock.nextDeadline, start.addingTimeInterval(1))
        clock.advance(by: 3)
        try await tasks[1]!.value
        try await tasks[3]!.value
        XCTAssertEqual(woke.get(), [1, 3])
        XCTAssertEqual(clock.sleeperCount, 1)
        clock.advance(by: 10)
        try await tasks[5]!.value
        XCTAssertEqual(woke.get(), [1, 3, 5])
        XCTAssertEqual(clock.now(), start.addingTimeInterval(13))
    }

    func testAPastDeadlineReturnsAtOnce() async throws {
        let clock = ManualClock()
        try await clock.sleep(until: clock.now())
        try await clock.sleep(seconds: -5)
        XCTAssertEqual(clock.sleeperCount, 0)
    }

    func testCancellationWakesTheSleeperWithCancellationError() async {
        let clock = ManualClock()
        let task = Task { try await clock.sleep(seconds: 60) }
        let asleep = await clock.waitForSleepers(1)
        XCTAssertTrue(asleep)
        task.cancel()
        do {
            try await task.value
            XCTFail("should have thrown")
        } catch {
            XCTAssertTrue(error is CancellationError)
        }
        XCTAssertEqual(clock.sleeperCount, 0)
    }
}
