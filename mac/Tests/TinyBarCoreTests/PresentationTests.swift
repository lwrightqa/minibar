import Foundation
import XCTest
@testable import TinyBarCore

// What the menu, the icon and Settings › Connection say: `MenuPresenter`,
// `Formatting` and the messages (mac-app-ux.md 2 to 6, criteria 17, 25, 26).
//
// The other acceptance criteria the core can check on Linux are in
// DetectionTests (1 to 11, pause choices), ConnectionTests, USBLinkTests,
// HTTPTransportTests and LinkPolicyTests (13 to 15, 17 to 21, 23, 24, 28),
// PairingTests and SettingsTests (22, 24, 27, 32, the pairing code, name
// rules), and the QA*Tests files (edge cases).

/// 2026-10-04 2:12 PM in Los Angeles.
private let t0 = ManualClock.apiExampleStart
private let la = TimeZone(identifier: "America/Los_Angeles")!
private let enUS = Locale(identifier: "en_US")
private let barName = "MiniBar 2A1C"

/// The menu as text, the way mac-app-ux.md 4.7 draws it.
private struct MenuText: Equatable, CustomStringConvertible {
    var icon: IconState
    var tooltip: String
    var lines: [String]
    var actions: [String]
    var pause: String

    init(icon: IconState, tooltip: String, lines: [String], actions: [String] = [], pause: String = "Pause Detection ▸") {
        self.icon = icon
        self.tooltip = tooltip
        self.lines = lines
        self.actions = actions
        self.pause = pause
    }

    init(_ content: MenuContent) {
        icon = content.icon
        tooltip = plain(content.tooltip)
        lines = ([content.line1] + (content.line2.map { [$0] } ?? [])).map(plain)
        actions = (content.fixItem.map { [$0.title] } ?? []) + content.countingItems.map { item in
            if case .dontCountSubmenu = item { return item.title + " ▸" }
            return item.title
        }
        pause = content.pauseItem == .resume ? "Resume Detection" : "Pause Detection ▸"
    }

    var description: String {
        "\(icon) “\(tooltip)” \(lines) \(actions) \(pause)"
    }
}

/// States to show, built the way the engine fills them in.
private enum Make {
    static func settings(paired: Bool = true, pause: PauseState = .notPaused) -> AppSettings {
        var settings = AppSettings(installID: ConnectionRig.client)
        if paired {
            settings.bar = bar()
        }
        settings.pause = pause
        return settings
    }

    static func bar(auth: AuthMode = .bearer) -> KnownBar {
        KnownBar(deviceID: "f412fa3f2a1c", name: barName, host: "minibar.local", auth: auth, tokenID: "74d8a526")
    }

    static func connection(_ phase: ConnectionState.Phase, link: LinkKind? = .usb, reply: CallReply? = nil,
                           lastSuccess: Date? = nil, auth: AuthMode = .bearer) -> ConnectionState {
        ConnectionState(phase: phase, link: phase == .connected ? link : nil,
                        bar: phase == .notSetUp ? nil : bar(auth: auth),
                        lastReply: reply, lastSuccess: lastSuccess)
    }

    static func reply(active: Bool, aside: Bool = false, screen: ScreenState = .on, showing: Showing? = nil,
                      mac: Bool = true) -> CallReply {
        CallReply(deviceID: "f412fa3f2a1c", showing: showing ?? (active && mac && !aside ? .call : .own), screen: screen,
                  call: BarCall(active: active, app: active ? "Slack" : nil, aside: aside),
                  sources: SourceSwitches(calendar: true, mac: mac))
    }

    static func use(_ process: MicProcess, counted: Bool = true, since: Date = t0,
                    catalog: AppCatalog = .defaults) -> MicUse {
        MicUse(classification: catalog.classify(process), counted: counted, since: since)
    }

    /// A call from a call app that started `minutes` before `t0`.
    static func call(_ process: MicProcess, minutes: Int) -> (DetectedCall, MacObservation) {
        let use = use(process)
        guard case .callApp(let short) = use.classification.category else { fatalError("not a call app") }
        let call = DetectedCall(callID: 1, startedAt: t0.addingTimeInterval(-Double(minutes * 60)), app: short,
                                localName: short, identity: use.classification.identity)
        return (call, MacObservation(micUses: [use]))
    }

    static func state(_ connection: ConnectionState, settings: AppSettings? = nil, call: DetectedCall? = nil,
                      observation: MacObservation = .idle) -> EngineState {
        EngineState(settings: settings ?? Make.settings(paired: connection.phase != .notSetUp), observation: observation,
                    call: call, connection: connection)
    }
}

