import Foundation

/// An error code from api.md Appendix B. *Open:* later minor versions may add
/// codes; anything unknown is handled by its HTTP status.
public struct APIErrorCode: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }

    // 400
    public static let badJSON = APIErrorCode(rawValue: "bad_json")
    public static let badRequest = APIErrorCode(rawValue: "bad_request")
    public static let badValue = APIErrorCode(rawValue: "bad_value")
    public static let unsupportedChars = APIErrorCode(rawValue: "unsupported_chars")
    public static let notAURL = APIErrorCode(rawValue: "not_a_url")
    public static let httpNotAllowed = APIErrorCode(rawValue: "http_not_allowed")
    public static let publicAddress = APIErrorCode(rawValue: "public_address")
    public static let notICS = APIErrorCode(rawValue: "not_ics")
    // 401, 403
    /// No token, or an unknown or revoked one. Pair again (api.md 4.5, 16).
    public static let unauthorized = APIErrorCode(rawValue: "unauthorized")
    public static let wrongScope = APIErrorCode(rawValue: "wrong_scope")
    public static let badOrigin = APIErrorCode(rawValue: "bad_origin")
    /// Comes with `attempts_left`.
    public static let wrongCode = APIErrorCode(rawValue: "wrong_code")
    /// A call's `client` isn't one this token may report for (api.md 5.2). For
    /// the Mac app, whose token is tied to its install ID, it means the token
    /// isn't this Mac's: pair again.
    public static let wrongClient = APIErrorCode(rawValue: "wrong_client")
    // 404, 405
    public static let notFound = APIErrorCode(rawValue: "not_found")
    public static let methodNotAllowed = APIErrorCode(rawValue: "method_not_allowed")
    // 409
    /// Comes with `retry_after_s`.
    public static let pairingBusy = APIErrorCode(rawValue: "pairing_busy")
    public static let notPairing = APIErrorCode(rawValue: "not_pairing")
    public static let tokenLimit = APIErrorCode(rawValue: "token_limit")
    public static let inSetup = APIErrorCode(rawValue: "in_setup")
    public static let noMessage = APIErrorCode(rawValue: "no_message")
    public static let nothingToSetAside = APIErrorCode(rawValue: "nothing_to_set_aside")
    public static let nothingSetAside = APIErrorCode(rawValue: "nothing_set_aside")
    public static let notRunning = APIErrorCode(rawValue: "not_running")
    public static let nothingToExtend = APIErrorCode(rawValue: "nothing_to_extend")
    public static let noCalendar = APIErrorCode(rawValue: "no_calendar")
    // 413, 415, 421, 429
    public static let tooLarge = APIErrorCode(rawValue: "too_large")
    public static let unsupportedMediaType = APIErrorCode(rawValue: "unsupported_media_type")
    public static let wrongHost = APIErrorCode(rawValue: "wrong_host")
    /// Comes with `retry_after_s`.
    public static let rateLimited = APIErrorCode(rawValue: "rate_limited")
    // 500, 503
    public static let internalError = APIErrorCode(rawValue: "internal")
    public static let offline = APIErrorCode(rawValue: "offline")
    /// The bar is starting up or saving; retry after a second.
    public static let busy = APIErrorCode(rawValue: "busy")
    // USB only
    public static let unknownCmd = APIErrorCode(rawValue: "unknown_cmd")
    /// The bar speaks a different major version (api.md 6.6).
    public static let unsupportedAPI = APIErrorCode(rawValue: "unsupported_api")
    // Calendar check results (never an HTTP status)
    public static let calendarRejected = APIErrorCode(rawValue: "calendar_rejected")
    public static let calendarUnreachable = APIErrorCode(rawValue: "calendar_unreachable")
    public static let notACalendar = APIErrorCode(rawValue: "not_a_calendar")
}

/// The body of an error reply (api.md 2.4):
/// `{"ok": false, "error": "bad_value", "message": "…", "field": "…"}`, plus
/// `retry_after_s`, `attempts_left` or `chars` with some codes.
public struct APIErrorBody: Codable, Hashable, Sendable, Error {
    public var ok: Bool
    public var error: APIErrorCode
    /// One English sentence for logs. Never shown to the user or parsed.
    public var message: String?
    /// The request field at fault, as a dotted path.
    public var field: String?
    /// With `rate_limited` and `pairing_busy`.
    public var retryAfterS: Int?
    /// With `wrong_code`.
    public var attemptsLeft: Int?
    /// With `unsupported_chars`.
    public var chars: [String]?

    public init(
        error: APIErrorCode,
        message: String? = nil,
        field: String? = nil,
        retryAfterS: Int? = nil,
        attemptsLeft: Int? = nil,
        chars: [String]? = nil
    ) {
        self.ok = false
        self.error = error
        self.message = message
        self.field = field
        self.retryAfterS = retryAfterS
        self.attemptsLeft = attemptsLeft
        self.chars = chars
    }

    enum CodingKeys: String, CodingKey {
        case ok, error, message, field
        case retryAfterS = "retry_after_s"
        case attemptsLeft = "attempts_left"
        case chars
    }
}

/// Everything that can go wrong talking to a bar, over either link. Every
/// transport method throws only this.
public enum BarError: Error, Hashable, Sendable {
    /// The bar answered `{"ok": false, …}`. `httpStatus` is `nil` over USB,
    /// except for the `request` command, whose reply carries `http_status`.
    case api(APIErrorBody, httpStatus: Int?)
    /// The reply wasn't JSON, or not the expected shape. The string is for logs.
    case malformedReply(String)
    /// No reply in time (5 s over HTTP, 3 s over USB; api.md 2.6).
    case timedOut
    /// The link went away: the port disappeared or the transport was closed.
    case closed
    /// Couldn't connect: name lookup, refused, no route, client isolation. For logs.
    case unreachable(String)
    /// macOS refused local network access (macOS 15 and later, api.md 16).
    case localNetworkDenied
    /// USB: nothing answered `hello` as a MiniBar within 10 s (api.md 6.2).
    case notATinyBar
    /// The reply came from another bar than the one the app paired with (api.md 5.3, 16).
    case wrongDevice(expected: String, got: String)
    /// A message would be longer than the bar accepts (api.md 2.5). A bug in the app.
    case tooLarge
    /// The task was cancelled.
    case cancelled

    /// The API error code, if the bar answered with one.
    public var apiCode: APIErrorCode? {
        if case .api(let body, _) = self { return body.error }
        return nil
    }

    /// `retry_after_s`, if the bar's answer carried it (`rate_limited`, `pairing_busy`).
    public var retryAfterSeconds: Int? {
        if case .api(let body, _) = self { return body.retryAfterS }
        return nil
    }

    /// `401 unauthorized`: the bar doesn't accept this app's token (api.md 4.5).
    public var isUnauthorized: Bool {
        if case .api(let body, let status) = self {
            return body.error == .unauthorized || status == 401
        }
        return false
    }

    /// The bar won't take this Mac's token: `401 unauthorized` (unknown or
    /// revoked), or `403 wrong_client` on a call (the token is tied to another
    /// `client`, api.md 5.2, for example after the install ID was made again).
    /// Either way the answer is to pair again (api.md 4.5, 16).
    public var refusesToken: Bool {
        isUnauthorized || apiCode == .wrongClient
    }

    /// Network-level problems worth retrying at the next heartbeat (api.md 16),
    /// as opposed to answers from the bar.
    public var isConnectionProblem: Bool {
        switch self {
        case .timedOut, .closed, .unreachable, .localNetworkDenied: return true
        case .api(let body, _): return body.error == .busy
        default: return false
        }
    }
}
