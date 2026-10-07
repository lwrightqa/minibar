import Foundation

/// A string field with a known set of values that a later minor version of the
/// API may add to (api.md 2.1: clients handle unknown values of *open* fields).
///
/// Decoding never fails on an unknown value; it keeps the raw string. Compare
/// with the static constants (`showing == .call`).
public protocol OpenEnum: RawRepresentable, Codable, Hashable, Sendable, CustomStringConvertible
where RawValue == String {
    init(rawValue: String)
}

extension OpenEnum {
    public var description: String { rawValue }

    public init(from decoder: any Decoder) throws {
        self.init(rawValue: try decoder.singleValueContainer().decode(String.self))
    }

    public func encode(to encoder: any Encoder) throws {
        var container = encoder.singleValueContainer()
        try container.encode(rawValue)
    }
}

/// What the bar shows (api.md 7.3). *Open:* treat an unknown value like `.own`.
public struct Showing: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }

    /// On a call.
    public static let call = Showing(rawValue: "call")
    /// In a meeting, from the calendar.
    public static let meeting = Showing(rawValue: "meeting")
    /// Your own status, including while a call or meeting is set aside.
    public static let own = Showing(rawValue: "own")
    /// The Wi-Fi setup screens.
    public static let setup = Showing(rawValue: "setup")

    public static let known: [Showing] = [.call, .meeting, .own, .setup]

    /// The value with unknown ones mapped to `.own`, as api.md 7.3 asks.
    public var normalized: Showing { Showing.known.contains(self) ? self : .own }
}

/// `"on"` or `"dark"` (api.md 5.3).
public struct ScreenState: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let on = ScreenState(rawValue: "on")
    public static let dark = ScreenState(rawValue: "dark")
}

/// Which link a Mac's latest message came over (`via`, api.md 5.2).
public struct LinkVia: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let usb = LinkVia(rawValue: "usb")
    public static let wifi = LinkVia(rawValue: "wifi")
}

/// Whether the bar requires pairing (`auth`, api.md 3 and 7.1).
public struct AuthMode: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    /// Pairing required: send `Authorization: Bearer <token>`.
    public static let bearer = AuthMode(rawValue: "bearer")
    /// No pairing: tokens aren't checked, and clients skip pairing (wire value `"none"`).
    public static let notRequired = AuthMode(rawValue: "none")
}

/// The bar's pairing screen (`pairing` in `info`, api.md 7.1).
public struct PairingState: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let idle = PairingState(rawValue: "idle")
    /// A code is on the screen.
    public static let showing = PairingState(rawValue: "showing")
    /// Back-off after failed pairings (api.md 4.9).
    public static let locked = PairingState(rawValue: "locked")
}

/// Where the bar's clock came from (`time_source`, api.md 7.1).
public struct TimeSource: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let ntp = TimeSource(rawValue: "ntp")
    public static let rtc = TimeSource(rawValue: "rtc")
    /// Set from the Mac's `hello`.
    public static let mac = TimeSource(rawValue: "mac")
    /// The bar doesn't know the time (wire value `"none"`).
    public static let unknown = TimeSource(rawValue: "none")
}

/// The bar's Wi-Fi (`wifi` in `info`, `wifi.state` in `status`).
public struct WiFiState: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let connected = WiFiState(rawValue: "connected")
    /// Skipped or dropped.
    public static let offline = WiFiState(rawValue: "offline")
    public static let setup = WiFiState(rawValue: "setup")
}

/// What a pairing client is (`kind`, api.md 4.6).
public struct ClientKind: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let mac = ClientKind(rawValue: "mac")
    public static let remote = ClientKind(rawValue: "remote")
    public static let automation = ClientKind(rawValue: "automation")
}

/// A token's scope (api.md 4.4). The Mac app always pairs with `.call`.
public struct TokenScope: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let call = TokenScope(rawValue: "call")
    public static let full = TokenScope(rawValue: "full")
}

/// **Proposed** (api.md 14.2): which inputs a call uses. The app doesn't send
/// `inputs` until the user agrees.
public struct CallInput: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let mic = CallInput(rawValue: "mic")
    public static let camera = CallInput(rawValue: "camera")
}

/// Your own status on the bar (`own.status`, api.md 7.3). *Open.*
public struct OwnStatus: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let available = OwnStatus(rawValue: "available")
    public static let busy = OwnStatus(rawValue: "busy")
    public static let meeting = OwnStatus(rawValue: "meeting")
    public static let pomodoro = OwnStatus(rawValue: "pomodoro")
    public static let away = OwnStatus(rawValue: "away")
    public static let message = OwnStatus(rawValue: "message")
    public static let clock = OwnStatus(rawValue: "clock")
}

/// `pomodoro.state` (api.md 7.3).
public struct PomodoroState: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let ready = PomodoroState(rawValue: "ready")
    public static let running = PomodoroState(rawValue: "running")
    public static let paused = PomodoroState(rawValue: "paused")
    public static let waiting = PomodoroState(rawValue: "waiting")
}

/// `pomodoro.phase` (api.md 7.3).
public struct PomodoroPhase: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let focus = PomodoroPhase(rawValue: "focus")
    public static let short = PomodoroPhase(rawValue: "short")
    public static let long = PomodoroPhase(rawValue: "long")
}

/// `pomodoro.paused_by` (api.md 7.3).
public struct PausedBy: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let you = PausedBy(rawValue: "you")
    public static let call = PausedBy(rawValue: "call")
    public static let meeting = PausedBy(rawValue: "meeting")
}

/// HTTP methods, for endpoints and the USB `request` command (api.md 6.6).
public struct HTTPMethod: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let get = HTTPMethod(rawValue: "GET")
    public static let post = HTTPMethod(rawValue: "POST")
    public static let put = HTTPMethod(rawValue: "PUT")
    public static let patch = HTTPMethod(rawValue: "PATCH")
    public static let delete = HTTPMethod(rawValue: "DELETE")
}

/// A USB command (`cmd`, api.md 6.5).
public struct USBCommandName: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }
    public static let hello = USBCommandName(rawValue: "hello")
    public static let call = USBCommandName(rawValue: "call")
    public static let status = USBCommandName(rawValue: "status")
    /// **Proposed** (api.md 6.6): a Wi-Fi token without a code.
    public static let pair = USBCommandName(rawValue: "pair")
    /// Any endpoint over USB.
    public static let request = USBCommandName(rawValue: "request")
}

/// The bar's Time format setting (`display.time_format`, api.md 7.1 and 10.1): `"12h"` (the default, 3:30 PM) or
/// `"24h"` (15:30). *Open:* an unknown value is treated like no answer, so the Mac falls back to its own format.
public struct TimeFormat: OpenEnum {
    public let rawValue: String
    public init(rawValue: String) { self.rawValue = rawValue }

    public static let h12 = TimeFormat(rawValue: "12h")
    public static let h24 = TimeFormat(rawValue: "24h")
}
