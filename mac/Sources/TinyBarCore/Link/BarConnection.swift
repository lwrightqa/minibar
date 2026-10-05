import Foundation

/// The call state the app wants the bar to know about.
public struct ReportedCall: Hashable, Sendable {
    public var active: Bool
    /// The short name to send, or `nil`.
    public var app: String?
    /// The call's `call_id` while active.
    public var callID: UInt32?
    /// When the call started, for `elapsed_s`.
    public var startedAt: Date?

    public init(active: Bool, app: String? = nil, callID: UInt32? = nil, startedAt: Date? = nil) {
        self.active = active
        self.app = active ? app : nil
        self.callID = active ? callID : nil
        self.startedAt = active ? startedAt : nil
    }

    /// Not on a call.
    public static let idle = ReportedCall(active: false)

    /// The state of a detected call.
    public init(_ call: DetectedCall) {
        self.init(active: true, app: call.app, callID: call.callID, startedAt: call.startedAt)
    }

    /// The `call` message for this state (api.md 5.1): `call_id` and
    /// `elapsed_s` only while active, `leaving` only when `true` (and then
    /// `active` is sent as `false`), never `inputs`.
    public func request(client: String, session: String, seq: UInt32, at now: Date, leaving: Bool = false) -> CallRequest {
        let active = self.active && !leaving
        let elapsed = startedAt.map { start in
            min(max(Int(now.timeIntervalSince(start).rounded(.down)), 0), TinyBarAPI.Limits.elapsedSeconds.upperBound)
        }
        return CallRequest(
            client: client,
            session: session,
            seq: seq,
            active: active,
            app: active ? app : nil,
            inputs: nil,
            callID: active ? callID : nil,
            elapsedS: active ? (elapsed ?? 0) : nil,
            leaving: leaving ? true : nil
        )
    }
}

/// The link to the bar, as the menu and Settings show it.
public struct ConnectionState: Hashable, Sendable {
    public enum Phase: Hashable, Sendable {
        /// No bar set up: never connected and no address (menu: "Not set up yet").
        case notSetUp
        /// Within the 15-second grace after launch, wake or a link switch,
        /// before a reply ("Looking for TinyBar…").
        case looking
        /// The latest message got through.
        case connected
        /// No successful reply for 15 seconds ("Can't reach TinyBar 2A1C since 2:04 PM").
        case unreachable
        /// Wi-Fi is off in Settings and the bar isn't plugged in.
        case notPluggedIn
        /// macOS is blocking Local Network access, and USB isn't connected. Needs you.
        case localNetworkBlocked
        /// While on USB the bar reported Wi-Fi, but its address didn't answer
        /// from this Mac (client isolation, a VPN); now not plugged in.
        case wifiCantReachHere
        /// The paired bar refuses this Mac's token (`401`, or `403
        /// wrong_client` on a call), and USB isn't connected. Needs you: Pair Again.
        case unrecognized
        /// The bar's API major version is older than the app's. Needs you.
        case barNeedsUpdate
        /// The bar's API major version is newer than the app's. Needs you.
        case appNeedsUpdate
    }

    public var phase: Phase
    /// The link in use, if any.
    public var link: LinkKind?
    /// The paired bar. The engine saves it to the settings whenever it changes.
    public var bar: KnownBar?
    /// The latest `info` or `hello` reply.
    public var info: InfoReply?
    /// The latest `call` reply: `showing`, `screen`, `call.aside`, `sources.mac`.
    public var lastReply: CallReply?
    /// The last successful reply, in memory only.
    public var lastSuccess: Date?
    /// Pause USB is on.
    public var usbPaused: Bool
    /// A TinyBar is or was on USB this session (shows Pause USB in the Option-click menu).
    public var usbSeen: Bool
    /// For the Option-click details: the port or the address.
    public var endpointDescription: String?
    /// USB pairing was refused with `token_limit`: the bar works over USB only
    /// until a device is removed on the Remote (mac-app-ux.md 5.2). The app
    /// keeps asking while the bar stays plugged in.
    public var tokenLimitReached: Bool
    /// While on USB with the bar on Wi-Fi, whether the bar's Wi-Fi address
    /// answered from this Mac (mac-app-ux.md 4.3, 5.2). `nil` until checked,
    /// and again after a wake or a settings change.
    public var wifiReachableHere: Bool?

    public init(
        phase: Phase,
        link: LinkKind? = nil,
        bar: KnownBar? = nil,
        info: InfoReply? = nil,
        lastReply: CallReply? = nil,
        lastSuccess: Date? = nil,
        usbPaused: Bool = false,
        usbSeen: Bool = false,
        endpointDescription: String? = nil,
        tokenLimitReached: Bool = false,
        wifiReachableHere: Bool? = nil
    ) {
        self.phase = phase
        self.link = link
        self.bar = bar
        self.info = info
        self.lastReply = lastReply
        self.lastSuccess = lastSuccess
        self.usbPaused = usbPaused
        self.usbSeen = usbSeen
        self.endpointDescription = endpointDescription
        self.tokenLimitReached = tokenLimitReached
        self.wifiReachableHere = wifiReachableHere
    }
}

