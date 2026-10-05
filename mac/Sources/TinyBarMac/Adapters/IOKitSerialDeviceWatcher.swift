#if os(macOS)
import Foundation
import IOKit
import IOKit.serial
import TinyBarCore

/// Watches for the bar's USB serial port (api.md 6.2). Reports only
/// Espressif USB Serial/JTAG devices (VID 0x303A, PID 0x1001); never opens or
/// writes to anything (criterion 16: no other serial device is ever touched).
///
/// How:
/// - `IOServiceMatching(kIOSerialBSDServiceValue)` with
///   `IOServiceAddMatchingNotification` twice, `kIOFirstMatchNotification`
///   (arrivals) and `kIOTerminatedNotification` (removals), on a notification
///   port whose callbacks run on `queue`. Each iterator is drained at once,
///   including right after it's added, or no further notifications come; the
///   first drain reports the devices already plugged in.
/// - For each service: `kIOCalloutDeviceKey` (`/dev/cu.…`, never `tty.`);
///   `idVendor`, `idProduct` and `USB Serial Number` searched for up the
///   parents (`kIORegistryIterateRecursively | kIORegistryIterateParents`);
///   `IORegistryEntryGetRegistryEntryID` for `registryID`.
/// - A registryID → device map, so a removal is reported with the same
///   `SerialDevice` value and a device is never reported twice.
/// - Every 5 seconds, a rescan with `IOServiceGetMatchingServices` catches a
///   missed notification (docs/mac-app.md, "Plugging in and out").
///
/// No `/dev/cu.usbmodem*` directory scan as a fallback: a path alone can't
/// prove the vendor is Espressif, and criterion 16 says nothing else is ever
/// opened. If IOKit can't be watched, `start` throws and the app uses Wi-Fi.
final class IOKitSerialDeviceWatcher: SerialDeviceWatcher, @unchecked Sendable {
    enum WatchError: Error {
        case notificationPort
        case matching(kern_return_t)
    }

    private let work = AdapterQueue(label: "MiniBar.serial")

    // Everything below is touched only on `work.queue`.
    private var onEvent: (@Sendable (SerialDeviceEvent) -> Void)?
    private var notifyPort: IONotificationPortRef?
    private var arrivals: io_iterator_t = 0
    private var removals: io_iterator_t = 0
    private var known: [UInt64: SerialDevice] = [:]
    private var rescanTimer: DispatchSourceTimer?

    init() {}

    deinit {
        stop()
    }

    func start(onEvent: @escaping @Sendable (SerialDeviceEvent) -> Void) throws {
        try work.sync {
            guard notifyPort == nil else { return }
            guard let port = IONotificationPortCreate(kIOMainPortDefault) else {
                throw WatchError.notificationPort
            }
            IONotificationPortSetDispatchQueue(port, work.queue)
            notifyPort = port
            self.onEvent = onEvent

            // The callbacks are C functions: `self` travels as the refcon.
            // Unretained is safe because `stop()` (also run by `deinit`)
            // destroys the port before `self` goes away.
            let refcon = Unmanaged.passUnretained(self).toOpaque()
            var result = IOServiceAddMatchingNotification(
                port, kIOFirstMatchNotification, IOServiceMatching(kIOSerialBSDServiceValue),
                { refcon, iterator in
                    guard let refcon else { return }
                    Unmanaged<IOKitSerialDeviceWatcher>.fromOpaque(refcon).takeUnretainedValue().drainArrivals(iterator)
                },
                refcon, &arrivals
            )
            guard result == KERN_SUCCESS else {
                tearDown()
                throw WatchError.matching(result)
            }
            result = IOServiceAddMatchingNotification(
                port, kIOTerminatedNotification, IOServiceMatching(kIOSerialBSDServiceValue),
                { refcon, iterator in
                    guard let refcon else { return }
                    Unmanaged<IOKitSerialDeviceWatcher>.fromOpaque(refcon).takeUnretainedValue().drainRemovals(iterator)
                },
                refcon, &removals
            )
            guard result == KERN_SUCCESS else {
                tearDown()
                throw WatchError.matching(result)
            }
            // Arm both, and report what's already plugged in.
            drainArrivals(arrivals)
            drainRemovals(removals)
            rescanTimer = work.repeatingTimer(every: 5) { [weak self] in
                self?.rescan()
            }
        }
    }

