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
        /// The paired bar refuses this Mac's token (`401`), and USB isn't
        /// connected. Needs you: Pair Again.
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
    /// until a device is removed on the Remote (mac-app-ux.md 5.2).
    public var tokenLimitReached: Bool

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
        tokenLimitReached: Bool = false
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
///   token (api.md 6.6). A different bar on USB becomes the paired bar, and
///   the old token is deleted (mac-app-ux.md 5.3). `auth: none` skips pairing.
/// - **Messages.** Sends `report(_:)` at once, and the full state every
///   `heartbeat_s` (from the latest reply), idle or not, with `seq` going up by
///   1 per message in this `session`. One request in flight at a time; a newer
///   state replaces a queued one.
/// - **Failures.** Retries on the `RetrySchedule`; Not connected after 15
///   seconds without success (`ReachabilityTracker`). Over USB, 3 replies in a
///   row missed → close the port, use Wi-Fi, reopen after 10 seconds or when
///   IOKit reports the device again. On a `ready` event: `hello`, then the state.
/// - **Checks.** Every reply's `device_id` against the paired bar's. On `401`
///   over Wi-Fi: `GET info` first; another bar at that address → find the
///   right one again and keep the token; the paired bar itself → delete the
///   token and report `.unrecognized` (api.md 16). `unsupported_api` or an
///   `info` with another major version → `.barNeedsUpdate`/`.appNeedsUpdate`.
/// - **Leaving.** `sendLeaving()` sends `"leaving": true` with
///   `"active": false` over the current link, waiting at most about a second.
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
            timeZone: TimeZone = .current
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

    /// The current state.
    public private(set) var state: ConnectionState

    private var configuration: Configuration
    private let clock: any TinyClock
    private let transports: any TransportFactory
    private let tokens: any TokenStore

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
        self.states = AsyncStream { _ in }
    }

    /// Starts the heartbeat loop and the grace period.
    public func start() {
        unimplemented()
    }

    /// Stops everything and closes the links (after `sendLeaving()` on quit).
    public func stop() async {
        unimplemented()
    }

    /// The call state changed: send it at once, then keep repeating it.
    public func report(_ call: ReportedCall) {
        unimplemented()
    }

    /// Sends `"leaving": true` (quit, sleep, power off), waiting at most about
    /// a second. Best effort: never throws.
    public func sendLeaving() async {
        unimplemented()
    }

    /// The Mac woke: start a grace period and send the state at once.
    public func didWake() {
        unimplemented()
    }

    /// From the IOKit watcher.
    public func handle(_ event: SerialDeviceEvent) {
        unimplemented()
    }

    /// From the Bonjour browser: the bars on the network now.
    public func discovered(_ bars: [DiscoveredBar]) {
        unimplemented()
    }

    /// From the Bonjour browser.
    public func discoveryFailed(_ error: DiscoveryError) {
        unimplemented()
    }

    /// Pause USB / Resume USB (api.md 6.3).
    public func setUSBPaused(_ paused: Bool) {
        unimplemented()
    }

    /// New settings (Wi-Fi switch, address, Mac name).
    public func update(_ configuration: Configuration) {
        unimplemented()
    }

    /// Use this bar from now on (after pairing over Wi-Fi; the token is
    /// already in the token store).
    public func adopt(_ bar: KnownBar) {
        unimplemented()
    }

    /// Forget This TinyBar: tells the bar (`DELETE /api/v1/clients/self`, best
    /// effort), deletes the token, and leaves a plugged-in bar's port alone
    /// until it's replugged (mac-app-ux.md 5.7).
    public func forget() async {
        unimplemented()
    }

    /// The latest `status` from the bar, over the current link (for the
    /// "Wi-Fi can't reach it here" check and the Option-click details).
    public func fetchStatus() async throws -> StatusReply {
        unimplemented()
    }
}
