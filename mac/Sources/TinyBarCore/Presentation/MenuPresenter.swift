import Foundation

/// Works out the menu, the icon and the Settings status from the engine's
/// state, with every string from mac-app-ux.md sections 3 and 4.
///
/// Pure, so all of the copy is tested on Linux, including each menu in 4.7.
/// Text uses curly apostrophes (mac-app-ux.md 2).
public enum MenuPresenter {
    /// The status item and menu.
    ///
    /// - Parameters:
    ///   - now: the engine clock's time (`TinyClock.now()`), for "12m".
    ///   - timeZone: for clock times ("Paused until 3:15 PM").
    ///   - locale: the Mac's format settings for clock times (12- or 24-hour).
    ///   - clockOffset: `TinyClock.wallClockOffset()`, added to a time before
    ///     it's shown as a clock time.
    ///   - showDetails: Option was held while opening the menu (4.8).
    public static func content(
        for state: EngineState,
        now: Date,
        timeZone: TimeZone,
        locale: Locale = .autoupdatingCurrent,
        clockOffset: TimeInterval = 0,
        showDetails: Bool
    ) -> MenuContent {
        let context = Context(state: state, now: now, timeZone: timeZone, locale: locale, clockOffset: clockOffset)
        let line1 = context.line1
        let line2 = context.line2
        let icon = context.icon
        var content = MenuContent(
            icon: icon,
            tooltip: context.tooltip(icon: icon, line1: line1, line2: line2),
            line1: line1,
            line2: line2,
            fixItem: context.fixItem,
            countingItems: context.countingItems,
            pauseItem: context.paused ? .resume : .pauseSubmenu
        )
        if showDetails {
            content.details = context.details
            content.showsUSBToggle = state.connection.usbSeen
            content.usbPaused = state.connection.usbPaused
        }
        return content
    }

    /// Settings › Connection, "Your MiniBar" (mac-app-ux.md 6.4): the status
    /// text, whether it shows a filled dot, and which buttons apply.
    public static func connectionSummary(
        for state: EngineState,
        now: Date,
        timeZone: TimeZone,
        locale: Locale = .autoupdatingCurrent,
        clockOffset: TimeInterval = 0
    ) -> ConnectionSummary {
        let context = Context(state: state, now: now, timeZone: timeZone, locale: locale, clockOffset: clockOffset)
        let connection = state.connection
        let name: String? = connection.phase == .notSetUp ? nil : context.barName
        let noPairing = (connection.bar?.auth ?? connection.info?.auth) == .notRequired
        let status: String
        var isConnected = false
        var buttons: [ConnectionSummary.Button]
        switch connection.phase {
        case .notSetUp:
            return ConnectionSummary(barName: nil, status: "Not set up yet", isConnected: false,
                                     buttons: [.connect], testCallUnavailableReason: nil)
        case .connected:
            isConnected = true
            let over = connection.link == .usb ? "Connected over USB" : "Connected over Wi-Fi"
            status = noPairing ? over + " · no pairing needed" : over
            buttons = noPairing ? [.sendTestCall] : [.sendTestCall, .forget]
        case .looking:
            status = "Looking for MiniBar…"
            buttons = [.sendTestCall, .forget]
        case .unreachable:
            status = connection.lastSuccess.map { "Can\u{2019}t reach it since \(context.clock($0))" } ?? "Can\u{2019}t reach it"
            buttons = [.connect, .forget]
        case .notPluggedIn:
            status = "It isn\u{2019}t plugged in"
            buttons = [.connect, .forget]
        case .wifiCantReachHere:
            status = "Wi-Fi can\u{2019}t reach it here"
            buttons = [.connect, .forget]
        case .localNetworkBlocked:
            status = "Wi-Fi is blocked in Privacy settings"
            buttons = [.forget]
        case .unrecognized:
            status = "It doesn\u{2019}t recognize this Mac"
            buttons = [.pairAgain, .forget]
        case .barNeedsUpdate:
            status = "It needs a firmware update"
            buttons = [.forget]
        case .appNeedsUpdate:
            status = "This app needs an update for it"
            buttons = [.forget]
        }
        if noPairing { buttons.removeAll { $0 == .forget } }
        var reason: String?
        if buttons.contains(.sendTestCall) {
            if state.call != nil {
                reason = "Not available during a call."
            } else if connection.phase != .connected {
                reason = "Connect MiniBar first."
            }
        }
        return ConnectionSummary(barName: name, status: status, isConnected: isConnected, buttons: buttons,
                                 testCallUnavailableReason: reason)
    }

