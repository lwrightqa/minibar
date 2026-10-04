import Foundation

/// A value guarded by a lock, for the few classes that must be `Sendable` and
/// still hold mutable state (transports, test fakes, the manual clock).
///
/// `Synchronization.Mutex` would do the same, but it needs macOS 15 and the app
/// supports macOS 14.
public final class Locked<Value>: @unchecked Sendable {
    private let lock = NSLock()
    private var value: Value

    public init(_ value: Value) {
        self.value = value
    }

    /// Runs `body` with exclusive access to the value. Don't call back into the
    /// same `Locked` from `body`: `NSLock` isn't recursive.
    @discardableResult
    public func withLock<Result>(_ body: (inout Value) throws -> Result) rethrows -> Result {
        lock.lock()
        defer { lock.unlock() }
        return try body(&value)
    }

    /// A copy of the current value.
    public func get() -> Value {
        withLock { $0 }
    }

    /// Replaces the value.
    public func set(_ newValue: Value) {
        withLock { $0 = newValue }
    }
}
