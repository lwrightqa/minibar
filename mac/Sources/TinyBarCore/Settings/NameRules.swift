import Foundation

/// Why a name was refused, with the message Settings shows (mac-app-ux.md 6.3, 6.4).
public enum NameProblem: Hashable, Sendable {
    case empty
    /// More than `limit` characters: "Use 24 characters or fewer."
    case tooLong(limit: Int)
    /// Characters the bar's fonts can't draw: "TinyBar can't show “é”."
    case unsupportedCharacters([Character])

    /// The help line under the field.
    public var message: String {
        unimplemented()
    }
}

/// Rules for the names the app sends.
public enum NameRules {
    /// Longest call-app short name: what the bar shows (api.md 5.2).
    public static let appNameLimit = TinyBarAPI.Limits.appNameShownCharacters
    /// Longest name for this Mac (api.md 4.6, 6.6).
    public static let macNameLimit = TinyBarAPI.Limits.clientNameCharacters.upperBound

    /// A call app's short name: 1 to 24 printable ASCII characters (the bar
    /// shows 24, api.md 5.2; its fonts have printable ASCII, api.md 2.3).
    /// Leading and trailing spaces are trimmed first.
    public static func appNameProblem(_ name: String) -> NameProblem? {
        problem(trimmed(name), limit: appNameLimit)
    }

    /// Name for this Mac: 1 to 32 characters (api.md 4.6, 6.6), printable
    /// ASCII. `nil` or empty means "send none", which is fine.
    public static func macNameProblem(_ name: String) -> NameProblem? {
        let name = trimmed(name)
        if name.isEmpty { return nil }
        return problem(name, limit: macNameLimit)
    }

    /// The name with leading and trailing white space removed.
    public static func trimmed(_ name: String) -> String {
        name.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    /// Characters the bar can draw: printable ASCII, space to tilde.
    public static func isPrintableASCII(_ character: Character) -> Bool {
        character.unicodeScalars.count == 1 && (0x20...0x7E).contains(character.unicodeScalars.first!.value)
    }

    private static func problem(_ name: String, limit: Int) -> NameProblem? {
        if name.isEmpty { return .empty }
        var unsupported: [Character] = []
        for character in name where !isPrintableASCII(character) && !unsupported.contains(character) {
            unsupported.append(character)
        }
        if !unsupported.isEmpty { return .unsupportedCharacters(unsupported) }
        if name.count > limit { return .tooLong(limit: limit) }
        return nil
    }
}
