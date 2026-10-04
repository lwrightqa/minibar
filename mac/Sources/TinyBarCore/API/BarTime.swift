import Foundation

/// A time as the API carries it: an RFC 3339 string with the bar's UTC offset,
/// to the second (api.md 2.3), for example `"2026-10-04T14:12:00-07:00"`.
///
/// Decoding keeps the string as it came and never fails on its format, so a
/// time the bar formats oddly can't stop the app from reading a call reply.
/// `date` parses it when needed.
public struct BarTime: Codable, Hashable, Sendable, CustomStringConvertible {
    public let rawValue: String

    public init(rawValue: String) {
        self.rawValue = rawValue
    }

    /// Formats `date` with `timeZone`'s offset, as the app sends its clock in
    /// `hello` (api.md 6.6).
    public init(_ date: Date, timeZone: TimeZone) {
        rawValue = RFC3339.string(from: date, timeZone: timeZone)
    }

    /// The parsed time, or `nil` if the string isn't RFC 3339.
    public var date: Date? {
        RFC3339.date(from: rawValue)
    }

    public var description: String { rawValue }

    public init(from decoder: any Decoder) throws {
        rawValue = try decoder.singleValueContainer().decode(String.self)
    }

    public func encode(to encoder: any Encoder) throws {
        var container = encoder.singleValueContainer()
        try container.encode(rawValue)
    }
}
