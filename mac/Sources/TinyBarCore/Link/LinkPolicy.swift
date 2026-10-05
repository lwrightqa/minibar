import Foundation

/// When to try again after a failed send (docs/mac-app.md: after 2, 5, 10 and
/// 30 seconds, then every 30; api.md 16: at most every 30 while unreachable).
public struct RetrySchedule: Hashable, Sendable {
    /// Delays after the 1st, 2nd, 3rd… failure in a row; the last one repeats.
    public var delays: [TimeInterval]

    public init(delays: [TimeInterval]) {
        precondition(!delays.isEmpty)
        self.delays = delays
    }

    public static let standard = RetrySchedule(delays: [2, 5, 10, 30])

    /// The delay before the next try after `failures` failures in a row
    /// (`failures` ≥ 1).
    public func delay(afterFailures failures: Int) -> TimeInterval {
        delays[min(max(failures, 1), delays.count) - 1]
    }
}

/// Tracks replies to decide when the bar counts as not connected: after 15
/// seconds without a successful reply (mac-app-ux.md 3.2). Launching, waking
/// and switching links get the same 15-second grace, so the icon doesn't flicker.
public struct ReachabilityTracker: Hashable, Sendable {
    public static let grace: TimeInterval = 15

    /// The last successful reply.
    public private(set) var lastSuccess: Date?
    /// When the current grace period started (launch, wake, link switch, or
    /// the first failure after a success).
    public private(set) var graceStart: Date?

    public init() {}

    /// A reply got through: reachable, and no grace period running.
    public mutating func recordSuccess(at now: Date) {
        lastSuccess = now
        graceStart = nil
    }

    /// A send failed. The first failure after a success starts the 15
    /// seconds; later ones don't restart them.
    public mutating func recordFailure(at now: Date) {
        if graceStart == nil { graceStart = now }
    }

    /// Starts a new grace period (launch, wake, link switch).
    public mutating func beginGrace(at now: Date) {
        graceStart = now
    }

    /// When the current grace period ends, if one is running.
    public var graceEnd: Date? {
        graceStart?.addingTimeInterval(ReachabilityTracker.grace)
    }

    /// Inside a grace period: nothing has gotten through since it started, and
    /// 15 seconds haven't passed yet ("Looking for MiniBar…").
    public func isInGrace(at now: Date) -> Bool {
        guard let graceEnd else { return false }
        return now < graceEnd
    }

    /// No success for the last 15 seconds, and not in a grace period.
    public func isUnreachable(at now: Date) -> Bool {
        guard let graceEnd else { return false }
        return now >= graceEnd
    }
}

/// What the link chooser knows (api.md 6.8, 16; mac-app-ux.md 6.4).
public struct LinkInputs: Hashable, Sendable {
    /// A MiniBar answered `hello` on USB and its port is open.
    public var usbReady: Bool
    /// Pause USB is on.
    public var usbPaused: Bool
    /// "Use Wi-Fi when MiniBar isn't plugged in" is on.
    public var wifiEnabled: Bool
    /// The best Wi-Fi address for the paired bar (Bonjour match by `device_id`,
    /// manual address, or the last that worked), if any.
    public var wifiEndpoint: BarEndpoint?
    /// The app has a token for the bar, or the bar doesn't need one (`auth: none`).
    public var wifiAuthorized: Bool
    /// A bar is set up: paired (or known from USB), or an address typed in
    /// Settings. Not set up, the app never tries Wi-Fi on its own, so the
    /// Local Network prompt only comes after the Connect window explains it.
    public var isSetUp: Bool

    public init(usbReady: Bool, usbPaused: Bool, wifiEnabled: Bool, wifiEndpoint: BarEndpoint?, wifiAuthorized: Bool,
                isSetUp: Bool = true) {
        self.usbReady = usbReady
        self.usbPaused = usbPaused
        self.wifiEnabled = wifiEnabled
        self.wifiEndpoint = wifiEndpoint
        self.wifiAuthorized = wifiAuthorized
        self.isSetUp = isSetUp
    }
}

/// Which link to use now.
public enum LinkChoice: Hashable, Sendable {
    case usb
    case wifi(BarEndpoint)
    /// Neither: the reason, for the menu.
    case none(NoLinkReason)
}

public enum NoLinkReason: Hashable, Sendable {
    /// No bar set up.
    case notSetUp
    /// Wi-Fi is off in Settings and the bar isn't plugged in.
    case wifiOff
    /// No address known for the bar yet (still browsing).
    case noAddress
    /// Wi-Fi would work, but the app has no token (the bar answered 401, or
    /// pairing never happened over Wi-Fi).
    case notPaired
}

/// USB first, else Wi-Fi if allowed, addressed and authorized (api.md 6.8, 16).
public enum LinkChooser {
    public static func choose(_ inputs: LinkInputs) -> LinkChoice {
        if inputs.usbReady && !inputs.usbPaused { return .usb }
        guard inputs.isSetUp else { return .none(.notSetUp) }
        guard inputs.wifiEnabled else { return .none(.wifiOff) }
        guard let endpoint = inputs.wifiEndpoint else { return .none(.noAddress) }
        guard inputs.wifiAuthorized else { return .none(.notPaired) }
        return .wifi(endpoint)
    }
}
