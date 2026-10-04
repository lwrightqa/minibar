import Foundation

/// The IDs the app sends (api.md 5.1) and receives (api.md 3, 4.3).
///
/// None of them identifies the Mac or the person: the install ID is a random
/// UUID made once per install, the session ID is random per launch.
public enum Identifiers {
    /// A new install ID for `client`: a random UUID, 36 characters of
    /// `A-F 0-9 -`. Made once and kept in the settings.
    public static func newInstallID() -> String {
        UUID().uuidString
    }

    /// A new session ID for `session`: 8 random characters of `A-Z a-z 0-9`.
    /// Made at every launch.
    public static func newSessionID() -> String {
        var generator = SystemRandomNumberGenerator()
        return newSessionID(using: &generator)
    }

    /// A new session ID from the given random number generator (for tests).
    public static func newSessionID<G: RandomNumberGenerator>(using generator: inout G) -> String {
        let alphabet = Array("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")
        return String((0..<8).map { _ in alphabet[Int.random(in: 0..<alphabet.count, using: &generator)] })
    }

    /// `client`: 8 to 64 characters of `A-Z a-z 0-9 -` (api.md 5.1).
    public static func isValidClientID(_ value: String) -> Bool {
        (8...64).contains(value.utf8.count) && value.utf8.allSatisfy { isAlphanumeric($0) || $0 == 0x2D }
    }

    /// `session`: 1 to 16 characters of `A-Z a-z 0-9` (api.md 5.1).
    public static func isValidSessionID(_ value: String) -> Bool {
        (1...16).contains(value.utf8.count) && value.utf8.allSatisfy(isAlphanumeric)
    }

    /// `device_id`: 12 lowercase hex digits, the bar's Wi-Fi MAC address (api.md 3).
    public static func isValidDeviceID(_ value: String) -> Bool {
        value.utf8.count == 12 && value.utf8.allSatisfy { ($0 >= 0x30 && $0 <= 0x39) || ($0 >= 0x61 && $0 <= 0x66) }
    }

    /// A token as the bar issues it: `tb1_` and 43 characters of base64url,
    /// 47 in all (api.md 4.3). Only a sanity check before storing one.
    public static func looksLikeToken(_ value: String) -> Bool {
        guard value.hasPrefix("tb1_"), value.utf8.count == 47 else { return false }
        return value.utf8.dropFirst(4).allSatisfy { isAlphanumeric($0) || $0 == 0x2D || $0 == 0x5F }
    }

    private static func isAlphanumeric(_ byte: UInt8) -> Bool {
        (byte >= 0x30 && byte <= 0x39) || (byte >= 0x41 && byte <= 0x5A) || (byte >= 0x61 && byte <= 0x7A)
    }
}
