import Foundation

/// Lines on the USB serial port (api.md 6.4).
///
/// The port carries the firmware's log output as well as protocol messages.
/// Every protocol line, both ways, is `@tb ` + one JSON object + LF. Anything
/// else is log output, which the app ignores (or shows in a debug view).
public enum USBFraming {
    /// The 4-character marker that starts every protocol line: at sign, t, b, space.
    public static let marker = "@tb "

    private static let markerBytes: [UInt8] = Array(marker.utf8)

    /// One command line: `@tb {"cmd":"call","id":2,…}` and LF. `cmd` and `id`
    /// come first, then the body's fields in byte order.
    ///
    /// Throws `BarError.tooLarge` if the line (marker included, line ending
    /// not) would be over 2,048 bytes (api.md 2.5).
    public static func commandLine<Body: Encodable>(_ cmd: USBCommandName, id: Int?, body: Body) throws(BarError) -> Data {
        let value: JSONValue
        do {
            value = try JSONValue.from(body)
        } catch {
            throw .malformedReply("Couldn't encode the \(cmd) command: \(error)")
        }
        guard case .object(var object) = value else {
            throw .malformedReply("A USB command body must be a JSON object")
        }
        object["cmd"] = .string(cmd.rawValue)
        if let id { object["id"] = .int(Int64(id)) }
        let text = marker + JSONValue.object(object).serialized(leadingKeys: ["cmd", "id"])
        guard text.utf8.count <= TinyBarAPI.Limits.usbLineOutBytes else { throw .tooLarge }
        return Data((text + "\n").utf8)
    }

    /// A command line with no fields besides `cmd` and `id` (`status`).
    public static func commandLine(_ cmd: USBCommandName, id: Int?) throws(BarError) -> Data {
        try commandLine(cmd, id: id, body: EmptyBody())
    }

    /// Classifies one line as read from the port, without its LF. Drops a
    /// trailing CR and any ANSI color codes at the start of the line, then
    /// checks for the marker (api.md 6.4). On a protocol line, color codes
    /// after the JSON object are dropped too (more lenient than api.md asks,
    /// in case a log color is still being reset).
    public static func classify(_ line: Data) -> USBIncomingLine {
        var bytes = Array(line)
        if bytes.last == 0x0D { bytes.removeLast() }
        bytes = stripLeadingANSI(bytes)
        guard bytes.starts(with: markerBytes) else {
            return .log(String(decoding: bytes, as: UTF8.self))
        }
        let json = Data(stripTrailingANSI(Array(bytes.dropFirst(markerBytes.count))))
        guard (try? JSONValue.parse(json))?.objectValue != nil,
              let header = try? JSONDecoder().decode(USBLineHeader.self, from: json)
        else {
            return .invalid(String(decoding: bytes, as: UTF8.self))
        }
        if let event = header.event {
            return .event(name: event, json: json)
        }
        return .reply(id: header.id, json: json)
    }

    /// Removes ANSI escape sequences (`ESC [ … final-byte`) from the start of
    /// a line. ESP-IDF colors its log lines this way.
    static func stripLeadingANSI(_ bytes: [UInt8]) -> [UInt8] {
        var index = 0
        while index + 1 < bytes.count, bytes[index] == 0x1B, bytes[index + 1] == 0x5B {
            var end = index + 2
            while end < bytes.count, !(0x40...0x7E).contains(bytes[end]) { end += 1 }
            guard end < bytes.count else { return [] }
            index = end + 1
        }
        return Array(bytes[index...])
    }

    /// Removes ANSI escape sequences that follow the last `}` of a line.
    static func stripTrailingANSI(_ bytes: [UInt8]) -> [UInt8] {
        guard let close = bytes.lastIndex(of: 0x7D), close + 1 < bytes.count else { return bytes }
        var index = close + 1
        while index < bytes.count {
            guard index + 1 < bytes.count, bytes[index] == 0x1B, bytes[index + 1] == 0x5B else { return bytes }
            var end = index + 2
            while end < bytes.count, !(0x40...0x7E).contains(bytes[end]) { end += 1 }
            guard end < bytes.count else { return bytes }
            index = end + 1
        }
        return Array(bytes[...close])
    }
}

/// One line from the port, classified (api.md 6.4).
public enum USBIncomingLine: Hashable, Sendable {
    /// A reply to a command: the JSON object after the marker. `id` is `nil`
    /// for the reply to a line the bar couldn't parse, or if the command had none.
    case reply(id: Int?, json: Data)
    /// An event, such as `ready`, with its JSON object. *Open:* ignore unknown events.
    case event(name: String, json: Data)
    /// Log output: any line that doesn't start with the marker.
    case log(String)
    /// Started with the marker but isn't a JSON object. Dropped (api.md 6.4).
    case invalid(String)
}

/// Splits the bytes read from the port into lines at each LF.
///
/// A line longer than `maxLineBytes` is thrown away up to its LF (counted in
/// `droppedLines`), so a crash dump without line breaks can't grow the buffer.
public struct USBLineSplitter: Sendable {
    /// The longest line kept, in bytes without the LF.
    public let maxLineBytes: Int
    /// Lines thrown away for being too long.
    public private(set) var droppedLines = 0

    private var buffer: [UInt8] = []
    private var discarding = false

    /// - Parameter maxLineBytes: by default the bar's 8,192-byte limit
    ///   (api.md 2.5) plus room for the marker and a CR.
    public init(maxLineBytes: Int = TinyBarAPI.Limits.usbLineInBytes + 64) {
        self.maxLineBytes = maxLineBytes
    }

    /// Bytes of a line that hasn't ended yet.
    public var pendingByteCount: Int { buffer.count }

    /// Adds bytes read from the port and returns the lines they complete,
    /// without their LF (a CR before it is left for `USBFraming.classify`).
    public mutating func append(_ data: Data) -> [Data] {
        var lines: [Data] = []
        for byte in data {
            if byte == 0x0A {
                if discarding {
                    discarding = false
                } else {
                    lines.append(Data(buffer))
                }
                buffer.removeAll(keepingCapacity: true)
            } else if !discarding {
                buffer.append(byte)
                if buffer.count > maxLineBytes {
                    discarding = true
                    droppedLines += 1
                    buffer.removeAll(keepingCapacity: true)
                }
            }
        }
        return lines
    }

    /// Forgets a partial line, for example after the port was reopened.
    public mutating func reset() {
        buffer.removeAll()
        discarding = false
    }
}
