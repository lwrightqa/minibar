import Foundation
import XCTest
@testable import TinyBarCore

/// A TinyBar as the fake transports see it: who it is, what it accepts, and
/// every message it got.
final class FakeBar: @unchecked Sendable {
    struct Message: Sendable {
        var via: LinkKind
        var request: CallRequest
        var at: Date
        var token: String?
    }

    struct State: Sendable {
        var deviceID: String
        var name: String
        var auth: AuthMode = .bearer
        var api = "1.0"
        var wifi: WiFiState = .connected
        var heartbeatS = 30
        var sourcesMac = true
        /// The bar's call is set aside (a tap on the bar).
        var aside = false
        /// Tokens the bar accepts over Wi-Fi.
        var tokens: Set<String> = []
        /// Tokens the bar knows but that were paired for another `client`:
        /// a call with one gets `403 wrong_client` (api.md 5.2).
        var foreignTokens: Set<String> = []
        var tokenLimit = false
        /// Make the next requests over a link fail with this.
        var usbFailure: BarError?
        var wifiFailure: BarError?
        /// Answer `call` with another bar's `device_id`.
        var replyDeviceID: String?
        var messages: [Message] = []
        /// "usb hello", "usb call", "usb pair", "wifi info", "wifi call", "wifi unpair"…
        var log: [String] = []
        var revoked: [String] = []
        var issued = 0
    }

    let clock: ManualClock
    let state: Locked<State>

    init(clock: ManualClock, deviceID: String = "f412fa3f2a1c", name: String = "TinyBar 2A1C") {
        self.clock = clock
        state = Locked(State(deviceID: deviceID, name: name))
    }

    var deviceID: String { state.withLock { $0.deviceID } }
    var messages: [Message] { state.withLock { $0.messages } }
    var log: [String] { state.withLock { $0.log } }

    func set(_ change: (inout State) -> Void) {
        state.withLock(change)
    }

    var info: InfoReply {
        state.withLock { s in
            InfoReply(deviceID: s.deviceID, name: s.name, fw: "1.0.0", api: s.api, host: "tinybar.local", auth: s.auth,
                      heartbeatS: s.heartbeatS, wifi: s.wifi)
        }
    }

    func record(_ entry: String) {
        state.withLock { $0.log.append(entry) }
    }

    func failure(for kind: LinkKind) -> BarError? {
        state.withLock { kind == .usb ? $0.usbFailure : $0.wifiFailure }
    }

    func call(_ request: CallRequest, via kind: LinkKind, token: String?) throws(BarError) -> CallReply {
        if let failure = failure(for: kind) {
            record("\(kind.rawValue) call failed")
            throw failure
        }
        let result: Result<CallReply, BarError> = state.withLock { s in
            if kind == .wifi, s.auth == .bearer, let token, s.foreignTokens.contains(token) {
                s.log.append("wifi call wrong_client")
                return .failure(.api(APIErrorBody(error: .wrongClient, message: "This token can't report calls for that client.",
                                                  field: "client"), httpStatus: 403))
            }
            if kind == .wifi, s.auth == .bearer, !(token.map(s.tokens.contains) ?? false) {
                s.log.append("wifi call refused")
                return .failure(.api(APIErrorBody(error: .unauthorized, message: "Pair with this TinyBar first."), httpStatus: 401))
            }
            s.messages.append(Message(via: kind, request: request, at: clock.now(), token: token))
            s.log.append("\(kind.rawValue) call")
            let active = request.active
            let showing: Showing = active && s.sourcesMac && !s.aside ? .call : .own
            return .success(CallReply(
                deviceID: s.replyDeviceID ?? s.deviceID,
                showing: showing,
                call: active ? BarCall(active: true, app: request.app, via: kind.via, aside: s.aside) : .none,
                sources: SourceSwitches(calendar: true, mac: s.sourcesMac),
                heartbeatS: s.heartbeatS,
                timeoutS: s.heartbeatS * 3
            ))
        }
        return try result.get()
    }

