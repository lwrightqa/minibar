#if os(macOS)
import AppKit
import CoreAudio
import Darwin
import Foundation
import TinyBarCore

/// Which processes have audio input running, from CoreAudio's process objects
/// (macOS 14 and later; docs/mac-app.md "Signals").
///
/// Reads only state properties. It never creates an IO proc, opens an input
/// stream, or makes a process tap, so it should never trigger the microphone
/// prompt or the orange dot (criterion 30). *Unverified on a real Mac:* that
/// reading the per-process list causes no prompt at all (docs/mac-app.md,
/// "It doesn't need microphone or camera permission").
///
/// How:
/// - `kAudioHardwarePropertyProcessObjectList` on the system object gives the
///   process objects. For each, `kAudioProcessPropertyIsRunningInput` (UInt32,
///   non-zero = input running); for those running, `kAudioProcessPropertyPID`
///   and `kAudioProcessPropertyBundleID` (a CFString the caller releases).
/// - `proc_pidpath` gives the executable's path; `AppPaths.outermostAppPath`
///   finds the app around it (Chrome's and Slack's helpers live inside their
///   apps), whose bundle ID and Finder name go into the `MicProcess`. Cached
///   per process object for the life of the process.
/// - Listeners (`AudioObjectAddPropertyListenerBlock`, on `queue`): the system
///   object's process list, and each process object's "is running input". Plus
///   a re-read every `safetyNetInterval` (1 second, docs/mac-app.md "Signals")
///   as a safety net. `onChange` is called only when the list differs from
///   the last one reported.
/// - CoreAudio delivers its notifications through the main run loop unless
///   `kAudioHardwarePropertyRunLoop` is set to NULL (AudioHardware.h), so a
///   blocked main thread (an open panel, a Keychain prompt) would delay them
///   to the next re-read. The first monitor made sets it to NULL, before any
///   other CoreAudio call, so CoreAudio uses its own notification thread. The
///   setting is process-wide; TinyBar does no other audio.
///
/// **Fallback.** If the system object doesn't have the process list (it
/// should on every macOS 14 and later; the scratch type-check against the
/// macOS 14.5 SDK accepted these constants at a 14.0 deployment target with no
/// `#available`), the monitor falls back to the device-level flag:
/// `kAudioDevicePropertyDeviceIsRunningSomewhere` on every device that has
/// input streams, with `kAudioHardwarePropertyDevices` tracked for devices
/// arriving and leaving. It then reports one anonymous "Microphone" process
/// with no app name, so the ignore list can't work and a headset playing music
/// may read as in use (docs/mac-app.md explains why the per-process flag is
/// the real signal). `mode` says which is in use.
final class CoreAudioMicMonitor: MicActivitySource, @unchecked Sendable {
    enum Mode: Sendable {
        /// Per-process input flags, with app names.
        case perProcess
        /// The device-level fallback, no names.
        case deviceLevel
    }

    enum MonitorError: Error {
        /// Neither the process list nor the device list could be read.
        case unavailable
    }

    /// The process the device-level fallback reports while any input device
    /// is running. pid -1 never matches a real process.
    static let anonymousMicrophone = MicProcess(pid: -1, displayName: "Microphone")

    let mode: Mode

    /// How often the safety-net re-read runs. Each one costs an IPC round trip
    /// to coreaudiod per process object (dozens on a typical Mac). *Measure*
    /// Energy Impact on a Mac (criterion 36); if it shows, 2 seconds still
    /// meets criterion 33's start delay + 2 s, since the listeners carry the
    /// normal case.
    static let safetyNetInterval: Double = 1

    private let work = AdapterQueue(label: "TinyBar.mic")

    // Everything below is touched only on `work.queue`.
    private var started = false
    private var onChange: (@Sendable ([MicProcess]) -> Void)?
    private var lastReported: [MicProcess]?
    private var timer: DispatchSourceTimer?
    private var listListener: AudioObjectPropertyListenerBlock?
    private var objectListeners: [AudioObjectID: AudioObjectPropertyListenerBlock] = [:]
    private var identities: [AudioObjectID: MicProcess] = [:]