private func menu(_ state: EngineState, at now: Date = t0, offset: TimeInterval = 0, details: Bool = false) -> MenuContent {
    MenuPresenter.content(for: state, now: now, timeZone: la, locale: enUS, clockOffset: offset, showDetails: details)
}

private func text(_ state: EngineState, at now: Date = t0) -> MenuText {
    MenuText(menu(state, at: now))
}

final class PresentationTests: XCTestCase {
    // MARK: - mac-app-ux.md 4.7, each menu

    func test_menusOfUX47() {
        // Not set up yet.
        XCTAssertEqual(text(Make.state(Make.connection(.notSetUp))),
                       MenuText(icon: .notConnected, tooltip: "Not set up yet", lines: ["Not set up yet"],
                                actions: ["Connect…"]))

        // Connected, not on a call.
        XCTAssertEqual(text(Make.state(Make.connection(.connected, reply: Make.reply(active: false)))),
                       MenuText(icon: .connected, tooltip: "MiniBar 2A1C · not on a call",
                                lines: ["Not on a call", "MiniBar 2A1C · USB"]))

        // On a call.
        let (slack12, slackUse) = Make.call(Proc.slack, minutes: 12)
        XCTAssertEqual(text(Make.state(Make.connection(.connected, link: .wifi, reply: Make.reply(active: true)),
                                       call: slack12, observation: slackUse)),
                       MenuText(icon: .onCall, tooltip: "On a call · Slack · 12m",
                                lines: ["On a call · Slack · 12m", "MiniBar 2A1C · Wi-Fi"],
                                actions: ["Don\u{2019}t Count Slack"]))

        // On a call, set aside on the bar.
        XCTAssertEqual(text(Make.state(Make.connection(.connected, link: .wifi, reply: Make.reply(active: true, aside: true)),
                                       call: slack12, observation: slackUse)),
                       MenuText(icon: .onCall, tooltip: "On a call · Slack · 12m",
                                lines: ["On a call · Slack · 12m", "Set aside on MiniBar 2A1C for this call"],
                                actions: ["Don\u{2019}t Count Slack"]))

        // On a call, bar can't be reached: Zoom by its short name.
        let (zoom3, zoomUse) = Make.call(Proc.zoom, minutes: 3)
        let lost = Make.connection(.unreachable, lastSuccess: t0.addingTimeInterval(-8 * 60))
        XCTAssertEqual(text(Make.state(lost, call: zoom3, observation: zoomUse)),
                       MenuText(icon: .notConnected, tooltip: "Can\u{2019}t reach MiniBar 2A1C since 2:04 PM",
                                lines: ["On a call · Zoom · 3m", "Can\u{2019}t reach MiniBar 2A1C since 2:04 PM"],
                                actions: ["Connect…", "Don\u{2019}t Count Zoom"]))

        // Paused until 3:15 PM.
        let paused = Make.settings(pause: .until(t0.addingTimeInterval(63 * 60)))
        XCTAssertEqual(text(Make.state(Make.connection(.connected, reply: Make.reply(active: false)), settings: paused)),
                       MenuText(icon: .paused, tooltip: "Paused until 3:15 PM",
                                lines: ["Paused until 3:15 PM", "MiniBar 2A1C · USB"], pause: "Resume Detection"))

        // Needs you: Local Network blocked.
        XCTAssertEqual(text(Make.state(Make.connection(.localNetworkBlocked))),
                       MenuText(icon: .needsYou, tooltip: "Wi-Fi is blocked in Privacy settings",
                                lines: ["Not on a call", "Wi-Fi is blocked in Privacy settings"],
                                actions: ["Open Local Network Settings…"]))

        // The bar is ignoring calls from your Mac.
        let (slack5, _) = Make.call(Proc.slack, minutes: 5)
        let ignoring = Make.state(Make.connection(.connected, link: .wifi, reply: Make.reply(active: true, mac: false)),
                                  call: slack5, observation: slackUse)
        XCTAssertEqual(text(ignoring),
                       MenuText(icon: .paused, tooltip: "MiniBar 2A1C is ignoring calls from your Mac",
                                lines: ["On a call · Slack · 5m", "MiniBar 2A1C is ignoring calls from your Mac"],
                                actions: ["Open MiniBar Remote…", "Don\u{2019}t Count Slack"]))
        XCTAssertEqual(menu(ignoring).fixItem, .openRemote(URL(string: "http://minibar.local/")!))
    }

    // MARK: - Line 1 (4.2)

