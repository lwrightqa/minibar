import Foundation
import XCTest
@testable import TinyBarCore

/// A clock that behaves like `SystemClock` when the Mac's time is changed:
/// `now()` and `sleep(until:)` run on monotonic time, which only `advance(by:)`
/// moves, while `jump(by:)` changes the wall clock (`wallClockOffset()`)
/// either way, as setting the Mac's clock or a large NTP step does.
///
/// (QA first wrote this to model the old `SystemClock`, whose `now()` was the
/// wall clock; with `now()` monotonic, the jump only moves the offset.)
final class JumpClock: TinyClock, @unchecked Sendable {
    private struct Sleeper {
        let id: UUID
        let wakeAt: Double
        let continuation: CheckedContinuation<Void, any Error>
    }
    private struct State {
        var monotonic: Double = 0
        var offset: TimeInterval = 0
        var sleepers: [Sleeper] = []
    }
    private let base: Date
    private let state = Locked(State())

    init(start: Date = ManualClock.apiExampleStart) { base = start }

    func now() -> Date {
        state.withLock { base.addingTimeInterval($0.monotonic) }
    }

    func wallClockOffset() -> TimeInterval {
        state.withLock { $0.offset }
    }

    var monotonic: Double { state.withLock { $0.monotonic } }

    func sleep(until deadline: Date) async throws {
        let id = UUID()
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, any Error>) in
                let resume: Bool? = state.withLock { s in
                    if Task.isCancelled { return false }
                    let wakeAt = deadline.timeIntervalSince(base)
                    if wakeAt <= s.monotonic { return true }
                    s.sleepers.append(Sleeper(id: id, wakeAt: wakeAt, continuation: continuation))
                    return nil
                }
                switch resume {
                case true?: continuation.resume()
                case false?: continuation.resume(throwing: CancellationError())
                case nil: break
                }
            }
        } onCancel: {
            let c = state.withLock { s -> CheckedContinuation<Void, any Error>? in
                guard let i = s.sleepers.firstIndex(where: { $0.id == id }) else { return nil }
                return s.sleepers.remove(at: i).continuation
            }
            c?.resume(throwing: CancellationError())
        }
    }

    /// Real (monotonic) time passes.
    func advance(by seconds: Double) {
        let due = state.withLock { s -> [Sleeper] in
            s.monotonic += seconds
            let m = s.monotonic
            let due = s.sleepers.filter { $0.wakeAt <= m + 1e-9 }
            s.sleepers.removeAll { $0.wakeAt <= m + 1e-9 }
            return due
        }
        due.forEach { $0.continuation.resume() }
    }

    /// The Mac's clock is changed: wall time moves, `now()` and sleepers don't.
    func jump(by seconds: TimeInterval) {
        state.withLock { $0.offset += seconds }
    }
}

/// Waits until a connection's loop is idle (as `ConnectionRig.settle`).
func settle(_ connection: BarConnection, file: StaticString = #filePath, line: UInt = #line) async {
    let giveUp = Date().addingTimeInterval(4)
    while Date() < giveUp {
        if await connection.isSettled {
            try? await Task.sleep(nanoseconds: 3_000_000)
            if await connection.isSettled { return }
        }
        try? await Task.sleep(nanoseconds: 1_000_000)
    }
    XCTFail("the connection didn't settle", file: file, line: line)
}

/// A MiniBar on the controller side of a pseudo-terminal that answers by
/// itself on a background thread, like the firmware: `hello`, `call`, `pair`
/// and `status`. It can add firmware log noise around its replies, go quiet,
/// or vanish (the port disappearing).
final class PTYBarResponder: @unchecked Sendable {
    struct Options {
        /// Write log lines (colored, CR LF) before and after every reply.
        var logNoise = false
        /// Write each reply in two pieces with a pause between them.
        var splitReplies = false
        /// Don't answer anything.
        var muted = false
        /// Before each reply, write a log line WITHOUT a line ending (breaks the
        /// firmware's promise that protocol lines start at the start of a line).
        var unterminatedLogBeforeReply = false
        var auth = "bearer"
        var wifi = "connected"
        var deviceID = "f412fa3f2a1c"
    }

    let pty: PseudoTerminal
    private let state: Locked<(options: Options, received: [String], stopped: Bool, issued: Int)>
    private var buffer = Data()

    init(options: Options = Options()) throws {
        pty = try PseudoTerminal()
        state = Locked((options, [], false, 0))
        let thread = Thread { [self] in loop() }
        thread.start()
    }

    func device(registryID: UInt64 = 7) -> SerialDevice {
        SerialDevice(calloutPath: pty.devicePath, vendorID: 0x303A, productID: 0x1001, registryID: registryID)
    }

