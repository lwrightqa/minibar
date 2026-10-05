import Foundation
@testable import TinyBarCore

/// Holds a fake request until the test opens it, so a test can act (Back,
/// sleep, quitting) while the request is on its way. A held request whose task
/// is cancelled lets go at once, as a URLSession request does.
final class Gate: @unchecked Sendable {
    private struct Waiter {
        let id: UUID
        let continuation: CheckedContinuation<Void, Never>
    }

    private let state = Locked<(open: Bool, waiters: [Waiter], arrived: Int)>((false, [], 0))

    /// How many requests have reached the gate so far.
    var arrived: Int { state.withLock { $0.arrived } }

    func wait() async {
        let id = UUID()
        await withTaskCancellationHandler {
            await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
                let goOn = state.withLock { state -> Bool in
                    state.arrived += 1
                    if state.open || Task.isCancelled { return true }
                    state.waiters.append(Waiter(id: id, continuation: continuation))
                    return false
                }
                if goOn { continuation.resume() }
            }
        } onCancel: {
            let waiter = state.withLock { state -> Waiter? in
                guard let index = state.waiters.firstIndex(where: { $0.id == id }) else { return nil }
                return state.waiters.remove(at: index)
            }
            waiter?.continuation.resume()
        }
    }

    /// Lets every held request go on, and every later one through at once.
    func open() {
        let waiters = state.withLock { state -> [Waiter] in
            state.open = true
            defer { state.waiters.removeAll() }
            return state.waiters
        }
        waiters.forEach { $0.continuation.resume() }
    }

    /// Waits (in real time, up to 2 seconds) until `count` requests have
    /// reached the gate. Returns `false` on time-out.
    @discardableResult
    func waitForArrivals(_ count: Int = 1) async -> Bool {
        let giveUp = Date().addingTimeInterval(2)
        while arrived < count {
            if Date() > giveUp { return false }
            await Task.yield()
            try? await Task.sleep(nanoseconds: 1_000_000)
        }
        return true
    }
}