    func test_line1() {
        let connected = Make.connection(.connected, reply: Make.reply(active: false))
        func line1(_ observation: MacObservation, settings: AppSettings? = nil, call: DetectedCall? = nil) -> String {
            plain(menu(Make.state(connected, settings: settings, call: call, observation: observation)).line1)
        }
        XCTAssertEqual(line1(MacObservation(micUses: [Make.use(Proc.slack)], pendingSince: t0)), "Mic in use by Slack")
        XCTAssertEqual(line1(MacObservation(micUses: [Make.use(Proc.zoom)], pendingSince: t0)), "Mic in use by Zoom",
                       "a call app by its short name")
        XCTAssertEqual(line1(MacObservation(micUses: [Make.use(Proc.voiceMemos, counted: false)])),
                       "Mic in use by Voice Memos · not counted")
        XCTAssertEqual(line1(MacObservation(micUses: [Make.use(Proc.garageBand, counted: false)])),
                       "Mic in use by GarageBand · not counted")
        XCTAssertEqual(line1(MacObservation(cameraInUse: true, cameraCounted: false)), "Camera in use · not counted")
        XCTAssertEqual(line1(MacObservation(cameraInUse: true, cameraCounted: true, pendingSince: t0)), "Camera in use")
        let garage = Make.use(Proc.garageBand)
        let garageCall = DetectedCall(callID: 2, startedAt: t0.addingTimeInterval(-720), app: nil, localName: "GarageBand",
                                      identity: garage.classification.identity)
        XCTAssertEqual(line1(MacObservation(micUses: [garage]), call: garageCall), "On a call · GarageBand · 12m")
        let camera = DetectedCall(callID: 3, startedAt: t0.addingTimeInterval(-720), app: nil, localName: "Camera", identity: nil)
        XCTAssertEqual(line1(MacObservation(cameraInUse: true, cameraCounted: true), call: camera), "On a call · Camera · 12m")
        XCTAssertEqual(line1(.idle, settings: Make.settings(pause: .restOfToday(endsAt: t0.addingTimeInterval(3600)))),
                       "Paused for the rest of today")
        XCTAssertEqual(line1(MacObservation(micUses: [Make.use(Proc.slack)], pendingSince: t0),
                             settings: Make.settings(pause: .untilResumed)), "Paused", "only that it's paused")
        var test = Make.state(connected)
        test.testCall = TestCall(startedAt: t0, callID: 4)
        XCTAssertEqual(menu(test).line1, "Sending a test call")
        XCTAssertEqual(menu(test).icon, .onCall, "a test call shows On a call (3.2)")
    }

    // MARK: - Line 2 (4.3)

    func test_line2() {
        let (call, use) = Make.call(Proc.slack, minutes: 1)
        func line2(_ connection: ConnectionState, onCall: Bool = true, settings: AppSettings? = nil) -> String? {
            menu(Make.state(connection, settings: settings, call: onCall ? call : nil, observation: onCall ? use : .idle))
                .line2.map(plain)
        }
        var usbPaused = Make.connection(.connected, link: .wifi, reply: Make.reply(active: true))
        usbPaused.usbPaused = true
        XCTAssertEqual(line2(usbPaused), "MiniBar 2A1C · Wi-Fi · USB paused")
        XCTAssertEqual(line2(Make.connection(.connected, reply: Make.reply(active: true, screen: .dark))),
                       "MiniBar 2A1C · screen off")
        XCTAssertEqual(line2(Make.connection(.connected, reply: Make.reply(active: true, showing: .setup))),
                       "MiniBar 2A1C is setting up Wi-Fi")
        XCTAssertEqual(line2(Make.connection(.connected, reply: Make.reply(active: true, aside: true)), onCall: false),
                       "MiniBar 2A1C · USB", "set aside, screen off and setup show only during a call")
        XCTAssertEqual(line2(Make.connection(.looking)), "Looking for MiniBar…")
        XCTAssertEqual(line2(Make.connection(.unreachable)), "Can\u{2019}t reach MiniBar 2A1C")
        XCTAssertEqual(line2(Make.connection(.notPluggedIn)), "MiniBar 2A1C isn\u{2019}t plugged in")
        XCTAssertEqual(line2(Make.connection(.wifiCantReachHere)), "Wi-Fi can\u{2019}t reach MiniBar 2A1C here · plug it in")
        XCTAssertEqual(line2(Make.connection(.unrecognized)), "MiniBar 2A1C doesn\u{2019}t recognize this Mac")
        XCTAssertEqual(line2(Make.connection(.barNeedsUpdate)), "MiniBar 2A1C needs a firmware update")
        XCTAssertEqual(line2(Make.connection(.appNeedsUpdate)), "This app needs an update for MiniBar 2A1C")
        XCTAssertNil(line2(Make.connection(.notSetUp), onCall: false))
        XCTAssertEqual(line2(Make.connection(.notSetUp), onCall: false, settings: Make.settings(paired: false, pause: .untilResumed)),
                       "Not set up yet", "paused and not set up: line 2 says so")

        // Fix items (4.4).
        func fix(_ phase: ConnectionState.Phase) -> FixItem? { menu(Make.state(Make.connection(phase))).fixItem }
        XCTAssertEqual(fix(.notPluggedIn), .connect)
        XCTAssertEqual(fix(.wifiCantReachHere), .connect)
        XCTAssertEqual(fix(.unrecognized), .pairAgain)
        XCTAssertNil(fix(.barNeedsUpdate))
        XCTAssertNil(fix(.appNeedsUpdate))
        XCTAssertNil(fix(.looking))
    }