    var received: [String] { state.withLock { $0.received } }

    func set(_ change: (inout Options) -> Void) { state.withLock { change(&$0.options) } }

    /// The bar goes away: the port disappears.
    func vanish() {
        state.withLock { $0.stopped = true }
        pty.close()
    }

    private func loop() {
        while !state.withLock({ $0.stopped }) {
            guard let data = try? pty.read(timeout: 0.02) else { return }
            buffer.append(data)
            while let newline = buffer.firstIndex(of: 0x0A) {
                let line = String(decoding: buffer[buffer.startIndex..<newline], as: UTF8.self)
                buffer.removeSubrange(buffer.startIndex...newline)
                handle(line)
            }
        }
    }

    private func write(_ text: String) {
        try? pty.write(text)
    }

    private func handle(_ line: String) {
        let options: Options = state.withLock { s in
            s.received.append(line)
            return s.options
        }
        guard !options.muted, line.hasPrefix("@tb "),
              let command = try? JSONValue.parse(String(line.dropFirst(4))) else { return }
        let id = command["id"] ?? .null
        var body: String
        switch command["cmd"]?.stringValue {
        case "hello":
            body = #"{"ok": true, "device": "MiniBar", "device_id": "\#(options.deviceID)", "name": "MiniBar 2A1C", "fw": "1.0.0", "api": "1.0", "host": "minibar.local", "auth": "\#(options.auth)", "pairing": "idle", "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:11:58-07:00", "time_source": "ntp", "wifi": "\#(options.wifi)"}"#
        case "call":
            let active = command["active"]?.boolValue ?? false
            let app = command["app"]?.stringValue.map { "\"\($0)\"" } ?? "null"
            body = #"{"ok": true, "device_id": "\#(options.deviceID)", "showing": "\#(active ? "call" : "own")", "screen": "on", "stale": false, "call": {"active": \#(active), "app": \#(active ? app : "null"), "inputs": null, "via": \#(active ? "\"usb\"" : "null"), "since": null, "aside": false}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:11:58-07:00"}"#
        case "pair":
            let n = state.withLock { s -> Int in s.issued += 1; return s.issued }
            let token = "tb1_" + String(repeating: "Q", count: 42) + String(n % 10)
            body = #"{"ok": true, "token": "\#(token)", "token_id": "0000000\#(n % 10)", "scope": "call", "device_id": "\#(options.deviceID)", "name": "MiniBar 2A1C", "host": "minibar.local"}"#
        default:
            body = #"{"ok": false, "error": "unknown_cmd", "message": "Unknown.", "field": "cmd"}"#
        }
        var object = (try? JSONValue.parse(body).objectValue) ?? [:]
        object["id"] = id
        let reply = "@tb " + JSONValue.object(object).serialized(leadingKeys: ["id"]) + "\r\n"
        if options.logNoise {
            write("\u{1B}[0;32mI (24312) wifi: connected to Office-WiFi, ip 10.0.4.42\u{1B}[0m\r\n")
            write("ESP-ROM:esp32s3-20210327 \u{00}\u{01}\u{FF}garbage\r\n")
            write("W (24890) proto: a log line mentioning @tb {\"id\": 99} in the middle\r\n")
        }
        if options.unterminatedLogBeforeReply {
            write("I (100) busy: no newline here")
        }
        if options.splitReplies {
            let cut = reply.index(reply.startIndex, offsetBy: reply.count / 2)
            write(String(reply[..<cut]))
            Thread.sleep(forTimeInterval: 0.01)
            write(String(reply[cut...]))
        } else {
            write(reply)
        }
        if options.logNoise {
            write("\u{1B}[0;33mW (24891) cal: sync took 4.2 s\u{1B}[0m\r\n")
        }
    }
}

/// Real USB (SerialPort + USBTransport) on pseudo-terminals, fake Wi-Fi.
final class PTYTransportFactory: TransportFactory, @unchecked Sendable {
    let clock: any TinyClock
    let wifi: FakeTransportFactory
    private let opened = Locked<[USBTransport]>([])

    init(clock: any TinyClock, wifi: FakeTransportFactory) {
        self.clock = clock
        self.wifi = wifi
    }

    func openUSB(_ device: SerialDevice) throws -> any USBLinkTransport {
        let port = try SerialPort(path: device.calloutPath)
        let transport = USBTransport(port: port, device: device, clock: clock)
        opened.withLock { $0.append(transport) }
        return transport
    }

    func makeWiFi(endpoint: BarEndpoint, token: String?) -> any WiFiLinkTransport {
        wifi.makeWiFi(endpoint: endpoint, token: token)
    }
}
