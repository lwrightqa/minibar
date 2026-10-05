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
/// - On sleep and power-off: `sendLeaving()` (after a power-off, carrying on
///   if the app is still running 10 seconds later), and a pairing code the
///   Connect window asked for comes off the bar (`pair/cancel`), as on quit.
///   Quitting also waits (briefly) for pairing requests still on their way,
///   the closed window's included, so their answers can take a code off.
///   On wake: a fresh start (`didWake`, a new grace period, and the state at once).
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
        /// The Mac's time zone, for midnight and the times shown. Pass
        /// `.autoupdatingCurrent`, so it follows the Mac when it travels.
        public var timeZone: TimeZone
        /// The Mac's format settings, for the times shown (12- or 24-hour).
        public var locale: Locale
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
            timeZone: TimeZone = .autoupdatingCurrent,
            locale: Locale = .autoupdatingCurrent,
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
            self.locale = locale
            self.hasLocalNetworkPrivacy = hasLocalNetworkPrivacy
        }
    }

    /// Everything the UI shows.
    public private(set) var state: EngineState

    @ObservationIgnored private let dependencies: Dependencies
    /// `session` for this launch (api.md 5.1).
    @ObservationIgnored private let sessionID: String
    @ObservationIgnored private var detector: CallDetector
    @ObservationIgnored private let connection: BarConnection
    @ObservationIgnored private let discoveryHub: BarDiscoveryHub
    /// The connection's own Bonjour client while Wi-Fi is wanted.
    @ObservationIgnored private var connectionDiscovery: (any BarDiscovery)?
    /// Messages to the connection, in order.
    @ObservationIgnored private let commands: AsyncStream<ConnectionCommand>.Continuation
    @ObservationIgnored private let pendingCommands = Locked(0)
    @ObservationIgnored private var tasks: [Task<Void, Never>] = []
    @ObservationIgnored private var tickTask: Task<Void, Never>?
    @ObservationIgnored private var tickDeadline: Date?
    @ObservationIgnored private var mic: [MicProcess] = []
    @ObservationIgnored private var cameraInUse = false
    /// Monitor callbacks are numbered so a late one can't undo a newer one.
    @ObservationIgnored private let callbackCounter = Locked(0)
    @ObservationIgnored private let callbacksInFlight = Locked(0)
    @ObservationIgnored private var lastMicCallback = 0
    @ObservationIgnored private var lastCameraCallback = 0
    @ObservationIgnored private var started = false
    @ObservationIgnored private var stopped = false
    /// Tries monitors that failed to start again (`monitorRetryInterval`).
    @ObservationIgnored private var monitorRetryTask: Task<Void, Never>?
    /// The Connect window's Pair Over Wi-Fi flow, while the window keeps it,
    /// so quitting and sleep can take its code off the bar.
    @ObservationIgnored private weak var pairingFlow: WiFiPairingFlow?
    /// Pairing requests still on their way from any flow, whether or not the
    /// window still holds it: a `pair/cancel` for a code given up (the window
    /// closed), or the wait for an answer that decides (`pair/start`, `pair`)
    /// and the cancel it may start. Each gives up after
    /// `WiFiPairingFlow.cancelTimeout`; quitting waits for them all.
    @ObservationIgnored private var pairingWork: Set<Task<Void, Never>> = []

    /// How long to wait before trying a monitor that failed to start again.
    public static let monitorRetryInterval: TimeInterval = 60

    /// What the engine asks of the connection, kept in order.
    enum ConnectionCommand: Sendable {
        case start
        case report(ReportedCall)
        case device(SerialDeviceEvent)
        case discovered([DiscoveredBar])
        case discoveryFailed(DiscoveryError)
        case usbPaused(Bool)
        case configure(BarConnection.Configuration)
        case adopt(KnownBar)
        case didWake
    }

    /// Loads the settings (or makes first-launch ones). Doesn't start anything.
    public init(dependencies: Dependencies) {
        self.dependencies = dependencies
        let saved = dependencies.settings.load()
        var settings = saved ?? AppSettings.firstLaunch()
        // A pause is saved on the Mac's clock; detection runs on the engine's.
        settings.pause = settings.pause.shifted(by: -dependencies.clock.wallClockOffset())
        let sessionID = Identifiers.newSessionID()
        self.sessionID = sessionID
        self.state = EngineState(
            settings: settings,
            connection: ConnectionState(phase: settings.bar == nil ? .notSetUp : .looking, bar: settings.bar)
        )
        self.detector = CallDetector(settings: settings.detection, pause: settings.pause)
        self.connection = BarConnection(
            configuration: TinyBarEngine.configuration(for: settings, dependencies: dependencies, sessionID: sessionID),
            bar: settings.bar,
            clock: dependencies.clock,
            transports: dependencies.transports,
            tokens: dependencies.tokens
        )
        self.discoveryHub = BarDiscoveryHub(dependencies.discovery)
        let (stream, continuation) = AsyncStream.makeStream(of: ConnectionCommand.self)
        self.commands = continuation
        let connection = self.connection
        let pending = pendingCommands
        // One consumer, so the connection sees everything in the order it happened.
        tasks.append(Task.detached {
            for await command in stream {
                switch command {
                case .start: await connection.start()
                case .report(let call): await connection.report(call)
                case .device(let event): await connection.handle(event)
                case .discovered(let bars): await connection.discovered(bars)
                case .discoveryFailed(let error): await connection.discoveryFailed(error)
                case .usbPaused(let paused): await connection.setUSBPaused(paused)
                case .configure(let configuration): await connection.update(configuration)
                case .adopt(let bar): await connection.adopt(bar)
                case .didWake: await connection.didWake()
                }
                pending.withLock { $0 -= 1 }
            }
        })
        if saved == nil {
            save()  // keeps the new install ID from the first launch on
        }
    }

    private static func configuration(for settings: AppSettings, dependencies: Dependencies, sessionID: String)
        -> BarConnection.Configuration {
        BarConnection.Configuration(
            clientID: settings.installID,
            sessionID: sessionID,
            appVersion: dependencies.appVersion,
            macName: settings.macNameToSend,
            useWiFi: settings.useWiFi,
            manualEndpoint: settings.manualEndpoint,
            timeZone: dependencies.timeZone
        )
    }


    private func send(_ command: ConnectionCommand) {
        pendingCommands.withLock { $0 += 1 }
        commands.yield(command)
    }

    /// The first launch: the Welcome window should open by itself.
    public var shouldShowWelcome: Bool {
        !state.settings.didShowWelcome
    }

    /// Starts the monitors, the USB watcher, the connection, and Bonjour if
    /// Wi-Fi is on and a bar is set up.
    public func start() {
        guard !started, !stopped else { return }
        started = true
        let connection = self.connection

        // The connection's state, for the menu; the bar is saved when it changes.
        tasks.append(Task { [weak self] in
            for await connectionState in connection.states {
                self?.connectionChanged(connectionState)
            }
        })
        send(.start)

        // A pause that ended while the app wasn't running is over.
        apply(detector.setPause(state.settings.pause, at: now))

        startMonitors([.mic, .camera, .usb])
        dependencies.power.start { [weak self] event in
            await self?.handlePower(event)
        }
        state.loginItem = dependencies.loginItem.status
        updateDiscovery()
        send(.report(currentReport()))
    }

    /// Quitting: sends `leaving` (best effort, about a second at most) and,
    /// while the Connect window has asked a bar for a code, takes that code
    /// off the bar (`pair/cancel`, at the same time, `WiFiPairingFlow.cancelTimeout`
    /// at most), then stops everything. A pairing request still on its way
    /// (from this window, or from one closed a moment ago) is waited for the
    /// same way, so its answer can take the code off the bar. Call from
    /// `applicationShouldTerminate` and reply `.terminateLater` until it returns.
    public func shutdown() async {
        guard !stopped else { return }
        stopped = true
        if let codeCanceled = pairingFlow?.cancel() { keep(codeCanceled) }
        await connection.sendLeaving()
        for work in Array(pairingWork) { await work.value }
        await connection.stop()
        dependencies.mic.stop()
        dependencies.camera.stop()
        dependencies.serialDevices.stop()
        dependencies.power.stop()
        monitorRetryTask?.cancel()
        monitorRetryTask = nil
        connectionDiscovery?.stop()
        connectionDiscovery = nil
        discoveryHub.shutdown()
        tickTask?.cancel()
        commands.finish()
        tasks.forEach { $0.cancel() }
    }

    // MARK: Menu actions

    /// Pause Detection ▸ (ends a call at once). Midnight is found on the
    /// Mac's clock, then kept on the engine's.
    public func pause(_ choice: PauseChoice) {
        let clock = dependencies.clock
        let pause = choice.pauseState(at: clock.wallNow(), timeZone: dependencies.timeZone)
        setPause(pause.shifted(by: -clock.wallClockOffset()))
    }

    /// Resume Detection.
    public func resume() {
        setPause(.notPaused)
    }

    private func setPause(_ pause: PauseState) {
        state.settings.pause = pause
        save()
        apply(detector.setPause(pause, at: now))
    }

    /// Don't Count <app> (ends a call from it at once).
    public func dontCount(_ app: AppIdentity) {
        var settings = state.settings
        let edit = settings.detection.catalog.ignore(app)
        state.undo.app = edit
        commit(settings)
    }

    /// Count <app> Again (undo).
    public func countAgain(_ app: AppIdentity) {
        guard let edit = state.undo.app, edit.app.key == app.key else { return }
        var settings = state.settings
        settings.detection.catalog.undo(edit)
        state.undo.app = nil
        commit(settings)
    }

    /// Don't Count the Camera / Count the Camera Again; also Settings' switch.
    public func setCountCamera(_ on: Bool) {
        var settings = state.settings
        settings.detection.countCamera = on
        state.undo.camera = !on && cameraInUse
        commit(settings)
    }

    /// Pause USB / Resume USB.
    public func setUSBPaused(_ paused: Bool) {
        send(.usbPaused(paused))
    }

    // MARK: Settings

    /// Changes settings. Validates first and throws without changing anything
    /// if the result is invalid; applies detection changes at once.
    public func updateSettings(_ change: (inout AppSettings) -> Void) throws(SettingsError) {
        var settings = state.settings
        change(&settings)
        try settings.validate()
        commit(settings)
    }

    private func commit(_ settings: AppSettings) {
        let old = state.settings
        state.settings = settings
        save()
        if settings.detection != old.detection {
            apply(detector.update(settings: settings.detection, at: now))
        }
        if settings.pause != old.pause {
            apply(detector.setPause(settings.pause, at: now))
        }
        if settings.useWiFi != old.useWiFi || settings.manualAddress != old.manualAddress || settings.macName != old.macName {
            send(.configure(TinyBarEngine.configuration(for: settings, dependencies: dependencies, sessionID: sessionID)))
        }
        updateDiscovery()
    }

    /// Start MiniBar when you log in: registers or unregisters, and records
    /// what macOS did (`state.loginItem`). A registration macOS refused or
    /// holds for approval turns the switch off (mac-app-ux.md 6.2).
    public func setLaunchAtLogin(_ on: Bool) {
        let status = on ? dependencies.loginItem.register() : dependencies.loginItem.unregister()
        state.loginItem = status
        state.settings.launchAtLogin = on && status == .enabled
        save()
    }

    /// Send Test Call: On a call for 10 seconds as "Test" (not during a real
    /// call, and only when connected).
    public func sendTestCall() {
        guard state.call == nil, state.testCall == nil, state.connection.phase == .connected else { return }
        let test = TestCall(startedAt: now, callID: detector.reserveCallID())
        state.testCall = test
        send(.report(ReportedCall(active: true, app: TestCall.appName, callID: test.callID, startedAt: test.startedAt)))
        scheduleTick()
    }

    /// Forget This MiniBar.
    public func forgetBar() async {
        await connection.forget()
        state.settings.bar = nil
        state.connection = await connection.state
        save()
        updateDiscovery()
    }

    /// The Welcome window was closed (it won't open by itself again).
    public func markWelcomeShown() {
        state.settings.didShowWelcome = true
        save()
    }

    /// The Local Network explanation was shown.
    public func markLocalNetworkExplained() {
        state.settings.didExplainLocalNetwork = true
        save()
    }

    /// A new Pair Over Wi-Fi flow for the Connect window. The engine keeps a
    /// weak reference to the latest one, so quitting and sleep can take its
    /// code off the bar, and keeps every request the flow starts in the
    /// background (`pairingWork`), so quitting can wait for those even after
    /// the window has let go of the flow.
    public func makeWiFiPairingFlow() -> WiFiPairingFlow {
        let flow = WiFiPairingFlow(
            clientID: state.settings.installID,
            macName: state.settings.macNameToSend,
            clock: dependencies.clock,
            discovery: discoveryHub.makeClient(),
            transports: dependencies.transports,
            tokens: dependencies.tokens,
            needsLocalNetworkExplanation: dependencies.hasLocalNetworkPrivacy && !state.settings.didExplainLocalNetwork,
            onPaired: { [weak self] bar in self?.adopt(bar) },
            onBackgroundWork: { [weak self] work in self?.keep(work) }
        )
        pairingFlow = flow
        return flow
    }

    /// Keeps a pairing request that runs in the background until it's done.
    private func keep(_ work: Task<Void, Never>) {
        guard pairingWork.insert(work).inserted else { return }
        Task { [weak self] in
            await work.value
            self?.pairingWork.remove(work)
        }
    }

    private func adopt(_ bar: KnownBar) {
        state.settings.bar = bar
        save()
        send(.adopt(bar))
        updateDiscovery()
    }

    // MARK: - Detection

    private var now: Date { dependencies.clock.now() }

    private func micChanged(_ processes: [MicProcess], number: Int) {
        guard number > lastMicCallback, !stopped else { return }
        lastMicCallback = number
        mic = processes
        observe()
    }

    private func cameraChanged(_ inUse: Bool, number: Int) {
        guard number > lastCameraCallback, !stopped else { return }
        lastCameraCallback = number
        cameraInUse = inUse
        observe()
    }

    private func observe() {
        apply(detector.observe(ActivitySnapshot(mic: mic, cameraInUse: cameraInUse), at: now))
    }

    /// The state the bar should know about: the detected call, else a test call, else idle.
    private func currentReport() -> ReportedCall {
        if let call = detector.call { return ReportedCall(call) }
        if let test = state.testCall {
            return ReportedCall(active: true, app: TestCall.appName, callID: test.callID, startedAt: test.startedAt)
        }
        return .idle
    }

    /// Sends what the detector decided, and refreshes what the UI shows.
    private func apply(_ events: [DetectorEvent]) {
        for event in events {
            switch event {
            case .started, .changed:
                // A real call takes over from a test call.
                state.testCall = nil
                send(.report(currentReport()))
            case .ended:
                send(.report(currentReport()))
            }
        }
        state.call = detector.call
        state.observation = detector.observation
        state.seenApps = detector.seenApps
        if state.settings.pause != detector.pause {
            // A timed pause ended by itself.
            state.settings.pause = detector.pause
            save()
        }
        // "Count … Again" is offered only while that app or the camera is in use.
        if let edit = state.undo.app,
           !state.observation.micUses.contains(where: { $0.classification.identity.key == edit.app.key }) {
            state.undo.app = nil
        }
        if state.undo.camera, !state.observation.cameraInUse {
            state.undo.camera = false
        }
        scheduleTick()
    }

    /// Wakes at the detector's next deadline, or when a test call ends.
    private func scheduleTick() {
        let deadline = [detector.nextDeadline, state.testCall?.endsAt].compactMap { $0 }.min()
        guard deadline != tickDeadline else { return }
        tickTask?.cancel()
        tickDeadline = deadline
        guard let deadline, !stopped else {
            tickTask = nil
            return
        }
        let clock = dependencies.clock
        tickTask = Task { [weak self] in
            do { try await clock.sleep(until: deadline) } catch { return }
            self?.tick()
        }
    }

    private func tick() {
        tickDeadline = nil
        let now = self.now
        if let test = state.testCall, now >= test.endsAt {
            state.testCall = nil
            send(.report(currentReport()))
        }
        apply(detector.tick(at: now))
        scheduleTick()
    }

    // MARK: - Connection and platform

    private func connectionChanged(_ connectionState: ConnectionState) {
        state.connection = connectionState
        if connectionState.bar != state.settings.bar {
            state.settings.bar = connectionState.bar
            save()
            updateDiscovery()
        }
    }

    private func handlePower(_ event: PowerEvent) async {
        switch event {
        case .willSleep:
            // A wake always follows (kIOMessageSystemWillSleep can't be refused).
            // A pairing code nobody will type now comes off the bar, while
            // `leaving` goes out.
            let codeCanceled = pairingFlow?.macWillSleep()
            await connection.sendLeaving()
            await codeCanceled?.value
        case .willPowerOff:
            // A logout or shutdown can still be canceled by another app, and
            // then no wake follows: carry on if still running 10 s later.
            let codeCanceled = pairingFlow?.macWillSleep()
            await connection.sendLeaving(resumeAfter: BarConnection.powerOffResumeAfter)
            await codeCanceled?.value
        case .didWake:
            // Start fresh: the bar was told `leaving` before the sleep.
            detector.reset()
            state.testCall = nil
            mic = dependencies.mic.current()
            cameraInUse = dependencies.camera.current()
            observe()
            send(.report(currentReport()))
            send(.didWake)
        }
    }

    /// Browses for the paired bar while Wi-Fi is on and a bar is set up.
    private func updateDiscovery() {
        let wanted = !stopped && state.settings.useWiFi && state.isSetUp
        if wanted, connectionDiscovery == nil {
            let client = discoveryHub.makeClient()
            connectionDiscovery = client
            let commands = self.commands
            let pending = pendingCommands
            client.start(
                onChange: { [weak self] bars in
                    pending.withLock { $0 += 1 }
                    commands.yield(.discovered(bars))
                    // Any list, even an empty one, means browsing works now
                    // (the browser reports one when access is allowed again).
                    Task { @MainActor in
                        if self?.state.localNetworkBlocked == true { self?.state.localNetworkBlocked = false }
                    }
                },
                onError: { [weak self] error in
                    pending.withLock { $0 += 1 }
                    commands.yield(.discoveryFailed(error))
                    if error == .localNetworkDenied {
                        Task { @MainActor in self?.state.localNetworkBlocked = true }
                    }
                }
            )
        } else if !wanted, let client = connectionDiscovery {
            client.stop()
            connectionDiscovery = nil
        }
    }

    private func save() {
        var saved = state.settings
        saved.pause = saved.pause.shifted(by: dependencies.clock.wallClockOffset())
        try? dependencies.settings.save(saved)
    }

    // MARK: - Monitors

    /// Starts the mic and camera monitors and the USB watcher (those in
    /// `which`). One that fails is recorded in `state.monitorProblems` (the
    /// menu shows Needs you for the mic and camera), logged, and tried again
    /// after `monitorRetryInterval`.
    private func startMonitors(_ which: MonitorProblems) {
        guard !stopped else { return }
        var failed: MonitorProblems = []
        let counter = callbackCounter
        let inFlight = callbacksInFlight
        if which.contains(.mic) {
            do {
                try dependencies.mic.start { [weak self] processes in
                    let number = counter.withLock { $0 += 1; return $0 }
                    inFlight.withLock { $0 += 1 }
                    Task { @MainActor in
                        self?.micChanged(processes, number: number)
                        inFlight.withLock { $0 -= 1 }
                    }
                }
                mic = dependencies.mic.current()
            } catch {
                failed.insert(.mic)
                Log.error("detection", "The mic monitor didn't start: \(error)")
            }
        }
        if which.contains(.camera) {
            do {
                try dependencies.camera.start { [weak self] inUse in
                    let number = counter.withLock { $0 += 1; return $0 }
                    inFlight.withLock { $0 += 1 }
                    Task { @MainActor in
                        self?.cameraChanged(inUse, number: number)
                        inFlight.withLock { $0 -= 1 }
                    }
                }
                cameraInUse = dependencies.camera.current()
            } catch {
                failed.insert(.camera)
                Log.error("detection", "The camera monitor didn't start: \(error)")
            }
        }
        if which.contains(.usb) {
            let commands = self.commands
            let pending = pendingCommands
            do {
                try dependencies.serialDevices.start { event in
                    pending.withLock { $0 += 1 }
                    commands.yield(.device(event))
                }
            } catch {
                failed.insert(.usb)
                Log.error("usb", "The USB device watcher didn't start: \(error)")
            }
        }
        state.monitorProblems.subtract(which)
        state.monitorProblems.formUnion(failed)
        observe()
        if !failed.isEmpty { scheduleMonitorRetry() }
    }

    private func scheduleMonitorRetry() {
        guard monitorRetryTask == nil, !stopped else { return }
        let clock = dependencies.clock
        monitorRetryTask = Task { [weak self] in
            do { try await clock.sleep(seconds: TinyBarEngine.monitorRetryInterval) } catch { return }
            guard let self else { return }
            self.monitorRetryTask = nil
            let failed = self.state.monitorProblems
            if !failed.isEmpty { self.startMonitors(failed) }
        }
    }

    // MARK: - For tests

    /// Nothing waiting for the connection, and the connection idle.
    var isSettled: Bool {
        get async {
            guard pendingCommands.get() == 0, callbacksInFlight.get() == 0 else { return false }
            if let tickDeadline, tickDeadline <= now { return false }
            guard await connection.isSettled else { return false }
            return await connection.state == state.connection
        }
    }

    /// The connection's `seq` so far.
    var lastSeq: UInt32 {
        get async { await connection.lastSeq }
    }

    // MARK: For the UI

    /// The menu as it should look now (Option held: `showDetails`).
    public func menuContent(showDetails: Bool) -> MenuContent {
        MenuPresenter.content(
            for: state,
            now: dependencies.clock.now(),
            timeZone: dependencies.timeZone,
            locale: dependencies.locale,
            clockOffset: dependencies.clock.wallClockOffset(),
            showDetails: showDetails
        )
    }

    /// Settings › Connection's top section.
    public func connectionSummary() -> ConnectionSummary {
        MenuPresenter.connectionSummary(for: state, now: dependencies.clock.now(), timeZone: dependencies.timeZone,
                                        locale: dependencies.locale, clockOffset: dependencies.clock.wallClockOffset())
    }
}