    func stop() {
        work.sync {
            tearDown()
        }
    }

    // MARK: - On the queue

    private func tearDown() {
        rescanTimer?.cancel()
        rescanTimer = nil
        if arrivals != 0 {
            IOObjectRelease(arrivals)
            arrivals = 0
        }
        if removals != 0 {
            IOObjectRelease(removals)
            removals = 0
        }
        if let notifyPort {
            IONotificationPortDestroy(notifyPort)
        }
        notifyPort = nil
        known = [:]
        onEvent = nil
    }

    private func drainArrivals(_ iterator: io_iterator_t) {
        for device in Self.devices(in: iterator) where known[device.registryID] == nil {
            known[device.registryID] = device
            onEvent?(.appeared(device))
        }
    }

    private func drainRemovals(_ iterator: io_iterator_t) {
        var service = IOIteratorNext(iterator)
        while service != 0 {
            var entryID: UInt64 = 0
            if IORegistryEntryGetRegistryEntryID(service, &entryID) == KERN_SUCCESS,
               let device = known.removeValue(forKey: entryID) {
                onEvent?(.disappeared(device))
            }
            IOObjectRelease(service)
            service = IOIteratorNext(iterator)
        }
    }

    /// The safety net: compares what IOKit has now with what was reported.
    private func rescan() {
        guard notifyPort != nil else { return }
        var iterator: io_iterator_t = 0
        guard IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(kIOSerialBSDServiceValue), &iterator) == KERN_SUCCESS else {
            return
        }
        defer { IOObjectRelease(iterator) }
        var present: [UInt64: SerialDevice] = [:]
        for device in Self.devices(in: iterator) {
            present[device.registryID] = device
        }
        for (id, device) in known where present[id] == nil {
            known[id] = nil
            onEvent?(.disappeared(device))
        }
        for (id, device) in present.sorted(by: { $0.key < $1.key }) where known[id] == nil {
            known[id] = device
            onEvent?(.appeared(device))
        }
    }

    // MARK: - Reading the registry

    /// Every Espressif USB Serial/JTAG device in `iterator`, which is drained.
    /// Other serial devices are skipped and never reported.
    private static func devices(in iterator: io_iterator_t) -> [SerialDevice] {
        var devices: [SerialDevice] = []
        var service = IOIteratorNext(iterator)
        while service != 0 {
            if let device = device(for: service), device.isEspressifSerialJTAG {
                devices.append(device)
            }
            IOObjectRelease(service)
            service = IOIteratorNext(iterator)
        }
        return devices
    }

    private static func device(for service: io_object_t) -> SerialDevice? {
        guard let path = retained(IORegistryEntryCreateCFProperty(service, kIOCalloutDeviceKey as CFString, kCFAllocatorDefault, 0)) as? String,
              path.hasPrefix("/dev/cu.") else {
            return nil
        }
        var entryID: UInt64 = 0
        guard IORegistryEntryGetRegistryEntryID(service, &entryID) == KERN_SUCCESS else { return nil }
        return SerialDevice(
            calloutPath: path,
            vendorID: (searchParents(service, "idVendor") as? NSNumber)?.intValue,
            productID: (searchParents(service, "idProduct") as? NSNumber)?.intValue,
            serialNumber: searchParents(service, "USB Serial Number") as? String,
            registryID: entryID
        )
    }

    /// A property of the service or the nearest parent that has it (the USB
    /// device or interface above the serial port).
    private static func searchParents(_ service: io_object_t, _ key: String) -> AnyObject? {
        retained(IORegistryEntrySearchCFProperty(
            service, kIOServicePlane, key as CFString, kCFAllocatorDefault,
            IOOptionBits(kIORegistryIterateRecursively | kIORegistryIterateParents)
        ))
    }

    // `IORegistryEntryCreateCFProperty` comes into Swift as returning
    // `Unmanaged<CFTypeRef>!` (unaudited, +1), and `IORegistryEntrySearchCFProperty`
    // as a managed `CFTypeRef!` (`CF_RETURNS_RETAINED`). These two overloads take
    // whichever the SDK hands over, so a different import can't silently turn
    // the casts above into ones that always fail.
    private static func retained(_ value: Unmanaged<CFTypeRef>?) -> AnyObject? {
        value?.takeRetainedValue()
    }

    private static func retained(_ value: CFTypeRef?) -> AnyObject? {
        value
    }
}
#endif