    // MARK: - The icon (3.1, 3.2)

    func test_iconPrecedence() {
        let (call, use) = Make.call(Proc.slack, minutes: 1)
        func icon(_ phase: ConnectionState.Phase, paused: Bool = false, onCall: Bool = false,
                  reply: CallReply? = nil) -> IconState {
            let settings = Make.settings(pause: paused ? .untilResumed : .notPaused)
            return menu(Make.state(Make.connection(phase, reply: reply), settings: settings,
                                   call: onCall ? call : nil, observation: onCall ? use : .idle)).icon
        }
        // Needs you > Paused > Not connected > On a call > Connected.
        XCTAssertEqual(icon(.unrecognized, paused: true), .needsYou)
        XCTAssertEqual(icon(.barNeedsUpdate, onCall: true), .needsYou)
        XCTAssertEqual(icon(.appNeedsUpdate), .needsYou)
        XCTAssertEqual(icon(.unreachable, paused: true), .paused)
        XCTAssertEqual(icon(.connected, onCall: true, reply: Make.reply(active: true, mac: false)), .paused,
                       "sources.mac false shows as a pause")
        XCTAssertEqual(icon(.unreachable, onCall: true), .notConnected, "never claims a call the bar can't show")
        XCTAssertEqual(icon(.notPluggedIn, onCall: true), .notConnected)
        XCTAssertEqual(icon(.connected, onCall: true, reply: Make.reply(active: true)), .onCall)
        XCTAssertEqual(icon(.connected, onCall: true, reply: Make.reply(active: true, aside: true)), .onCall,
                       "set aside: the bar knows about the call")
        XCTAssertEqual(icon(.connected, reply: Make.reply(active: false)), .connected)
        // The 15-second grace: Looking shows Connected (or On a call).
        XCTAssertEqual(icon(.looking), .connected)
        XCTAssertEqual(icon(.looking, onCall: true), .onCall)
        XCTAssertEqual(text(Make.state(Make.connection(.looking))).tooltip, "MiniBar 2A1C · not on a call")
        // Before any bar is set up: Not connected from the start.
        XCTAssertEqual(icon(.notSetUp), .notConnected)

        // Labels and symbols (3.1).
        XCTAssertEqual(IconState.allCases.map(\.symbolName),
                       ["exclamationmark.triangle", "pause.rectangle", "rectangle.slash", "rectangle.fill", "rectangle"])
        XCTAssertEqual(IconState.onCall.accessibilityLabel, "MiniBar, on a call")
    }

    /// A monitor that couldn't start: Needs you, with the reason in line 1.
    func test_monitorProblems() {
        var state = Make.state(Make.connection(.connected, reply: Make.reply(active: false)))
        state.monitorProblems = [.mic]
        var content = menu(state)
        XCTAssertEqual(content.icon, .needsYou)
        XCTAssertEqual(content.line1, "Can\u{2019}t tell when the mic is in use")
        XCTAssertEqual(content.tooltip, "Can\u{2019}t tell when the mic is in use")
        state.monitorProblems = [.camera]
        XCTAssertEqual(menu(state).line1, "Can\u{2019}t tell when the camera is in use")
        state.settings.detection.countCamera = false
        content = menu(state)
        XCTAssertEqual(content.icon, .connected, "the camera doesn't count, so it doesn't matter")
        XCTAssertEqual(content.line1, "Not on a call")
        state.monitorProblems = [.usb]
        content = menu(state, details: true)
        XCTAssertEqual(content.icon, .connected)
        XCTAssertTrue(content.details.contains("USB devices can\u{2019}t be watched"))
    }

    // MARK: - Counting items (4.5, criterion 26)

