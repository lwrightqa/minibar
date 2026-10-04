import Foundation

/// `POST /api/v1/call`, or the USB `call` command (api.md 5.1). The app's
/// whole state: sent on every change and every `heartbeat_s` seconds.
///
/// Optional fields that are `nil` are left out of the message. The app always
/// sends `session` and `seq`, sends `call_id` and `elapsed_s` while `active`,
/// `app` only for a listed call app with names on, `leaving` only as `true`,
/// and never `inputs` (Proposed, api.md 14.2).
public struct CallRequest: Codable, Hashable, Sendable {
    /// This install's ID, the same on USB and Wi-Fi.
    public var client: String
    /// Random, new each launch.
    public var session: String?
    /// Goes up by 1 with every call message in a session, from 1.
    public var seq: UInt32?
    /// `true` while the Mac is on a call.
    public var active: Bool
    /// The call app's short name ("Slack"), or `nil`.
    public var app: String?
    /// **Proposed**, not sent until the user agrees (api.md 14.2).
    public var inputs: [CallInput]?
    /// Changes for each new call.
    public var callID: UInt32?
    /// How long the call has been on, by the Mac's count.
    public var elapsedS: Int?
    /// `true` when quitting or going to sleep. Requires `active: false`.
    public var leaving: Bool?

    public init(
        client: String,
        session: String? = nil,
        seq: UInt32? = nil,
        active: Bool,
        app: String? = nil,
        inputs: [CallInput]? = nil,
        callID: UInt32? = nil,
        elapsedS: Int? = nil,
        leaving: Bool? = nil
    ) {
        self.client = client
        self.session = session
        self.seq = seq
        self.active = active
        self.app = app
        self.inputs = inputs
        self.callID = callID
        self.elapsedS = elapsedS
        self.leaving = leaving
    }

    enum CodingKeys: String, CodingKey {
        case client, session, seq, active, app, inputs
        case callID = "call_id"
        case elapsedS = "elapsed_s"
        case leaving
    }

    /// The contract's rules for this message (api.md 5.1), as readable
    /// problems; empty when the message is valid. The app checks its own
    /// messages with this in debug builds and in tests.
    public var problems: [String] {
        var problems: [String] = []
        if !Identifiers.isValidClientID(client) { problems.append("client must be 8 to 64 of A-Z a-z 0-9 -") }
        if let session, !Identifiers.isValidSessionID(session) { problems.append("session must be 1 to 16 of A-Z a-z 0-9") }
        if seq == 0 { problems.append("seq must be 1 to 4294967295") }
        if let app, !TinyBarAPI.Limits.appNameBytes.contains(app.utf8.count) { problems.append("app must be 1 to 64 bytes") }
        if callID == 0 { problems.append("call_id must be 1 to 4294967295") }
        if let elapsedS, !TinyBarAPI.Limits.elapsedSeconds.contains(elapsedS) { problems.append("elapsed_s must be 0 to 86400") }
        if leaving == true, active { problems.append("leaving needs active: false") }
        return problems
    }
}

/// The bar's call, as every `call` reply and `status` carry it (api.md 5.3, 7.3).
/// All `null` (and `false`) when there's no call.
public struct BarCall: Codable, Hashable, Sendable {
    /// Any connected Mac reports a call, whether or not Calls from your Mac is on.
    public var active: Bool
    /// From the call that started most recently.
    public var app: String?
    public var inputs: [CallInput]?
    public var via: LinkVia?
    /// When the bar's call started.
    public var since: BarTime?
    /// Set aside on the bar.
    public var aside: Bool

    public init(active: Bool, app: String? = nil, inputs: [CallInput]? = nil, via: LinkVia? = nil, since: BarTime? = nil, aside: Bool = false) {
        self.active = active
        self.app = app
        self.inputs = inputs
        self.via = via
        self.since = since
        self.aside = aside
    }

    /// No call.
    public static let none = BarCall(active: false)
}

/// The Remote's two Automatic status switches (api.md 5.3, 7.3).
public struct SourceSwitches: Codable, Hashable, Sendable {
    /// Calendar meetings.
    public var calendar: Bool
    /// Calls from your Mac. `false`: the bar records the call but doesn't show it.
    public var mac: Bool

    public init(calendar: Bool, mac: Bool) {
        self.calendar = calendar
        self.mac = mac
    }
}

/// The reply to a call message (api.md 5.3). The menu's line 2 comes from
/// the latest one (`showing`, `screen`, `call.aside`, `sources.mac`).
public struct CallReply: Codable, Hashable, Sendable {
    public var ok: Bool
    /// Which bar answered. The app checks it against the paired bar.
    public var deviceID: String
    public var showing: Showing
    public var screen: ScreenState
    /// The message was ignored as out of order (api.md 5.2).
    public var stale: Bool
    public var call: BarCall
    public var sources: SourceSwitches
    /// The heartbeat interval the bar wants now. Use the latest.
    public var heartbeatS: Int
    /// When the bar gives up on a silent Mac. Always at least 3 × `heartbeatS`.
    public var timeoutS: Int
    /// The bar's clock.
    public var time: BarTime?

    public init(
        deviceID: String,
        showing: Showing,
        screen: ScreenState = .on,
        stale: Bool = false,
        call: BarCall,
        sources: SourceSwitches = SourceSwitches(calendar: true, mac: true),
        heartbeatS: Int = TinyBarAPI.Defaults.heartbeatSeconds,
        timeoutS: Int = TinyBarAPI.Defaults.timeoutSeconds,
        time: BarTime? = nil
    ) {
        self.ok = true
        self.deviceID = deviceID
        self.showing = showing
        self.screen = screen
        self.stale = stale
        self.call = call
        self.sources = sources
        self.heartbeatS = heartbeatS
        self.timeoutS = timeoutS
        self.time = time
    }

    enum CodingKeys: String, CodingKey {
        case ok
        case deviceID = "device_id"
        case showing, screen, stale, call, sources
        case heartbeatS = "heartbeat_s"
        case timeoutS = "timeout_s"
        case time
    }
}
