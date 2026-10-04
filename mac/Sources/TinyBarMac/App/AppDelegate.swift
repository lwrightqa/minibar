#if os(macOS)
import AppKit
import TinyBarCore

/// Owns the engine, the status item and the windows.
///
/// - `applicationDidFinishLaunching`: makes the engine with the live adapters,
///   starts it, puts up the status item, and opens the Welcome window on first
///   launch (`engine.shouldShowWelcome`).
/// - `applicationShouldTerminate`: `.terminateLater`, then
///   `await engine.shutdown()` (sends `leaving`), then
///   `NSApp.reply(toApplicationShouldTerminate: true)`.
/// - `applicationShouldHandleReopen`: opening the app again from Finder or
///   Spotlight opens Settings on General, or the Connect window if TinyBar
///   isn't set up (mac-app-ux.md 8). *Unverified* that it fires for an
///   `LSUIElement` app.
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private var engine: TinyBarEngine?
    private var statusItem: StatusItemController?
    private var windows: WindowCoordinator?

    func applicationDidFinishLaunching(_ notification: Notification) {
        unimplemented()
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        unimplemented()
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        unimplemented()
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
            timeZone: .current,
            hasLocalNetworkPrivacy: ProcessInfo.processInfo.isOperatingSystemAtLeast(
                OperatingSystemVersion(majorVersion: 15, minorVersion: 0, patchVersion: 0)
            )
        )
    }
}

/// The app's version from Info.plist.
enum AppInfo {
    /// "1.0 (12)": `CFBundleShortVersionString` and `CFBundleVersion`.
    static var versionString: String {
        let info = Bundle.main.infoDictionary ?? [:]
        let short = info["CFBundleShortVersionString"] as? String ?? "1.0"
        let build = info["CFBundleVersion"] as? String ?? "0"
        return "\(short) (\(build))"
    }
}
#endif
