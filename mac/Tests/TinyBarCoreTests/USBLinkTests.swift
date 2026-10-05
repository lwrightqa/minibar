import Foundation
import XCTest
@testable import TinyBarCore
#if canImport(Glibc)
import Glibc
#elseif canImport(Darwin)
import Darwin
#endif

/// `SerialPort`, `PseudoTerminal` and `USBTransport` against a fake bar on a
/// pseudo-terminal (criterion 14, api.md 6).
final class USBLinkTests: XCTestCase {
    let client = FakeBarPTY.client

    private func open(_ bar: FakeBarPTY, clock: ManualClock = ManualClock()) throws -> USBTransport {
        let port = try SerialPort(path: bar.devicePath)
        return USBTransport(port: port, device: bar.device(), clock: clock)
    }

    static let helloRequest = HelloRequest(client: FakeBarPTY.client, appVersion: "1.0 (12)")

    // MARK: - SerialPort

    func test_serialPortOnAPseudoTerminal() async throws {
        let bar = try FakeBarPTY()
        let port = try SerialPort(path: bar.devicePath)
        XCTAssertTrue(port.isOpen)

        // api.md 6.3: raw, 8N1, no flow control, CLOCAL and CREAD, HUPCL off.
        let settings = try bar.pty.deviceSettings()
        XCTAssertEqual(settings.c_lflag & tcflag_t(ICANON | ECHO | ISIG), 0)
        XCTAssertEqual(settings.c_oflag & tcflag_t(OPOST), 0)
        XCTAssertEqual(settings.c_cflag & tcflag_t(CSIZE), tcflag_t(CS8))
        XCTAssertEqual(settings.c_cflag & tcflag_t(CLOCAL | CREAD), tcflag_t(CLOCAL | CREAD))
        XCTAssertEqual(settings.c_cflag & tcflag_t(HUPCL | PARENB | CSTOPB), 0)
        XCTAssertEqual(settings.c_cflag & Posix.hardwareFlowControl, 0)
        XCTAssertEqual(settings.c_iflag & tcflag_t(IXON | IXOFF), 0)
        withUnsafeBytes(of: settings.c_cc) { cc in
            XCTAssertEqual(cc[Int(VMIN)], 0)
            XCTAssertEqual(cc[Int(VTIME)], 0)
        }

        // Bytes go through unchanged both ways (no LF → CR LF).
        try port.write(Data("@tb {}\n".utf8), timeout: 1)
        let written = try await offPool { try bar.pty.read(until: 0x0A, timeout: 2) }
        XCTAssertEqual(String(decoding: written, as: UTF8.self), "@tb {}\n")
        try bar.pty.write("I (1) boot\r\n")
        let read = try port.read(timeout: 2)
        XCTAssertEqual(String(decoding: read, as: UTF8.self), "I (1) boot\r\n")

        // A read with nothing to read returns empty at its time-out.
        let start = Date()
        XCTAssertEqual(try port.read(timeout: 0.2), Data())
        XCTAssertGreaterThanOrEqual(Date().timeIntervalSince(start), 0.15)

        port.close()
        port.close()  // idempotent
        XCTAssertFalse(port.isOpen)
        XCTAssertThrowsError(try port.read(timeout: 0.1)) { XCTAssertEqual($0 as? SerialPortError, .closed) }
        XCTAssertThrowsError(try port.write(Data("x".utf8), timeout: 0.1)) { XCTAssertEqual($0 as? SerialPortError, .closed) }
        // The device is still there: it opens again (opening and closing doesn't need a replug).
        let again = try SerialPort(path: bar.devicePath)
        XCTAssertTrue(again.isOpen)
        again.close()
    }

    func test_serialPortCloseWakesABlockedRead() async throws {
        let bar = try FakeBarPTY()
        let port = try SerialPort(path: bar.devicePath)
        let start = Date()
        let reader = Task.detached { () -> SerialPortError? in
            do {
                _ = try port.read(timeout: 10)
                return nil
            } catch {
                return error as? SerialPortError
            }
        }
        try await Task.sleep(nanoseconds: 100_000_000)
        port.close()
        let error = await reader.value
        XCTAssertEqual(error, .closed)
        XCTAssertLessThan(Date().timeIntervalSince(start), 2, "close() wakes the read at once")
    }

