#if os(macOS)
import Dispatch

/// The private serial queue an adapter's mutable state is confined to.
///
/// Each adapter class is `@unchecked Sendable`: every stored property that
/// changes after `init` is read and written only on `queue`. The system calls
/// the adapters' listener blocks and notification callbacks on that same
/// queue, and the public methods hop onto it with `sync`, so nothing races.
final class AdapterQueue: @unchecked Sendable {
    let queue: DispatchQueue
    private let key = DispatchSpecificKey<UInt8>()

    init(label: String) {
        queue = DispatchQueue(label: label)
        queue.setSpecific(key: key, value: 1)
    }

    /// Runs `body` on the queue and waits for it. Safe to call from the queue
    /// itself (a listener block calling back into the adapter), where it just
    /// runs `body`.
    func sync<T>(_ body: () throws -> T) rethrows -> T {
        if DispatchQueue.getSpecific(key: key) != nil {
            return try body()
        }
        return try queue.sync(execute: body)
    }

    /// A repeating timer on the queue. The caller keeps it and cancels it.
    func repeatingTimer(every interval: Double, handler: @escaping @Sendable () -> Void) -> DispatchSourceTimer {
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now() + interval, repeating: interval, leeway: .milliseconds(Int(interval * 250)))
        timer.setEventHandler(handler: handler)
        timer.resume()
        return timer
    }
}
#endif
