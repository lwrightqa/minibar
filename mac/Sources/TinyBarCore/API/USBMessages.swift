import Foundation

/// The `ready` event the bar sends after it boots (api.md 6.7). On it, the app
/// sends `hello` and then its call state at once.
public struct ReadyEvent: Codable, Hashable, Sendable {
    /// `"ready"`.
    public var event: String
    public var deviceID: String
    public var api: String
    public var fw: String

    public init(deviceID: String, api: String = TinyBarAPI.version, fw: String) {
        self.event = "ready"
        self.deviceID = deviceID
        self.api = api
        self.fw = fw
    }

    enum CodingKeys: String, CodingKey {
        case event
        case deviceID = "device_id"
        case api, fw
    }
}

/// The USB `request` command (api.md 6.6): any endpoint over USB. The Mac app
/// uses `hello`, `call`, `status` and `pair` for everything but one thing:
/// Forget This TinyBar while the bar is plugged in sends
/// `DELETE /api/v1/clients/{token_id}` this way (api.md 12.2), since USB
/// carries no token for `clients/self`.
public struct USBRequestCommand: Codable, Hashable, Sendable {
    public var method: HTTPMethod
    /// Starts `/api/v1/`; may include a query.
    public var path: String
    /// The JSON body, for methods that take one.
    public var body: JSONValue?

    public init(method: HTTPMethod, path: String, body: JSONValue? = nil) {
        self.method = method
        self.path = path
        self.body = body
    }
}

/// A command with no fields besides `cmd` and `id` (the USB `status` command).
public struct EmptyBody: Codable, Hashable, Sendable {
    public init() {}
}

/// The routing fields of any protocol line from the bar (api.md 6.4): replies
/// carry `id` (`null` for a line the bar couldn't parse) and `ok`; events carry
/// `event` and no `id`; replies to `request` add `http_status`.
public struct USBLineHeader: Decodable, Hashable, Sendable {
    public var id: Int?
    public var ok: Bool?
    public var event: String?
    public var httpStatus: Int?

    enum CodingKeys: String, CodingKey {
        case id, ok, event
        case httpStatus = "http_status"
    }
}
