import Foundation

/// Shares one Bonjour browser between the connection (finding the paired bar
/// by `device_id`) and the Connect window's Wi-Fi page.
///
/// The platform browser runs only while at least one client wants it: it's
/// started for the first client and stopped when the last one stops, so
/// turning off "Use Wi-Fi when MiniBar isn't plugged in" means no browsing at
/// all (mac-app-ux.md 6.4). A client that starts while it's already running
/// gets the bars found so far at once. Each run of the browser starts with an
/// empty list, and callbacks from an earlier run are ignored.
public final class BarDiscoveryHub: @unchecked Sendable {
    fileprivate typealias Callbacks = (onChange: @Sendable ([DiscoveredBar]) -> Void, onError: @Sendable (DiscoveryError) -> Void)

    private struct State {
        var clients: [UUID: Callbacks] = [:]
        /// The platform browser is running now.
        var running = false
        /// It has been started at least once (the Local Network prompt).
        var everStarted = false
        /// `shutdown()` was called.
        var stopped = false
        /// Which run of the browser the callbacks belong to.
        var run = 0
        var latest: [DiscoveredBar] = []
        var lastError: DiscoveryError?
    }

    private let underlying: any BarDiscovery
    private let state = Locked(State())
    /// Keeps the platform browser's `start` and `stop` calls in the order the
    /// state decided them. Recursive, in case a callback starts or stops a client.
    private let operations = NSRecursiveLock()

    public init(_ underlying: any BarDiscovery) {
        self.underlying = underlying
    }

    /// A browser for one user of it. Each client's `stop()` stops its own
    /// callbacks, and the platform browser when no other client is left.
    public func makeClient() -> any BarDiscovery {
        Client(hub: self)
    }

    /// Whether the platform browser has ever been started (it brings up the
    /// Local Network prompt on macOS 15).
    public var hasStarted: Bool {
        state.withLock { $0.everStarted }
    }

    /// Whether the platform browser is running now.
    public var isRunning: Bool {
        state.withLock { $0.running }
    }

    /// Stops the platform browser for good (on quit).
    public func shutdown() {
        operations.lock()
        defer { operations.unlock() }
        let wasRunning = state.withLock { state -> Bool in
            defer {
                state.stopped = true
                state.running = false
                state.clients.removeAll()
                state.run += 1
            }
            return state.running
        }
        if wasRunning { underlying.stop() }
    }

    fileprivate func register(_ id: UUID, _ callbacks: Callbacks) {
        operations.lock()
        defer { operations.unlock() }
        enum Action { case start(Int), replay([DiscoveredBar], DiscoveryError?), nothing }
        let action: Action = state.withLock { state in
            guard !state.stopped else { return .nothing }
            state.clients[id] = callbacks
            if !state.running {
                state.running = true
                state.everStarted = true
                state.run += 1
                state.latest = []
                state.lastError = nil
                return .start(state.run)
            }
            return .replay(state.latest, state.lastError)
        }
        switch action {
        case .start(let run):
            underlying.start(
                onChange: { [weak self] bars in self?.publish(bars, run: run) },
                onError: { [weak self] error in self?.fail(error, run: run) }
            )
        case .replay(let bars, let error):
            if !bars.isEmpty { callbacks.onChange(bars) }
            if let error { callbacks.onError(error) }
        case .nothing:
            break
        }
    }

    fileprivate func unregister(_ id: UUID) {
        operations.lock()
        defer { operations.unlock() }
        let stopBrowser = state.withLock { state -> Bool in
            guard state.clients.removeValue(forKey: id) != nil else { return false }
            guard state.clients.isEmpty, state.running else { return false }
            state.running = false
            state.run += 1
            return true
        }
        if stopBrowser { underlying.stop() }
    }

    private func publish(_ bars: [DiscoveredBar], run: Int) {
        let clients = state.withLock { state -> [Callbacks] in
            guard state.run == run, state.running else { return [] }
            state.latest = bars
            // A list, even an empty one, means browsing works again.
            state.lastError = nil
            return Array(state.clients.values)
        }
        clients.forEach { $0.onChange(bars) }
    }

    private func fail(_ error: DiscoveryError, run: Int) {
        let clients = state.withLock { state -> [Callbacks] in
            guard state.run == run, state.running else { return [] }
            state.lastError = error
            return Array(state.clients.values)
        }
        clients.forEach { $0.onError(error) }
    }

    private final class Client: BarDiscovery, @unchecked Sendable {
        private let hub: BarDiscoveryHub
        private let id = UUID()

        init(hub: BarDiscoveryHub) {
            self.hub = hub
        }

        func start(onChange: @escaping @Sendable ([DiscoveredBar]) -> Void, onError: @escaping @Sendable (DiscoveryError) -> Void) {
            hub.register(id, (onChange, onError))
        }

        func stop() {
            hub.unregister(id)
        }
    }
}