    // MARK: - Working it out

    private struct Context {
        let state: EngineState
        let now: Date
        let timeZone: TimeZone
        let locale: Locale
        let clockOffset: TimeInterval

        var connection: ConnectionState { state.connection }

        /// The bar's own name, as the bar reports it ("MiniBar 2A1C").
        var barName: String {
            connection.bar?.name ?? connection.info?.name ?? state.settings.bar?.name ?? "MiniBar"
        }

        var paused: Bool { state.isPaused(at: now) }

        /// A call is being reported: a detected call, or a test call.
        var onCall: Bool { state.call != nil || state.testCall != nil }

        var isSetUp: Bool { connection.phase != .notSetUp }

        /// The bar says Calls from your Mac is off (only known while connected).
        var ignoringCalls: Bool {
            connection.phase == .connected && connection.lastReply?.sources.mac == false
        }

        /// A clock time on the Mac's clock: "2:04 PM", or "2:31:04 PM" with seconds.
        func clock(_ date: Date, seconds: Bool = false) -> String {
            Formatting.clockTime(date.addingTimeInterval(clockOffset), timeZone: timeZone, locale: locale,
                                 withSeconds: seconds, timeFormat: connection.info?.timeFormat)
        }

        // MARK: Line 1 (4.2)

        var line1: String {
            if state.testCall != nil, state.call == nil { return "Sending a test call" }
            if paused { return pausedLine }
            if let call = state.call, isSetUp {
                let seconds = Int(now.timeIntervalSince(call.startedAt).rounded(.down))
                return "On a call · \(call.localName) · \(Formatting.duration(seconds: seconds))"
            }
            if let problem = problemLine { return problem }
            if !isSetUp { return "Not set up yet" }
            let observation = state.observation
            if observation.pendingSince != nil {
                if let use = observation.micUses.first(where: \.counted) {
                    return "Mic in use by \(MenuPresenter.menuName(use.classification))"
                }
                if observation.cameraInUse { return "Camera in use" }
            }
            if let use = observation.micUses.first(where: { !$0.counted }) {
                return "Mic in use by \(MenuPresenter.menuName(use.classification)) · not counted"
            }
            if observation.cameraInUse, !observation.cameraCounted {
                return "Camera in use · not counted"
            }
            return "Not on a call"
        }

        var pausedLine: String {
            switch state.settings.pause {
            case .until(let end): return "Paused until \(clock(end))"
            case .restOfToday: return "Paused for the rest of today"
            case .untilResumed, .notPaused: return "Paused"
            }
        }

        /// **Proposed copy** (the UX spec has none yet): a monitor that
        /// couldn't start, so calls may not be seen.
        var problemLine: String? {
            let problems = state.detectionProblems
            if problems.contains(.mic) && problems.contains(.camera) { return "Can\u{2019}t tell when the mic or camera is in use" }
            if problems.contains(.mic) { return "Can\u{2019}t tell when the mic is in use" }
            if problems.contains(.camera) { return "Can\u{2019}t tell when the camera is in use" }
            return nil
        }

        // MARK: Line 2 (4.3)

        var line2: String? {
            let name = barName
            switch connection.phase {
            case .notSetUp:
                // Not set up has no line 2, unless line 1 is busy saying
                // something else (paused, or a monitor that didn't start).
                return paused || state.testCall != nil || problemLine != nil ? "Not set up yet" : nil
            case .looking:
                return "Looking for MiniBar…"
            case .connected:
                if ignoringCalls { return "\(name) is ignoring calls from your Mac" }
                if onCall, let reply = connection.lastReply {
                    if reply.call.aside { return "Set aside on \(name) for this call" }
                    if reply.screen == .dark { return "\(name) · screen off" }
                    if reply.showing == .setup { return "\(name) is setting up Wi-Fi" }
                }
                switch connection.link {
                case .usb?: return "\(name) · USB"
                case .wifi?: return connection.usbPaused ? "\(name) · Wi-Fi · USB paused" : "\(name) · Wi-Fi"
                case nil: return name
                }
            case .unreachable:
                if let last = connection.lastSuccess { return "Can\u{2019}t reach \(name) since \(clock(last))" }
                return "Can\u{2019}t reach \(name)"
            case .notPluggedIn:
                return "\(name) isn\u{2019}t plugged in"
            case .localNetworkBlocked:
                return "Wi-Fi is blocked in Privacy settings"
            case .wifiCantReachHere:
                return "Wi-Fi can\u{2019}t reach \(name) here · plug it in"
            case .unrecognized:
                return "\(name) doesn\u{2019}t recognize this Mac"
            case .barNeedsUpdate:
                return "\(name) needs a firmware update"
            case .appNeedsUpdate:
                return "This app needs an update for \(name)"
            }
        }