    func test_C26_dontCountAndUndo() {
        let connected = Make.connection(.connected, reply: Make.reply(active: false))
        // Only while a counted app uses the mic.
        XCTAssertEqual(text(Make.state(connected, observation: MacObservation(micUses: [Make.use(Proc.voiceMemos, counted: false)]))).actions, [])
        let two = MacObservation(micUses: [Make.use(Proc.zoom, since: t0.addingTimeInterval(5)), Make.use(Proc.garageBand)],
                                 pendingSince: t0)
        let content = menu(Make.state(connected, observation: two))
        XCTAssertEqual(MenuText(content).actions, ["Don\u{2019}t Count ▸"])
        guard case .dontCountSubmenu(let apps) = content.countingItems.first else { return XCTFail("\(content.countingItems)") }
        XCTAssertEqual(apps.map(\.name), ["Zoom", "GarageBand"], "short names for call apps, most recent first")
        XCTAssertEqual(apps.map(\.identity.key), ["zoom", "com.apple.garageband10"])
    }

    func test_C26_dontCountThroughTheEngine() async throws {
        try await PresentationScenarios().dontCountAndUndo()
    }

    func test_C26_cameraThroughTheEngine() async throws {
        try await PresentationScenarios().camera()
    }

    // MARK: - Option-click details (4.8)

    func test_details() {
        var connection = Make.connection(.connected, link: .wifi, reply: Make.reply(active: false),
                                         lastSuccess: t0.addingTimeInterval(-29))
        connection.info = InfoReply(deviceID: "f412fa3f2a1c", name: barName, fw: "1.0.0")
        connection.endpointDescription = "minibar.local"
        connection.usbSeen = true
        let content = menu(Make.state(connection), details: true)
        XCTAssertEqual(content.details.map(plain), ["Address: minibar.local", "Firmware 1.0.0 · API 1.0", "Last reply 2:11:31 PM"])
        XCTAssertTrue(content.showsUSBToggle)
        XCTAssertFalse(menu(Make.state(connection), details: false).showsUSBToggle)
        XCTAssertEqual(menu(Make.state(connection), details: false).details, [])
        connection.link = .usb
        connection.endpointDescription = "cu.usbmodem1101"
        XCTAssertEqual(menu(Make.state(connection), details: true).details.first, "Port: cu.usbmodem1101")
    }

    /// Clock times are shown on the Mac's clock (`clockOffset`), and in its
    /// format (12- or 24-hour).
    func test_clockTimesFollowTheMacsClockAndFormat() {
        let paused = Make.state(Make.connection(.connected, reply: Make.reply(active: false)),
                                settings: Make.settings(pause: .until(t0.addingTimeInterval(63 * 60))))
        XCTAssertEqual(plain(menu(paused, offset: -3600).line1), "Paused until 2:15 PM")
        let content = MenuPresenter.content(for: paused, now: t0, timeZone: la, locale: Locale(identifier: "en_GB"),
                                            showDetails: false)
        XCTAssertEqual(content.line1, "Paused until 15:15")
    }

    /// The bar's Time format (info.time_format, api.md 7.1) wins over the Mac's own: the menu and the bar never
    /// disagree (decisions.md "Time format (2026-10-07)"). The pattern is the bar's: hour zero-padded in 24-hour, no
    /// AM or PM there; "2:15 PM" in 12-hour even on a 24-hour Mac.
    func test_clockTimesFollowTheBarsTimeFormat() {
        let paused = Make.settings(pause: .until(t0.addingTimeInterval(63 * 60)))
        func line(_ format: TimeFormat?, locale: String) -> String {
            var connection = Make.connection(.connected, reply: Make.reply(active: false))
            connection.info = InfoReply(deviceID: "f412fa3f2a1c", name: barName, fw: "1.0.9", timeFormat: format)
            return plain(MenuPresenter.content(for: Make.state(connection, settings: paused), now: t0, timeZone: la,
                                               locale: Locale(identifier: locale), showDetails: false).line1)
        }
        XCTAssertEqual(line(.h24, locale: "en_US"), "Paused until 15:15")
        XCTAssertEqual(line(.h12, locale: "en_GB"), "Paused until 3:15 PM")
        XCTAssertEqual(line(.h12, locale: "en_US"), "Paused until 3:15 PM")
        XCTAssertEqual(line(.h24, locale: "en_GB"), "Paused until 15:15")
        // an older bar, or a value this app doesn't know: the Mac's own format, as before
        XCTAssertEqual(line(nil, locale: "en_US"), "Paused until 3:15 PM")
        XCTAssertEqual(line(nil, locale: "en_GB"), "Paused until 15:15")
        XCTAssertEqual(line(TimeFormat(rawValue: "later"), locale: "en_GB"), "Paused until 15:15")
        // the same in the connection summary's "Can't reach it since ..."
        var connection = Make.connection(.unreachable, lastSuccess: t0.addingTimeInterval(-8 * 60))
        connection.info = InfoReply(deviceID: "f412fa3f2a1c", name: barName, fw: "1.0.9", timeFormat: .h24)
        let summary = MenuPresenter.connectionSummary(for: Make.state(connection), now: t0, timeZone: la, locale: enUS)
        XCTAssertEqual(plain(summary.status), "Can\u{2019}t reach it since 14:04")
    }

