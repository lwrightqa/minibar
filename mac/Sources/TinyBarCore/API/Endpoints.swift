import Foundation

/// An HTTP endpoint the app uses (api.md Appendix A).
public struct Endpoint: Hashable, Sendable, CustomStringConvertible {
    public var method: HTTPMethod
    /// The full path, starting `/api/v1/`.
    public var path: String
    /// Whether the request carries `Authorization: Bearer` (when the app has a token).
    public var sendsToken: Bool

    public init(_ method: HTTPMethod, _ path: String, sendsToken: Bool) {
        self.method = method
        self.path = path
        self.sendsToken = sendsToken
    }

    public var description: String { "\(method.rawValue) \(path)" }
}

/// The endpoints the Mac app calls. It pairs with the `call` scope, so these
/// are all it may use (api.md 4.4). Over USB, `info`, `call` and `status` are
/// the `hello`, `call` and `status` commands, and pairing is the `pair` command.
public enum Endpoints {
    /// Who the bar is. No token needed.
    public static let info = Endpoint(.get, "\(TinyBarAPI.basePath)/info", sendsToken: false)
    public static let status = Endpoint(.get, "\(TinyBarAPI.basePath)/status", sendsToken: true)
    public static let call = Endpoint(.post, "\(TinyBarAPI.basePath)/call", sendsToken: true)
    /// No token needed.
    public static let pairStart = Endpoint(.post, "\(TinyBarAPI.basePath)/pair/start", sendsToken: false)
    /// No token needed.
    public static let pair = Endpoint(.post, "\(TinyBarAPI.basePath)/pair", sendsToken: false)
    /// Takes the code this app asked for off the bar (api.md 4.7). No token needed.
    public static let pairCancel = Endpoint(.post, "\(TinyBarAPI.basePath)/pair/cancel", sendsToken: false)
    /// Unpair this Mac (Forget This MiniBar).
    public static let clientsSelf = Endpoint(.delete, "\(TinyBarAPI.basePath)/clients/self", sendsToken: true)
    /// Revoke the token with this `token_id` (api.md 12.2). Scope `full` over
    /// Wi-Fi, which the app doesn't have; the app sends it only over USB, as
    /// a `request` command, for Forget This MiniBar while the bar is plugged
    /// in (USB carries no token, so `clients/self` can't name the Mac there).
    public static func client(tokenID: String) -> Endpoint {
        Endpoint(.delete, "\(TinyBarAPI.basePath)/clients/\(tokenID)", sendsToken: false)
    }
}