    func test_serialPortReportsTheDeviceGoingAway() throws {
        let bar = try FakeBarPTY()
        let port = try SerialPort(path: bar.devicePath)
        bar.close()  // unplugged
        XCTAssertThrowsError(try port.read(timeout: 1)) { XCTAssertEqual($0 as? SerialPortError, .closed) }
    }

    func test_serialPortOpenErrors() throws {
        XCTAssertThrowsError(try SerialPort(path: "/dev/cu.no-such-device-\(UUID().uuidString)")) {
            XCTAssertEqual($0 as? SerialPortError, .openFailed(errno: ENOENT))
        }
    }

    func test_serialPortExclusiveOpen() throws {
        let bar = try FakeBarPTY()
        let port = try SerialPort(path: bar.devicePath)
        defer { port.close() }
        // Linux lets a process with CAP_SYS_ADMIN (root, in this container)
        // open a TIOCEXCL terminal anyway, so only check where it applies.
        try XCTSkipIf(geteuid() == 0, "running as root, which TIOCEXCL doesn't stop")
        XCTAssertThrowsError(try SerialPort(path: bar.devicePath)) { XCTAssertEqual($0 as? SerialPortError, .busy) }
        // Without exclusive access the same check passes, so it's TIOCEXCL that refuses.
        port.close()
        let shared = try SerialPort(path: bar.devicePath, options: .init(exclusive: false))
        XCTAssertNoThrow(try SerialPort(path: bar.devicePath, options: .init(exclusive: false)).close())
        shared.close()
    }

    // MARK: - Criterion 14: the handshake

