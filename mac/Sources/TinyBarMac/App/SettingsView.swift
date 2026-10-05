#if os(macOS)
import AppKit
import SwiftUI
import TinyBarCore
import UniformTypeIdentifiers

/// Settings: four tabs, each a grouped `Form` (`.formStyle(.grouped)`), as in
/// mac-app-ux.md 6. Switches apply at once; there's no Save button. Every
/// change goes through `engine.updateSettings` (validated by the core) or one
/// of the engine's actions, and the views redraw from `engine.state`.
///
/// - General (`gearshape`): Start MiniBar when you log in (with the reasons
///   from `LoginItemStatus` when macOS refuses), Start a call after / End a
///   call after (pop-ups with `DetectionSettings.startDelayPresets` and
///   `endDelayPresets`, plus the current value if it isn't one), Count the
///   camera (dimmed in "Only the call apps" mode), Send the app's name to
///   MiniBar. Restore Defaults.
/// - Apps (`square.grid.2x2`): Count calls from; Call apps table with an
///   editable "Shown on MiniBar as" column (`NameRules.appNameProblem`); Ignored
///   apps; Used the mic since MiniBar opened (`state.seenApps`). Restore
///   Defaults asks first. Add App… uses `NSOpenPanel` in /Applications and
///   reads the bundle ID with `Bundle(url:)`.
/// - Connection (`cable.connector`): `engine.connectionSummary()`, Send Test
///   Call, Forget This MiniBar… (confirmation alert), Use Wi-Fi when MiniBar
///   isn't plugged in, Advanced (Address, checked with `info`; Name for this
///   Mac; Pause USB).
/// - Privacy (`hand.raised`): the read-only text of mac-app-ux.md 6.5, plus the
///   Local Network line and button when `state.localNetworkBlocked`.
///
/// Labels with two `Text`s show the second as the help line under the
/// control, as System Settings does (macOS 14 grouped forms). *Unverified:*
/// how every tab looks, and the tables inside the grouped form on the Apps tab.
struct SettingsView: View {
    let engine: TinyBarEngine
    let tab: WindowCoordinator.SettingsTab
    let windows: WindowCoordinator

    var body: some View {
        switch tab {
        case .general:
            GeneralSettings(engine: engine)
        case .apps:
            AppsSettings(engine: engine)
        case .connection:
            ConnectionSettings(engine: engine, windows: windows)
        case .privacy:
            PrivacySettings(engine: engine)
        }
    }
}

// MARK: - General

private struct GeneralSettings: View {
    let engine: TinyBarEngine

    private var detection: DetectionSettings { engine.state.settings.detection }

    var body: some View {
        Form {
            Section {
                VStack(alignment: .leading, spacing: 6) {
                    Toggle("Start MiniBar when you log in", isOn: mainActorBinding(
                        get: { engine.state.loginItem == .enabled },
                        set: { engine.setLaunchAtLogin($0) }
                    ))
                    .toggleStyle(.switch)
                    if let help = LoginItemHelp(status: engine.state.loginItem) {
                        help.font(.callout)
                    }
                }
            }

            Section("Calls") {
                Picker(selection: mainActorBinding(
                    get: { detection.startDelay },
                    set: { value in engine.applySettings { $0.detection.startDelay = value } }
                )) {
                    ForEach(Self.choices(DetectionSettings.startDelayPresets, current: detection.startDelay), id: \.self) { seconds in
                        Text(Self.startDelayLabel(seconds)).tag(seconds)
                    }
                } label: {
                    Text("Start a call after")
                    Text("How long the mic or camera has to be in use first. Filters out apps that open the mic for a moment.")
                }

                Picker(selection: mainActorBinding(
                    get: { detection.endDelay },
                    set: { value in engine.applySettings { $0.detection.endDelay = value } }
                )) {
                    ForEach(Self.choices(DetectionSettings.endDelayPresets, current: detection.endDelay), id: \.self) { seconds in
                        Text(Self.seconds(seconds)).tag(seconds)
                    }
                } label: {
                    Text("End a call after")
                    Text("How long they have to be idle first. Bridges short gaps, like switching to AirPods.")
                }

                Toggle(isOn: mainActorBinding(
                    get: { detection.cameraCounts },
                    set: { engine.setCountCamera($0) }
                )) {
                    Text("Count the camera")
                    if detection.mode == .onlyCallApps {
                        Text("Not available with “Only the call apps on my list”, because macOS doesn’t say which app is using the camera.")
                    } else {
                        Text("The camera alone counts as a call, without an app name. Turn this off if a webcam app or Photo Booth shows you as on a call.")
                    }
                }
                .toggleStyle(.switch)
                .disabled(detection.mode == .onlyCallApps)

                Toggle(isOn: mainActorBinding(
                    get: { detection.sendAppName },
                    set: { value in engine.applySettings { $0.detection.sendAppName = value } }
                )) {
                    Text("Send the app’s name to MiniBar")
                    Text("MiniBar shows it, like “From your Mac · Slack”. Only apps on your call app list send a name, and anyone near your desk can read it.")
                }
                .toggleStyle(.switch)
            }

            RestoreDefaultsRow {
                let wasEnabled = engine.state.loginItem == .enabled
                engine.applySettings { $0.restoreGeneralDefaults() }
                if engine.state.settings.launchAtLogin != wasEnabled {
                    engine.setLaunchAtLogin(engine.state.settings.launchAtLogin)
                }
            }
        }
        .formStyle(.grouped)
    }

