#if os(macOS)
import AppKit
import TinyBarCore

/// Owns the engine, the status item and the windows.
///
/// - `applicationDidFinishLaunching`: if another copy of MiniBar is already
///   running (one in `build/` and one in Applications, or `open -n`), asks it
///   to show itself and quits, since both would share the settings and the
///   install ID and fight over the bar. Then opts out of App Nap for the
///   app's lifetime (`ProcessInfo.beginActivity`, which still allows idle
///   sleep), so the start and end delays, the 1-second re-reads and the
///   30-second heartbeat aren't coalesced or delayed while no window is open.
///   Then the hidden main menu (for ⌘, ⌘Q ⌘W and the Edit shortcuts in text
///   fields), the engine with the live adapters, the status item, then
///   `engine.start()`, and the Welcome window on first launch
///   (`engine.shouldShowWelcome`).
/// - `applicationShouldTerminate`: `.terminateLater`, then
///   `await engine.shutdown()` (sends `leaving`), then
///   `NSApp.reply(toApplicationShouldTerminate: true)`; after 3 seconds it
///   quits anyway.
/// - `applicationShouldHandleReopen`: opening the app again from Finder or
///   Spotlight opens Settings on General, or the Connect window if MiniBar
///   isn't set up (mac-app-ux.md 8). *Unverified* that it fires for an
///   `LSUIElement` app.
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private var engine: TinyBarEngine?
    private var statusItem: StatusItemController?
    private var windows: WindowCoordinator?
    private var isTerminating = false
    private var didReplyToTerminate = false
    /// Keeps App Nap off while the app runs. *Unverified:* check Activity
    /// Monitor's App Nap column, and Energy Impact (criterion 36), on a Mac.
    private var activity: (any NSObjectProtocol)?

    func applicationDidFinishLaunching(_ notification: Notification) {
        if handOverToRunningCopy() { return }
        activity = ProcessInfo.processInfo.beginActivity(
            options: .userInitiatedAllowingIdleSystemSleep,
            reason: "Watching the mic and camera for calls"
        )
        NSApp.mainMenu = MainMenu.make()
        let engine = TinyBarEngine(dependencies: .live())
        let windows = WindowCoordinator(engine: engine)
        self.engine = engine
        self.windows = windows
        statusItem = StatusItemController(engine: engine, windows: windows)
        engine.start()
        if engine.shouldShowWelcome {
            windows.showConnect(welcome: true)
        }
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let engine else { return .terminateNow }
        guard !isTerminating else { return .terminateLater }
        isTerminating = true
        Task { @MainActor [weak self] in
            await engine.shutdown()
            self?.finishTerminating()
        }
        // `shutdown()` waits about a second at most; this is a backstop so a
        // stuck link can never keep the app from quitting.
        Task { @MainActor [weak self] in
            try? await Task.sleep(for: .seconds(3))
            self?.finishTerminating()
        }
        return .terminateLater
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        guard let engine, let windows else { return false }
        if engine.state.isSetUp {
            windows.showSettings(tab: .general)
        } else {
            windows.showConnect(welcome: false)
        }
        return false
    }

    func applicationSupportsSecureRestorableState(_ app: NSApplication) -> Bool {
        true
    }

    /// Another copy with this bundle ID is running: reopen it (which shows its
    /// Settings or Connect window, as `applicationShouldHandleReopen` does)
    /// and quit this one before it touches anything. Returns `true` if so.
    private func handOverToRunningCopy() -> Bool {
        guard let bundleID = Bundle.main.bundleIdentifier else { return false }
        let me = NSRunningApplication.current
        guard let other = NSRunningApplication.runningApplications(withBundleIdentifier: bundleID)
            .first(where: { $0.processIdentifier != me.processIdentifier && !$0.isTerminated })
        else { return false }
        if let url = other.bundleURL {
            NSWorkspace.shared.openApplication(at: url, configuration: NSWorkspace.OpenConfiguration()) { _, _ in
                Task { @MainActor in NSApp.terminate(nil) }
            }
        } else {
            _ = other.activate()
            NSApp.terminate(nil)
        }
        return true
    }

    private func finishTerminating() {
        guard !didReplyToTerminate else { return }
        didReplyToTerminate = true
        NSApp.reply(toApplicationShouldTerminate: true)
    }

    // MARK: Main menu actions (reached through the responder chain)

    @objc func showAbout(_ sender: Any?) {
        AboutPanel.show()
    }

    @objc func showSettings(_ sender: Any?) {
        windows?.showSettings()
    }
}

