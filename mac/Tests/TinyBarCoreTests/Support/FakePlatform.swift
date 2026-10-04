import Foundation
@testable import TinyBarCore

/// The mic monitor, driven by the test.
final class FakeMic: MicActivitySource, @unchecked Sendable {
    private let state = Locked<(processes: [MicProcess], onChange: (@Sendable ([MicProcess]) -> Void)?, stopped: Bool)>(([], nil, false))

    func start(onChange: @escaping @Sendable ([MicProcess]) -> Void) throws {
        state.withLock { $0.onChange = onChange }
    }

    func stop() {
        state.withLock { $0.onChange = nil; $0.stopped = true }
    }

    func current() -> [MicProcess] {
        state.withLock { $0.processes }
    }

    var isStopped: Bool { state.withLock { $0.stopped } }

    /// These processes use the mic now.
    func set(_ processes: MicProcess...) {
        let onChange = state.withLock { s -> (@Sendable ([MicProcess]) -> Void)? in
            s.processes = processes
            return s.onChange
        }
        onChange?(processes)
    }
}

/// The camera monitor, driven by the test.
final class FakeCamera: CameraActivitySource, @unchecked Sendable {
    private let state = Locked<(inUse: Bool, onChange: (@Sendable (Bool) -> Void)?, stopped: Bool)>((false, nil, false))

    func start(onChange: @escaping @Sendable (Bool) -> Void) throws {
        state.withLock { $0.onChange = onChange }
    }

    func stop() {
        state.withLock { $0.onChange = nil; $0.stopped = true }
    }

    func current() -> Bool {
        state.withLock { $0.inUse }
    }

    func set(_ inUse: Bool) {
        let onChange = state.withLock { s -> (@Sendable (Bool) -> Void)? in
            s.inUse = inUse
            return s.onChange
        }
        onChange?(inUse)
    }
}

/// The IOKit watcher, driven by the test.
final class FakeSerialWatcher: SerialDeviceWatcher, @unchecked Sendable {
    private let state = Locked<(onEvent: (@Sendable (SerialDeviceEvent) -> Void)?, stopped: Bool)>((nil, false))

    func start(onEvent: @escaping @Sendable (SerialDeviceEvent) -> Void) throws {
        state.withLock { $0.onEvent = onEvent }
    }

    func stop() {
        state.withLock { $0.onEvent = nil; $0.stopped = true }
    }

    var isStopped: Bool { state.withLock { $0.stopped } }

    func send(_ event: SerialDeviceEvent) {
        state.withLock { $0.onEvent }?(event)
    }
}

/// `SMAppService`, as the test says it behaves.
final class FakeLoginItem: LoginItemService, @unchecked Sendable {
    private let state = Locked<(status: LoginItemStatus, registerResult: LoginItemStatus)>((.notRegistered, .enabled))

    var status: LoginItemStatus { state.withLock { $0.status } }

    /// What `register()` will do.
    func willRegister(as status: LoginItemStatus) {
        state.withLock { $0.registerResult = status }
    }

    func register() -> LoginItemStatus {
        state.withLock { s in
            s.status = s.registerResult
            return s.status
        }
    }

    func unregister() -> LoginItemStatus {
        state.withLock { s in
            s.status = .notRegistered
            return s.status
        }
    }

    func openSystemSettings() {}
}

/// Sleep and wake, driven by the test.
final class FakePower: PowerEventSource, @unchecked Sendable {
    private let state = Locked<(@Sendable (PowerEvent) async -> Void)?>(nil)

    func start(handler: @escaping @Sendable (PowerEvent) async -> Void) {
        state.set(handler)
    }

    func stop() {
        state.set(nil)
    }

    /// Delivers the event and waits for the handler, as the adapter does before sleep.
    func send(_ event: PowerEvent) async {
        await state.get()?(event)
    }
}