    /// The presets, plus `current` if it isn't one (a value set some other
    /// way; mac-app-ux.md 6.2).
    static func choices(_ presets: [Int], current: Int) -> [Int] {
        presets.contains(current) ? presets : (presets + [current]).sorted()
    }

    static func startDelayLabel(_ seconds: Int) -> String {
        seconds == 0 ? "Right away" : Self.seconds(seconds)
    }

    static func seconds(_ seconds: Int) -> String {
        seconds == 1 ? "1 second" : "\(seconds) seconds"
    }
}

// MARK: - Apps

private struct AppsSettings: View {
    let engine: TinyBarEngine

    @State private var selectedCallApp: CallApp.ID?
    @State private var selectedIgnoredApp: IgnoredApp.ID?
    @State private var nameProblem: String?
    @State private var confirmRestore = false
    @State private var pendingMove: PendingMove?
    @State private var cantTellApp = false

    /// An app chosen with + that's already on the other list.
    private struct PendingMove: Hashable {
        var app: AppPicker.PickedApp
        var toCallApps: Bool
    }

    private var settings: AppSettings { engine.state.settings }
    private var catalog: AppCatalog { settings.detection.catalog }

    var body: some View {
        Form {
            Section {
                Picker(selection: mainActorBinding(
                    get: { settings.detection.mode },
                    set: { mode in engine.applySettings { $0.detection.mode = mode } }
                )) {
                    Text("Any app, except ignored apps").tag(CountingMode.anyExceptIgnored)
                    Text("Only the call apps on my list").tag(CountingMode.onlyCallApps)
                } label: {
                    Text("Count calls from")
                    Text("Any app catches call apps that aren’t on your list. They show on MiniBar without a name.")
                }
                .pickerStyle(.radioGroup)
            }

            Section {
                VStack(alignment: .leading, spacing: 0) {
                    callAppsTable
                    ListEditButtons(
                        add: { addCallApp() },
                        remove: {
                            if let id = selectedCallApp {
                                engine.applySettings { $0.detection.catalog.removeCallApp(id: id) }
                                selectedCallApp = nil
                            }
                        },
                        canRemove: selectedCallApp != nil
                    )
                }
                if let nameProblem {
                    ErrorLine(message: nameProblem)
                        .font(.callout)
                }
            } header: {
                Text("Call apps")
            } footer: {
                Text("Calls from these apps show their name on MiniBar. Double-click a name to change it.")
                    .foregroundStyle(.secondary)
            }

            Section {
                VStack(alignment: .leading, spacing: 0) {
                    List(selection: $selectedIgnoredApp) {
                        ForEach(catalog.ignoredApps) { app in
                            HStack(spacing: 8) {
                                AppIconView(bundleID: app.matchers.firstBundleID, isSystem: app.isSystem)
                                Text(app.displayName)
                            }
                            .tag(app.id)
                            .contextMenu {
                                Button("Count \(app.displayName)") {
                                    engine.applySettings { $0.detection.catalog.removeIgnoredApp(id: app.id) }
                                }
                            }
                        }
                    }
                    .listStyle(.bordered(alternatesRowBackgrounds: false))
                    .frame(height: 110)
                    ListEditButtons(
                        add: { addIgnoredApp() },
                        remove: {
                            if let id = selectedIgnoredApp {
                                engine.applySettings { $0.detection.catalog.removeIgnoredApp(id: id) }
                                selectedIgnoredApp = nil
                            }
                        },
                        canRemove: selectedIgnoredApp != nil
                    )
                }
            } header: {
                Text("Ignored apps")
            } footer: {
                Text("These never count as a call, even while they use the mic.")
                    .foregroundStyle(.secondary)
            }

            Section {
                if engine.state.seenApps.isEmpty {
                    Text("No apps have used the mic since MiniBar opened.")
                        .foregroundStyle(.secondary)
                } else {
                    ForEach(engine.state.seenApps, id: \.identity.key) { seen in
                        SeenAppRow(seen: seen, mode: settings.detection.mode, engine: engine)
                    }
                }
            } header: {
                Text("Used the mic since MiniBar opened")
            } footer: {
                Text("This list is kept only until MiniBar quits.")
                    .foregroundStyle(.secondary)
            }

            RestoreDefaultsRow {
                confirmRestore = true
            }
        }
        .formStyle(.grouped)
        .alert("Restore the default app lists?", isPresented: $confirmRestore) {
            Button("Restore", role: .destructive) {
                engine.applySettings { $0.restoreAppsDefaults() }
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Your changes to call apps and ignored apps will be lost.")
        }
        .alert(
            Text(verbatim: pendingMove.map { move in
                move.toCallApps
                    ? "\(move.app.displayName) is in Ignored Apps. Move it to Call Apps?"
                    : "\(move.app.displayName) is in Call Apps. Move it to Ignored Apps?"
            } ?? ""),
            isPresented: mainActorBinding(get: { pendingMove != nil }, set: { if !$0 { pendingMove = nil } }),
            presenting: pendingMove
        ) { move in
            Button("Move") {
                if move.toCallApps {
                    addToCallApps(move.app)
                } else {
                    addToIgnoredApps(move.app)
                }
            }
            .keyboardShortcut(.defaultAction)
            Button("Cancel", role: .cancel) {}
        }
        .alert("MiniBar can’t tell when this app uses the mic.", isPresented: $cantTellApp) {
            Button("OK", role: .cancel) {}
        }
    }

    private var callAppsTable: some View {
        Table(catalog.callApps, selection: $selectedCallApp) {
            TableColumn("App") { app in
                HStack(spacing: 8) {
                    AppIconView(bundleID: app.matchers.firstBundleID, isSystem: false)
                    Text(app.displayName)
                }
            }
            TableColumn("Shown on MiniBar as") { app in
                ShortNameField(app: app, engine: engine, problem: $nameProblem)
            }
        }
        .contextMenu(forSelectionType: CallApp.ID.self) { ids in
            if let id = ids.first, let app = catalog.callApps.first(where: { $0.id == id }) {
                Button("Ignore \(app.displayName)") {
                    engine.applySettings { settings in
                        settings.detection.catalog.removeCallApp(id: app.id)
                        settings.detection.catalog.addIgnoredApp(IgnoredApp(id: app.id, displayName: app.displayName, matchers: app.matchers))
                    }
                }
                Button("Remove \(app.displayName)") {
                    engine.applySettings { $0.detection.catalog.removeCallApp(id: app.id) }
                }
            }
        }
        .frame(height: 190)
    }

    // MARK: Adding apps

    private func addCallApp() {
        guard let picked = AppPicker.choose() else { return }
        guard let bundleID = picked.bundleID else {
            cantTellApp = true
            return
        }
        if catalog.ignoredApps.contains(where: { $0.matchers.contains(.bundleID(bundleID)) }) {
            pendingMove = PendingMove(app: picked, toCallApps: true)
        } else {
            addToCallApps(picked)
        }
    }

    private func addIgnoredApp() {
        guard let picked = AppPicker.choose() else { return }
        guard let bundleID = picked.bundleID else {
            cantTellApp = true
            return
        }
        if catalog.callApps.contains(where: { $0.matchers.contains(.bundleID(bundleID)) }) {
            pendingMove = PendingMove(app: picked, toCallApps: false)
        } else {
            addToIgnoredApps(picked)
        }
    }

    private func addToCallApps(_ picked: AppPicker.PickedApp) {
        guard let bundleID = picked.bundleID else { return }
        let app = CallApp(
            id: bundleID,
            displayName: picked.displayName,
            shortName: ShortNames.suggested(for: picked.displayName),
            matchers: [.bundleID(bundleID)]
        )
        engine.applySettings { settings throws(SettingsError) in
            // addCallApp moves it off the ignored list (the alert asked first).
            try settings.detection.catalog.addCallApp(app)
        }
    }

    private func addToIgnoredApps(_ picked: AppPicker.PickedApp) {
        guard let bundleID = picked.bundleID else { return }
        engine.applySettings { settings in
            if let existing = settings.detection.catalog.callApps.first(where: { $0.matchers.contains(.bundleID(bundleID)) }) {
                settings.detection.catalog.removeCallApp(id: existing.id)
            }
            settings.detection.catalog.addIgnoredApp(
                IgnoredApp(id: bundleID, displayName: picked.displayName, matchers: [.bundleID(bundleID)])
            )
        }
    }
}

/// "Shown on MiniBar as": 1 to 24 printable ASCII characters (mac-app-ux.md
/// 6.3). Refuses a 25th character as you type, saves on Return or when the
/// field loses focus, and an empty name puts back the app's default.
private struct ShortNameField: View {
    let app: CallApp
    let engine: TinyBarEngine
    @Binding var problem: String?

