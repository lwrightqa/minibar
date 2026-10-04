import Foundation

/// Works out the menu, the icon and the Settings status from the engine's
/// state, with every string from mac-app-ux.md sections 3 and 4.
///
/// Pure, so all of the copy is tested on Linux, including each menu in 4.7.
public enum MenuPresenter {
    /// The status item and menu.
    ///
    /// - Parameters:
    ///   - now: for "12m" and "since 2:04 PM".
    ///   - timeZone: for clock times ("Paused until 3:15 PM").
    ///   - showDetails: Option was held while opening the menu (4.8).
    public static func content(for state: EngineState, now: Date, timeZone: TimeZone, showDetails: Bool) -> MenuContent {
        unimplemented()
    }

    /// Settings › Connection, "Your TinyBar" (mac-app-ux.md 6.4): the status
    /// text, whether it shows a filled dot, and which buttons apply.
    public static func connectionSummary(for state: EngineState, now: Date, timeZone: TimeZone) -> ConnectionSummary {
        unimplemented()
    }
}

/// Settings › Connection's top section (mac-app-ux.md 6.4).
public struct ConnectionSummary: Hashable, Sendable {
    public enum Button: Hashable, Sendable {
        case connect, sendTestCall, forget, pairAgain
    }

    /// "TinyBar 2A1C", or nil when not set up.
    public var barName: String?
    /// "Connected over USB", "Can't reach it since 2:04 PM"…
    public var status: String
    /// ● when connected, ○ when not.
    public var isConnected: Bool
    public var buttons: [Button]
    /// Why Send Test Call is dimmed: "Not available during a call." or
    /// "Connect TinyBar first.", else nil.
    public var testCallUnavailableReason: String?

    public init(barName: String?, status: String, isConnected: Bool, buttons: [Button], testCallUnavailableReason: String?) {
        self.barName = barName
        self.status = status
        self.isConnected = isConnected
        self.buttons = buttons
        self.testCallUnavailableReason = testCallUnavailableReason
    }
}

/// Durations and times in the bar's style (docs/mac-app.md, Menu bar).
public enum Formatting {
    /// A call's length: "0m", "12m", "1h 5m" (whole minutes, rounded down).
    public static func duration(seconds: Int) -> String {
        unimplemented()
    }

    /// A clock time: "2:04 PM" (en_US, 12-hour).
    public static func clockTime(_ date: Date, timeZone: TimeZone) -> String {
        unimplemented()
    }

    /// A wait, rounded up: "45 seconds" under a minute, else "4 minutes";
    /// "1 second", "1 minute" in the singular (mac-app-ux.md 5.5).
    public static func wait(seconds: Int) -> String {
        unimplemented()
    }
}