    func test_infoDecodesTheTimeFormat() throws {
        func info(_ extra: String) throws -> InfoReply {
            let json = #"{"ok": true, "device": "MiniBar", "device_id": "f412fa3f2a1c", "name": "MiniBar 2A1C", "fw": "1.0.9", "api": "1.0", "host": "minibar.local", "auth": "bearer", "pairing": "idle", "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:11:58-07:00", "time_source": "ntp", "wifi": "connected"\#(extra)}"#
            return try JSONDecoder().decode(InfoReply.self, from: Data(json.utf8))
        }
        XCTAssertEqual(try info(#", "time_format": "24h""#).timeFormat, .h24)
        XCTAssertEqual(try info(#", "time_format": "12h""#).timeFormat, .h12)
        XCTAssertNil(try info("").timeFormat)                                       // firmware before 1.0.9
        XCTAssertEqual(try info(#", "time_format": "later""#).timeFormat, TimeFormat(rawValue: "later"))   // open field
    }

    func test_clockTimeFormats() {
        let midnight = Calendar(identifier: .gregorian).date(from: DateComponents(timeZone: la, year: 2026, month: 10,
                                                                                    day: 5, hour: 0, minute: 15, second: 7))!
        let morning = midnight.addingTimeInterval(8 * 3600 + 40 * 60)      // 08:55:07
        XCTAssertEqual(Formatting.clockTime(midnight, timeZone: la, locale: enUS, timeFormat: .h24), "00:15")
        XCTAssertEqual(Formatting.clockTime(midnight, timeZone: la, locale: enUS, timeFormat: .h12), "12:15 AM")
        XCTAssertEqual(Formatting.clockTime(morning, timeZone: la, locale: enUS, timeFormat: .h24), "08:55")
        XCTAssertEqual(Formatting.clockTime(morning, timeZone: la, locale: enUS, timeFormat: .h12), "8:55 AM")
        XCTAssertEqual(Formatting.clockTime(morning, timeZone: la, locale: enUS, withSeconds: true, timeFormat: .h24), "08:55:07")
        XCTAssertEqual(Formatting.clockTime(t0, timeZone: la, locale: enUS, withSeconds: true, timeFormat: .h12), "2:12:00 PM")
        XCTAssertEqual(Formatting.clockTime(t0, timeZone: la, locale: Locale(identifier: "ar_SA"), timeFormat: .h24), "14:12")
    }

    // MARK: - Settings › Connection (6.4)

    func test_connectionSummary() {
        func summary(_ connection: ConnectionState, call: Bool = false) -> ConnectionSummary {
            let (detected, use) = Make.call(Proc.slack, minutes: 1)
            return MenuPresenter.connectionSummary(for: Make.state(connection, call: call ? detected : nil,
                                                                   observation: call ? use : .idle),
                                                   now: t0, timeZone: la, locale: enUS)
        }
        XCTAssertEqual(summary(Make.connection(.notSetUp)),
                       ConnectionSummary(barName: nil, status: "Not set up yet", isConnected: false, buttons: [.connect],
                                         testCallUnavailableReason: nil))
        XCTAssertEqual(summary(Make.connection(.connected, link: .usb)),
                       ConnectionSummary(barName: barName, status: "Connected over USB", isConnected: true,
                                         buttons: [.sendTestCall, .forget], testCallUnavailableReason: nil))
        XCTAssertEqual(summary(Make.connection(.connected, link: .wifi)).status, "Connected over Wi-Fi")
        XCTAssertEqual(summary(Make.connection(.connected, link: .usb), call: true).testCallUnavailableReason,
                       "Not available during a call.")
        let unreachable = summary(Make.connection(.unreachable, lastSuccess: t0.addingTimeInterval(-8 * 60)))
        XCTAssertEqual(plain(unreachable.status), "Can\u{2019}t reach it since 2:04 PM")
        XCTAssertFalse(unreachable.isConnected)
        XCTAssertEqual(unreachable.buttons, [.connect, .forget])
        XCTAssertEqual(summary(Make.connection(.unrecognized)),
                       ConnectionSummary(barName: barName, status: "It doesn\u{2019}t recognize this Mac", isConnected: false,
                                         buttons: [.pairAgain, .forget], testCallUnavailableReason: nil))
        XCTAssertEqual(summary(Make.connection(.connected, link: .wifi, auth: .notRequired)),
                       ConnectionSummary(barName: barName, status: "Connected over Wi-Fi · no pairing needed", isConnected: true,
                                         buttons: [.sendTestCall], testCallUnavailableReason: nil))
        XCTAssertEqual(summary(Make.connection(.looking)).testCallUnavailableReason, "Connect MiniBar first.")
    }

    // MARK: - Formatting and messages

    func test_formatting() {
        // As the bar writes them: minutes rounded up, so 1m from the start.
        XCTAssertEqual([0, 1, 59, 60, 61, 720, 3900, 36_000, 36_060].map(Formatting.duration),
                       ["1m", "1m", "1m", "1m", "2m", "12m", "1h 5m", "10h", "10h"])
        XCTAssertEqual(plain(Formatting.clockTime(t0, timeZone: la, locale: enUS)), "2:12 PM")
        XCTAssertEqual(plain(Formatting.clockTime(t0, timeZone: la, locale: enUS, withSeconds: true)), "2:12:00 PM")
        XCTAssertEqual(Formatting.clockTime(t0, timeZone: la, locale: Locale(identifier: "en_GB")), "14:12")
        XCTAssertEqual([0, 1, 2, 45, 59, 60, 61, 240].map(Formatting.wait),
                       ["1 second", "1 second", "2 seconds", "45 seconds", "59 seconds", "1 minute", "2 minutes", "4 minutes"])
    }

    func test_nameProblemMessages() {
        XCTAssertEqual(NameProblem.tooLong(limit: 24).message, "Use 24 characters or fewer.")
        XCTAssertEqual(NameRules.appNameProblem("Café")?.message, "MiniBar can\u{2019}t show \u{201C}é\u{201D}.")
        XCTAssertEqual(NameRules.appNameProblem("Zoë – Ünï")?.message,
                       "MiniBar can\u{2019}t show \u{201C}ë\u{201D}, \u{201C}–\u{201D} or \u{201C}Ü\u{201D}.")
        XCTAssertEqual(NameRules.macNameProblem("Alex\tMac")?.message, "MiniBar can\u{2019}t show U+0009.")
        XCTAssertEqual(NameProblem.empty.message, "Type a name.")
    }

    func test_pairingProblemMessages() {
        let name = barName
        XCTAssertEqual(PairingProblem.wrongCode(attemptsLeft: 2).message(barName: name), "That code didn\u{2019}t match. 2 tries left.")
        XCTAssertEqual(PairingProblem.wrongCode(attemptsLeft: 1).message(barName: name), "That code didn\u{2019}t match. 1 try left.")
        XCTAssertEqual(PairingProblem.busy(retryAfter: 45).message(barName: name),
                       "Someone else is pairing with this MiniBar. Try again in 45 seconds.")
        XCTAssertEqual(PairingProblem.rateLimited(retryAfter: 221).message(barName: name),
                       "Too many tries. You can try again in 4 minutes.")
        XCTAssertEqual(PairingProblem.tokenLimit.message(barName: name),
                       "MiniBar 2A1C already has 10 paired devices. Remove one on its Remote, then try again.")
        XCTAssertEqual(PairingProblem.inSetup.message(barName: name),
                       "MiniBar 2A1C is setting up Wi-Fi. Finish setup on the bar, then try again.")
        XCTAssertEqual(PairingProblem.noAnswer.message(barName: name), "MiniBar 2A1C didn\u{2019}t answer. Make sure it\u{2019}s on, then try again.")
        XCTAssertEqual(PairingProblem.nothingAt("10.0.4.42").message(barName: name), "Nothing answered at 10.0.4.42.")
        XCTAssertEqual(PairingProblem.notATinyBar.message(barName: name), "That address isn\u{2019}t a MiniBar.")
        XCTAssertEqual(PairingProblem.addressNotAllowed.message(barName: name), "Use MiniBar\u{2019}s .local name or its IP address.")
        XCTAssertEqual(PairingProblem.codeUsedUp.message(barName: name),
                       "That code didn\u{2019}t match, so MiniBar canceled pairing. Show a new code to try again.")
        XCTAssertEqual(PairingProblem.expired.message(barName: name),
                       "That code has expired or was canceled on MiniBar. Show a new code to try again.")
        XCTAssertEqual(PairingProblem.localNetworkBlocked.message(barName: name),
                       "macOS is blocking MiniBar from your local network, so Wi-Fi can\u{2019}t work.")
        XCTAssertTrue(PairingProblem.noneFound.message(barName: name).hasPrefix("Can\u{2019}t find a MiniBar on this network."))
        // No straight apostrophes anywhere in the app's copy (mac-app-ux.md 2).
        let all: [PairingProblem] = [.noneFound, .localNetworkBlocked, .nothingAt("x"), .notATinyBar, .addressNotAllowed,
                                     .wrongCode(attemptsLeft: 2), .codeUsedUp, .expired, .busy(retryAfter: 1),
                                     .rateLimited(retryAfter: 1), .busyForASecond, .tokenLimit, .inSetup, .noAnswer]
        XCTAssertFalse(all.contains { $0.message(barName: name).contains("'") })
    }

    /// Every menu string uses curly apostrophes.
    func test_noStraightApostrophesInTheMenu() {
        let (call, use) = Make.call(Proc.zoom, minutes: 3)
        var strings: [String] = []
        for phase in [ConnectionState.Phase.notSetUp, .looking, .connected, .unreachable, .notPluggedIn,
                      .localNetworkBlocked, .wifiCantReachHere, .unrecognized, .barNeedsUpdate, .appNeedsUpdate] {
            let content = menu(Make.state(Make.connection(phase, reply: Make.reply(active: true)), call: call, observation: use),
                               details: true)
            strings += [content.line1, content.tooltip] + (content.line2.map { [$0] } ?? []) + content.details
            strings += content.countingItems.map(\.title) + (content.fixItem.map { [$0.title] } ?? [])
        }
        XCTAssertFalse(strings.contains { $0.contains("'") }, "\(strings.filter { $0.contains("'") })")
    }
}

/// Through the engine, on the main actor like the app.
@MainActor
final class PresentationScenarios {
    func dontCountAndUndo() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        await rig.plugIn()
        await rig.micUse(Proc.zoom)
        await rig.run(for: 4)
        var content = rig.engine.menuContent(showDetails: false)
        XCTAssertEqual(content.line1, "On a call · Zoom · 1m")
        XCTAssertEqual(content.countingItems.map(\.title), ["Don\u{2019}t Count Zoom"])
        guard case .dontCount(let zoom) = content.countingItems.first else { return XCTFail("\(content.countingItems)") }
        rig.engine.dontCount(zoom.identity)
        await rig.settle()
        content = rig.engine.menuContent(showDetails: false)
        XCTAssertNil(rig.engine.state.call, "the call ends at once")
        XCTAssertEqual(rig.messageLog().last, "4.0 usb idle")
        XCTAssertEqual(content.line1, "Mic in use by zoom.us · not counted", "ignored: its own name, as in Ignored apps")
        XCTAssertEqual(content.countingItems.map(\.title), ["Count Zoom Again"])
        guard case .countAgain(let again) = content.countingItems.first else { return XCTFail() }
        rig.engine.countAgain(again.identity)
        await rig.settle()
        await rig.run(for: 3)
        XCTAssertEqual(rig.engine.state.call?.app, "Zoom", "counted again, a new call after the start delay")
        XCTAssertEqual(rig.engine.menuContent(showDetails: false).countingItems.map(\.title), ["Don\u{2019}t Count Zoom"])
        // Undo shows only while the app is still in use.
        rig.engine.dontCount(zoom.identity)
        await rig.micUse()
        XCTAssertEqual(rig.engine.menuContent(showDetails: false).countingItems, [])
        await rig.engine.shutdown()
    }

    func camera() async throws {
        let rig = EngineRig.paired()
        await rig.begin()
        await rig.plugIn()
        rig.camera.set(true)
        await rig.settle()
        await rig.run(for: 4)
        var content = rig.engine.menuContent(showDetails: false)
        XCTAssertEqual(content.line1, "On a call · Camera · 1m")
        XCTAssertEqual(content.countingItems, [.dontCountCamera])
        rig.engine.setCountCamera(false)
        await rig.settle()
        content = rig.engine.menuContent(showDetails: false)
        XCTAssertEqual(content.line1, "Camera in use · not counted")
        XCTAssertEqual(content.countingItems.map(\.title), ["Count the Camera Again"])
        rig.engine.setCountCamera(true)
        await rig.settle()
        await rig.run(for: 3)
        XCTAssertNotNil(rig.engine.state.call)
        await rig.engine.shutdown()
    }
}