    @State private var draft = ""
    @FocusState private var focused: Bool

    var body: some View {
        TextField("Name", text: $draft)
            .textFieldStyle(.plain)
            .focused($focused)
            .onAppear { draft = app.shortName }
            .onChange(of: app.shortName) { _, name in
                if !focused { draft = name }
            }
            .onChange(of: draft) { _, text in
                let limit = TinyBarAPI.Limits.appNameShownCharacters
                if text.count > limit {
                    draft = String(text.prefix(limit))
                    problem = NameProblem.tooLong(limit: limit).message
                } else if let found = NameRules.appNameProblem(text), found != .empty {
                    problem = found.message
                } else {
                    problem = nil
                }
            }
            .onSubmit { commit() }
            .onChange(of: focused) { _, isFocused in
                if !isFocused { commit() }
            }
            .accessibilityLabel("Shown on MiniBar as, for \(app.displayName)")
    }

    private func commit() {
        guard draft != app.shortName else { return }
        let name = draft
        if let error = engine.applySettings({ settings throws(SettingsError) in
            try settings.detection.catalog.rename(callAppID: app.id, to: name)
        }) {
            if case .badAppName(let found) = error {
                problem = found.message
            }
            draft = app.shortName
        } else {
            problem = nil
            draft = engine.state.settings.detection.catalog.callApps.first(where: { $0.id == app.id })?.shortName ?? name
        }
    }
}

/// One row of "Used the mic since MiniBar opened".
private struct SeenAppRow: View {
    let seen: SeenApp
    let mode: CountingMode
    let engine: TinyBarEngine

