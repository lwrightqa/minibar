import Foundation

/// The USB link: newline-delimited JSON with the `@tb ` marker over the bar's
/// USB Serial/JTAG port (api.md 6).
///
/// How it should work:
/// - A reader runs from `init` until `close()`, **always reading**, even when
///   nothing is pending, so the bar's output never backs up (api.md 6.3). It
///   splits lines with `USBLineSplitter`, classifies them with
///   `USBFraming.classify`, matches replies to requests by `id`, and sends
///   `ready` events and log lines to `events`.
/// - Each request gets the next `id` (1 to 2147483647) and waits up to 3
///   seconds for its reply (`TinyBarAPI.Timeouts.usbReply`), then throws
///   `BarError.timedOut`. A reply with `"id": null` answers the oldest
///   outstanding request (the bar couldn't parse it).
/// - Replies are decoded with `WireJSON.decodeReply(_:from:httpStatus:)`.
/// - When the port reports `closed`, every waiting request throws
///   `BarError.closed` and `events` yields `.closed` and finishes.
/// - Counting missed replies and closing after 3 in a row (api.md 6.8) is
///   `BarConnection`'s job, not this class's.
public final class USBTransport: USBLinkTransport, @unchecked Sendable {
    public let kind: LinkKind = .usb
    public let device: SerialDevice
    public let events: AsyncStream<USBEvent>

    private let port: SerialPort
    private let clock: any TinyClock
    private let router: USBReplyRouter

    /// How long a write may wait for room before the link counts as stuck.
    static let writeTimeout: TimeInterval = 1
    /// How long each read waits in `poll`. Data, the device going away and
    /// `close()` all wake it at once, so this only bounds an idle wait; it's
    /// long so an idle link costs nothing (docs/mac-app.md: never poll faster
    /// than once a second).
    static let readPollInterval: TimeInterval = 30

    /// Takes over an open port and starts reading.
    public init(port: SerialPort, device: SerialDevice, clock: any TinyClock) {
        self.port = port
        self.device = device
        self.clock = clock
        let (stream, continuation) = AsyncStream.makeStream(of: USBEvent.self, bufferingPolicy: .bufferingNewest(64))
        self.events = stream
        let router = USBReplyRouter(events: continuation)
        self.router = router
        let thread = Thread { USBTransport.readLoop(port: port, router: router) }
        thread.name = "TinyBar USB reader"
        thread.start()
    }

    deinit {
        port.close()
        router.closeAll()
    }

    /// `cu.usbmodem1101`.
    public var endpointDescription: String {
        device.calloutPath.split(separator: "/").last.map(String.init) ?? device.calloutPath
    }

    public func handshake(_ request: HelloRequest) async throws -> InfoReply {
        let slot = ReplySlot()
        var ids: [Int] = []
        defer { ids.forEach(router.unregister) }
        let start = clock.now()
        let giveUp = start.addingTimeInterval(TinyBarAPI.Timeouts.helloGiveUp)
        var attempt = 0
        /// The latest error reply: proof the device speaks the protocol.
        var lastError: APIErrorBody?
        while true {
            let id = try router.register(slot)
            ids.append(id)
            try writeLine(USBFraming.commandLine(.hello, id: id, body: request))
            attempt += 1
            let nextHello = min(start.addingTimeInterval(TinyBarAPI.Timeouts.helloInterval * Double(attempt)), giveUp)
            // A reply to any of the hellos sent so far counts.
            waiting: while true {
                switch await slot.wait(until: nextHello, clock: clock) {
                case .timeout:
                    break waiting
                case .failure(let error):
                    throw error
                case .reply(let data):
                    do {
                        let info = try WireJSON.decodeReply(InfoReply.self, from: data, httpStatus: nil)
                        guard info.isTinyBar else { throw BarError.notATinyBar }
                        return info
                    } catch BarError.api(let body, let status) {
                        // A version the app can't speak: no point asking again.
                        if body.error == .unsupportedAPI { throw BarError.api(body, httpStatus: status) }
                        // Any other error reply (`busy` while starting up,
                        // `bad_json` for a line garbled at boot or left by a
                        // killed app, `internal`, `too_large`) still proves
                        // it's a TinyBar: wait for a reply to a later hello,
                        // and ask again at the next interval.
                        lastError = body
                        slot.reset()
                    } catch BarError.malformedReply {
                        throw BarError.notATinyBar
                    }
                }
            }
            if clock.now() >= giveUp {
                // Error replies only: a TinyBar that wasn't ready. The caller
                // tries the port again later instead of leaving it alone.
                if let lastError { throw BarError.api(lastError, httpStatus: nil) }
                throw BarError.notATinyBar
            }
        }
    }

