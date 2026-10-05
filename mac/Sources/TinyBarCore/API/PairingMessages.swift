import Foundation

/// `POST /api/v1/pair/start` (api.md 4.6, **Proposed**): asks the bar to show a
/// 6-digit code for 2 minutes. No token needed.
public struct PairStartRequest: Codable, Hashable, Sendable {
    /// Only if the user named the Mac in Settings; the bar then says "Mac".
    public var name: String?
    /// Always `.mac` from this app.
    public var kind: ClientKind
    /// Always `.call` from this app (api.md 4.4).
    public var scope: TokenScope
    /// The install ID, so re-pairing replaces the old token.
    public var client: String?

    public init(name: String? = nil, kind: ClientKind = .mac, scope: TokenScope = .call, client: String?) {
        self.name = name
        self.kind = kind
        self.scope = scope
        self.client = client
    }
}

/// The `202` reply to `pair/start` (api.md 4.6).
public struct PairStartReply: Codable, Hashable, Sendable {
    public var ok: Bool
    /// Ties the code to this client. Send it back with the code.
    public var pairingID: String
    /// This code's number, counted from the bar's start-up (api.md 4.6,
    /// added 2026-10-05): `info.pairing_seq` carries the same number while
    /// the code is on the screen, so the Remote can tell its code from a
    /// later one. `nil` from firmware that doesn't send it. The Mac app
    /// doesn't use it.
    public var pairingSeq: Int?
    /// 120.
    public var expiresInS: Int
    /// 6.
    public var codeLength: Int
    /// Tries allowed for this code: 3.
    public var attempts: Int

    public init(pairingID: String, pairingSeq: Int? = nil, expiresInS: Int = 120, codeLength: Int = 6, attempts: Int = 3) {
        self.ok = true
        self.pairingID = pairingID
        self.pairingSeq = pairingSeq
        self.expiresInS = expiresInS
        self.codeLength = codeLength
        self.attempts = attempts
    }

    enum CodingKeys: String, CodingKey {
        case ok
        case pairingID = "pairing_id"
        case pairingSeq = "pairing_seq"
        case expiresInS = "expires_in_s"
        case codeLength = "code_length"
        case attempts
    }
}

/// `POST /api/v1/pair` (api.md 4.7): the code the user read off the bar.
public struct PairRequest: Codable, Hashable, Sendable {
    public var pairingID: String
    /// Six digits. The bar ignores spaces and dashes; the app sends digits only.
    public var code: String
    /// The Remote page only. The app never sends it.
    public var cookie: Bool?

    public init(pairingID: String, code: String, cookie: Bool? = nil) {
        self.pairingID = pairingID
        self.code = code
        self.cookie = cookie
    }

    enum CodingKeys: String, CodingKey {
        case pairingID = "pairing_id"
        case code, cookie
    }
}

/// `POST /api/v1/pair/cancel` (api.md 4.7): takes the code this app asked for
/// off the bar, when you go Back, close the window, quit, or the Mac sleeps.
/// No token needed; only the `pairing_id` from `pair/start` works.
public struct PairCancelRequest: Codable, Hashable, Sendable {
    public var pairingID: String

    public init(pairingID: String) {
        self.pairingID = pairingID
    }

    enum CodingKeys: String, CodingKey {
        case pairingID = "pairing_id"
    }
}

/// The `200` reply to `pair/cancel`: `{"ok": true}`. A code that had already
/// ended answers `409 not_pairing` instead.
public struct PairCancelReply: Codable, Hashable, Sendable {
    public var ok: Bool

    public init() {
        self.ok = true
    }
}

/// The USB `pair` command (api.md 6.6, **Proposed**): a `call`-scope token
/// without a code, because the cable proves someone is at the desk.
public struct USBPairRequest: Codable, Hashable, Sendable {
    public var client: String

    public init(client: String) {
        self.client = client
    }
}

/// The reply to `POST /api/v1/pair` and the USB `pair` command (api.md 4.7, 6.6).
/// The token appears here once and never again.
public struct PairReply: Codable, Hashable, Sendable {
    public var ok: Bool
    /// `tb1_…`. `nil` only with `"cookie": true`, which the app never sends.
    public var token: String?
    /// Public ID of the token, 8 hex digits.
    public var tokenID: String
    public var scope: TokenScope
    public var deviceID: String
    /// The bar's name, "TinyBar 2A1C".
    public var name: String
    /// The bar's mDNS name.
    public var host: String?

    public init(token: String?, tokenID: String, scope: TokenScope = .call, deviceID: String, name: String, host: String?) {
        self.ok = true
        self.token = token
        self.tokenID = tokenID
        self.scope = scope
        self.deviceID = deviceID
        self.name = name
        self.host = host
    }

    enum CodingKeys: String, CodingKey {
        case ok, token
        case tokenID = "token_id"
        case scope
        case deviceID = "device_id"
        case name, host
    }
}

/// The reply to `DELETE /api/v1/clients/self` (api.md 12.3).
public struct RevokeReply: Codable, Hashable, Sendable {
    public var ok: Bool
    /// The `token_id` that was revoked.
    public var revoked: String

    public init(revoked: String) {
        self.ok = true
        self.revoked = revoked
    }
}
