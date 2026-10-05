import Foundation

/// Which apps count (Settings › Apps, "Count calls from").
public enum CountingMode: String, Hashable, Sendable, Codable {
    /// Any app except ignored ones; unlisted apps count without a name. Default.
    case anyExceptIgnored
    /// Only the call apps on the list. The camera doesn't count either,
    /// because macOS doesn't say which app uses it.
    case onlyCallApps
}

/// Everything that decides what counts as a call (Settings › General and Apps).
public struct DetectionSettings: Hashable, Sendable, Codable {
    /// Seconds of continuous use before a call starts. 0 to 30, default 3.
    public var startDelay: Int
    /// Seconds without use before a call ends. 3 to 60, default 10.
    public var endDelay: Int
    /// Camera use alone counts as a call (with no name). Default on.
    public var countCamera: Bool
    /// "Send the app's name to MiniBar". Off: no message has `app`. Default on.
    public var sendAppName: Bool
    public var mode: CountingMode
    public var catalog: AppCatalog

    public init(
        startDelay: Int = 3,
        endDelay: Int = 10,
        countCamera: Bool = true,
        sendAppName: Bool = true,
        mode: CountingMode = .anyExceptIgnored,
        catalog: AppCatalog = .defaults
    ) {
        self.startDelay = startDelay
        self.endDelay = endDelay
        self.countCamera = countCamera
        self.sendAppName = sendAppName
        self.mode = mode
        self.catalog = catalog
    }

    public static let defaults = DetectionSettings()

    public static let startDelayRange = 0...30
    public static let endDelayRange = 3...60
    /// The pop-up's choices (mac-app-ux.md 6.2). Other values in range are valid.
    public static let startDelayPresets = [0, 1, 2, 3, 5, 10, 15, 30]
    public static let endDelayPresets = [3, 5, 10, 15, 30, 60]

    /// Whether camera use counts now: the switch is on and the mode isn't
    /// "Only the call apps on my list".
    public var cameraCounts: Bool {
        countCamera && mode == .anyExceptIgnored
    }

    /// Checks the ranges and every call app's short name (criterion 9).
    public func validate() throws(SettingsError) {
        guard DetectionSettings.startDelayRange.contains(startDelay) else { throw .startDelayOutOfRange(startDelay) }
        guard DetectionSettings.endDelayRange.contains(endDelay) else { throw .endDelayOutOfRange(endDelay) }
        for app in catalog.callApps {
            if let problem = NameRules.appNameProblem(app.shortName) { throw .badAppName(problem) }
        }
    }

    enum CodingKeys: String, CodingKey {
        case startDelay, endDelay, countCamera, sendAppName, mode, catalog
    }

    /// Reads saved settings; a field a newer version added and an older one
    /// didn't save gets its default.
    public init(from decoder: any Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let defaults = DetectionSettings.defaults
        startDelay = try container.decodeIfPresent(Int.self, forKey: .startDelay) ?? defaults.startDelay
        endDelay = try container.decodeIfPresent(Int.self, forKey: .endDelay) ?? defaults.endDelay
        countCamera = try container.decodeIfPresent(Bool.self, forKey: .countCamera) ?? defaults.countCamera
        sendAppName = try container.decodeIfPresent(Bool.self, forKey: .sendAppName) ?? defaults.sendAppName
        mode = try container.decodeIfPresent(CountingMode.self, forKey: .mode) ?? defaults.mode
        catalog = try container.decodeIfPresent(AppCatalog.self, forKey: .catalog) ?? defaults.catalog
    }
}

/// Pause Detection (mac-app-ux.md 4.6).
public enum PauseState: Hashable, Sendable, Codable {
    case notPaused
    /// For 1 hour: until this time.
    case until(Date)
    /// For the rest of today: until the next local midnight, this time.
    case restOfToday(endsAt: Date)
    /// Until I Resume.
    case untilResumed

    /// Whether detection is paused at `now`.
    public func isPaused(at now: Date) -> Bool {
        switch self {
        case .notPaused: return false
        case .until(let end), .restOfToday(let end): return now < end
        case .untilResumed: return true
        }
    }

    /// When the pause ends on its own, if it does.
    public var endsAt: Date? {
        switch self {
        case .until(let end), .restOfToday(let end): return end
        case .notPaused, .untilResumed: return nil
        }
    }

    /// The same pause with its end moved by `seconds`: to the Mac's clock for
    /// saving and back for detection (`TinyClock.wallClockOffset()`).
    public func shifted(by seconds: TimeInterval) -> PauseState {
        switch self {
        case .until(let end): return .until(end.addingTimeInterval(seconds))
        case .restOfToday(let end): return .restOfToday(endsAt: end.addingTimeInterval(seconds))
        case .notPaused, .untilResumed: return self
        }
    }
}

/// The three choices in Pause Detection ▸.
public enum PauseChoice: Hashable, Sendable, CaseIterable {
    case oneHour
    case restOfToday
    case untilResumed

    /// The pause this choice makes at `now` (midnight in `timeZone` for the
    /// rest of today). `now` is the Mac's clock (`TinyClock.wallNow()`), so
    /// midnight is the real one.
    public func pauseState(at now: Date, timeZone: TimeZone) -> PauseState {
        switch self {
        case .oneHour:
            return .until(now.addingTimeInterval(3600))
        case .restOfToday:
            return .restOfToday(endsAt: PauseChoice.nextMidnight(after: now, timeZone: timeZone))
        case .untilResumed:
            return .untilResumed
        }
    }

    /// The start of the next day in `timeZone`: midnight, or the first moment
    /// of the day where a clock change skips midnight.
    static func nextMidnight(after now: Date, timeZone: TimeZone) -> Date {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = timeZone
        let today = calendar.startOfDay(for: now)
        // Noon tomorrow is never skipped by a clock change, so it's a safe step.
        let noonTomorrow = calendar.date(byAdding: DateComponents(day: 1, hour: 12), to: today)
            ?? today.addingTimeInterval(36 * 3600)
        let next = calendar.startOfDay(for: noonTomorrow)
        return next > now ? next : now.addingTimeInterval(24 * 3600)
    }
}
