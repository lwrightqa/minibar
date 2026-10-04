import Foundation

/// The USB `hello` command (api.md 6.6). Sent right after opening the port,
/// after a `ready` event, and whenever replies stop. Over Wi-Fi the same
/// information comes from `GET /api/v1/info`, which takes no body.
public struct HelloRequest: Codable, Hashable, Sendable {
    /// As in `call`.
    public var client: String
    /// Only if the user named the Mac in Settings; never the computer's name.
    public var name: String?
    /// For the bar's logs, for example `"1.0 (12)"`.
    public var appVersion: String?
    /// The API version the app speaks: `TinyBarAPI.version`.
    public var api: String
    /// **Proposed** (api.md 14.2): the Mac's clock, so a bar without Wi-Fi has one.
    public var time: BarTime?
    /// **Proposed** (api.md 14.2): the Mac's IANA time zone.
    public var timeZone: String?

    public init(
        client: String,
        name: String? = nil,
        appVersion: String? = nil,
        api: String = TinyBarAPI.version,
        time: BarTime? = nil,
        timeZone: String? = nil
    ) {
        self.client = client
        self.name = name
        self.appVersion = appVersion
        self.api = api
        self.time = time
        self.timeZone = timeZone
    }

    enum CodingKeys: String, CodingKey {
        case client, name
        case appVersion = "app_version"
        case api, time
        case timeZone = "time_zone"
    }
}

/// `GET /api/v1/info`, and the reply to the USB `hello` (api.md 7.1). Who the
/// bar is, before pairing.
public struct InfoReply: Codable, Hashable, Sendable {
    public var ok: Bool
    /// Always `"TinyBar"`. Over USB, the only proof the port is a TinyBar.
    public var device: String
    /// 12 lowercase hex digits. The key tokens are stored under.
    public var deviceID: String
    /// The bar's name, for example "TinyBar 2A1C". Shown in the menu.
    public var name: String
    /// Firmware version.
    public var fw: String
    /// API version, for example `"1.0"`.
    public var api: String
    /// The mDNS name the bar has now, for example `"tinybar.local"`.
    public var host: String?
    public var auth: AuthMode
    public var pairing: PairingState
    public var heartbeatS: Int
    public var timeoutS: Int
    public var time: BarTime?
    public var timeSource: TimeSource
    /// Over USB, tells the app whether Wi-Fi is worth trying.
    public var wifi: WiFiState

    public init(
        device: String = "TinyBar",
        deviceID: String,
        name: String,
        fw: String,
        api: String = TinyBarAPI.version,
        host: String? = TinyBarAPI.Bonjour.defaultHost,
        auth: AuthMode = .bearer,
        pairing: PairingState = .idle,
        heartbeatS: Int = TinyBarAPI.Defaults.heartbeatSeconds,
        timeoutS: Int = TinyBarAPI.Defaults.timeoutSeconds,
        time: BarTime? = nil,
        timeSource: TimeSource = .ntp,
        wifi: WiFiState = .connected
    ) {
        self.ok = true
        self.device = device
        self.deviceID = deviceID
        self.name = name
        self.fw = fw
        self.api = api
        self.host = host
        self.auth = auth
        self.pairing = pairing
        self.heartbeatS = heartbeatS
        self.timeoutS = timeoutS
        self.time = time
        self.timeSource = timeSource
        self.wifi = wifi
    }

    enum CodingKeys: String, CodingKey {
        case ok, device
        case deviceID = "device_id"
        case name, fw, api, host, auth, pairing
        case heartbeatS = "heartbeat_s"
        case timeoutS = "timeout_s"
        case time
        case timeSource = "time_source"
        case wifi
    }

    /// The `device` field says this is a TinyBar.
    public var isTinyBar: Bool { device == "TinyBar" }

    /// The bar's API version, if it parses.
    public var apiVersion: APIVersion? { APIVersion(api) }
}