    var body: some View {
        HStack(spacing: 8) {
            AppIconView(bundleID: seen.identity.bundleID, isSystem: false)
            Text(seen.identity.displayName)
            Text(stateText)
                .foregroundStyle(.secondary)
            Spacer()
            if canAddToCallApps {
                Button("Add to Call Apps") {
                    addToCallApps()
                }
            }
            if canIgnore {
                Button("Ignore") {
                    engine.applySettings { $0.detection.catalog.ignore(seen.identity) }
                }
            }
        }
    }

    private var stateText: String {
        switch seen.category {
        case .callApp: return "counted"
        case .other: return mode == .anyExceptIgnored ? "counted, no name" : "not counted"
        case .ignored: return "ignored"
        }
    }

    private var canAddToCallApps: Bool {
        if case .other = seen.category {
            return seen.identity.bundleID != nil || seen.identity.processName != nil
        }
        return false
    }

    private var canIgnore: Bool {
        if case .ignored = seen.category { return false }
        return true
    }

    private func addToCallApps() {
        let identity = seen.identity
        let matchers: [AppMatcher]
        if let bundleID = identity.bundleID {
            matchers = [.bundleID(bundleID)]
        } else if let processName = identity.processName {
            matchers = [.processName(processName)]
        } else {
            return
        }
        let app = CallApp(
            id: identity.bundleID ?? identity.key,
            displayName: identity.displayName,
            shortName: ShortNames.suggested(for: identity.displayName),
            matchers: matchers
        )
        engine.applySettings { settings throws(SettingsError) in
            try settings.detection.catalog.addCallApp(app)
        }
    }
}

// MARK: - Connection

private struct ConnectionSettings: View {
    let engine: TinyBarEngine
    let windows: WindowCoordinator