    func pairOverUSB(_ request: USBPairRequest) throws(BarError) -> PairReply {
        let result: Result<PairReply, BarError> = state.withLock { s in
            s.log.append("usb pair")
            if s.tokenLimit {
                return .failure(.api(APIErrorBody(error: .tokenLimit, message: "TinyBar already has 10 paired devices."), httpStatus: nil))
            }
            s.issued += 1
            let token = "tb1_" + String(format: "%043d", s.issued)
            s.tokens.insert(token)
            return .success(PairReply(token: token, tokenID: String(format: "%08x", s.issued), deviceID: s.deviceID,
                                      name: s.name, host: "tinybar.local"))
        }
        return try result.get()
    }

    func revoke(_ token: String?) throws(BarError) -> RevokeReply {
        let result: Result<RevokeReply, BarError> = state.withLock { s in
            s.log.append("wifi unpair")
            guard let token, s.tokens.remove(token) != nil || s.foreignTokens.remove(token) != nil else {
                return .failure(.api(APIErrorBody(error: .unauthorized), httpStatus: 401))
            }
            s.revoked.append(token)
            return .success(RevokeReply(revoked: "00000001"))
        }
        return try result.get()
    }

    var status: StatusReply {
        get throws {
            try JSONValue.parse(USBLinkTests.statusJSON.replacingOccurrences(of: "f412fa3f2a1c", with: deviceID)).decode(StatusReply.self)
        }
    }
}

/// `USBLinkTransport` without a port: answers at once from a `FakeBar`, or
/// isn't a TinyBar when `bar` is `nil`.
final class FakeUSBTransport: USBLinkTransport, @unchecked Sendable {
    let kind = LinkKind.usb
    let device: SerialDevice
    let bar: FakeBar?
    let events: AsyncStream<USBEvent>
    private let continuation: AsyncStream<USBEvent>.Continuation
    private let closed = Locked(false)

    init(device: SerialDevice, bar: FakeBar?) {
        self.device = device
        self.bar = bar
        (events, continuation) = AsyncStream.makeStream(of: USBEvent.self)
    }

    var isClosed: Bool { closed.get() }
    var endpointDescription: String { "cu.usbmodem\(device.registryID)" }

    private func live() throws(BarError) -> FakeBar {
        guard !closed.get() else { throw .closed }
        guard let bar else { throw .timedOut }
        return bar
    }

    func handshake(_ request: HelloRequest) async throws -> InfoReply {
        guard !closed.get() else { throw BarError.closed }
        guard let bar else { throw BarError.notATinyBar }
        bar.record("usb handshake")
        return bar.info
    }

    func hello(_ request: HelloRequest) async throws -> InfoReply {
        let bar = try live()
        if let failure = bar.failure(for: .usb) { throw failure }
        bar.record("usb hello")
        return bar.info
    }

    func sendCall(_ request: CallRequest) async throws -> CallReply {
        try live().call(request, via: .usb, token: nil)
    }

    func status() async throws -> StatusReply {
        try live().status
    }

    func pair(_ request: USBPairRequest) async throws -> PairReply {
        try live().pairOverUSB(request)
    }

    /// The bar sends an event (`ready`), or the port goes away (`closed`).
    func emit(_ event: USBEvent) {
        continuation.yield(event)
        if event == .closed {
            closed.set(true)
            continuation.finish()
        }
    }

    func close() async {
        guard !closed.withLock({ c in defer { c = true }; return c }) else { return }
        bar?.record("usb closed")
        continuation.yield(.closed)
        continuation.finish()
    }
}

/// `WiFiLinkTransport` without a network: the bar at `endpoint`, if any.
final class FakeWiFiTransport: WiFiLinkTransport, @unchecked Sendable {
    let kind = LinkKind.wifi
    let endpoint: BarEndpoint
    let token: String?
    let bar: FakeBar?
    /// Never answer (sendLeaving's time-out).
    let hangs: Bool
    private let closed = Locked(false)

