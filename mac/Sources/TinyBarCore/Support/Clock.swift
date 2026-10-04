import Foundation

/// The app's source of time, injected everywhere so tests can run the start and
/// end delays, heartbeats, back-offs and pauses without waiting.
///
/// **`now()` is monotonic.** It starts at the Mac's wall-clock time when the
/// clock is made, then moves only with real elapsed time (including time the
/// Mac spends asleep), so setting the Mac's clock, or a large NTP step, never
/// stretches or skips a delay, a heartbeat, a retry or a grace period, and
/// never changes `elapsed_s`.
///
/// **Wall-clock time** is `now()` plus `wallClockOffset()`. Use it only for
/// what's shown ("Paused until 3:15 PM", "since 2:04 PM") and for finding
/// midnight; `wallClock(_:)` and `fromWallClock(_:)` convert. The offset is 0
/// until the Mac's clock is changed.
public protocol TinyClock: Sendable {
    /// The app's monotonic time (see above).
    func now() -> Date

    /// Suspends until `now()` reaches `deadline`. Returns at once if it has
    /// passed. Throws `CancellationError` if the task is cancelled while waiting.
    func sleep(until deadline: Date) async throws

    /// How far the Mac's wall clock is ahead of `now()`, in seconds.
    func wallClockOffset() -> TimeInterval
}

extension TinyClock {
    /// Suspends for `seconds` from now.
    public func sleep(seconds: TimeInterval) async throws {
        try await sleep(until: now().addingTimeInterval(seconds))
    }

    /// A time from `now()` as the Mac's clock shows it.
    public func wallClock(_ date: Date) -> Date {
        date.addingTimeInterval(wallClockOffset())
    }

    /// A time on the Mac's clock (midnight, a saved pause) as this clock's time.
    public func fromWallClock(_ date: Date) -> Date {
        date.addingTimeInterval(-wallClockOffset())
    }

    /// The Mac's clock now.
    public func wallNow() -> Date {
        wallClock(now())
    }
}

/// The real clock: `ContinuousClock` (which keeps counting while the Mac
/// sleeps, and never jumps), anchored to the wall clock at launch.
public struct SystemClock: TinyClock {
    private let startDate: Date
    private let startInstant: ContinuousClock.Instant

    public init() {
        startInstant = ContinuousClock.now
        startDate = Date()
    }

    public func now() -> Date {
        startDate.addingTimeInterval(SystemClock.seconds(ContinuousClock.now - startInstant))
    }

    public func sleep(until deadline: Date) async throws {
        let target = startInstant + .seconds(deadline.timeIntervalSince(startDate))
        guard target > ContinuousClock.now else {
            try Task.checkCancellation()
            return
        }
        try await Task.sleep(until: target, clock: .continuous)
    }

    public func wallClockOffset() -> TimeInterval {
        Date().timeIntervalSince(now())
    }

    static func seconds(_ duration: Duration) -> TimeInterval {
        let parts = duration.components
        return TimeInterval(parts.seconds) + TimeInterval(parts.attoseconds) / 1e18
    }
}

/// A clock that only moves when a test moves it.
///
/// `sleep(until:)` suspends until `advance(by:)` or `advance(to:)` passes the
/// deadline. Sleepers wake in deadline order. Use `waitForSleepers(_:)` to let
/// the code under test reach its next sleep before advancing.
/// `changeWallClock(by:)` sets the "Mac's clock" without moving `now()`.
public final class ManualClock: TinyClock, @unchecked Sendable {
    private struct Sleeper {
        let id: UUID
        let deadline: Date
        let continuation: CheckedContinuation<Void, any Error>
    }

    private struct State {
        var now: Date
        var wallClockOffset: TimeInterval = 0
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

    public func wallClockOffset() -> TimeInterval {
        state.withLock { $0.wallClockOffset }
    }

    /// The Mac's clock is set forward (positive) or back (negative). `now()`
    /// and the sleepers don't notice.
    public func changeWallClock(by seconds: TimeInterval) {
        state.withLock { $0.wallClockOffset += seconds }
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