    func test_C14_usbHandshakeOnAPseudoTerminal() async throws {
        let clock = ManualClock()
        let bar = try FakeBarPTY()
        try bar.log("ESP-ROM:esp32s3-20210327")   // boot output already waiting
        let usb = try open(bar, clock: clock)
        let handshake = Task { try await usb.handshake(USBLinkTests.helloRequest) }

        // The first thing the app sends is hello.
        let first = try await offPool { try bar.nextLine() }
        XCTAssertEqual(first, "@tb " + #"{"cmd":"hello","id":1,"api":"1.0","app_version":"1.0 (12)","client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"}"#)

        // Log output, a bad protocol line and an event before any reply are ignored.
        try bar.log("I (24312) wifi: connected to Office-WiFi, ip 10.0.4.42")
        try bar.pty.write("@tb {not json\r\n")
        try bar.send(#"{"event": "something_new"}"#)
        await clock.waitForSleepers()
        clock.advance(by: 2)
        let second = try await offPool { try bar.nextCommand() }
        XCTAssertEqual(second?["cmd"], .string("hello"))
        XCTAssertEqual(second?["id"], .int(2), "hello again after 2 s")

        // A late reply to the first hello still counts.
        try bar.reply(to: .int(1), FakeBarPTY.infoJSON)
        let info = try await handshake.value
        XCTAssertEqual(info.deviceID, "f412fa3f2a1c")
        XCTAssertEqual(info.name, "MiniBar 2A1C")
        let rest = try await offPool { try bar.drain() }
        XCTAssertEqual(rest, "", "nothing else was sent")
        await usb.close()
    }

    func test_C14_aPortThatDoesntAnswerGetsFiveHellosIn10Seconds() async throws {
        let clock = ManualClock()
        let bar = try FakeBarPTY()
        let usb = try open(bar, clock: clock)
        let start = clock.now()
        let handshake = Task { try await usb.handshake(USBLinkTests.helloRequest) }
        var sentAt: [TimeInterval] = []
        for _ in 0..<5 {
            let command = try await offPool { try bar.nextCommand() }
            XCTAssertEqual(command?["cmd"], .string("hello"))
            sentAt.append(clock.now().timeIntervalSince(start))
            try bar.log("not a MiniBar, just chatter")
            await clock.waitForSleepers()
            clock.advance(by: 2)
        }
        do {
            _ = try await handshake.value
            XCTFail("expected notATinyBar")
        } catch {
            XCTAssertEqual(error as? BarError, .notATinyBar)
        }
        XCTAssertEqual(sentAt, [0, 2, 4, 6, 8], "one hello every 2 seconds for 10 seconds")
        XCTAssertEqual(clock.now().timeIntervalSince(start), 10)

        // Closed, it's never written to again.
        await usb.close()
        do {
            _ = try await usb.sendCall(CallRequest(client: client, session: "q8Zr2Lx0", seq: 1, active: false))
            XCTFail("expected closed")
        } catch {
            XCTAssertEqual(error as? BarError, .closed)
        }
        let rest = try await offPool { try bar.drain() }
        XCTAssertEqual(rest, "")
    }

    func test_C14_handshakeAnswers() async throws {
        // Another device that speaks the protocol isn't a MiniBar.
        do {
            let bar = try FakeBarPTY()
            let usb = try open(bar)
            let handshake = Task { try await usb.handshake(USBLinkTests.helloRequest) }
            let command = try await offPool { try bar.nextCommand() }
            try bar.reply(to: command?["id"], FakeBarPTY.infoJSON.replacingOccurrences(of: #""device": "MiniBar""#, with: #""device": "OtherThing""#))
            await XCTAssertThrowsBarError(try await handshake.value, .notATinyBar)
            await usb.close()
        }
        // A bar still on firmware 1.0.2 says "TinyBar", the product's old
        // name; accepted for one release (api.md 14.6).
        do {
            let bar = try FakeBarPTY()
            let usb = try open(bar)
            let handshake = Task { try await usb.handshake(USBLinkTests.helloRequest) }
            let command = try await offPool { try bar.nextCommand() }
            try bar.reply(to: command?["id"], FakeBarPTY.infoJSON.replacingOccurrences(of: #""device": "MiniBar""#, with: #""device": "TinyBar""#))
            let info = try await handshake.value
            XCTAssertTrue(info.isKnownBar)
            XCTAssertEqual(info.device, TinyBarAPI.legacyDeviceName)
            await usb.close()
        }
        // A bar that speaks another major version refuses (api.md 6.6).
        do {
            let bar = try FakeBarPTY()
            let usb = try open(bar)
            let handshake = Task { try await usb.handshake(USBLinkTests.helloRequest) }
            let command = try await offPool { try bar.nextCommand() }
            try bar.reply(to: command?["id"], #"{"ok": false, "error": "unsupported_api", "message": "This MiniBar speaks API 2.0.", "field": "api"}"#)
            do {
                _ = try await handshake.value
                XCTFail("expected unsupported_api")
            } catch {
                XCTAssertEqual((error as? BarError)?.apiCode, .unsupportedAPI)
            }
            await usb.close()
        }
        // A bar that's still starting up answers busy: the app asks again.
        do {
            let clock = ManualClock()
            let bar = try FakeBarPTY()
            let usb = try open(bar, clock: clock)
            let handshake = Task { try await usb.handshake(USBLinkTests.helloRequest) }
            let first = try await offPool { try bar.nextCommand() }
            try bar.reply(to: first?["id"], #"{"ok": false, "error": "busy", "message": "Starting up.", "field": null}"#)
            await clock.waitForSleepers()
            clock.advance(by: 2)
            let second = try await offPool { try bar.nextCommand() }
            try bar.reply(to: second?["id"], FakeBarPTY.infoJSON)
            let info = try await handshake.value
            XCTAssertTrue(info.isKnownBar)
            await usb.close()
        }
    }

    // MARK: - Requests and replies

    func test_requestsMatchRepliesByID() async throws {
        let clock = ManualClock()
        let bar = try FakeBarPTY()
        let usb = try open(bar, clock: clock)
        let request = CallRequest(client: client, session: "q8Zr2Lx0", seq: 1, active: false)
        let call = Task { try await usb.sendCall(request) }
        let line = try await offPool { try bar.nextLine() }
        // api.md 6.6's call example, with the keys in the app's order.
        XCTAssertEqual(line, "@tb " + #"{"cmd":"call","id":1,"active":false,"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","seq":1,"session":"q8Zr2Lx0"}"#)

        try bar.reply(to: .int(77), #"{"ok": true, "device_id": "000000000000", "showing": "call", "screen": "on", "stale": false, "call": {"active": false, "app": null, "inputs": null, "via": null, "since": null, "aside": false}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 30, "timeout_s": 90, "time": null}"#)
        try bar.log("W (24890) cal: sync took 4.2 s")
        try bar.reply(to: .int(1), FakeBarPTY.callReplyJSON)
        let reply = try await call.value
        XCTAssertEqual(reply.deviceID, "f412fa3f2a1c", "the reply with id 1, not the one with id 77")
        XCTAssertEqual(reply.showing, .own)
        await usb.close()
    }

    func test_aReplyWithANullIDAnswersTheOldestRequest() async throws {
        let bar = try FakeBarPTY()
        let usb = try open(bar)
        let status = Task { try await usb.status() }
        let first = try await offPool { try bar.nextCommand() }
        XCTAssertEqual(first, .object(["cmd": .string("status"), "id": .int(1)]))
        let call = Task { try await usb.sendCall(CallRequest(client: FakeBarPTY.client, session: "q8Zr2Lx0", seq: 2, active: false)) }
        let second = try await offPool { try bar.nextCommand() }
        XCTAssertEqual(second?["id"], .int(2))

        try bar.send(#"{"id": null, "ok": false, "error": "bad_json", "message": "That line isn't a JSON object.", "field": null}"#)
        try bar.reply(to: .int(2), FakeBarPTY.callReplyJSON)
        await XCTAssertThrowsBarError(try await status.value, .api(
            APIErrorBody(error: .badJSON, message: "That line isn't a JSON object.", field: nil), httpStatus: nil))
        let callReply = try await call.value
        XCTAssertEqual(callReply.deviceID, "f412fa3f2a1c")
        await usb.close()
    }

    func test_aMissingReplyTimesOutAfter3Seconds() async throws {
        let clock = ManualClock()
        let bar = try FakeBarPTY()
        let usb = try open(bar, clock: clock)
        let call = Task { try await usb.sendCall(CallRequest(client: FakeBarPTY.client, session: "q8Zr2Lx0", seq: 1, active: false)) }
        _ = try await offPool { try bar.nextCommand() }
        await clock.waitForSleepers()
        XCTAssertEqual(clock.nextDeadline, clock.now().addingTimeInterval(3))
        clock.advance(by: 3)
        await XCTAssertThrowsBarError(try await call.value, .timedOut)

        // The late reply is dropped; the link still works.
        try bar.reply(to: .int(1), FakeBarPTY.callReplyJSON)
        let next = Task { try await usb.sendCall(CallRequest(client: FakeBarPTY.client, session: "q8Zr2Lx0", seq: 2, active: false)) }
        let command = try await offPool { try bar.nextCommand() }
        XCTAssertEqual(command?["seq"], .int(2))
        try bar.reply(to: command?["id"], FakeBarPTY.callReplyJSON)
        let nextReply = try await next.value
        XCTAssertEqual(nextReply.showing, .own)
        await usb.close()
    }

    func test_statusPairAndRequestReplies() async throws {
        let bar = try FakeBarPTY()
        let usb = try open(bar)

        let pair = Task { try await usb.pair(USBPairRequest(client: FakeBarPTY.client)) }
        let pairLine = try await offPool { try bar.nextLine() }
        XCTAssertEqual(pairLine, "@tb " + #"{"cmd":"pair","id":1,"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"}"#)
        try bar.send(#"{"id": 1, "ok": true, "token": "tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4", "token_id": "74d8a526", "scope": "call", "device_id": "f412fa3f2a1c", "name": "MiniBar 2A1C", "host": "minibar.local"}"#)
        let paired = try await pair.value
        XCTAssertEqual(paired.token, "tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4")
        XCTAssertEqual(paired.tokenID, "74d8a526")

        let refused = Task { try await usb.pair(USBPairRequest(client: FakeBarPTY.client)) }
        let second = try await offPool { try bar.nextCommand() }
        try bar.reply(to: second?["id"], #"{"ok": false, "error": "token_limit", "message": "MiniBar already has 10 paired devices. Remove one on the Remote.", "field": null}"#)
        do {
            _ = try await refused.value
            XCTFail("expected token_limit")
        } catch {
            XCTAssertEqual((error as? BarError)?.apiCode, .tokenLimit)
        }

        let status = Task { try await usb.status() }
        let third = try await offPool { try bar.nextCommand() }
        try bar.reply(to: third?["id"], Self.statusJSON)
        let reply = try await status.value
        XCTAssertEqual(reply.rev, 1843)
        XCTAssertEqual(reply.macs.first?.via, .usb)

        let request = Task { try await usb.request(USBRequestCommand(method: .delete, path: "/api/v1/clients/0000")) }
        let fourth = try await offPool { try bar.nextCommand() }
        XCTAssertEqual(fourth?["method"], .string("DELETE"))
        try bar.reply(to: fourth?["id"], #"{"http_status": 404, "ok": false, "error": "not_found", "message": "No such device.", "field": null}"#)
        await XCTAssertThrowsBarError(try await request.value, .api(
            APIErrorBody(error: .notFound, message: "No such device.", field: nil), httpStatus: 404))
        await usb.close()
    }

    // MARK: - Events and the port going away

    func test_readyEventsAndLogLines() async throws {
        let bar = try FakeBarPTY()
        let usb = try open(bar)
        var events = usb.events.makeAsyncIterator()
        try bar.log("I (312) main: MiniBar 1.0.0")
        try bar.send(#"{"event": "ready", "device_id": "f412fa3f2a1c", "api": "1.0", "fw": "1.0.0"}"#)
        try bar.send(#"{"event": "later_event", "x": 1}"#)  // unknown events are ignored
        let first = await events.next()
        guard case .log(let text) = first else { return XCTFail("expected a log line, got \(String(describing: first))") }
        XCTAssertTrue(text.hasPrefix("I (312) main: MiniBar 1.0.0"))
        let second = await events.next()
        XCTAssertEqual(second, .ready(ReadyEvent(deviceID: "f412fa3f2a1c", fw: "1.0.0")))

        // Unplugged: waiting requests fail with closed, and the stream ends.
        let call = Task { try await usb.sendCall(CallRequest(client: FakeBarPTY.client, session: "q8Zr2Lx0", seq: 1, active: false)) }
        _ = try await offPool { try bar.nextCommand() }
        bar.close()
        await XCTAssertThrowsBarError(try await call.value, .closed)
        let closed = await events.next()
        XCTAssertEqual(closed, .closed)
        let end = await events.next()
        XCTAssertNil(end)
    }

    func test_closeFailsWaitingRequests() async throws {
        let bar = try FakeBarPTY()
        let usb = try open(bar)
        let call = Task { try await usb.sendCall(CallRequest(client: FakeBarPTY.client, session: "q8Zr2Lx0", seq: 1, active: false)) }
        _ = try await offPool { try bar.nextCommand() }
        await usb.close()
        await XCTAssertThrowsBarError(try await call.value, .closed)
        XCTAssertEqual(usb.endpointDescription, bar.devicePath.split(separator: "/").last.map(String.init))
    }

    static let statusJSON = #"{"ok": true, "device_id": "f412fa3f2a1c", "rev": 1843, "time": "2026-10-04T14:24:05-07:00", "time_source": "ntp", "showing": "own", "screen": "on", "own": {"status": "busy", "since": "2026-10-04T14:01:00-07:00", "previous": "busy"}, "message": {"text": null, "set_at": null}, "away": {"back_at": null, "note": null}, "call": {"active": false, "app": null, "inputs": null, "via": null, "since": null, "aside": false}, "meeting": {"active": false, "aside": false, "current": null, "next": null, "left_today": null}, "pomodoro": {"state": "ready", "phase": "focus", "round": 1, "rounds": 4, "length_s": 1500, "remaining_s": 1500, "ends_at": null, "paused_by": null, "ringing": false, "done_today": 0, "focused_today_s": 0}, "sources": {"calendar": false, "mac": true}, "macs": [{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "name": "Mac", "via": "usb", "connected": true, "active": false, "last_heard": "2026-10-04T14:24:01-07:00"}], "calendar": {"saved": false, "last_sync": null, "syncing": false, "error": null}, "wifi": {"state": "connected", "ssid": "Office-WiFi", "ip": "10.0.4.42", "host": "minibar.local", "rssi": -61}}"#
}

/// Asserts that an async expression throws exactly `expected`.
func XCTAssertThrowsBarError<T>(
    _ expression: @autoclosure () async throws -> T,
    _ expected: BarError,
    file: StaticString = #filePath,
    line: UInt = #line
) async {
    do {
        _ = try await expression()
        XCTFail("expected \(expected)", file: file, line: line)
    } catch {
        XCTAssertEqual(error as? BarError, expected, file: file, line: line)
    }
}