    init(endpoint: BarEndpoint, token: String?, bar: FakeBar?, hangs: Bool = false) {
        self.endpoint = endpoint
        self.token = token
        self.bar = bar
        self.hangs = hangs
    }

    var endpointDescription: String { endpoint.description }

    private func live() async throws(BarError) -> FakeBar {
        guard !closed.get() else { throw .closed }
        if hangs {
            do { try await Task.sleep(nanoseconds: 60_000_000_000) } catch { throw .cancelled }
        }
        guard let bar else { throw .unreachable("nobody at \(endpoint)") }
        if let failure = bar.failure(for: .wifi) {
            bar.record("wifi failed")
            throw failure
        }
        return bar
    }

    func hello(_ request: HelloRequest) async throws -> InfoReply {
        let bar = try await live()
        bar.record("wifi info")
        return bar.info
    }

    func sendCall(_ request: CallRequest) async throws -> CallReply {
        try await live().call(request, via: .wifi, token: token)
    }

    func status() async throws -> StatusReply {
        try await live().status
    }

    func pairStart(_ request: PairStartRequest) async throws -> PairStartReply {
        let bar = try await live()
        bar.record("wifi pair/start")
        return PairStartReply(pairingID: "d407580a9215e992")
    }

    func pair(_ request: PairRequest) async throws -> PairReply {
        let bar = try await live()
        bar.record("wifi pair")
        return try bar.pairOverUSB(USBPairRequest(client: "x"))
    }

    func pairCancel(_ request: PairCancelRequest) async throws -> PairCancelReply {
        let bar = try await live()
        bar.record("wifi pair/cancel \(request.pairingID)")
        return PairCancelReply()
    }

    func unpairSelf() async throws -> RevokeReply {
        try await live().revoke(token)
    }

    func close() async {
        closed.set(true)
    }
}

/// Hands out fake transports and remembers them.
final class FakeTransportFactory: TransportFactory, @unchecked Sendable {
    private struct State {
        var usbBars: [UInt64: FakeBar] = [:]
        var wifiBars: [BarEndpoint: FakeBar] = [:]
        var hangingEndpoints: Set<BarEndpoint> = []
        var openError: (any Error)?
        var opened: [FakeUSBTransport] = []
        var made: [FakeWiFiTransport] = []
    }

    private let state = Locked(State())

    /// What answers on the port of the device with this registry ID.
    func plug(_ bar: FakeBar?, registryID: UInt64) {
        state.withLock { $0.usbBars[registryID] = bar }
    }

    /// What answers at this address.
    func place(_ bar: FakeBar?, at endpoint: BarEndpoint) {
        state.withLock { $0.wifiBars[endpoint] = bar }
    }

    func hang(at endpoint: BarEndpoint) {
        state.withLock { _ = $0.hangingEndpoints.insert(endpoint) }
    }

    func failOpening(with error: (any Error)?) {
        state.withLock { $0.openError = error }
    }

    var opened: [FakeUSBTransport] { state.withLock { $0.opened } }
    var made: [FakeWiFiTransport] { state.withLock { $0.made } }

    func openUSB(_ device: SerialDevice) throws -> any USBLinkTransport {
        try state.withLock { s in
            if let error = s.openError { throw error }
            let transport = FakeUSBTransport(device: device, bar: s.usbBars[device.registryID])
            s.opened.append(transport)
            return transport
        }
    }

    func makeWiFi(endpoint: BarEndpoint, token: String?) -> any WiFiLinkTransport {
        state.withLock { s in
            let transport = FakeWiFiTransport(endpoint: endpoint, token: token, bar: s.wifiBars[endpoint],
                                              hangs: s.hangingEndpoints.contains(endpoint))
            s.made.append(transport)
            return transport
        }
    }
}

/// A `BarConnection` with fakes and a manual clock, and ways to wait for it.
final class ConnectionRig: @unchecked Sendable {
    static let client = "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"
    static let espressif = SerialDevice(calloutPath: "/dev/cu.usbmodem1101", vendorID: 0x303A, productID: 0x1001, registryID: 1)
    static let barAddress = BarEndpoint(host: "10.0.4.42")