    init() {
        Self.useOwnNotificationThread()
        mode = CoreAudioProperty.has(CoreAudioProperty.system, kAudioHardwarePropertyProcessObjectList)
            ? .perProcess
            : .deviceLevel
    }

    /// Sets `kAudioHardwarePropertyRunLoop` to NULL once per process: CoreAudio
    /// then creates its own thread for notifications instead of using the
    /// main run loop. *Unverified on a Mac* (AudioHardware.h documents it).
    private static let ownNotificationThread: Void = {
        var runLoop: CFRunLoop? = nil
        var address = CoreAudioProperty.address(kAudioHardwarePropertyRunLoop)
        _ = withUnsafePointer(to: &runLoop) { pointer in
            AudioObjectSetPropertyData(CoreAudioProperty.system, &address, 0, nil,
                                       UInt32(MemoryLayout<CFRunLoop?>.size), pointer)
        }
    }()

    private static func useOwnNotificationThread() {
        _ = ownNotificationThread
    }

    deinit {
        stop()
    }

    func start(onChange: @escaping @Sendable ([MicProcess]) -> Void) throws {
        try work.sync {
            guard !started else { return }
            guard read() != nil else { throw MonitorError.unavailable }
            started = true
            self.onChange = onChange
            installListListener()
            syncObjectListeners()
            timer = work.repeatingTimer(every: Self.safetyNetInterval) { [weak self] in
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
                var address = CoreAudioProperty.address(listSelector)
                _ = AudioObjectRemovePropertyListenerBlock(CoreAudioProperty.system, &address, work.queue, listListener)
            }
            listListener = nil
            for (object, block) in objectListeners {
                var address = CoreAudioProperty.address(objectSelector)
                _ = AudioObjectRemovePropertyListenerBlock(object, &address, work.queue, block)
            }
            objectListeners = [:]
            identities = [:]
            lastReported = nil
            onChange = nil
        }
    }

    func current() -> [MicProcess] {
        work.sync {
            read() ?? lastReported ?? []
        }
    }

    // MARK: - On the queue

    /// The system property whose changes add or remove objects to watch.
    private var listSelector: AudioObjectPropertySelector {
        mode == .perProcess ? kAudioHardwarePropertyProcessObjectList : kAudioHardwarePropertyDevices
    }

    /// The per-object property that says "in use".
    private var objectSelector: AudioObjectPropertySelector {
        mode == .perProcess ? kAudioProcessPropertyIsRunningInput : kAudioDevicePropertyDeviceIsRunningSomewhere
    }

    /// Re-reads and reports a change.
    private func refresh() {
        guard started, let now = read() else { return }
        guard now != lastReported else { return }
        lastReported = now
        onChange?(now)
    }

    /// The list right now, or `nil` if CoreAudio wouldn't say.
    private func read() -> [MicProcess]? {
        switch mode {
        case .perProcess: return readProcesses()
        case .deviceLevel: return readDevices()
        }
    }

    private func readProcesses() -> [MicProcess]? {
        guard let objects = CoreAudioProperty.objectList(CoreAudioProperty.system, kAudioHardwarePropertyProcessObjectList) else {
            return nil
        }
        var result: [MicProcess] = []
        for object in objects where (CoreAudioProperty.uint32(object, kAudioProcessPropertyIsRunningInput) ?? 0) != 0 {
            if let process = identity(of: object) {
                result.append(process)
            }
        }
        return result.sorted { $0.pid < $1.pid }
    }

    private func readDevices() -> [MicProcess]? {
        guard let devices = CoreAudioProperty.objectList(CoreAudioProperty.system, kAudioHardwarePropertyDevices) else {
            return nil
        }
        let running = devices.contains { device in
            CoreAudioProperty.hasInputStreams(device)
                && (CoreAudioProperty.uint32(device, kAudioDevicePropertyDeviceIsRunningSomewhere) ?? 0) != 0
        }
        return running ? [Self.anonymousMicrophone] : []
    }