        // MARK: The icon (3.1, 3.2)

        var icon: IconState {
            switch connection.phase {
            case .localNetworkBlocked, .unrecognized, .barNeedsUpdate, .appNeedsUpdate:
                return .needsYou
            default:
                break
            }
            if !state.detectionProblems.isEmpty { return .needsYou }
            if paused || ignoringCalls { return .paused }
            switch connection.phase {
            case .notSetUp, .unreachable, .notPluggedIn, .wifiCantReachHere:
                return .notConnected
            case .looking, .connected, .localNetworkBlocked, .unrecognized, .barNeedsUpdate, .appNeedsUpdate:
                break
            }
            return onCall ? .onCall : .connected
        }

        /// Line 1, or line 2 when that's the one saying what's wrong (3.1).
        func tooltip(icon: IconState, line1: String, line2: String?) -> String {
            switch icon {
            case .needsYou:
                if !state.detectionProblems.isEmpty, let problem = problemLine { return problem }
                return line2 ?? line1
            case .notConnected:
                return isSetUp ? (line2 ?? line1) : line1
            case .paused:
                return paused ? line1 : (line2 ?? line1)
            case .onCall:
                return line1
            case .connected:
                return line1 == "Not on a call" ? "\(barName) · not on a call" : line1
            }
        }

        // MARK: Items (4.4, 4.5)

        var fixItem: FixItem? {
            switch connection.phase {
            case .notSetUp, .unreachable, .notPluggedIn, .wifiCantReachHere:
                return .connect
            case .localNetworkBlocked:
                return .openLocalNetworkSettings
            case .unrecognized:
                return .pairAgain
            case .connected:
                guard ignoringCalls, let url = remotePageURL else { return nil }
                return .openRemote(url)
            case .looking, .barNeedsUpdate, .appNeedsUpdate:
                return nil
            }
        }

        /// The bar's Remote page: the typed address, else the last one that
        /// worked, else its mDNS name.
        var remotePageURL: URL? {
            if let manual = state.settings.manualEndpoint { return manual.remotePageURL }
            let bar = connection.bar ?? state.settings.bar
            if let last = bar?.lastEndpoint { return last.remotePageURL }
            if let host = bar?.host ?? connection.info?.host, !host.isEmpty {
                return BarEndpoint(host: host).remotePageURL
            }
            return nil
        }

        var countingItems: [CountingItem] {
            guard !paused else { return [] }
            let observation = state.observation
            var items: [CountingItem] = []
            let counted = observation.micUses.filter(\.counted).map {
                CountedApp(identity: $0.classification.identity, name: MenuPresenter.menuName($0.classification))
            }
            if counted.count == 1 {
                items.append(.dontCount(counted[0]))
            } else if counted.count > 1 {
                items.append(.dontCountSubmenu(counted))
            }
            if let edit = state.undo.app,
               observation.micUses.contains(where: { $0.classification.identity.key == edit.app.key }) {
                items.append(.countAgain(CountedApp(identity: edit.app, name: MenuPresenter.undoName(edit))))
            }
            if let call = state.call, call.identity == nil, observation.cameraCounted {
                items.append(.dontCountCamera)
            }
            if state.undo.camera, observation.cameraInUse {
                items.append(.countCameraAgain)
            }
            return items
        }

        // MARK: Option-click details (4.8)

        var details: [String] {
            var lines: [String] = []
            if let endpoint = connection.endpointDescription {
                lines.append(connection.link == .usb ? "Port: \(endpoint)" : "Address: \(endpoint)")
            }
            if let info = connection.info {
                lines.append("Firmware \(info.fw) · API \(info.api)")
            }
            if let last = connection.lastSuccess {
                lines.append("Last reply \(clock(last, seconds: true))")
            }
            if state.monitorProblems.contains(.usb) {
                lines.append("USB devices can\u{2019}t be watched")
            }
            return lines
        }
    }

