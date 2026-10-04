#if os(macOS)
import CoreMediaIO
import Foundation
import TinyBarCore

/// Whether any camera is running, from CoreMediaIO (docs/mac-app.md "Signals").
/// macOS has no public way to tell which app, so camera use never has a name.
///
/// Reads only state properties; never starts an `AVCaptureSession`. *Unverified
/// on a real Mac:* no camera prompt for an unsandboxed app (OverSight doesn't
/// ask; the sandboxed Mute does). If a prompt appears, "Count the camera"
/// should default to off (docs/decisions.md, Mac app).
///
/// How:
/// - Devices: `kCMIOHardwarePropertyDevices` on the system object.
/// - For each device: `kCMIODevicePropertyDeviceIsRunningSomewhere` (UInt32).
///   In use = any device reads non-zero.
/// - Listeners (`CMIOObjectAddPropertyListenerBlock`, on `queue`): the system
///   object's device list, and each device's "running somewhere"; plus a
///   once-a-second re-read. `onChange` is called only when the answer changes.
/// - Virtual cameras (OBS, Camo) and Continuity Camera count like any other;
///   that's expected.
/// - `kCMIOHardwarePropertyAllowScreenCaptureDevices` is deliberately left
///   off. It only adds iOS screen-capture devices (an iPhone's screen, for
///   QuickTime), which aren't cameras; Continuity Camera shows up without it.
final class CoreMediaIOCameraMonitor: CameraActivitySource, @unchecked Sendable {
    enum MonitorError: Error {
        /// CoreMediaIO wouldn't list the devices.
        case unavailable
    }

    private let work = AdapterQueue(label: "TinyBar.camera")

    // Everything below is touched only on `work.queue`.
    private var started = false
    private var onChange: (@Sendable (Bool) -> Void)?
    private var lastReported: Bool?
    private var timer: DispatchSourceTimer?
    private var listListener: CMIOObjectPropertyListenerBlock?
    private var deviceListeners: [CMIOObjectID: CMIOObjectPropertyListenerBlock] = [:]

    init() {}

    deinit {
        stop()
    }

    func start(onChange: @escaping @Sendable (Bool) -> Void) throws {
        try work.sync {
            guard !started else { return }
            guard CMIOProperty.devices() != nil else { throw MonitorError.unavailable }
            started = true
            self.onChange = onChange
            installListListener()
            syncDeviceListeners()
            timer = work.repeatingTimer(every: 1) { [weak self] in
                self?.refresh()
            }
            refresh()
        }
    }

    func stop() {
        work.sync {
            guard started else { return }
            started = false
            timer?.cancel()
            timer = nil
            if let listListener {
                var address = CMIOProperty.address(CMIOProperty.devicesSelector)
                _ = CMIOObjectRemovePropertyListenerBlock(CMIOProperty.system, &address, work.queue, listListener)
            }
            listListener = nil
            for (device, block) in deviceListeners {
                var address = CMIOProperty.address(CMIOProperty.runningSomewhereSelector)
                _ = CMIOObjectRemovePropertyListenerBlock(device, &address, work.queue, block)
            }
            deviceListeners = [:]
            lastReported = nil
            onChange = nil
        }
    }

    func current() -> Bool {
        work.sync {
            read() ?? lastReported ?? false
        }
    }

    // MARK: - On the queue

    private func refresh() {
        guard started, let now = read() else { return }
        guard now != lastReported else { return }
        lastReported = now
        onChange?(now)
    }

    private func read() -> Bool? {
        guard let devices = CMIOProperty.devices() else { return nil }
        return devices.contains { (CMIOProperty.uint32($0, CMIOProperty.runningSomewhereSelector) ?? 0) != 0 }
    }

    private func installListListener() {
        let block: CMIOObjectPropertyListenerBlock = { [weak self] _, _ in
            guard let self else { return }
            self.syncDeviceListeners()
            self.refresh()
        }
        var address = CMIOProperty.address(CMIOProperty.devicesSelector)
        if CMIOObjectAddPropertyListenerBlock(CMIOProperty.system, &address, work.queue, block) == noErr {
            listListener = block
        }
    }

    private func syncDeviceListeners() {
        guard started else { return }
        let current = Set(CMIOProperty.devices() ?? [])
        for (device, block) in deviceListeners where !current.contains(device) {
            var address = CMIOProperty.address(CMIOProperty.runningSomewhereSelector)
            _ = CMIOObjectRemovePropertyListenerBlock(device, &address, work.queue, block)
            deviceListeners[device] = nil
        }
        for device in current where deviceListeners[device] == nil {
            let block: CMIOObjectPropertyListenerBlock = { [weak self] _, _ in
                self?.refresh()
            }
            var address = CMIOProperty.address(CMIOProperty.runningSomewhereSelector)
            if CMIOObjectAddPropertyListenerBlock(device, &address, work.queue, block) == noErr {
                deviceListeners[device] = block
            }
        }
    }
}

/// Typed wrappers over `CMIOObjectGetPropertyData`. CoreMediaIO's constants
/// come into Swift as plain integers from anonymous C enums, hence the
/// conversions.
enum CMIOProperty {
    static let system = CMIOObjectID(kCMIOObjectSystemObject)
    static let devicesSelector = CMIOObjectPropertySelector(kCMIOHardwarePropertyDevices)
    static let runningSomewhereSelector = CMIOObjectPropertySelector(kCMIODevicePropertyDeviceIsRunningSomewhere)

    static func address(_ selector: CMIOObjectPropertySelector) -> CMIOObjectPropertyAddress {
        CMIOObjectPropertyAddress(
            mSelector: selector,
            mScope: CMIOObjectPropertyScope(kCMIOObjectPropertyScopeGlobal),
            mElement: CMIOObjectPropertyElement(kCMIOObjectPropertyElementMain)
        )
    }

    static func devices() -> [CMIOObjectID]? {
        var propertyAddress = address(devicesSelector)
        var size: UInt32 = 0
        guard CMIOObjectGetPropertyDataSize(system, &propertyAddress, 0, nil, &size) == noErr else {
            return nil
        }
        let stride = MemoryLayout<CMIOObjectID>.stride
        guard size >= stride else { return [] }
        var ids = [CMIOObjectID](repeating: 0, count: Int(size) / stride)
        var used: UInt32 = 0
        let status = ids.withUnsafeMutableBytes { buffer in
            CMIOObjectGetPropertyData(system, &propertyAddress, 0, nil, UInt32(buffer.count), &used, buffer.baseAddress)
        }
        guard status == noErr else { return nil }
        return Array(ids.prefix(Int(used) / stride))
    }

    static func uint32(_ object: CMIOObjectID, _ selector: CMIOObjectPropertySelector) -> UInt32? {
        var propertyAddress = address(selector)
        var value: UInt32 = 0
        var used: UInt32 = 0
        let status = withUnsafeMutablePointer(to: &value) { pointer in
            CMIOObjectGetPropertyData(object, &propertyAddress, 0, nil, UInt32(MemoryLayout<UInt32>.size), &used, pointer)
        }
        return status == noErr ? value : nil
    }
}
#endif
