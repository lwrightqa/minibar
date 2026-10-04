import Foundation

/// The app's source of time, injected everywhere so tests can run the start and
/// end delays, heartbeats, back-offs and pauses without waiting.
///
/// Times are wall-clock `Date`s because the app shows them ("Paused until
/// 3:15 PM", "Can't reach TinyBar since 2:04 PM") and pauses end at midnight.
public protocol TinyClock: Sendable {
    /// The current time.
    func now() -> Date

    /// Suspends until `deadline`. Returns at once if it has passed. Throws
    /// `CancellationError` if the task is cancelled while waiting.
    func sleep(until deadline: Date) async throws
}

extension TinyClock {
    /// Suspends for `seconds` from now.
    public func sleep(seconds: TimeInterval) async throws {
        try await sleep(until: now().addingTimeInterval(seconds))
    }
}

/// The real clock.
public struct SystemClock: TinyClock {
    public init() {}

    public func now() -> Date {
        Date()
    }

    public func sleep(until deadline: Date) async throws {
        let seconds = deadline.timeIntervalSinceNow
        guard seconds > 0 else {
            try Task.checkCancellation()
            return
        }
        try await Task.sleep(nanoseconds: UInt64(seconds * 1_000_000_000))
    }
}

/// A clock that only moves when a test moves it.
///
/// `sleep(until:)` suspends until `advance(by:)` or `advance(to:)` passes the
/// deadline. Sleepers wake in deadline order. Use `waitForSleepers(_:)` to let
/// the code under test reach its next sleep before advancing.
public final class ManualClock: TinyClock, @unchecked Sendable {
    private struct Sleeper {
        let id: UUID
        let deadline: Date
        let continuation: CheckedContinuation<Void, any Error>
    }

    private struct State {
        var now: Date
        var sleepers: [Sleeper] = []
    }

    private let state: Locked<State>

    /// 2026-10-04T14:12:00-07:00, the time in docs/api.md's first examples.
    public static let apiExampleStart = Date(timeIntervalSince1970: 1_791_148_320)

    public init(start: Date = ManualClock.apiExampleStart) {
        state = Locked(State(now: start))
    }

    public func now() -> Date {
        state.withLock { $0.now }
    }

    /// How many tasks are waiting in `sleep(until:)`.
    public var sleeperCount: Int {
        state.withLock { $0.sleepers.count }
    }

    /// The earliest deadline anyone is sleeping until, if any.
    public var nextDeadline: Date? {
        state.withLock { $0.sleepers.map(\.deadline).min() }
    }

    public func sleep(until deadline: Date) async throws {
        let id = UUID()
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, any Error>) in
                enum Outcome { case wait, resume, cancel }
                let outcome: Outcome = state.withLock { state in
                    if Task.isCancelled { return .cancel }
                    if deadline <= state.now { return .resume }
                    state.sleepers.append(Sleeper(id: id, deadline: deadline, continuation: continuation))
                    return .wait
                }
                switch outcome {
                case .wait: break
                case .resume: continuation.resume()
                case .cancel: continuation.resume(throwing: CancellationError())
                }
            }
        } onCancel: {
            let continuation = state.withLock { state -> CheckedContinuation<Void, any Error>? in
                guard let index = state.sleepers.firstIndex(where: { $0.id == id }) else { return nil }
                return state.sleepers.remove(at: index).continuation
            }
            continuation?.resume(throwing: CancellationError())
        }
    }

    /// Moves the clock forward and wakes every sleeper whose deadline has passed.
    public func advance(by seconds: TimeInterval) {
        precondition(seconds >= 0, "ManualClock only moves forward")
        advance(to: now().addingTimeInterval(seconds))
    }

    /// Moves the clock to `date` (never backward) and wakes every sleeper whose
    /// deadline has passed, earliest first.
    public func advance(to date: Date) {
        let due = state.withLock { state -> [Sleeper] in
            if date > state.now { state.now = date }
            let now = state.now
            let due = state.sleepers.filter { $0.deadline <= now }.sorted { $0.deadline < $1.deadline }
            state.sleepers.removeAll { $0.deadline <= now }
            return due
        }
        for sleeper in due {
            sleeper.continuation.resume()
        }
    }

    /// Waits (in real time, up to `timeout` seconds) until at least `count`
    /// tasks are sleeping on this clock. Returns `false` on time-out.
    @discardableResult
    public func waitForSleepers(_ count: Int = 1, timeout: TimeInterval = 2) async -> Bool {
        let giveUp = Date().addingTimeInterval(timeout)
        while sleeperCount < count {
            if Date() > giveUp { return false }
            await Task.yield()
            try? await Task.sleep(nanoseconds: 1_000_000)
        }
        return true
    }
}