    /// The name the menu uses for an app: a call app's short name ("Zoom"),
    /// else its own name ("GarageBand"). Matches line 1's "On a call · Zoom".
    static func menuName(_ classification: AppClassification) -> String {
        if case .callApp(let shortName) = classification.category { return shortName }
        return classification.identity.displayName
    }

    /// The name for "Count … Again": what the app was called before "Don’t
    /// Count" moved it.
    static func undoName(_ edit: CatalogEdit) -> String {
        if case .callApp(let shortName)? = edit.catalogBefore.category(ofKey: edit.app.key) { return shortName }
        return edit.app.displayName
    }
}

/// Settings › Connection's top section (mac-app-ux.md 6.4).
public struct ConnectionSummary: Hashable, Sendable {
    public enum Button: Hashable, Sendable {
        case connect, sendTestCall, forget, pairAgain
    }

    /// "MiniBar 2A1C", or nil when not set up.
    public var barName: String?
    /// "Connected over USB", "Can’t reach it since 2:04 PM"…
    public var status: String
    /// ● when connected, ○ when not.
    public var isConnected: Bool
    public var buttons: [Button]
    /// Why Send Test Call is dimmed: "Not available during a call." or
    /// "Connect MiniBar first.", else nil.
    public var testCallUnavailableReason: String?

    public init(barName: String?, status: String, isConnected: Bool, buttons: [Button], testCallUnavailableReason: String?) {
        self.barName = barName
        self.status = status
        self.isConnected = isConnected
        self.buttons = buttons
        self.testCallUnavailableReason = testCallUnavailableReason
    }
}

/// Durations, times and waits in the bar's style (mac-app-ux.md 2).
public enum Formatting {
    /// A call's length, as the bar writes it: minutes rounded up, so a call
    /// shows "1m" from its first second; "12m", "1h 5m", and from 10 hours
    /// on hours only ("10h"), as the mock-up's `hm(minsUp(…))`.
    public static func duration(seconds: Int) -> String {
        let minutes = max(1, (max(seconds, 0) + 59) / 60)
        if minutes >= 600 { return "\(minutes / 60)h" }
        if minutes >= 60 { return "\(minutes / 60)h \(minutes % 60)m" }
        return "\(minutes)m"
    }

    /// A clock time: in the bar's Time format when the bar said it (`timeFormat`, from `info`): "2:04 PM" for
    /// `"12h"`, "14:04" for `"24h"` (hour zero-padded, no AM or PM, as the bar writes it), whatever the Mac's own
    /// setting is, so the menu and the bar never disagree. Without it (older firmware, or a value this app doesn't
    /// know) the Mac's own format: "2:04 PM", or "14:04" on a 24-hour Mac. With `withSeconds`, "2:31:04 PM".
    public static func clockTime(_ date: Date, timeZone: TimeZone, locale: Locale = .autoupdatingCurrent,
                                 withSeconds: Bool = false, timeFormat: TimeFormat? = nil) -> String {
        let formatter = DateFormatter()
        formatter.timeZone = timeZone
        formatter.dateStyle = .none
        if timeFormat == .h24 || timeFormat == .h12 {
            // The bar's pattern, on a fixed locale so the Mac's region can't change it (digits, AM and PM symbols).
            formatter.locale = Locale(identifier: "en_US_POSIX")
            let seconds = withSeconds ? ":ss" : ""
            formatter.dateFormat = timeFormat == .h24 ? "HH:mm\(seconds)" : "h:mm\(seconds) a"
        } else {
            formatter.locale = locale
            formatter.timeStyle = withSeconds ? .medium : .short
        }
        return formatter.string(from: date)
    }

    /// A wait, rounded up: "45 seconds" under a minute, else "4 minutes";
    /// "1 second", "1 minute" in the singular (mac-app-ux.md 5.5).
    public static func wait(seconds: Int) -> String {
        let seconds = max(seconds, 1)
        if seconds < 60 { return seconds == 1 ? "1 second" : "\(seconds) seconds" }
        let minutes = (seconds + 59) / 60
        return minutes == 1 ? "1 minute" : "\(minutes) minutes"
    }
}