    /// Who a process object is, cached for the life of the object. `nil` if
    /// its PID can't be read (it's going away); not cached, so it's retried.
    private func identity(of object: AudioObjectID) -> MicProcess? {
        if let cached = identities[object] {
            return cached
        }
        guard let pid = CoreAudioProperty.int32(object, kAudioProcessPropertyPID), pid > 0 else {
            return nil
        }
        let process = ProcessIdentity.micProcess(
            pid: pid,
            bundleID: CoreAudioProperty.string(object, kAudioProcessPropertyBundleID)
        )
        identities[object] = process
        return process
    }

    private func installListListener() {
        let block: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
            guard let self else { return }
            self.syncObjectListeners()
            self.refresh()
        }
        var address = CoreAudioProperty.address(listSelector)
        if AudioObjectAddPropertyListenerBlock(CoreAudioProperty.system, &address, work.queue, block) == noErr {
            listListener = block
        }
    }

    /// Adds a listener to each new object and removes those of objects that
    /// are gone (and forgets their cached identity).
    private func syncObjectListeners() {
        guard started else { return }
        let current = Set(CoreAudioProperty.objectList(CoreAudioProperty.system, listSelector) ?? [])
        for (object, block) in objectListeners where !current.contains(object) {
            var address = CoreAudioProperty.address(objectSelector)
            _ = AudioObjectRemovePropertyListenerBlock(object, &address, work.queue, block)
            objectListeners[object] = nil
        }
        for object in identities.keys where !current.contains(object) {
            identities[object] = nil
        }
        for object in current where objectListeners[object] == nil {
            if mode == .deviceLevel && !CoreAudioProperty.hasInputStreams(object) {
                continue
            }
            let block: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
                self?.refresh()
            }
            var address = CoreAudioProperty.address(objectSelector)
            if AudioObjectAddPropertyListenerBlock(object, &address, work.queue, block) == noErr {
                objectListeners[object] = block
            }
        }
    }
}

// MARK: - Reading CoreAudio properties

/// Small typed wrappers over `AudioObjectGetPropertyData`. Every read is of
/// the global scope and main element unless stated. All return `nil` on any
/// error rather than throwing: a process can vanish between two reads.
enum CoreAudioProperty {
    static let system = AudioObjectID(kAudioObjectSystemObject)

    static func address(
        _ selector: AudioObjectPropertySelector,
        scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal
    ) -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(mSelector: selector, mScope: scope, mElement: kAudioObjectPropertyElementMain)
    }

    static func has(_ object: AudioObjectID, _ selector: AudioObjectPropertySelector) -> Bool {
        var propertyAddress = address(selector)
        return AudioObjectHasProperty(object, &propertyAddress)
    }

    /// An array of object IDs (the process list, the device list).
    static func objectList(_ object: AudioObjectID, _ selector: AudioObjectPropertySelector) -> [AudioObjectID]? {
        var propertyAddress = address(selector)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(object, &propertyAddress, 0, nil, &size) == noErr else {
            return nil
        }
        let stride = MemoryLayout<AudioObjectID>.stride
        guard size >= stride else { return [] }
        var ids = [AudioObjectID](repeating: 0, count: Int(size) / stride)
        let status = ids.withUnsafeMutableBytes { buffer in
            AudioObjectGetPropertyData(object, &propertyAddress, 0, nil, &size, buffer.baseAddress!)
        }
        guard status == noErr else { return nil }
        // The list may have shrunk between the two calls.
        return Array(ids.prefix(Int(size) / stride))
    }

    static func uint32(_ object: AudioObjectID, _ selector: AudioObjectPropertySelector) -> UInt32? {
        scalar(object, selector, as: UInt32.self)
    }

    static func int32(_ object: AudioObjectID, _ selector: AudioObjectPropertySelector) -> Int32? {
        scalar(object, selector, as: Int32.self)
    }

    /// A CFString property the caller owns (`kAudioProcessPropertyBundleID`).
    /// Empty strings come back as `nil`.
    static func string(_ object: AudioObjectID, _ selector: AudioObjectPropertySelector) -> String? {
        var propertyAddress = address(selector)
        var value: Unmanaged<CFString>?
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        let status = withUnsafeMutablePointer(to: &value) { pointer in
            AudioObjectGetPropertyData(object, &propertyAddress, 0, nil, &size, pointer)
        }
        guard status == noErr, let value else { return nil }
        let string = value.takeRetainedValue() as String
        return string.isEmpty ? nil : string
    }

    /// The device has at least one input stream (a microphone, or a headset's mic).
    static func hasInputStreams(_ device: AudioObjectID) -> Bool {
        var propertyAddress = address(kAudioDevicePropertyStreams, scope: kAudioObjectPropertyScopeInput)
        var size: UInt32 = 0
        return AudioObjectGetPropertyDataSize(device, &propertyAddress, 0, nil, &size) == noErr && size > 0
    }

    private static func scalar<T: FixedWidthInteger>(
        _ object: AudioObjectID,
        _ selector: AudioObjectPropertySelector,
        as type: T.Type
    ) -> T? {
        var propertyAddress = address(selector)
        var value: T = 0
        var size = UInt32(MemoryLayout<T>.size)
        let status = withUnsafeMutablePointer(to: &value) { pointer in
            AudioObjectGetPropertyData(object, &propertyAddress, 0, nil, &size, pointer)
        }
        return status == noErr ? value : nil
    }
}