    let clock = ManualClock()
    let factory = FakeTransportFactory()
    let tokens: InMemoryTokenStore
    let bar: FakeBar
    let connection: BarConnection
    let start: Date

    init(known: KnownBar? = nil, tokens: [String: String] = [:], configuration: ((inout BarConnection.Configuration) -> Void)? = nil) {
        bar = FakeBar(clock: clock)
        self.tokens = InMemoryTokenStore(tokens)
        var config = BarConnection.Configuration(clientID: ConnectionRig.client, sessionID: "q8Zr2Lx0", appVersion: "1.0 (12)")
        configuration?(&config)
        connection = BarConnection(configuration: config, bar: known, clock: clock, transports: factory, tokens: self.tokens)
        start = clock.now()
    }

    /// A bar this Mac paired with before, with its token in the store and the bar.
    static func paired(_ configure: ((inout BarConnection.Configuration) -> Void)? = nil) -> ConnectionRig {
        let token = "tb1_" + String(repeating: "A", count: 43)
        let known = KnownBar(deviceID: "f412fa3f2a1c", name: "TinyBar 2A1C", host: "tinybar.local",
                             lastEndpoint: barAddress, auth: .bearer, tokenID: "74d8a526")
        let rig = ConnectionRig(known: known, tokens: ["f412fa3f2a1c": token], configuration: configure)
        rig.bar.set { $0.tokens.insert(token) }
        return rig
    }

    /// Seconds since the rig started.
    var elapsed: TimeInterval { clock.now().timeIntervalSince(start) }

    func seconds(_ date: Date) -> TimeInterval { (date.timeIntervalSince(start) * 1000).rounded() / 1000 }

    /// Waits (in real time) until the connection is idle: its loop asleep,
    /// nothing due and no background work.
    func settle(file: StaticString = #filePath, line: UInt = #line) async {
        let giveUp = Date().addingTimeInterval(3)
        while Date() < giveUp {
            if await connection.isSettled {
                // Let other tasks (event listeners) run, and check again.
                try? await Task.sleep(nanoseconds: 2_000_000)
                if await connection.isSettled { return }
            }
            try? await Task.sleep(nanoseconds: 1_000_000)
        }
        XCTFail("the connection didn't settle", file: file, line: line)
    }

    func start(file: StaticString = #filePath, line: UInt = #line) async {
        await connection.start()
        await settle(file: file, line: line)
    }

    /// Moves the clock forward in steps, letting the connection act at each.
    func run(for seconds: TimeInterval, step: TimeInterval = 0.5) async {
        let end = clock.now().addingTimeInterval(seconds)
        while clock.now() < end {
            let next = min(clock.now().addingTimeInterval(step), end)
            clock.advance(to: next)
            await settle()
        }
    }

    func plugIn(_ device: SerialDevice = ConnectionRig.espressif, bar: FakeBar? = nil, answers: Bool = true) async {
        factory.plug(answers ? (bar ?? self.bar) : nil, registryID: device.registryID)
        await connection.handle(.appeared(device))
        await settle()
    }

    func unplug(_ device: SerialDevice = ConnectionRig.espressif) async {
        await connection.handle(.disappeared(device))
        await settle()
    }

    var state: ConnectionState {
        get async { await connection.state }
    }

    /// Messages the bar got: "0.0 usb idle", "3.0 wifi Slack", "47.0 usb leaving".
    func messageLog(_ bar: FakeBar? = nil) -> [String] {
        (bar ?? self.bar).messages.map { message in
            let what: String
            if message.request.leaving == true {
                what = "leaving"
            } else if message.request.active {
                what = message.request.app ?? "active"
            } else {
                what = "idle"
            }
            return String(format: "%.1f %@ %@", seconds(message.at), message.via.rawValue, what)
        }
    }
}
