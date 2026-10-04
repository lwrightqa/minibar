import Foundation
import Observation

/// The app's brain: wires the mic and camera monitors to the `CallDetector`,
/// the detector to the `BarConnection`, and both to the UI.
///
/// The macOS app makes one with the real adapters (`TinyBarMac`); tests make
/// one with fakes and a `ManualClock`, so everything below the UI is tested on
/// Linux. The UI reads `state` (it's `@Observable`, so SwiftUI views update by
/// themselves; the AppKit menu uses `withObservationTracking` or re-reads it
/// in `menuWillOpen`) and calls the action methods.
///
/// What it does:
/// - Feeds every mic and camera change to the detector, calls `tick` at the
///   detector's `nextDeadline`, and turns detector events into
///   `BarConnection.report(_:)`.
/// - Saves settings on every change (`SettingsStore`), and the bar whenever
///   the connection's `bar` changes.
/// - Starts Bonjour only when Wi-Fi is on and a bar is set up, or while the
///   Connect window's Wi-Fi page is browsing.
/// - On sleep and power-off: `sendLeaving()`. On wake: a fresh start
///   (`didWake`, a new grace period, and the state at once).
/// - Keeps "Count … Again" available while the app or camera is still in use.
@MainActor
@Observable
public final class TinyBarEngine {
    /// Everything the engine needs from the platform.
    public struct Dependencies: Sendable {
        public var clock: any TinyClock
        public var mic: any MicActivitySource
        public var camera: any CameraActivitySource
        public var serialDevices: any SerialDeviceWatcher
        public var discovery: any BarDiscovery
        public var tokens: any TokenStore
        public var settings: any SettingsStore
        public var loginItem: any LoginItemService
        public var power: any PowerEventSource
        public var transports: any TransportFactory
        /// "1.0 (12)", for `hello` and the `User-Agent`.
        public var appVersion: String
        public var timeZone: TimeZone
        /// macOS 15 or later: the Local Network prompt exists.
        public var hasLocalNetworkPrivacy: Bool

        public init(
            clock: any TinyClock,
            mic: any MicActivitySource,
            camera: any CameraActivitySource,
            serialDevices: any SerialDeviceWatcher,
            discovery: any BarDiscovery,
            tokens: any TokenStore,
            settings: any SettingsStore,
            loginItem: any LoginItemService,
            power: any PowerEventSource,
            transports: any TransportFactory,
            appVersion: String,
            timeZone: TimeZone = .current,
            hasLocalNetworkPrivacy: Bool
        ) {
            self.clock = clock
            self.mic = mic
            self.camera = camera
            self.serialDevices = serialDevices
            self.discovery = discovery
            self.tokens = tokens
            self.settings = settings
            self.loginItem = loginItem
            self.power = power
            self.transports = transports
            self.appVersion = appVersion
            self.timeZone = timeZone
            self.hasLocalNetworkPrivacy = hasLocalNetworkPrivacy
        }
    }

    /// Everything the UI shows.
    public private(set) var state: EngineState

    @ObservationIgnored private let dependencies: Dependencies

    /// Loads the settings (or makes first-launch ones). Doesn't start anything.
    public init(dependencies: Dependencies) {
        self.dependencies = dependencies
        let settings = dependencies.settings.load() ?? AppSettings.firstLaunch()
        self.state = EngineState(
            settings: settings,
            connection: ConnectionState(phase: settings.bar == nil ? .notSetUp : .looking, bar: settings.bar)
        )
    }

    /// The first launch: the Welcome window should open by itself.
    public var shouldShowWelcome: Bool {
        !state.settings.didShowWelcome
    }

    /// Starts the monitors, the USB watcher, the connection, and Bonjour if
    /// Wi-Fi is on and a bar is set up.
    public func start() {
        unimplemented()
    }

    /// Quitting: sends `leaving` (best effort, about a second at most), then
    /// stops everything. Call from `applicationShouldTerminate` and reply
    /// `.terminateLater` until it returns.
    public func shutdown() async {
        unimplemented()
    }

    // MARK: Menu actions

    /// Pause Detection ▸ (ends a call at once).
    public func pause(_ choice: PauseChoice) {
        unimplemented()
    }

    /// Resume Detection.
    public func resume() {
        unimplemented()
    }

    /// Don't Count <app> (ends a call from it at once).
    public func dontCount(_ app: AppIdentity) {
        unimplemented()
    }

    /// Count <app> Again (undo).
    public func countAgain(_ app: AppIdentity) {
        unimplemented()
    }

    /// Don't Count the Camera / Count the Camera Again; also Settings' switch.
    public func setCountCamera(_ on: Bool) {
        unimplemented()
    }

    /// Pause USB / Resume USB.
    public func setUSBPaused(_ paused: Bool) {
        unimplemented()
    }

    // MARK: Settings

    /// Changes settings. Validates first and throws without changing anything
    /// if the result is invalid; applies detection changes at once.
    public func updateSettings(_ change: (inout AppSettings) -> Void) throws(SettingsError) {
        unimplemented()
    }

    /// Start TinyBar when you log in: registers or unregisters, and records
    /// what macOS did (`state.loginItem`).
    public func setLaunchAtLogin(_ on: Bool) {
        unimplemented()
    }

    /// Send Test Call: On a call for 10 seconds as "Test" (not during a real
    /// call, and only when connected).
    public func sendTestCall() {
        unimplemented()
    }

    /// Forget This TinyBar.
    public func forgetBar() async {
        unimplemented()
    }

    /// The Welcome window was closed (it won't open by itself again).
    public func markWelcomeShown() {
        unimplemented()
    }

    /// The Local Network explanation was shown.
    public func markLocalNetworkExplained() {
        unimplemented()
    }

    /// A new Pair Over Wi-Fi flow for the Connect window.
    public func makeWiFiPairingFlow() -> WiFiPairingFlow {
        unimplemented()
    }

    // MARK: For the UI

    /// The menu as it should look now (Option held: `showDetails`).
    public func menuContent(showDetails: Bool) -> MenuContent {
        MenuPresenter.content(
            for: state,
            now: dependencies.clock.now(),
            timeZone: dependencies.timeZone,
            showDetails: showDetails
        )
    }

    /// Settings › Connection's top section.
    public func connectionSummary() -> ConnectionSummary {
        MenuPresenter.connectionSummary(for: state, now: dependencies.clock.now(), timeZone: dependencies.timeZone)
    }
}