/// Keeps the bar told about the call state, over the best link, for as long
/// as the app runs (api.md 5, 6.8 and 16; docs/mac-app.md "Talking to the bar").
///
/// What it does:
/// - **Links.** Opens each TinyBar-looking serial device (`openUSB`, then
///   `handshake`); uses USB whenever a TinyBar answers there, else Wi-Fi to
///   the paired bar (Bonjour match by `device_id`, the manual address, or the
///   last address that worked), one link at a time (`LinkChooser`). Switches
///   within 5 seconds and sends the full state at once after every switch.
///   Ignores a device that didn't answer as a TinyBar, and a forgotten bar's
///   port, until it's unplugged and plugged in again.
/// - **Pairing over USB.** When a TinyBar answers on USB and there's no token
///   for it (or Wi-Fi answered `401`), sends `pair` at once and stores the
///   token (api.md 6.6); if that fails (`busy`, no reply, `token_limit`), tries
///   again every `usbPairRetryInterval` while the bar stays plugged in. A
///   different bar on USB becomes the paired bar, and the old token is deleted
///   (mac-app-ux.md 5.3). `auth: none` skips pairing.
/// - **Messages.** Sends `report(_:)` at once, and the full state every
///   `heartbeat_s` (from the latest reply), idle or not, with `seq` going up by
///   1 per message in this `session`. One request in flight at a time; a newer
///   state replaces a queued one.
/// - **Failures.** Retries on the `RetrySchedule`; Not connected after 15
///   seconds without success (`ReachabilityTracker`). Over USB, 3 replies in a
///   row missed → close the port, use Wi-Fi, reopen after 10 seconds or when
///   IOKit reports the device again. On a `ready` event: `hello`, then the state.
/// - **Checks.** Every reply's `device_id` against the paired bar's. On `401`
///   over Wi-Fi, or `403 wrong_client` (a token tied to another install ID,
///   api.md 5.2): `GET info` first; another bar at that address → find the
///   right one again and keep the token; the paired bar itself → delete the
///   token and report `.unrecognized` (api.md 16). `unsupported_api` or an
///   `info` with another major version → `.barNeedsUpdate`/`.appNeedsUpdate`.
///   A device that answers the USB handshake with errors only is tried again
///   after 10 seconds, not ignored.
/// - **Leaving.** `sendLeaving(resumeAfter:)` sends `"leaving": true` with
///   `"active": false` over the current link, waiting at most about a second,
///   then sends nothing until `didWake()` (sleep) or until `resumeAfter` has
///   passed (a power-off that may still be canceled).
///
/// All of this is testable on Linux with a `ManualClock`, a fake
/// `TransportFactory` and an `InMemoryTokenStore`.
public actor BarConnection {
    public struct Configuration: Hashable, Sendable {
        /// `client`: the install ID.
        public var clientID: String
        /// `session`: new each launch.
        public var sessionID: String
        /// For `hello`'s `app_version`, for example `"1.0 (12)"`.
        public var appVersion: String
        /// Name for this Mac, sent as `name` in `hello` and `pair/start`.
        public var macName: String?
        /// Use Wi-Fi when TinyBar isn't plugged in.
        public var useWiFi: Bool
        /// Settings › Advanced › Address, parsed.
        public var manualEndpoint: BarEndpoint?
        /// **Proposed** (api.md 14.2): send the Mac's time and time zone in
        /// `hello`. Off until the user agrees.
        public var sendClockInHello: Bool
        /// The Mac's time zone, for `hello` when `sendClockInHello` is on.
        public var timeZone: TimeZone

        public init(
            clientID: String,
            sessionID: String,
            appVersion: String,
            macName: String? = nil,
            useWiFi: Bool = true,
            manualEndpoint: BarEndpoint? = nil,
            sendClockInHello: Bool = false,
            timeZone: TimeZone = .autoupdatingCurrent
        ) {
            self.clientID = clientID
            self.sessionID = sessionID
            self.appVersion = appVersion
            self.macName = macName
            self.useWiFi = useWiFi
            self.manualEndpoint = manualEndpoint
            self.sendClockInHello = sendClockInHello
            self.timeZone = timeZone
        }
    }

    /// Every change of `state`, for the engine. One consumer.
    public nonisolated let states: AsyncStream<ConnectionState>
    private let statesContinuation: AsyncStream<ConnectionState>.Continuation

    /// The current state.
    public private(set) var state: ConnectionState

    private var configuration: Configuration
    private let clock: any TinyClock
    private let transports: any TransportFactory
    private let tokens: any TokenStore
    private let retry = RetrySchedule.standard

    // MARK: What to send

    /// The call state the bar should know about.
    private var call = ReportedCall.idle
    /// The last `seq` sent in this session.
    private var seq: UInt32 = 0
    /// Send at once: the state changed, or a link came up.
    private var dirty = true
    /// `leaving` was sent (sleep, quit, power-off): nothing more until
    /// `didWake()`, or until `resumeAt`.
    private var suspended = false
    /// After `leaving` for a power-off: if the app is still running then, the
    /// power-off was canceled (another app refused to quit), so carry on.
    private var resumeAt: Date?
    /// Forget This TinyBar: no messages until a bar is adopted or plugged in.
    private var messagesStopped = false
    private var started = false
    private var stopped = false

    // MARK: How sending went

    /// The link the latest message went over, or was about to.
    private var lastLinkKind: LinkKind?
    /// Its address, for Wi-Fi.
    private var lastLinkEndpoint: BarEndpoint?
    /// The latest message got through.
    private var lastAttemptOK = false
    /// When the latest message got through.
    private var lastSentAt: Date?
    /// Failures in a row.
    private var failures = 0
    /// After a failure: when to try again.
    private var retryAt: Date?
    /// `heartbeat_s` from the latest reply.
    private var heartbeat = TimeInterval(TinyBarAPI.Defaults.heartbeatSeconds)
    private var reachability = ReachabilityTracker()

    // MARK: USB

    private struct USBLink {
        var transport: any USBLinkTransport
        var device: SerialDevice
        var info: InfoReply
        var id: ObjectIdentifier { ObjectIdentifier(transport) }
    }

    /// Espressif USB Serial/JTAG devices present now, by registry ID.
    private var devices: [UInt64: SerialDevice] = [:]
    /// Left alone until unplugged: not a TinyBar, a version the app can't
    /// speak, or the bar that was just forgotten.
    private var ignoredDevices: Set<UInt64> = []
    /// Devices to try again later (missed replies, port busy), and when.
    private var reopenAt: [UInt64: Date] = [:]
    private var handshaking: Set<UInt64> = []
    private var usb: USBLink?
    private var usbEventsTask: Task<Void, Never>?
    /// USB replies missed in a row.
    private var usbMissed = 0
    /// Send `hello` before the next USB message (after `ready`, or when replies stopped).
    private var usbNeedsHello = false
    /// Pause USB ends when the bar is unplugged and plugged in again.
    private var unpauseOnNextArrival = false

    // MARK: Wi-Fi

    private struct WiFiLink {
        var transport: any WiFiLinkTransport
        var endpoint: BarEndpoint
        var token: String?
    }

    private var wifi: WiFiLink?
    private var discoveredBars: [DiscoveredBar] = []
    /// Addresses that answered as another bar (api.md 16).
    private var wrongEndpoints: Set<BarEndpoint> = []
    /// The paired bar refuses this Mac's token, or wants one the app doesn't have.
    private var unauthorized = false
    private var localNetworkDenied = false
    /// The bar is on Wi-Fi, but its address didn't answer from this Mac (mac-app-ux.md 4.3).
    private var wifiCantReachHere: Bool { state.wifiReachableHere == false }
    /// When to send `pair` over USB again, while the bar is plugged in and
    /// there's no token. `nil`: as soon as it's needed.
    private var usbPairRetryAt: Date?

    /// How long to wait before asking a plugged-in bar to pair again after
    /// `pair` failed (`busy`, no reply, or `token_limit` until a device is
    /// removed on the Remote).
    public static let usbPairRetryInterval: TimeInterval = 30
    /// After `leaving` for a power-off, how long until the app carries on if
    /// it's still running (the power-off was canceled).
    public static let powerOffResumeAfter: TimeInterval = 10
    /// The bar or the app needs an update.
    private var versionProblem: ConnectionState.Phase?
    /// The paired bar's token, read from the token store once.
    private var cachedToken: (deviceID: String, token: String?)?

    // MARK: The loop

    private var loopTask: Task<Void, Never>?
    private var wakeContinuation: CheckedContinuation<Void, Never>?
    private var wakePending = false
    /// The deadline the loop is sleeping until, while it sleeps.
    private var parkedDeadline: Date?
    private var isParked = false
    /// Handshakes, Wi-Fi checks and other work started in the background.
    private var backgroundWork = 0
    private var lastPublished: ConnectionState?

    public init(
        configuration: Configuration,
        bar: KnownBar?,
        clock: any TinyClock,
        transports: any TransportFactory,
        tokens: any TokenStore
    ) {
        self.configuration = configuration
        self.clock = clock
        self.transports = transports
        self.tokens = tokens
        self.state = ConnectionState(phase: bar == nil ? .notSetUp : .looking, bar: bar)
        let (stream, continuation) = AsyncStream.makeStream(of: ConnectionState.self, bufferingPolicy: .bufferingNewest(32))
        self.states = stream
        self.statesContinuation = continuation
    }

    /// Starts the heartbeat loop and the grace period.
    public func start() {
        guard !started, !stopped else { return }
        started = true
        reachability.beginGrace(at: clock.now())
        loopTask = Task { await self.run() }
        publish()
    }

    /// Stops everything and closes the links (after `sendLeaving()` on quit).
    public func stop() async {
        guard !stopped else { return }
        stopped = true
        loopTask?.cancel()
        wake()
        usbEventsTask?.cancel()
        if let usb { await usb.transport.close() }
        if let wifi { await wifi.transport.close() }
        usb = nil
        wifi = nil
        publish()
        statesContinuation.finish()
    }

    /// The call state changed: send it at once, then keep repeating it.
    public func report(_ call: ReportedCall) {
        guard call != self.call else { return }
        self.call = call
        dirty = true
        wake()
    }

    /// Sends `"leaving": true` (quit, sleep, power off), waiting at most about
    /// a second. Best effort: never throws. Nothing more is sent until
    /// `didWake()`, or, with `resumeAfter`, until that long has passed: a
    /// logout or shutdown can still be canceled, and then no wake follows.
    /// Meanwhile the state isn't `.connected`.
    public func sendLeaving(resumeAfter: TimeInterval? = nil) async {
        let now = clock.now()
        suspended = true
        resumeAt = resumeAfter.map { now.addingTimeInterval($0) }
        lastAttemptOK = false
        reachability.beginGrace(at: now)
        defer {
            publish()
            wake()
        }
        let transport: (any Transport)?
        switch currentChoice() {
        case .usb: transport = usb?.transport
        case .wifi(let endpoint): transport = wifi?.endpoint == endpoint ? wifi?.transport : nil
        case .none: transport = nil
        }
        guard let transport, !messagesStopped else { return }
        let request = call.request(client: configuration.clientID, session: configuration.sessionID,
                                   seq: nextSeq(), at: clock.now(), leaving: true)
        await BarConnection.withTimeout(1, clock: clock) {
            _ = try? await transport.sendCall(request)
        }
    }

    /// The Mac woke: start a grace period and send the state at once.
    public func didWake() {
        suspended = false
        resumeAt = nil
        state.wifiReachableHere = nil
        usbPairRetryAt = nil
        reachability.beginGrace(at: clock.now())
        lastAttemptOK = false
        failures = 0
        retryAt = nil
        dirty = true
        wake()
    }

    /// From the IOKit watcher.
    public func handle(_ event: SerialDeviceEvent) {
        switch event {
        case .appeared(let device):
            // Never anything but Espressif's USB Serial/JTAG (criterion 16).
            guard device.isEspressifSerialJTAG else { return }
            if unpauseOnNextArrival, state.usbPaused {
                state.usbPaused = false
            }
            unpauseOnNextArrival = false
            devices[device.registryID] = device
            reopenAt[device.registryID] = nil
        case .disappeared(let device):
            devices[device.registryID] = nil
            ignoredDevices.remove(device.registryID)
            reopenAt[device.registryID] = nil
            if state.usbPaused { unpauseOnNextArrival = true }
            if usb?.device.registryID == device.registryID {
                dropUSB(reopenAfter: nil)
            }
        }
        wake()
    }

    /// From the Bonjour browser: the bars on the network now.
    /// Any list, even an empty one, means browsing works (the browser reports
    /// one when Local Network access is allowed again).
    public func discovered(_ bars: [DiscoveredBar]) {
        discoveredBars = bars
        localNetworkDenied = false
        wake()
    }

    /// From the Bonjour browser.
    public func discoveryFailed(_ error: DiscoveryError) {
        if error == .localNetworkDenied { localNetworkDenied = true }
        publish()
    }

    /// Pause USB / Resume USB (api.md 6.3).
    public func setUSBPaused(_ paused: Bool) {
        guard paused != state.usbPaused else { return }
        state.usbPaused = paused
        unpauseOnNextArrival = false
        if paused, usb != nil {
            dropUSB(reopenAfter: nil)
        }
        dirty = true
        wake()
    }

    /// New settings (Wi-Fi switch, address, Mac name).
    public func update(_ configuration: Configuration) {
        let old = self.configuration
        self.configuration = configuration
        if old.useWiFi != configuration.useWiFi || old.manualEndpoint != configuration.manualEndpoint {
            closeWiFi()
            wrongEndpoints.removeAll()
            state.wifiReachableHere = nil
            dirty = true
        }
        wake()
    }

    /// Use this bar from now on (after pairing over Wi-Fi; the token is
    /// already in the token store).
    public func adopt(_ bar: KnownBar) {
        if let old = state.bar, old.deviceID != bar.deviceID {
            try? tokens.removeToken(for: old.deviceID)
        }
        state.bar = bar
        cachedToken = nil
        unauthorized = false
        messagesStopped = false
        versionProblem = nil
        wrongEndpoints.removeAll()
        closeWiFi()
        reachability.beginGrace(at: clock.now())
        lastAttemptOK = false
        failures = 0
        retryAt = nil
        dirty = true
        wake()
    }

    /// Forget This TinyBar: tells the bar (`DELETE /api/v1/clients/self`, best
    /// effort), deletes the token, and leaves a plugged-in bar's port alone
    /// until it's replugged (mac-app-ux.md 5.7).
    public func forget() async {
        guard let bar = state.bar else { return }
        messagesStopped = true
        if let token = token(for: bar), let endpoint = wifi?.endpoint ?? wifiEndpoint() {
            let transport: any WiFiLinkTransport
            if let wifi, wifi.endpoint == endpoint, wifi.token == token {
                transport = wifi.transport
            } else {
                transport = transports.makeWiFi(endpoint: endpoint, token: token)
            }
            await BarConnection.withTimeout(2, clock: clock) {
                _ = try? await transport.unpairSelf()
            }
        }
        try? tokens.removeToken(for: bar.deviceID)
        cachedToken = nil
        if let usb {
            ignoredDevices.insert(usb.device.registryID)
            dropUSB(reopenAfter: nil)
        }
        closeWiFi()
        state.bar = nil
        state.info = nil
        state.lastReply = nil
        state.tokenLimitReached = false
        state.wifiReachableHere = nil
        usbPairRetryAt = nil
        unauthorized = false
        versionProblem = nil
        lastAttemptOK = false
        publish()
    }

    /// The latest `status` from the bar, over the current link (for the
    /// "Wi-Fi can't reach it here" check and the Option-click details).
    public func fetchStatus() async throws -> StatusReply {
        switch currentChoice() {
        case .usb:
            guard let usb else { throw BarError.closed }
            return try await usb.transport.status()
        case .wifi(let endpoint):
            guard let link = try await wifiLink(for: endpoint) else { throw BarError.api(BarConnection.noToken, httpStatus: nil) }
            return try await link.transport.status()
        case .none:
            throw BarError.closed
        }
    }

    // MARK: - The loop

    private func run() async {
        while !stopped, !Task.isCancelled {
            await step()
            if stopped { break }
            // A send that became due while this step was waiting on the
            // bar (a link dropped, the state changed) goes out now.
            if sendIsDue() {
                await Task.yield()
                continue
            }
            await sleep(until: nextDeadline())
        }
    }

    private func sendIsDue() -> Bool {
        guard !suspended, !messagesStopped, dirty else { return false }
        if case .none = currentChoice() { return false }
        return true
    }

    /// One pass: starts USB handshakes, then sends the state if it's due.
    private func step() async {
        startUSBHandshakes()
        defer { publish() }
        if suspended, let resumeAt, clock.now() >= resumeAt {
            // Still running after a power-off: it was canceled. Carry on.
            resume()
        }
        guard !suspended, !messagesStopped else { return }
        if let link = usb, usbPairIsDue(at: clock.now()) {
            await pairOverUSB(link.transport, deviceID: link.info.deviceID)
        }
        let choice = currentChoice()
        let kind: LinkKind?
        switch choice {
        case .usb: kind = .usb
        case .wifi: kind = .wifi
        case .none: kind = nil
        }
        guard let kind else { return }
        var endpoint: BarEndpoint?
        if case .wifi(let address) = choice { endpoint = address }
        if kind != lastLinkKind || endpoint != lastLinkEndpoint {
            // A new link or address: the full state at once (api.md 6.8, 16).
            // Switching between USB and Wi-Fi gets a new grace period.
            if lastLinkKind != nil, kind != lastLinkKind { reachability.beginGrace(at: clock.now()) }
            lastLinkKind = kind
            lastLinkEndpoint = endpoint
            lastAttemptOK = false
            failures = 0
            retryAt = nil
            dirty = true
        }
        guard isDue(at: clock.now()) else { return }
        await send(over: choice)
    }

    /// Carries on after `leaving` for a power-off that didn't happen.
    private func resume() {
        suspended = false
        resumeAt = nil
        reachability.beginGrace(at: clock.now())
        lastAttemptOK = false
        failures = 0
        retryAt = nil
        dirty = true
    }

    /// A plugged-in bar wants a token and the app has none, and it's time to ask.
    private func usbNeedsPairing() -> Bool {
        guard let usb, usb.info.auth == .bearer, let bar = state.bar, bar.deviceID == usb.info.deviceID else { return false }
        return token(for: bar) == nil
    }

    private func usbPairIsDue(at now: Date) -> Bool {
        guard usbNeedsPairing() else { return false }
        guard let usbPairRetryAt else { return true }
        return now >= usbPairRetryAt
    }

    private func isDue(at now: Date) -> Bool {
        if dirty { return true }
        if lastAttemptOK, let lastSentAt { return now >= lastSentAt.addingTimeInterval(heartbeat) }
        if let retryAt { return now >= retryAt }
        return true
    }

    /// When the loop must wake on its own: the heartbeat or a retry, the end
    /// of the grace period, or a USB device to try again.
    private func nextDeadline() -> Date? {
        var deadlines: [Date] = []
        let now = clock.now()
        var linkAvailable = true
        if case .none = currentChoice() { linkAvailable = false }
        if !suspended, !messagesStopped, linkAvailable, lastLinkKind != nil {
            if lastAttemptOK, let lastSentAt {
                deadlines.append(lastSentAt.addingTimeInterval(heartbeat))
            } else if let retryAt {
                deadlines.append(retryAt)
            }
        }
        if let graceEnd = reachability.graceEnd { deadlines.append(graceEnd) }
        deadlines += reopenAt.values
        if suspended, let resumeAt { deadlines.append(resumeAt) }
        if !suspended, !messagesStopped, let usbPairRetryAt, usbNeedsPairing() { deadlines.append(usbPairRetryAt) }
        // Anything due now was handled by the step that just ran; waking for
        // it again would only spin.
        return deadlines.filter { $0 > now }.min()
    }

    private func sleep(until deadline: Date?) async {
        if wakePending {
            wakePending = false
            return
        }
        var timer: Task<Void, Never>?
        if let deadline {
            let clock = self.clock
            timer = Task { [weak self] in
                do { try await clock.sleep(until: deadline) } catch { return }
                await self?.wake()
            }
        }
        parkedDeadline = deadline
        isParked = true
        await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
            if wakePending || stopped {
                wakePending = false
                continuation.resume()
            } else {
                wakeContinuation = continuation
            }
        }
        isParked = false
        parkedDeadline = nil
        timer?.cancel()
    }

    private func wake() {
        if let continuation = wakeContinuation {
            wakeContinuation = nil
            continuation.resume()
        } else {
            wakePending = true
        }
    }

    // MARK: - Sending

    private func nextSeq() -> UInt32 {
        seq = seq == UInt32.max ? 1 : seq + 1
        return seq
    }

    /// The message for the current state, with the next `seq`. Made right
    /// before it's sent, so `seq` only counts messages that went out.
    private func nextRequest() -> CallRequest {
        call.request(client: configuration.clientID, session: configuration.sessionID, seq: nextSeq(), at: clock.now())
    }

    private func send(over choice: LinkChoice) async {
        dirty = false
        switch choice {
        case .usb:
            guard let link = usb else { return }
            do {
                if usbNeedsHello {
                    let info = try await link.transport.hello(helloRequest())
                    try check(info)
                    usbNeedsHello = false
                    state.info = info
                }
                let reply = try await link.transport.sendCall(nextRequest())
                guard usb?.id == link.id else { return }
                try check(reply)
                succeeded(reply, over: .usb)
                usbMissed = 0
            } catch {
                guard usb?.id == link.id else { return }
                usbFailed(BarConnection.barError(error), link: link)
            }
        case .wifi(let endpoint):
            do {
                guard let link = try await wifiLink(for: endpoint) else {
                    // A bar that wants a token the app doesn't have: nothing
                    // to send until it's paired (mac-app-ux.md 4.3).
                    unauthorized = true
                    lastAttemptOK = false
                    return
                }
                let reply = try await link.transport.sendCall(nextRequest())
                try check(reply)
                guard currentChoice() == choice else { return }
                succeeded(reply, over: .wifi)
                if state.bar?.lastEndpoint != endpoint { state.bar?.lastEndpoint = endpoint }
                unauthorized = false
                localNetworkDenied = false
            } catch {
                await wifiFailed(BarConnection.barError(error), endpoint: endpoint)
            }
        case .none:
            return
        }
    }

    private func succeeded(_ reply: CallReply, over kind: LinkKind) {
        let now = clock.now()
        reachability.recordSuccess(at: now)
        lastAttemptOK = true
        lastSentAt = now
        failures = 0
        retryAt = nil
        heartbeat = TimeInterval(max(reply.heartbeatS, 1))
        state.lastReply = reply
        state.lastSuccess = now
    }

    private func recordFailure() {
        let now = clock.now()
        reachability.recordFailure(at: now)
        lastAttemptOK = false
        failures += 1
        retryAt = now.addingTimeInterval(retry.delay(afterFailures: failures))
    }

    private func usbFailed(_ error: BarError, link: USBLink) {
        recordFailure()
        switch error {
        case .timedOut:
            usbMissed += 1
            usbNeedsHello = true
            if usbMissed >= TinyBarAPI.Timeouts.usbMissedRepliesBeforeClose {
                // api.md 6.8: close the port, use Wi-Fi at once, reopen later.
                dropUSB(reopenAfter: TinyBarAPI.Timeouts.usbReopenAfter)
            }
        case .closed:
            dropUSB(reopenAfter: TinyBarAPI.Timeouts.usbReopenAfter)
        case .api(let body, _) where body.error == .unsupportedAPI:
            versionProblem = .appNeedsUpdate
            ignoredDevices.insert(link.device.registryID)
            dropUSB(reopenAfter: nil)
        case .wrongDevice:
            // Another bar answers on this port now: greet it again later.
            dropUSB(reopenAfter: TinyBarAPI.Timeouts.usbReopenAfter)
        default:
            break
        }
    }

    private func wifiFailed(_ error: BarError, endpoint: BarEndpoint) async {
        recordFailure()
        switch error {
        case _ where error.refusesToken:
            // `401`, or `403 wrong_client` (the token is tied to another
            // install ID, api.md 5.2): either way this Mac's token doesn't
            // match, so drop it and offer Pair Again.
            await handleUnauthorized(at: endpoint)
        case .wrongDevice, .notATinyBar:
            markWrong(endpoint)
        case .localNetworkDenied:
            localNetworkDenied = true
        case .api(let body, _) where body.error == .unsupportedAPI:
            versionProblem = .appNeedsUpdate
        default:
            break
        }
    }

    /// api.md 16: on `401` (or `403 wrong_client`), check `info` first. Only
    /// when the paired bar itself answers `info` is the token deleted (and
    /// Pair Again asked for).
    /// Another bar, or something that isn't a TinyBar (another device took
    /// the address and wants a login): find the right one again and keep the
    /// token. No answer: keep the token and try again later.
    private func handleUnauthorized(at endpoint: BarEndpoint) async {
        guard let bar = state.bar else { return }
        let probe = transports.makeWiFi(endpoint: endpoint, token: nil)
        let result: Result<InfoReply, BarError>
        do {
            result = .success(try await probe.hello(helloRequest()))
        } catch {
            result = .failure(BarConnection.barError(error))
        }
        await probe.close()
        switch result {
        case .success(let info) where info.isTinyBar && info.deviceID == bar.deviceID:
            break
        case .success:
            markWrong(endpoint)
            return
        case .failure(let error):
            switch error {
            case .timedOut, .unreachable, .closed, .cancelled, .localNetworkDenied:
                // Can't tell who refused: keep everything, and retry later.
                closeWiFi()
            default:
                // A 401 to `info` too, or not a TinyBar's reply: not the paired bar.
                markWrong(endpoint)
            }
            return
        }
        try? tokens.removeToken(for: bar.deviceID)
        cachedToken = (bar.deviceID, nil)
        state.bar?.tokenID = nil
        unauthorized = true
        closeWiFi()
    }

    private func markWrong(_ endpoint: BarEndpoint) {
        if endpoint != configuration.manualEndpoint { wrongEndpoints.insert(endpoint) }
        if state.bar?.lastEndpoint == endpoint { state.bar?.lastEndpoint = nil }
        closeWiFi()
    }

    /// Checks that a reply came from the paired bar (api.md 5.3).
    private func check(_ reply: CallReply) throws(BarError) {
        if let bar = state.bar, reply.deviceID != bar.deviceID {
            throw .wrongDevice(expected: bar.deviceID, got: reply.deviceID)
        }
    }

    private func check(_ info: InfoReply) throws(BarError) {
        guard info.isTinyBar else { throw .notATinyBar }
        if let bar = state.bar, info.deviceID != bar.deviceID {
            throw .wrongDevice(expected: bar.deviceID, got: info.deviceID)
        }
        if let problem = BarConnection.versionProblem(info) {
            versionProblem = problem
            throw .api(APIErrorBody(error: .unsupportedAPI, message: "API \(info.api)"), httpStatus: nil)
        }
    }

    static func versionProblem(_ info: InfoReply) -> ConnectionState.Phase? {
        guard let version = info.apiVersion else { return .appNeedsUpdate }
        if version.isCompatibleWithApp { return nil }
        return version.major > APIVersion.current.major ? .appNeedsUpdate : .barNeedsUpdate
    }

    private static let noToken = APIErrorBody(error: .unauthorized, message: "No token for this TinyBar.")

    private static func barError(_ error: any Error) -> BarError {
        if let error = error as? BarError { return error }
        if error is CancellationError { return .cancelled }
        return .closed
    }

    // MARK: - Choosing the link

    private func currentChoice() -> LinkChoice {
        let bar = state.bar
        let authorized: Bool
        if let bar {
            authorized = bar.auth == .notRequired || token(for: bar) != nil
        } else {
            authorized = true
        }
        return LinkChooser.choose(LinkInputs(
            usbReady: usb != nil,
            usbPaused: state.usbPaused,
            wifiEnabled: configuration.useWiFi,
            wifiEndpoint: wifiEndpoint(),
            wifiAuthorized: authorized,
            isSetUp: bar != nil || configuration.manualEndpoint != nil
        ))
    }

    /// The paired bar's Wi-Fi address: the typed one, else the Bonjour match
    /// by `device_id` (api.md 3), else the last that worked, else its mDNS name.
    private func wifiEndpoint() -> BarEndpoint? {
        guard configuration.useWiFi else { return nil }
        if let manual = configuration.manualEndpoint { return manual }
        guard let bar = state.bar else { return nil }
        if let found = discoveredBars.first(where: { $0.deviceID == bar.deviceID }) { return found.endpoint }
        if let last = bar.lastEndpoint, !wrongEndpoints.contains(last) { return last }
        if let host = bar.host, !host.isEmpty {
            let endpoint = BarEndpoint(host: host)
            if !wrongEndpoints.contains(endpoint) { return endpoint }
        }
        return nil
    }

    private func token(for bar: KnownBar) -> String? {
        if let cachedToken, cachedToken.deviceID == bar.deviceID { return cachedToken.token }
        let token = (try? tokens.token(for: bar.deviceID)) ?? nil
        cachedToken = (bar.deviceID, token)
        return token
    }

    /// The Wi-Fi transport for `endpoint`, made and checked with `info` when
    /// the address or token changes. `nil` when the bar wants a token the
    /// app doesn't have.
    private func wifiLink(for endpoint: BarEndpoint) async throws(BarError) -> WiFiLink? {
        let token = state.bar.flatMap { self.token(for: $0) }
        if let wifi, wifi.endpoint == endpoint, wifi.token == token { return wifi }
        closeWiFi()
        let transport = transports.makeWiFi(endpoint: endpoint, token: token)
        let info: InfoReply
        do {
            info = try await transport.hello(helloRequest())
            try check(info)
        } catch {
            await transport.close()
            throw BarConnection.barError(error)
        }
        if state.bar == nil {
            // An address typed in Settings, with no bar known yet.
            state.bar = KnownBar(info: info, endpoint: endpoint)
        } else {
            state.bar?.update(from: info)
        }
        state.info = info
        versionProblem = nil
        if info.auth == .bearer, token == nil {
            await transport.close()
            return nil
        }
        let link = WiFiLink(transport: transport, endpoint: endpoint, token: token)
        wifi = link
        return link
    }

    private func closeWiFi() {
        guard let old = wifi else { return }
        wifi = nil
        Task { await old.transport.close() }
    }

    // MARK: - USB

    private func helloRequest() -> HelloRequest {
        let now = clock.now()
        return HelloRequest(
            client: configuration.clientID,
            name: configuration.macName,
            appVersion: configuration.appVersion,
            time: configuration.sendClockInHello ? BarTime(now, timeZone: configuration.timeZone) : nil,
            timeZone: configuration.sendClockInHello ? configuration.timeZone.identifier : nil
        )
    }

    /// Opens and greets every TinyBar-looking device that isn't ignored,
    /// waiting or already open.
    private func startUSBHandshakes() {
        guard !state.usbPaused, usb == nil, !stopped else { return }
        let now = clock.now()
        for (id, device) in devices where !ignoredDevices.contains(id) && !handshaking.contains(id) {
            if let at = reopenAt[id] {
                guard at <= now else { continue }
                reopenAt[id] = nil
            }
            handshaking.insert(id)
            backgroundWork += 1
            let transports = self.transports
            let hello = helloRequest()
            Task {
                let result: Result<USBHandshakeResult, any Error>
                do {
                    let transport = try transports.openUSB(device)
                    do {
                        let info = try await transport.handshake(hello)
                        result = .success(USBHandshakeResult(transport: transport, info: info))
                    } catch {
                        await transport.close()
                        result = .failure(error)
                    }
                } catch {
                    result = .failure(error)
                }
                await self.handshakeFinished(device, result)
            }
        }
    }

    private struct USBHandshakeResult: Sendable {
        var transport: any USBLinkTransport
        var info: InfoReply
    }

    private func handshakeFinished(_ device: SerialDevice, _ result: Result<USBHandshakeResult, any Error>) async {
        defer {
            handshaking.remove(device.registryID)
            backgroundWork -= 1
            wake()
        }
        let id = device.registryID
        let usable = devices[id] != nil && !stopped && !state.usbPaused && usb == nil && !ignoredDevices.contains(id)
        let transport: any USBLinkTransport
        let info: InfoReply
        switch result {
        case .failure(let error):
            if (error as? SerialPortError) == .busy {
                // A flasher has it; try again later.
                reopenAt[id] = clock.now().addingTimeInterval(TinyBarAPI.Timeouts.usbReopenAfter)
            } else if (error as? BarError)?.apiCode == .unsupportedAPI {
                versionProblem = .appNeedsUpdate
                ignoredDevices.insert(id)
            } else if case .api? = error as? BarError, devices[id] != nil {
                // Error replies only: a TinyBar that wasn't ready (garbled
                // input at boot, out of memory). Greet it again later.
                reopenAt[id] = clock.now().addingTimeInterval(TinyBarAPI.Timeouts.usbReopenAfter)
            } else if devices[id] != nil {
                // Not a TinyBar (or it went away): leave it alone until replugged (api.md 6.2).
                ignoredDevices.insert(id)
            }
            return
        case .success(let success):
            transport = success.transport
            info = success.info
        }
        guard usable else {
            await transport.close()
            return
        }
        if let problem = BarConnection.versionProblem(info) {
            versionProblem = problem
            ignoredDevices.insert(id)
            await transport.close()
            return
        }

        // This is your TinyBar now (mac-app-ux.md 5.3).
        if let old = state.bar, old.deviceID != info.deviceID {
            forgetPreviousBar(old)
            state.bar = nil
        }
        if state.bar == nil {
            state.bar = KnownBar(info: info, endpoint: nil)
        } else {
            state.bar?.update(from: info)
        }
        messagesStopped = false
        versionProblem = nil
        state.info = info
        state.usbSeen = true

        // Pair over the cable when there's no token (api.md 6.6).
        usbPairRetryAt = nil
        if info.auth == .bearer, let bar = state.bar, token(for: bar) == nil {
            await pairOverUSB(transport, deviceID: info.deviceID)
        }
        guard devices[id] != nil, !stopped, !state.usbPaused, usb == nil else {
            await transport.close()
            return
        }
        usb = USBLink(transport: transport, device: device, info: info)
        usbMissed = 0
        usbNeedsHello = false
        dirty = true
        listen(to: transport)
        if info.wifi == .connected, configuration.useWiFi {
            checkWiFiReach(info)
        }
    }

    /// Sends `pair` over USB and keeps the token. On failure, asks again
    /// after `usbPairRetryInterval` while the bar stays plugged in.
    private func pairOverUSB(_ transport: any USBLinkTransport, deviceID: String) async {
        do {
            let reply = try await transport.pair(USBPairRequest(client: configuration.clientID))
            guard reply.deviceID == deviceID, state.bar?.deviceID == deviceID else {
                throw BarError.wrongDevice(expected: deviceID, got: reply.deviceID)
            }
            if let token = reply.token {
                do {
                    try tokens.setToken(token, for: reply.deviceID)
                } catch {
                    Log.error("pairing", "Couldn't keep TinyBar's token: \(error)")
                }
                cachedToken = (reply.deviceID, token)
            }
            state.bar?.tokenID = reply.tokenID
            state.bar?.name = reply.name
            if let host = reply.host { state.bar?.host = host }
            state.tokenLimitReached = false
            unauthorized = false
            usbPairRetryAt = nil
        } catch {
            if (error as? BarError)?.apiCode == .tokenLimit {
                state.tokenLimitReached = true
            }
            usbPairRetryAt = clock.now().addingTimeInterval(BarConnection.usbPairRetryInterval)
        }
    }

    /// A different TinyBar was plugged in: delete the old one's token, and
    /// tell it to forget this Mac if it can be reached.
    private func forgetPreviousBar(_ old: KnownBar) {
        let oldToken = token(for: old)
        try? tokens.removeToken(for: old.deviceID)
        cachedToken = nil
        if let oldToken, let endpoint = wifi?.endpoint ?? wifiEndpoint() {
            let transport = transports.makeWiFi(endpoint: endpoint, token: oldToken)
            let clock = self.clock
            Task {
                await BarConnection.withTimeout(5, clock: clock) { _ = try? await transport.unpairSelf() }
                await transport.close()
            }
        }
        closeWiFi()
        discoveredBars = []
        wrongEndpoints.removeAll()
        unauthorized = false
        state.tokenLimitReached = false
        state.wifiReachableHere = nil
        state.lastReply = nil
    }

    private func listen(to transport: any USBLinkTransport) {
        usbEventsTask?.cancel()
        let id = ObjectIdentifier(transport)
        let events = transport.events
        usbEventsTask = Task { [weak self] in
            for await event in events {
                await self?.usbEvent(event, from: id)
            }
        }
    }

    private func usbEvent(_ event: USBEvent, from id: ObjectIdentifier) {
        guard usb?.id == id else { return }
        switch event {
        case .ready:
            // The bar restarted: hello, then the state at once (api.md 6.7).
            usbNeedsHello = true
            dirty = true
            wake()
        case .closed:
            dropUSB(reopenAfter: TinyBarAPI.Timeouts.usbReopenAfter)
            wake()
        case .log:
            break
        }
    }

    /// Closes the USB link and falls back to Wi-Fi at once. `reopenAfter`:
    /// try the device again then, if it's still there.
    private func dropUSB(reopenAfter delay: TimeInterval?) {
        guard let link = usb else { return }
        usb = nil
        usbEventsTask?.cancel()
        usbEventsTask = nil
        usbMissed = 0
        usbNeedsHello = false
        Task { await link.transport.close() }
        if let delay, devices[link.device.registryID] != nil {
            reopenAt[link.device.registryID] = clock.now().addingTimeInterval(delay)
        }
        dirty = true
    }

    /// While on USB with the bar on Wi-Fi: does its address answer from this
    /// Mac? If not, say so once it's unplugged (mac-app-ux.md 4.3).
    private func checkWiFiReach(_ info: InfoReply) {
        guard let endpoint = wifiEndpoint() ?? info.host.map({ BarEndpoint(host: $0) }) else { return }
        backgroundWork += 1
        let transport = transports.makeWiFi(endpoint: endpoint, token: nil)
        let hello = helloRequest()
        Task {
            let reply = try? await transport.hello(hello)
            await transport.close()
            self.wifiReachChecked(reply, endpoint: endpoint, deviceID: info.deviceID)
        }
    }

    private func wifiReachChecked(_ reply: InfoReply?, endpoint: BarEndpoint, deviceID: String) {
        backgroundWork -= 1
        let reached = reply?.deviceID == deviceID
        state.wifiReachableHere = reached
        if reached, state.bar?.deviceID == deviceID, state.bar?.lastEndpoint == nil {
            state.bar?.lastEndpoint = endpoint
        }
        publish()
        wake()
    }

    // MARK: - State

    private func publish() {
        let now = clock.now()
        state.phase = phase(at: now)
        switch currentChoice() {
        case .usb:
            state.link = .usb
            state.endpointDescription = usb?.transport.endpointDescription
        case .wifi(let endpoint):
            state.link = .wifi
            state.endpointDescription = wifi?.transport.endpointDescription ?? endpoint.description
        case .none:
            state.link = nil
            state.endpointDescription = nil
        }
        if let usb, state.link == .usb { state.info = state.info ?? usb.info }
        guard state != lastPublished else { return }
        lastPublished = state
        statesContinuation.yield(state)
    }

    private func phase(at now: Date) -> ConnectionState.Phase {
        if messagesStopped || (state.bar == nil && configuration.manualEndpoint == nil && usb == nil) {
            return .notSetUp
        }
        if let versionProblem, usb == nil || versionProblem == .appNeedsUpdate { return versionProblem }
        let choice = currentChoice()
        let waiting: ConnectionState.Phase = reachability.isUnreachable(at: now) ? .unreachable : .looking
        // After `leaving`, nothing is being sent: never "connected".
        let sent = lastAttemptOK && !suspended
        switch choice {
        case .usb:
            return sent && lastLinkKind == .usb ? .connected : waiting
        case .wifi:
            if sent && lastLinkKind == .wifi { return .connected }
            if localNetworkDenied { return .localNetworkBlocked }
            if unauthorized { return .unrecognized }
            if waiting == .unreachable, wifiCantReachHere { return .wifiCantReachHere }
            return waiting
        case .none(let reason):
            switch reason {
            case .notSetUp: return .notSetUp
            case .wifiOff: return .notPluggedIn
            case .notPaired: return .unrecognized
            case .noAddress:
                if localNetworkDenied { return .localNetworkBlocked }
                if waiting == .unreachable, wifiCantReachHere { return .wifiCantReachHere }
                return waiting
            }
        }
    }

    // MARK: - Helpers

    /// Runs `operation`, giving up after `seconds` on `clock`.
    static func withTimeout(_ seconds: TimeInterval, clock: any TinyClock, _ operation: @escaping @Sendable () async -> Void) async {
        await withTaskGroup(of: Void.self) { group in
            group.addTask { await operation() }
            group.addTask { try? await clock.sleep(seconds: seconds) }
            await group.next()
            group.cancelAll()
        }
    }

    // MARK: - For tests

    /// The loop is waiting, with nothing due and nothing running in the
    /// background, so a test can look at what was sent.
    var isSettled: Bool {
        guard isParked, backgroundWork == 0, !wakePending else { return false }
        if let parkedDeadline, parkedDeadline <= clock.now() { return false }
        return true
    }

    /// The `seq` of the latest message.
    var lastSeq: UInt32 { seq }
}

extension KnownBar {
    /// A bar as `info` or `hello` describes it.
    init(info: InfoReply, endpoint: BarEndpoint?) {
        self.init(deviceID: info.deviceID, name: info.name, host: info.host, lastEndpoint: endpoint, auth: info.auth)
    }

    /// Takes the name, host and `auth` from a newer `info` or `hello`.
    mutating func update(from info: InfoReply) {
        guard info.deviceID == deviceID else { return }
        name = info.name
        if let host = info.host { self.host = host }
        auth = info.auth
    }
}
