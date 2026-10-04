import Foundation

/// Any JSON value.
///
/// Used for three things: the free-form `body` of the USB `request` command
/// (api.md 6.6), writing every outgoing message in one exact, platform-independent
/// form (`serialized`), and comparing messages with docs/api.md's examples in
/// the contract tests (`isEquivalent`).
public enum JSONValue: Sendable, Hashable {
    case null
    case bool(Bool)
    case int(Int64)
    case double(Double)
    case string(String)
    case array([JSONValue])
    case object([String: JSONValue])
}

extension JSONValue: Codable {
    public init(from decoder: any Decoder) throws {
        let container = try decoder.singleValueContainer()
        if container.decodeNil() {
            self = .null
        } else if let value = try? container.decode(Bool.self) {
            self = .bool(value)
        } else if let value = try? container.decode(Int64.self) {
            self = .int(value)
        } else if let value = try? container.decode(Double.self) {
            self = .double(value)
        } else if let value = try? container.decode(String.self) {
            self = .string(value)
        } else if let value = try? container.decode([JSONValue].self) {
            self = .array(value)
        } else if let value = try? container.decode([String: JSONValue].self) {
            self = .object(value)
        } else {
            throw DecodingError.dataCorruptedError(in: container, debugDescription: "Not a JSON value")
        }
    }

    public func encode(to encoder: any Encoder) throws {
        var container = encoder.singleValueContainer()
        switch self {
        case .null: try container.encodeNil()
        case .bool(let value): try container.encode(value)
        case .int(let value): try container.encode(value)
        case .double(let value): try container.encode(value)
        case .string(let value): try container.encode(value)
        case .array(let value): try container.encode(value)
        case .object(let value): try container.encode(value)
        }
    }
}

extension JSONValue {
    /// Parses UTF-8 JSON text.
    public static func parse(_ data: Data) throws -> JSONValue {
        try JSONDecoder().decode(JSONValue.self, from: data)
    }

    /// Parses UTF-8 JSON text.
    public static func parse(_ text: String) throws -> JSONValue {
        try parse(Data(text.utf8))
    }

    /// The JSON form of any `Encodable` value (through `JSONEncoder`, so
    /// optionals that are `nil` are left out, as synthesized `Codable` does).
    public static func from<T: Encodable>(_ value: T) throws -> JSONValue {
        try parse(JSONEncoder().encode(value))
    }

    /// Decodes this value as `T`.
    public func decode<T: Decodable>(_ type: T.Type) throws -> T {
        try JSONDecoder().decode(T.self, from: Data(serialized().utf8))
    }

    public subscript(key: String) -> JSONValue? {
        if case .object(let object) = self { return object[key] }
        return nil
    }

    public var objectValue: [String: JSONValue]? {
        if case .object(let object) = self { return object }
        return nil
    }

    public var stringValue: String? {
        if case .string(let value) = self { return value }
        return nil
    }

    public var boolValue: Bool? {
        if case .bool(let value) = self { return value }
        return nil
    }

    public var intValue: Int64? {
        switch self {
        case .int(let value): return value
        case .double(let value) where value.rounded() == value && abs(value) < 9.0e15: return Int64(value)
        default: return nil
        }
    }

    public var isNull: Bool {
        if case .null = self { return true }
        return false
    }

    // MARK: - Writing

    /// Compact JSON text, the same on every platform:
    /// - no whitespace;
    /// - object keys in byte order, except that at the top level the keys in
    ///   `leadingKeys` that are present come first, in that order (the USB
    ///   commands put `cmd` and `id` first so logs are easy to read);
    /// - `/` and non-ASCII characters are written as they are; `"`, `\` and
    ///   control characters are escaped.
    public func serialized(leadingKeys: [String] = []) -> String {
        var out = ""
        write(to: &out, leadingKeys: leadingKeys)
        return out
    }

    private func write(to out: inout String, leadingKeys: [String]) {
        switch self {
        case .null:
            out += "null"
        case .bool(let value):
            out += value ? "true" : "false"
        case .int(let value):
            out += String(value)
        case .double(let value):
            if value.rounded() == value, abs(value) < 9.0e15 {
                out += String(Int64(value))
            } else {
                out += "\(value)"
            }
        case .string(let value):
            JSONValue.writeString(value, to: &out)
        case .array(let values):
            out += "["
            for (index, value) in values.enumerated() {
                if index > 0 { out += "," }
                value.write(to: &out, leadingKeys: [])
            }
            out += "]"
        case .object(let object):
            let leading = leadingKeys.filter { object[$0] != nil }
            let rest = object.keys
                .filter { !leading.contains($0) }
                .sorted { $0.utf8.lexicographicallyPrecedes($1.utf8) }
            out += "{"
            for (index, key) in (leading + rest).enumerated() {
                if index > 0 { out += "," }
                JSONValue.writeString(key, to: &out)
                out += ":"
                object[key]!.write(to: &out, leadingKeys: [])
            }
            out += "}"
        }
    }

    private static func writeString(_ string: String, to out: inout String) {
        out += "\""
        for scalar in string.unicodeScalars {
            switch scalar {
            case "\"": out += "\\\""
            case "\\": out += "\\\\"
            case "\n": out += "\\n"
            case "\r": out += "\\r"
            case "\t": out += "\\t"
            case "\u{08}": out += "\\b"
            case "\u{0C}": out += "\\f"
            case _ where scalar.value < 0x20:
                let hex = String(scalar.value, radix: 16)
                out += "\\u" + String(repeating: "0", count: 4 - hex.count) + hex
            default:
                out.unicodeScalars.append(scalar)
            }
        }
        out += "\""
    }

    // MARK: - Comparing

    /// Equality for comparing messages: numbers compare by value (`1` equals
    /// `1.0`), and in objects a key whose value is `null` equals a missing key
    /// (api.md 2.3: sending `null` for an optional field means leaving it out).
    public func isEquivalent(to other: JSONValue) -> Bool {
        switch (self, other) {
        case (.null, .null):
            return true
        case (.bool(let a), .bool(let b)):
            return a == b
        case (.string(let a), .string(let b)):
            return a == b
        case (.int, _), (.double, _):
            guard let a = numberValue, let b = other.numberValue else { return false }
            return a == b
        case (.array(let a), .array(let b)):
            return a.count == b.count && zip(a, b).allSatisfy { $0.isEquivalent(to: $1) }
        case (.object(let a), .object(let b)):
            let keys = Set(a.keys).union(b.keys)
            return keys.allSatisfy { key in
                (a[key] ?? .null).isEquivalent(to: b[key] ?? .null)
            }
        default:
            return false
        }
    }

    private var numberValue: Double? {
        switch self {
        case .int(let value): return Double(value)
        case .double(let value): return value
        default: return nil
        }
    }
}
