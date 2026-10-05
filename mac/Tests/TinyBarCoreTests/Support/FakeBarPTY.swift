import Foundation
import XCTest
@testable import TinyBarCore

/// Runs blocking work (pseudo-terminal reads) on a GCD thread, so it never
/// ties up a thread of Swift's cooperative pool that the code under test needs.
func offPool<T: Sendable>(_ work: @escaping @Sendable () throws -> T) async throws -> T {
    try await withCheckedThrowingContinuation { continuation in
        DispatchQueue.global().async {
            continuation.resume(with: Result { try work() })
        }
    }
}

/// A MiniBar played by the test on the controller side of a pseudo-terminal
/// (criterion 14). Reads the app's lines and writes replies, log output and
/// events, byte for byte as the firmware would.
final class FakeBarPTY: @unchecked Sendable {
    let pty: PseudoTerminal
    private let buffer = Locked(Data())

    static let client = "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"
    static let infoJSON = #"{"ok": true, "device": "MiniBar", "device_id": "f412fa3f2a1c", "name": "MiniBar 2A1C", "fw": "1.0.0", "api": "1.0", "host": "minibar.local", "auth": "bearer", "pairing": "idle", "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:11:58-07:00", "time_source": "ntp", "wifi": "connected"}"#
    static let callReplyJSON = #"{"ok": true, "device_id": "f412fa3f2a1c", "showing": "own", "screen": "on", "stale": false, "call": {"active": false, "app": null, "inputs": null, "via": null, "since": null, "aside": false}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:11:58-07:00"}"#

    init() throws {
        pty = try PseudoTerminal()
    }

    var devicePath: String { pty.devicePath }

    func device(registryID: UInt64 = 1) -> SerialDevice {
        SerialDevice(calloutPath: pty.devicePath, vendorID: 0x303A, productID: 0x1001, registryID: registryID)
    }

    /// The next line the app wrote (without its LF), or `nil` after `timeout`.
    func nextLine(timeout: TimeInterval = 3) throws -> String? {
        let deadline = Date().addingTimeInterval(timeout)
        while true {
            let line: String? = buffer.withLock { data in
                guard let newline = data.firstIndex(of: 0x0A) else { return nil }
                let line = String(decoding: data[data.startIndex..<newline], as: UTF8.self)
                data.removeSubrange(data.startIndex...newline)
                return line
            }
            if let line { return line }
            if Date() >= deadline { return nil }
            let more = try pty.read(timeout: 0.05)
            buffer.withLock { $0.append(more) }
        }
    }

    /// The next protocol line from the app, parsed. Fails on anything else.
    func nextCommand(timeout: TimeInterval = 3, file: StaticString = #filePath, line: UInt = #line) throws -> JSONValue? {
        guard let text = try nextLine(timeout: timeout) else { return nil }
        guard text.hasPrefix("@tb ") else {
            XCTFail("not a protocol line: \(text)", file: file, line: line)
            return nil
        }
        return try JSONValue.parse(String(text.dropFirst(4)))
    }

    /// Everything the app wrote within `timeout`, for checking it wrote nothing.
    func drain(timeout: TimeInterval = 0.3) throws -> String {
        var all = buffer.withLock { data -> Data in defer { data.removeAll() }; return data }
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            all += try pty.read(timeout: 0.05)
        }
        return String(decoding: all, as: UTF8.self)
    }

    /// Writes a protocol line, CR LF ended as ESP-IDF's console does.
    func send(_ json: String) throws {
        try pty.write("@tb " + json + "\r\n")
    }

    /// Writes a reply to the command with `id`: the JSON object plus `"id"`.
    func reply(to id: JSONValue?, _ json: String) throws {
        var object = try JSONValue.parse(json).objectValue ?? [:]
        object["id"] = id ?? .null
        try send(JSONValue.object(object).serialized(leadingKeys: ["id"]))
    }

    /// Writes ESP-IDF log output, colored as it is by default.
    func log(_ text: String) throws {
        try pty.write("\u{1B}[0;32m" + text + "\u{1B}[0m\r\n")
    }

    func close() {
        pty.close()
    }
}