// MARK: - Who a process is

/// Fills in a `MicProcess` from a PID: the executable's path, and the
/// outermost app around it (docs/mac-app.md, "Which apps count"). Never sent
/// anywhere; the names are for this Mac's menu and Settings only.
enum ProcessIdentity {
    static func micProcess(pid: pid_t, bundleID: String?) -> MicProcess {
        let path = executablePath(of: pid)
        let appPath = path.flatMap { AppPaths.outermostAppPath($0) }
        var appBundleID: String?
        var appName: String?
        if let appPath {
            let home = FileManager.default.homeDirectoryForCurrentUser.path
            if AppPaths.isInProtectedFolder(appPath, home: home) {
                // Reading its Info.plist there would bring up a Files &
                // Folders prompt: ask LaunchServices about the running app.
                let running = NSWorkspace.shared.runningApplications.first { $0.bundleURL?.path == appPath }
                appBundleID = running?.bundleIdentifier
                appName = running?.localizedName ?? Self.nameFromPath(appPath)
            } else {
                appBundleID = Bundle(path: appPath)?.bundleIdentifier
                appName = finderName(ofApp: appPath)
            }
        }
        let name = appName ?? path.flatMap { $0.split(separator: "/").last.map(String.init) }
        return MicProcess(
            pid: pid,
            bundleID: bundleID,
            executablePath: path,
            outerAppBundleID: appBundleID,
            displayName: name
        )
    }

    /// `proc_pidpath`, or `nil` if macOS won't say (the process is gone, or
    /// belongs to a user this one can't see).
    static func executablePath(of pid: pid_t) -> String? {
        // PROC_PIDPATHINFO_MAXSIZE is 4 * MAXPATHLEN (a macro Swift can't import).
        var buffer = [UInt8](repeating: 0, count: 4 * Int(MAXPATHLEN))
        let length = buffer.withUnsafeMutableBytes { raw in
            proc_pidpath(pid, raw.baseAddress, UInt32(raw.count))
        }
        guard length > 0 else { return nil }
        return String(decoding: buffer.prefix(Int(length)), as: UTF8.self)
    }

    /// "Foo" for ".../Foo.app", without reading anything.
    static func nameFromPath(_ appPath: String) -> String {
        var name = appPath.split(separator: "/").last.map(String.init) ?? appPath
        if name.lowercased().hasSuffix(".app") { name = String(name.dropLast(4)) }
        return name
    }

    /// The app's name as Finder shows it ("zoom.us", "Google Chrome").
    static func finderName(ofApp path: String) -> String {
        var name = FileManager.default.displayName(atPath: path)
        if name.lowercased().hasSuffix(".app") {
            name = String(name.dropLast(4))
        }
        return name
    }
}
#endif