extension TinyBarEngine.Dependencies {
    /// The real adapters.
    @MainActor
    static func live() -> TinyBarEngine.Dependencies {
        let clock = SystemClock()
        let version = AppInfo.versionString
        return TinyBarEngine.Dependencies(
            clock: clock,
            mic: CoreAudioMicMonitor(),
            camera: CoreMediaIOCameraMonitor(),
            serialDevices: IOKitSerialDeviceWatcher(),
            discovery: BonjourBarBrowser(),
            tokens: KeychainTokenStore(),
            settings: UserDefaultsSettingsStore(),
            loginItem: MainAppLoginItem(),
            power: SystemPowerEvents(),
            transports: DefaultTransportFactory(clock: clock, appVersion: version),
            appVersion: version,
            // Auto-updating, so midnight and the times shown follow the Mac
            // when it travels, and its 12- or 24-hour setting when it changes.
            timeZone: .autoupdatingCurrent,
            locale: .autoupdatingCurrent,
            hasLocalNetworkPrivacy: AppInfo.hasLocalNetworkPrivacy
        )
    }
}

/// Facts about this copy of the app and the Mac it runs on.
enum AppInfo {
    /// "1.0 (12)": `CFBundleShortVersionString` and `CFBundleVersion`.
    static var versionString: String {
        let info = Bundle.main.infoDictionary ?? [:]
        let short = info["CFBundleShortVersionString"] as? String ?? "1.0"
        let build = info["CFBundleVersion"] as? String ?? "0"
        return "\(short) (\(build))"
    }

    /// macOS 15 or later: the Local Network prompt exists (Apple TN3179).
    static var hasLocalNetworkPrivacy: Bool {
        ProcessInfo.processInfo.isOperatingSystemAtLeast(
            OperatingSystemVersion(majorVersion: 15, minorVersion: 0, patchVersion: 0)
        )
    }
}

/// The standard About panel (mac-app-ux.md 8): icon, name, "Version 1.0 (12)"
/// from Info.plist, and the credits line.
@MainActor
enum AboutPanel {
    static func show() {
        let credits = NSAttributedString(
            string: "Shows On a call on your MiniBar when this Mac’s mic or camera is in use. It never listens or records.",
            attributes: [
                .font: NSFont.systemFont(ofSize: NSFont.smallSystemFontSize),
                .foregroundColor: NSColor.secondaryLabelColor,
                .paragraphStyle: centered,
            ]
        )
        NSApp.activate()
        NSApp.orderFrontStandardAboutPanel(options: [.credits: credits])
    }

    private static var centered: NSParagraphStyle {
        let style = NSMutableParagraphStyle()
        style.alignment = .center
        return style
    }
}

/// System Settings panes the app links to.
@MainActor
enum SystemSettingsLinks {
    /// Privacy & Security › Local Network. *Unverified:* the anchor is widely
    /// used but not documented by Apple (mac-app-ux.md 11).
    static let localNetwork = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork")!
    /// Privacy & Security, the fallback.
    static let privacy = URL(string: "x-apple.systempreferences:com.apple.preference.security")!

    static func openLocalNetwork() {
        if !NSWorkspace.shared.open(localNetwork) {
            NSWorkspace.shared.open(privacy)
        }
    }
}

/// The main menu. An `LSUIElement` app never shows it, but its key
/// equivalents still work while one of the app's windows is key: without it,
/// ⌘W, ⌘, and ⌘Q do nothing there, and text fields can't cut, copy or paste
/// (the pairing code can be pasted, mac-app-ux.md 5.4).
@MainActor
enum MainMenu {
    static func make() -> NSMenu {
        let main = NSMenu()

        let app = NSMenu(title: "MiniBar")
        app.addItem(withTitle: "About MiniBar", action: #selector(AppDelegate.showAbout(_:)), keyEquivalent: "")
        app.addItem(.separator())
        app.addItem(withTitle: "Settings…", action: #selector(AppDelegate.showSettings(_:)), keyEquivalent: ",")
        app.addItem(.separator())
        app.addItem(withTitle: "Quit MiniBar", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        main.addItem(submenuItem(app))

        let edit = NSMenu(title: "Edit")
        edit.addItem(withTitle: "Undo", action: Selector(("undo:")), keyEquivalent: "z")
        let redo = edit.addItem(withTitle: "Redo", action: Selector(("redo:")), keyEquivalent: "z")
        redo.keyEquivalentModifierMask = [.command, .shift]
        edit.addItem(.separator())
        edit.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
        edit.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        edit.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
        edit.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
        main.addItem(submenuItem(edit))

        let window = NSMenu(title: "Window")
        window.addItem(withTitle: "Close", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        window.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
        main.addItem(submenuItem(window))

        return main
    }

    private static func submenuItem(_ menu: NSMenu) -> NSMenuItem {
        let item = NSMenuItem(title: menu.title, action: nil, keyEquivalent: "")
        item.submenu = menu
        return item
    }
}
#endif