    @State private var confirmForget = false
    @State private var advancedOpen = false
    @State private var addressDraft = ""
    @State private var addressMessage: String?
    @State private var checkingAddress = false
    @State private var macNameDraft = ""
    @State private var macNameProblem: String?
    @State private var macNameChanged = false
    @FocusState private var addressFocused: Bool
    @FocusState private var macNameFocused: Bool

    var body: some View {
        let summary = engine.connectionSummary()
        Form {
            Section {
                yourTinyBar(summary)
            }

            Section {
                Toggle(isOn: mainActorBinding(
                    get: { engine.state.settings.useWiFi },
                    set: { value in engine.applySettings { $0.useWiFi = value } }
                )) {
                    Text("Use Wi-Fi when MiniBar isn’t plugged in")
                    Text("Off, this Mac talks to MiniBar only over the USB cable, and never asks for Local Network access.")
                }
                .toggleStyle(.switch)
            }

            Section {
                DisclosureGroup("Advanced", isExpanded: $advancedOpen) {
                    advanced
                }
            }

            RestoreDefaultsRow {
                engine.applySettings { $0.restoreConnectionDefaults() }
                addressDraft = ""
                addressMessage = nil
                macNameDraft = ""
                macNameProblem = nil
            }
        }
        .formStyle(.grouped)
        .onAppear {
            addressDraft = engine.state.settings.manualAddress ?? ""
            macNameDraft = engine.state.settings.macName ?? ""
        }
        .alert(Text(verbatim: "Forget \(summary.barName ?? "MiniBar")?"), isPresented: $confirmForget) {
            Button("Forget", role: .destructive) {
                Task { await engine.forgetBar() }
            }
            Button("Cancel", role: .cancel) {}
                .keyboardShortcut(.defaultAction)
        } message: {
            Text("This Mac will stop showing calls on it. To use it again, plug it into this Mac or pair over Wi-Fi.")
        }
    }

    /// "Your MiniBar" (mac-app-ux.md 6.4).
    private func yourTinyBar(_ summary: ConnectionSummary) -> some View {
        HStack(alignment: .firstTextBaseline) {
            Text(verbatim: summary.barName ?? "Your MiniBar")
                .fontWeight(.semibold)
            Spacer()
            VStack(alignment: .trailing, spacing: 8) {
                HStack(spacing: 6) {
                    // ● when connected, ○ when not: the shape and the words carry it.
                    Image(systemName: summary.isConnected ? "circle.fill" : "circle")
                        .font(.system(size: 8))
                        .foregroundStyle(summary.isConnected ? Color.green : Color.secondary)
                        .accessibilityHidden(true)
                    Text(summary.status)
                }
                ForEach(summary.buttons, id: \.self) { button in
                    connectionButton(button, summary: summary)
                }
            }
        }
    }