    public func hello(_ request: HelloRequest) async throws -> InfoReply {
        try await perform(.hello, body: request, as: InfoReply.self)
    }

    public func sendCall(_ request: CallRequest) async throws -> CallReply {
        try await perform(.call, body: request, as: CallReply.self)
    }

    public func status() async throws -> StatusReply {
        try await perform(.status, body: EmptyBody(), as: StatusReply.self)
    }

    public func pair(_ request: USBPairRequest) async throws -> PairReply {
        try await perform(.pair, body: request, as: PairReply.self)
    }

    /// Any endpoint over USB (api.md 6.6). The app uses it for Forget This
    /// TinyBar while the bar is plugged in (`DELETE /api/v1/clients/{token_id}`).
    /// The reply's `http_status` is passed to `WireJSON.decodeReply`.
    public func request(_ command: USBRequestCommand) async throws -> JSONValue {
        try await perform(.request, body: command, as: JSONValue.self, usesHTTPStatus: true)
    }

    public func close() async {
        port.close()
        router.closeAll()
    }

    // MARK: - Requests

    /// Sends one command and waits up to 3 seconds for the reply with its `id`.
    private func perform<Body: Encodable, Reply: Decodable>(
        _ cmd: USBCommandName,
        body: Body,
        as type: Reply.Type,
        usesHTTPStatus: Bool = false
    ) async throws(BarError) -> Reply {
        let slot = ReplySlot()
        let id = try router.register(slot)
        defer { router.unregister(id) }
        try writeLine(USBFraming.commandLine(cmd, id: id, body: body))
        let deadline = clock.now().addingTimeInterval(TinyBarAPI.Timeouts.usbReply)
        switch await slot.wait(until: deadline, clock: clock) {
        case .reply(let data):
            let status = usesHTTPStatus ? (try? JSONDecoder().decode(USBLineHeader.self, from: data))?.httpStatus : nil
            return try WireJSON.decodeReply(Reply.self, from: data, httpStatus: status)
        case .failure(let error):
            throw error
        case .timeout:
            throw .timedOut
        }
    }

    private func writeLine(_ line: Data) throws(BarError) {
        do {
            try port.write(line, timeout: USBTransport.writeTimeout)
        } catch SerialPortError.timedOut {
            throw .timedOut
        } catch {
            router.closeAll()
            throw .closed
        }
    }

    /// Reads until the port closes or goes away: splits lines, routes replies
    /// by `id`, passes `ready` events and log lines on, drops the rest.
    private static func readLoop(port: SerialPort, router: USBReplyRouter) {
        var splitter = USBLineSplitter()
        while true {
            let data: Data
            do {
                data = try port.read(upTo: 4096, timeout: readPollInterval)
            } catch {
                break
            }
            for line in splitter.append(data) {
                switch USBFraming.classify(line) {
                case .reply(let id, let json):
                    router.route(id: id, json: json)
                case .event(let name, let json):
                    if name == "ready", let event = try? JSONDecoder().decode(ReadyEvent.self, from: json) {
                        router.yield(.ready(event))
                    }
                case .log(let text):
                    router.yield(.log(text))
                case .invalid:
                    break
                }
            }
        }
        port.close()
        router.closeAll()
    }
}

/// Matches replies to waiting requests by `id` (api.md 6.4), and owns the
/// events stream.
final class USBReplyRouter: @unchecked Sendable {
    private struct State {
        var nextID = 1
        /// In the order sent, so a reply with `"id": null` goes to the oldest.
        var pending: [(id: Int, slot: ReplySlot)] = []
        var closed = false
    }

    private let state = Locked(State())
    private let events: AsyncStream<USBEvent>.Continuation

    init(events: AsyncStream<USBEvent>.Continuation) {
        self.events = events
    }

    /// Gives `slot` the next `id` (1 to 2147483647, then 1 again).
    func register(_ slot: ReplySlot) throws(BarError) -> Int {
        let id: Int? = state.withLock { state in
            guard !state.closed else { return nil }
            let id = state.nextID
            state.nextID = id >= Int(Int32.max) ? 1 : id + 1
            state.pending.append((id, slot))
            return id
        }
        guard let id else { throw .closed }
        return id
    }

