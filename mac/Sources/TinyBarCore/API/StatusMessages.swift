import Foundation

/// `GET /api/v1/status`, or the USB `status` command (api.md 7.3): everything
/// the Remote shows. The Mac app doesn't poll it (every `call` reply carries
/// what the menu needs); it reads it for `macs` and `wifi` when it wants them,
/// for example to check whether Wi-Fi reaches the bar (mac-app-ux.md 4.3).
public struct StatusReply: Codable, Hashable, Sendable {
    public var ok: Bool
    public var deviceID: String
    /// Goes up whenever anything here changes, except `time` and countdowns.
    public var rev: Int
    public var time: BarTime?
    public var timeSource: TimeSource
    public var showing: Showing
    public var screen: ScreenState
    public var own: OwnState
    public var message: MessageState
    /// **Proposed** (api.md 8.1).
    public var away: AwayState
    public var call: BarCall
    public var meeting: MeetingState
    public var pomodoro: PomodoroStatus
    public var sources: SourceSwitches
    /// Every Mac the bar has heard from since it started, most recent first, at most 4.
    public var macs: [MacEntry]
    public var calendar: CalendarSummary
    public var wifi: WiFiStatus

    enum CodingKeys: String, CodingKey {
        case ok
        case deviceID = "device_id"
        case rev, time
        case timeSource = "time_source"
        case showing, screen, own, message, away, call, meeting, pomodoro, sources, macs, calendar, wifi
    }
}

/// `own` in `status` (api.md 7.3).
public struct OwnState: Codable, Hashable, Sendable {
    public var status: OwnStatus
    public var since: BarTime?
    /// What Stop on the Pomodoro returns to.
    public var previous: OwnStatus?
}

/// `message` in `status` (api.md 7.3).
public struct MessageState: Codable, Hashable, Sendable {
    public var text: String?
    public var setAt: BarTime?

    enum CodingKeys: String, CodingKey {
        case text
        case setAt = "set_at"
    }
}

/// `away` in `status` (**Proposed**, api.md 8.1).
public struct AwayState: Codable, Hashable, Sendable {
    /// `"HH:MM"`, 24-hour, bar's local time.
    public var backAt: String?
    public var note: String?

    enum CodingKeys: String, CodingKey {
        case backAt = "back_at"
        case note
    }
}

/// `meeting` in `status` (api.md 7.3).
public struct MeetingState: Codable, Hashable, Sendable {
    public var active: Bool
    public var aside: Bool
    public var current: MeetingEvent?
    /// The rest of today only.
    public var next: MeetingEvent?
    public var leftToday: Int?

    enum CodingKeys: String, CodingKey {
        case active, aside, current, next
        case leftToday = "left_today"
    }
}

/// One meeting (api.md 7.3, 11.1). `title` and `location` are `nil` while Show
/// meeting titles is off and for events marked Private.
public struct MeetingEvent: Codable, Hashable, Sendable {
    public var start: BarTime
    public var end: BarTime
    public var title: String?
    public var location: String?
}

/// `pomodoro` in `status` (api.md 7.3).
public struct PomodoroStatus: Codable, Hashable, Sendable {
    public var state: PomodoroState
    public var phase: PomodoroPhase
    public var round: Int
    public var rounds: Int
    public var lengthS: Int
    public var remainingS: Int
    public var endsAt: BarTime?
    public var pausedBy: PausedBy?
    public var ringing: Bool
    public var doneToday: Int
    public var focusedTodayS: Int

    enum CodingKeys: String, CodingKey {
        case state, phase, round, rounds
        case lengthS = "length_s"
        case remainingS = "remaining_s"
        case endsAt = "ends_at"
        case pausedBy = "paused_by"
        case ringing
        case doneToday = "done_today"
        case focusedTodayS = "focused_today_s"
    }
}

/// One entry of `macs` in `status` (api.md 7.3).
public struct MacEntry: Codable, Hashable, Sendable {
    public var client: String
    /// From `hello` or its pairing; "Mac" by default.
    public var name: String?
    public var via: LinkVia?
    public var connected: Bool
    /// That Mac reports a call.
    public var active: Bool
    public var lastHeard: BarTime?

    enum CodingKeys: String, CodingKey {
        case client, name, via, connected, active
        case lastHeard = "last_heard"
    }
}

/// `calendar` in `status`: a summary (api.md 7.3). Never the address.
public struct CalendarSummary: Codable, Hashable, Sendable {
    public var saved: Bool
    public var lastSync: BarTime?
    public var syncing: Bool
    /// The last sync's error code, or `nil`.
    public var error: String?

    enum CodingKeys: String, CodingKey {
        case saved
        case lastSync = "last_sync"
        case syncing, error
    }
}

/// `wifi` in `status` (api.md 7.3). Fields are `nil` when not connected.
public struct WiFiStatus: Codable, Hashable, Sendable {
    public var state: WiFiState
    public var ssid: String?
    public var ip: String?
    public var host: String?
    /// dBm.
    public var rssi: Int?
}
