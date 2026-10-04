import Foundation

/// RFC 3339 times as docs/api.md 2.3 uses them: to the second, with the UTC
/// offset, for example `2026-10-04T14:12:00-07:00`.
///
/// Written with plain arithmetic rather than `DateFormatter` or
/// `ISO8601FormatStyle`, so the output is the same on macOS and Linux.
public enum RFC3339 {
    /// Formats `date` (rounded down to the second) in `timeZone`, with its
    /// offset: `2026-10-04T14:12:00-07:00`. UTC is written `+00:00`.
    public static func string(from date: Date, timeZone: TimeZone) -> String {
        let seconds = Int64(date.timeIntervalSince1970.rounded(.down))
        let offset = Int64(timeZone.secondsFromGMT(for: Date(timeIntervalSince1970: TimeInterval(seconds))))
        let local = seconds + offset
        let days = floorDiv(local, 86_400)
        let secondOfDay = local - days * 86_400
        let (year, month, day) = civil(fromDays: days)
        let sign = offset < 0 ? "-" : "+"
        let offsetMinutes = abs(offset) / 60
        return "\(pad(year, 4))-\(pad(month))-\(pad(day))T"
            + "\(pad(secondOfDay / 3600)):\(pad(secondOfDay % 3600 / 60)):\(pad(secondOfDay % 60))"
            + "\(sign)\(pad(offsetMinutes / 60)):\(pad(offsetMinutes % 60))"
    }

    /// Parses `YYYY-MM-DDTHH:MM:SS[.fraction](Z|±HH:MM)`. The `T` may also be
    /// `t` or a space, and `Z` may be `z`. Fractions of a second are accepted
    /// and dropped. Returns `nil` for anything else.
    public static func date(from string: String) -> Date? {
        let b = Array(string.utf8)
        guard b.count >= 20 else { return nil }
        func number(_ start: Int, _ length: Int) -> Int64? {
            guard start + length <= b.count else { return nil }
            var value: Int64 = 0
            for i in start..<(start + length) {
                let c = b[i]
                guard c >= 0x30, c <= 0x39 else { return nil }
                value = value * 10 + Int64(c - 0x30)
            }
            return value
        }
        guard let year = number(0, 4), b[4] == 0x2D,
              let month = number(5, 2), b[7] == 0x2D,
              let day = number(8, 2),
              b[10] == 0x54 || b[10] == 0x74 || b[10] == 0x20,
              let hour = number(11, 2), b[13] == 0x3A,
              let minute = number(14, 2), b[16] == 0x3A,
              let second = number(17, 2)
        else { return nil }
        guard (1...12).contains(month), (1...31).contains(day), hour <= 23, minute <= 59, second <= 60 else {
            return nil
        }
        var i = 19
        if i < b.count, b[i] == 0x2E {  // "."
            i += 1
            let digitsStart = i
            while i < b.count, b[i] >= 0x30, b[i] <= 0x39 { i += 1 }
            guard i > digitsStart else { return nil }
        }
        guard i < b.count else { return nil }
        var offset: Int64 = 0
        switch b[i] {
        case 0x5A, 0x7A:  // "Z", "z"
            guard i + 1 == b.count else { return nil }
        case 0x2B, 0x2D:  // "+", "-"
            guard i + 6 == b.count, let oh = number(i + 1, 2), b[i + 3] == 0x3A, let om = number(i + 4, 2),
                  oh <= 23, om <= 59
            else { return nil }
            offset = (oh * 3600 + om * 60) * (b[i] == 0x2D ? -1 : 1)
        default:
            return nil
        }
        let days = days(fromCivil: year, month, day)
        let seconds = days * 86_400 + hour * 3600 + minute * 60 + min(second, 59) - offset
        return Date(timeIntervalSince1970: TimeInterval(seconds))
    }

    // MARK: - Calendar arithmetic (Howard Hinnant's civil-days algorithms)

    private static func floorDiv(_ a: Int64, _ b: Int64) -> Int64 {
        a >= 0 ? a / b : -((-a + b - 1) / b)
    }

    private static func days(fromCivil y0: Int64, _ m: Int64, _ d: Int64) -> Int64 {
        let y = m <= 2 ? y0 - 1 : y0
        let era = floorDiv(y, 400)
        let yoe = y - era * 400
        let mp = (m + 9) % 12
        let doy = (153 * mp + 2) / 5 + d - 1
        let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy
        return era * 146_097 + doe - 719_468
    }

    private static func civil(fromDays z0: Int64) -> (Int64, Int64, Int64) {
        let z = z0 + 719_468
        let era = floorDiv(z, 146_097)
        let doe = z - era * 146_097
        let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146_096) / 365
        let doy = doe - (365 * yoe + yoe / 4 - yoe / 100)
        let mp = (5 * doy + 2) / 153
        let d = doy - (153 * mp + 2) / 5 + 1
        let m = mp < 10 ? mp + 3 : mp - 9
        return (yoe + era * 400 + (m <= 2 ? 1 : 0), m, d)
    }

    private static func pad(_ value: Int64, _ width: Int = 2) -> String {
        let digits = String(value)
        return digits.count >= width ? digits : String(repeating: "0", count: width - digits.count) + digits
    }
}
