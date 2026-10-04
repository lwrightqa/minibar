#if os(macOS)
import AppKit
import Foundation
import IOKit
import IOKit.pwr_mgt
import TinyBarCore

/// Sleep, wake and power-off, so the app can send `"leaving": true` before
/// the Mac sleeps and a fresh state after it wakes (api.md 5.2, 16).
///
/// How:
/// - Sleep and wake: `IORegisterForSystemPower`, whose callbacks run on
///   `queue`. On `kIOMessageSystemWillSleep` the handler runs, and
///   `IOAllowPowerChange` is called when it returns or after about a second,
///   whichever is first, so the `leaving` message has time to go out. On
///   `kIOMessageCanSystemSleep` (idle sleep asks first) sleep is allowed at
///   once. On `kIOMessageSystemHasPoweredOn`: `.didWake`.
/// - Power-off: `NSWorkspace.willPowerOffNotification`. It doesn't wait for
///   the handler; quitting (`applicationShouldTerminate`) sends `leaving` too.
/// - If `IORegisterForSystemPower` fails, `NSWorkspace.willSleepNotification`
///   and `didWakeNotification` stand in for it (without the delay).
/// - Each sleep is reported once, whichever mechanism reports it.
/// - *Unverified:* that a USB serial write still reaches the bar during
///   `kIOMessageSystemWillSleep` (it should; the bus is still up), and that an
///   HTTP request gets out in that second over Wi-Fi.
final class SystemPowerEvents: PowerEventSource, @unchecked Sendable {
    /// `iokit_common_msg(…)` values from `<IOKit/IOMessage.h>`. Swift can't
    /// import those macros: `sys_iokit` (0x38 << 26) | `sub_iokit_common` (0) | code.
    enum Message {
        static let canSystemSleep: UInt32 = 0xE000_0270
        static let systemWillSleep: UInt32 = 0xE000_0280
        static let systemWillNotSleep: UInt32 = 0xE000_0290
        static let systemHasPoweredOn: UInt32 = 0xE000_0300
    }

    /// How long sleep waits for the handler.
    static let sleepDelay: TimeInterval = 1.0

    private let work = AdapterQueue(label: "TinyBar.power")

    // Everything below is touched only on `work.queue`.
    private var handler: (@Sendable (PowerEvent) async -> Void)?
    private var rootPort: io_connect_t = 0
    private var notifyPort: IONotificationPortRef?
    private var notifier: io_object_t = 0
    /// Set between a sleep being reported and the next wake, so the IOKit and
    /// NSWorkspace paths don't both report it.
    private var asleep = false

    /// The `NSWorkspace` observers, added and removed on the main thread.
    private let observers = Locked<[any NSObjectProtocol]>([])

    init() {}

    func start(handler: @escaping @Sendable (PowerEvent) async -> Void) {
        let registered = work.sync { () -> Bool in
            guard self.handler == nil else { return rootPort != 0 }
            self.handler = handler
            var port: IONotificationPortRef?
            var notifier: io_object_t = 0
            let refcon = Unmanaged.passUnretained(self).toOpaque()
            let root = IORegisterForSystemPower(refcon, &port, { refcon, _, messageType, argument in
                guard let refcon else { return }
                Unmanaged<SystemPowerEvents>.fromOpaque(refcon).takeUnretainedValue()
                    .received(messageType, notificationID: Int(bitPattern: argument))
            }, &notifier)
            guard root != 0, let port else { return false }
            IONotificationPortSetDispatchQueue(port, work.queue)
            rootPort = root
            notifyPort = port
            self.notifier = notifier
            return true
        }
        onMain { [weak self] in
            self?.addWorkspaceObservers(sleepAndWake: !registered)
        }
    }

    func stop() {
        work.sync {
            if notifier != 0 {
                IODeregisterForSystemPower(&notifier)
                notifier = 0
            }
            if rootPort != 0 {
                IOServiceClose(rootPort)
                rootPort = 0
            }
            if let notifyPort {
                IONotificationPortDestroy(notifyPort)
            }
            notifyPort = nil
            handler = nil
        }
        onMain { [weak self] in
            self?.removeWorkspaceObservers()
        }
    }

    // MARK: - IOKit, on the queue

    private func received(_ message: UInt32, notificationID: Int) {
        switch message {
        case Message.canSystemSleep:
            IOAllowPowerChange(rootPort, notificationID)
        case Message.systemWillSleep:
            let allow = AllowPowerChangeOnce(rootPort: rootPort, notificationID: notificationID)
            if let handler, !asleep {
                asleep = true
                Task {
                    await handler(.willSleep)
                    allow.allow()
                }
                work.queue.asyncAfter(deadline: .now() + Self.sleepDelay) {
                    allow.allow()
                }
            } else {
                allow.allow()
            }
        case Message.systemHasPoweredOn:
            wake()
        default:
            break
        }
    }

    private func wake() {
        guard asleep else { return }
        asleep = false
        if let handler {
            Task { await handler(.didWake) }
        }
    }

    // MARK: - NSWorkspace

    @MainActor
    private func addWorkspaceObservers(sleepAndWake: Bool) {
        guard observers.get().isEmpty else { return }
        let center = NSWorkspace.shared.notificationCenter
        var added: [any NSObjectProtocol] = []
        added.append(center.addObserver(forName: NSWorkspace.willPowerOffNotification, object: nil, queue: nil) { [weak self] _ in
            self?.send(.willPowerOff)
        })
        if sleepAndWake {
            added.append(center.addObserver(forName: NSWorkspace.willSleepNotification, object: nil, queue: nil) { [weak self] _ in
                guard let self else { return }
                self.work.queue.async {
                    guard !self.asleep else { return }
                    self.asleep = true
                    self.send(.willSleep)
                }
            })
            added.append(center.addObserver(forName: NSWorkspace.didWakeNotification, object: nil, queue: nil) { [weak self] _ in
                guard let self else { return }
                self.work.queue.async { self.wake() }
            })
        }
        observers.set(added)
    }

    @MainActor
    private func removeWorkspaceObservers() {
        let center = NSWorkspace.shared.notificationCenter
        for observer in observers.get() {
            center.removeObserver(observer)
        }
        observers.set([])
    }

    private func send(_ event: PowerEvent) {
        guard let handler = work.sync({ handler }) else { return }
        Task { await handler(event) }
    }

    private func onMain(_ body: @escaping @MainActor @Sendable () -> Void) {
        if Thread.isMainThread {
            MainActor.assumeIsolated { body() }
        } else {
            Task { @MainActor in body() }
        }
    }
}

/// Calls `IOAllowPowerChange` exactly once, from whichever of the handler and
/// the time limit finishes first.
private final class AllowPowerChangeOnce: Sendable {
    private let rootPort: io_connect_t
    private let notificationID: Int
    private let done = Locked(false)

    init(rootPort: io_connect_t, notificationID: Int) {
        self.rootPort = rootPort
        self.notificationID = notificationID
    }

    func allow() {
        let first = done.withLock { done -> Bool in
            defer { done = true }
            return !done
        }
        if first {
            IOAllowPowerChange(rootPort, notificationID)
        }
    }
}
#endif