    @ViewBuilder
    private func connectionButton(_ button: ConnectionSummary.Button, summary: ConnectionSummary) -> some View {
        switch button {
        case .connect:
            Button("Connect…") { windows.showConnect(welcome: false) }
        case .pairAgain:
            Button("Pair Again…") { windows.showConnect(welcome: false, wifiPage: true) }
        case .forget:
            Button("Forget This MiniBar…") { confirmForget = true }
        case .sendTestCall:
            let running = engine.state.testCall != nil
            VStack(alignment: .trailing, spacing: 4) {
                Button(running ? "Sending Test Call…" : "Send Test Call") {
                    engine.sendTestCall()
                }
                .disabled(running || summary.testCallUnavailableReason != nil)
                Text(summary.testCallUnavailableReason ?? "Shows On a call on MiniBar for 10 seconds, as “Test”.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.trailing)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    @ViewBuilder private var advanced: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                TextField("Address", text: $addressDraft, prompt: Text("Automatic"))
                    .focused($addressFocused)
                    // With Wi-Fi off, nothing is checked or sent (and no
                    // Local Network prompt), as the switch above promises.
                    .disabled(!engine.state.settings.useWiFi)
                    .onSubmit { Task { await commitAddress() } }
                    .onChange(of: addressFocused) { _, focused in
                        if !focused { Task { await commitAddress() } }
                    }
                if checkingAddress {
                    ProgressView()
                        .controlSize(.small)
                }
            }
            if let addressMessage {
                ErrorLine(message: addressMessage)
                    .font(.callout)
            } else {
                Text("Leave empty to find MiniBar automatically, or type its address, like minibar.local or 10.0.4.42.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
        }

        VStack(alignment: .leading, spacing: 4) {
            TextField("Name for this Mac", text: $macNameDraft, prompt: Text("Mac"))
                .focused($macNameFocused)
                .onSubmit { commitMacName() }
                .onChange(of: macNameFocused) { _, focused in
                    if !focused { commitMacName() }
                }
                .onChange(of: macNameDraft) { _, text in
                    let limit = TinyBarAPI.Limits.clientNameCharacters.upperBound
                    if text.count > limit {
                        macNameDraft = String(text.prefix(limit))
                    }
                    macNameProblem = text.isEmpty ? nil : NameRules.macNameProblem(macNameDraft)?.message
                }
            if let macNameProblem {
                ErrorLine(message: macNameProblem)
                    .font(.callout)
            } else {
                Text(macNameChanged
                    ? "Shown in MiniBar’s list of paired devices. MiniBar never sends your Mac’s own name. MiniBar will show the new name after you plug it in or pair again."
                    : "Shown in MiniBar’s list of paired devices. MiniBar never sends your Mac’s own name.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
        }

        VStack(alignment: .leading, spacing: 4) {
            LabeledContent("USB") {
                let paused = engine.state.connection.usbPaused
                Button(paused ? "Resume USB" : "Pause USB") {
                    engine.setUSBPaused(!paused)
                }
            }
            Text("Lets another app, like a firmware flasher, use MiniBar’s USB port. Wi-Fi is used meanwhile.")
                .font(.callout)
                .foregroundStyle(.secondary)
        }
    }

    /// Saves the address and checks it with `info` (mac-app-ux.md 6.4): an
    /// address that answers as something else isn't saved. With Wi-Fi off,
    /// it's saved without a check: no request, so no Local Network prompt.
    private func commitAddress() async {
        let text = addressDraft.trimmingCharacters(in: .whitespaces)
        guard text != (engine.state.settings.manualAddress ?? ""), !checkingAddress else { return }
        guard !text.isEmpty else {
            engine.applySettings { $0.manualAddress = nil }
            addressMessage = nil
            return
        }
        guard let endpoint = BarEndpoint(userInput: text) else {
            addressMessage = "That address isn’t a MiniBar."
            return
        }
        guard endpoint.isAllowedOverPlainHTTP else {
            // App Transport Security refuses plain HTTP to other DNS names.
            addressMessage = PairingProblem.addressNotAllowed.message(barName: "MiniBar")
            return
        }
        guard engine.state.settings.useWiFi else {
            addressMessage = engine.applySettings({ $0.manualAddress = text }) == nil ? nil : "That address isn’t a MiniBar."
            return
        }
        checkingAddress = true
        let result = await AddressCheck.check(endpoint, clientID: engine.state.settings.installID)
        checkingAddress = false
        switch result {
        case .tinyBar, .nothingAnswered:
            if engine.applySettings({ $0.manualAddress = text }) != nil {
                addressMessage = "That address isn’t a MiniBar."
            } else {
                addressMessage = result == .nothingAnswered ? "Nothing answered at \(text)." : nil
            }
        case .notATinyBar:
            addressMessage = "That address isn’t a MiniBar."
        }
    }

    private func commitMacName() {
        let name = macNameDraft.trimmingCharacters(in: .whitespaces)
        guard name != (engine.state.settings.macName ?? "") else { return }
        if let error = engine.applySettings({ $0.macName = name.isEmpty ? nil : name }) {
            if case .badMacName(let problem) = error {
                macNameProblem = problem.message
            }
        } else {
            macNameProblem = nil
            macNameChanged = true
        }
    }
}

/// Settings › Connection › Address: is a MiniBar answering there?
enum AddressCheck {
    enum Result: Hashable, Sendable {
        case tinyBar
        case notATinyBar
        case nothingAnswered
    }

    static func check(_ endpoint: BarEndpoint, clientID: String) async -> Result {
        let transport = DefaultTransportFactory(appVersion: AppInfo.versionString).makeWiFi(endpoint: endpoint, token: nil)
        defer { Task { await transport.close() } }
        do {
            let info = try await transport.hello(HelloRequest(client: clientID))
            return info.isKnownBar ? .tinyBar : .notATinyBar
        } catch let error as BarError {
            switch error {
            case .malformedReply, .notATinyBar, .api:
                return .notATinyBar
            default:
                return .nothingAnswered
            }
        } catch {
            return .nothingAnswered
        }
    }
}

// MARK: - Privacy

private struct PrivacySettings: View {
    let engine: TinyBarEngine

    var body: some View {
        Form {
            Section("What MiniBar sends") {
                Text("Only to your MiniBar, over the USB cable or your local network:")
                Bullet("Whether you’re on a call: yes or no.")
                Bullet("The call app’s name, like “Slack”, if Send the app’s name is on. Names of other apps are never sent.")
                Bullet("A random ID for this copy of MiniBar, so your bar can tell your Mac apart from others. It isn’t your Mac’s name or serial number.")
                Text("It never sends audio, sound levels, window titles, websites, meeting names, contacts, or your name or your Mac’s. It never connects to the internet.")
            }
            Section("What MiniBar reads") {
                Text("Whether the mic and camera are in use, and which app is using the mic. It never turns them on, listens or records, so it doesn’t ask for microphone or camera access. It keeps no record of which apps used the mic.")
            }
            Section("Good to know") {
                Bullet("Anyone near your desk can see what MiniBar shows, including the app’s name. MiniBar’s Remote shows it too.")
                Bullet("Over Wi-Fi, messages aren’t encrypted, so someone watching the office network could see them. Over USB, they stay on the cable.")
            }
            if engine.state.localNetworkBlocked {
                Section {
                    Text("macOS is blocking MiniBar from your local network, so Wi-Fi can’t work.")
                    Button("Open Local Network Settings…") {
                        SystemSettingsLinks.openLocalNetwork()
                    }
                }
            }
        }
        .formStyle(.grouped)
        .textSelection(.enabled)
    }
}

private struct Bullet: View {
    let text: String

    init(_ text: String) {
        self.text = text
    }

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 6) {
            Text(verbatim: "•")
                .accessibilityHidden(true)
            Text(text)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

// MARK: - Shared pieces

/// Restore Defaults, at the bottom right of a tab.
private struct RestoreDefaultsRow: View {
    let action: @MainActor () -> Void

    var body: some View {
        Section {
            HStack {
                Spacer()
                Button("Restore Defaults") { action() }
            }
        }
    }
}

/// The + and − under a list (Add App… and Remove).
private struct ListEditButtons: View {
    let add: @MainActor () -> Void
    let remove: @MainActor () -> Void
    let canRemove: Bool

    var body: some View {
        HStack(spacing: 0) {
            Button {
                add()
            } label: {
                Image(systemName: "plus")
                    .frame(width: 22, height: 20)
            }
            .help("Add App…")
            .accessibilityLabel("Add App…")
            Divider()
                .frame(height: 16)
            Button {
                remove()
            } label: {
                Image(systemName: "minus")
                    .frame(width: 22, height: 20)
            }
            .disabled(!canRemove)
            .help("Remove")
            .accessibilityLabel("Remove")
            Spacer()
        }
        .buttonStyle(.borderless)
        .padding(.top, 4)
    }
}

/// An app's icon as Finder shows it, or a system icon for the built-in
/// entries (Siri, Dictation, Voice Control) and apps that aren't installed.
private struct AppIconView: View {
    let bundleID: String?
    let isSystem: Bool

    var body: some View {
        Group {
            if isSystem {
                Image(systemName: "gearshape")
                    .foregroundStyle(.secondary)
            } else {
                Image(nsImage: AppIcons.icon(bundleID: bundleID))
                    .resizable()
            }
        }
        .frame(width: 16, height: 16)
        .accessibilityHidden(true)
    }
}

@MainActor
enum AppIcons {
    private static var cache: [String: NSImage] = [:]

    static func icon(bundleID: String?) -> NSImage {
        let key = bundleID ?? ""
        if let cached = cache[key] {
            return cached
        }
        let icon: NSImage
        if let bundleID, let url = NSWorkspace.shared.urlForApplication(withBundleIdentifier: bundleID) {
            icon = NSWorkspace.shared.icon(forFile: url.path)
        } else {
            icon = NSWorkspace.shared.icon(for: .application)
        }
        cache[key] = icon
        return icon
    }
}

/// Settings › Apps › + (mac-app-ux.md 6.3): a standard open panel in
/// Applications, applications only.
@MainActor
enum AppPicker {
    struct PickedApp: Hashable {
        var url: URL
        var bundleID: String?
        /// As Finder shows it: "zoom.us".
        var displayName: String
    }

    static func choose() -> PickedApp? {
        let panel = NSOpenPanel()
        panel.title = "Choose an App"
        panel.prompt = "Add"
        panel.allowedContentTypes = [.application]
        panel.directoryURL = URL(fileURLWithPath: "/Applications", isDirectory: true)
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.treatsFilePackagesAsDirectories = false
        NSApp.activate()
        guard panel.runModal() == .OK, let url = panel.url else { return nil }
        return PickedApp(
            url: url,
            bundleID: Bundle(url: url)?.bundleIdentifier,
            displayName: ProcessIdentity.finderName(ofApp: url.path)
        )
    }
}

/// A short name for a newly added call app: its name with only the
/// characters the bar can show (printable ASCII), at most 24.
enum ShortNames {
    static func suggested(for displayName: String) -> String {
        let printable = displayName.unicodeScalars.filter { $0.value >= 0x20 && $0.value <= 0x7E }
        let trimmed = String(printable).trimmingCharacters(in: .whitespaces)
        let short = String(trimmed.prefix(TinyBarAPI.Limits.appNameShownCharacters))
            .trimmingCharacters(in: .whitespaces)
        return short.isEmpty ? "App" : short
    }
}

extension [AppMatcher] {
    /// The first bundle ID among the matchers, for the app's icon.
    var firstBundleID: String? {
        for matcher in self {
            if case .bundleID(let id) = matcher {
                return id
            }
        }
        return nil
    }
}

extension TinyBarEngine {
    /// Applies a settings change and returns why it was refused, if it was,
    /// instead of throwing. If `change` itself throws, nothing changes.
    @discardableResult
    func applySettings(_ change: (inout AppSettings) throws(SettingsError) -> Void) -> SettingsError? {
        var refused: SettingsError?
        do throws(SettingsError) {
            try updateSettings { settings in
                var copy = settings
                do throws(SettingsError) {
                    try change(&copy)
                    settings = copy
                } catch {
                    refused = error
                }
            }
        } catch {
            return error
        }
        return refused
    }
}

/// A binding whose getter and setter run on the main actor, where the
/// engine lives. SwiftUI calls both on the main thread.
@MainActor
func mainActorBinding<Value: Sendable>(
    get: @escaping @MainActor () -> Value,
    set: @escaping @MainActor (Value) -> Void
) -> Binding<Value> {
    Binding(
        get: { MainActor.assumeIsolated { get() } },
        set: { value in MainActor.assumeIsolated { set(value) } }
    )
}
#endif