    func unregister(_ id: Int) {
        state.withLock { $0.pending.removeAll { $0.id == id } }
    }

    func route(id: Int?, json: Data) {
        let slot: ReplySlot? = state.withLock { state in
            if let id { return state.pending.first { $0.id == id }?.slot }
            return state.pending.first?.slot
        }
        // A reply nobody waits for any more (it came after the time-out) is dropped.
        slot?.resolve(.success(json))
    }

    func yield(_ event: USBEvent) {
        guard !state.withLock({ $0.closed }) else { return }
        events.yield(event)
    }

    /// Fails every waiting request with `closed`, sends `.closed` and ends the stream.
    func closeAll() {
        let slots: [ReplySlot]? = state.withLock { state in
            guard !state.closed else { return nil }
            state.closed = true
            defer { state.pending.removeAll() }
            return state.pending.map(\.slot)
        }
        guard let slots else { return }
        slots.forEach { $0.resolve(.failure(.closed)) }
        events.yield(.closed)
        events.finish()
    }
}

/// One request's reply, which arrives on the reader thread, and the task
/// waiting for it on the injected clock.
final class ReplySlot: @unchecked Sendable {
    enum Outcome: Sendable {
        case reply(Data)
        case failure(BarError)
        case timeout
    }

    private struct State {
        var result: Result<Data, BarError>?
        var waiter: (generation: Int, continuation: CheckedContinuation<Outcome, Never>)?
        var generation = 0
        var expired: Int = 0
        var cancelled = false
    }

    private let state = Locked(State())

    /// The reply (or failure). Only the first counts; it's kept, so a later
    /// `wait` returns it at once.
    func resolve(_ result: Result<Data, BarError>) {
        let waiter: CheckedContinuation<Outcome, Never>? = state.withLock { state in
            guard state.result == nil else { return nil }
            state.result = result
            defer { state.waiter = nil }
            return state.waiter?.continuation
        }
        waiter?.resume(returning: ReplySlot.outcome(result))
    }

    /// Forgets a reply, so the next one can resolve the slot (the handshake
    /// shares one slot among its hellos, and a `busy` reply isn't the answer).
    func reset() {
        state.withLock { $0.result = nil }
    }

    /// Waits for the reply until `deadline` on `clock`. Returns `.timeout` if
    /// it hasn't come by then, `.failure(.cancelled)` if the task is cancelled.
    func wait(until deadline: Date, clock: any TinyClock) async -> Outcome {
        let generation: Int = state.withLock { state in
            state.generation += 1
            return state.generation
        }
        if let result = state.withLock({ $0.result }) { return ReplySlot.outcome(result) }
        let timer = Task { [weak self] in
            do { try await clock.sleep(until: deadline) } catch { return }
            self?.expire(generation)
        }
        defer { timer.cancel() }
        return await withTaskCancellationHandler {
            await withCheckedContinuation { (continuation: CheckedContinuation<Outcome, Never>) in
                let immediate: Outcome? = state.withLock { state in
                    if let result = state.result { return ReplySlot.outcome(result) }
                    if state.cancelled { return .failure(.cancelled) }
                    if state.expired >= generation { return .timeout }
                    state.waiter = (generation, continuation)
                    return nil
                }
                if let immediate { continuation.resume(returning: immediate) }
            }
        } onCancel: {
            let waiter: CheckedContinuation<Outcome, Never>? = state.withLock { state in
                state.cancelled = true
                defer { state.waiter = nil }
                return state.waiter?.continuation
            }
            waiter?.resume(returning: .failure(.cancelled))
        }
    }

    private func expire(_ generation: Int) {
        let waiter: CheckedContinuation<Outcome, Never>? = state.withLock { state in
            state.expired = max(state.expired, generation)
            guard let waiter = state.waiter, waiter.generation == generation else { return nil }
            state.waiter = nil
            return waiter.continuation
        }
        waiter?.resume(returning: .timeout)
    }

    private static func outcome(_ result: Result<Data, BarError>) -> Outcome {
        switch result {
        case .success(let data): return .reply(data)
        case .failure(let error): return .failure(error)
        }
    }
}
