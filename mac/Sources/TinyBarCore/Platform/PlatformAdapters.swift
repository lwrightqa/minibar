import Foundation

// The seams between the plain-Swift core and macOS. The TinyBarMac target
// implements each protocol with Apple's frameworks; tests use fakes.
//
// Rules for every adapter:
// - `start` may be called again after it throws (the engine retries a monitor
//   that failed to start every minute) and, for `BarDiscovery`, after `stop`
//   (the browser runs only while Wi-Fi is wanted). `stop` is idempotent.
// - Callbacks may come on any thread. They're `@Sendable`; the engine hops
//   to the main actor itself.
// - No adapter ever opens the mic or camera, creates an IO proc, an
//   `AVCaptureSession` or a process tap (criterion 30).

// MARK: - Mic and camera

/// Which processes have audio input running (macOS: CoreAudio's process
/// objects, `kAudioHardwarePropertyProcessObjectList` and
/// `kAudioProcessPropertyIsRunningInput`, macOS 14 and later).
public protocol MicActivitySource: AnyObject, Sendable {
    /// Starts listening. Calls `onChange` with the full list whenever it may
    /// have changed (property listeners), and also re-reads it once a second
    /// as a safety net, calling `onChange` only when the list differs.
    func start(onChange: @escaping @Sendable ([MicProcess]) -> Void) throws
    func stop()
    /// The list right now.
    func current() -> [MicProcess]
}

/// Whether any camera is running (macOS: CoreMediaIO,
/// `kCMIODevicePropertyDeviceIsRunningSomewhere` on every device in
/// `kCMIOHardwarePropertyDevices`).
public protocol CameraActivitySource: AnyObject, Sendable {
    /// Starts listening; calls `onChange` when the answer changes (listeners,
    /// device arrival and removal, plus a once-a-second re-read).
    func start(onChange: @escaping @Sendable (Bool) -> Void) throws
    func stop()
    func current() -> Bool
}

// MARK: - USB serial devices

/// A USB serial device, as IOKit describes it (api.md 6.2).
public struct SerialDevice: Hashable, Sendable {
    /// `kIOCalloutDeviceKey`: `/dev/cu.usbmodem1101`.
    public var calloutPath: String
    /// `idVendor` from the USB parent, if found.
    public var vendorID: Int?
    /// `idProduct` from the USB parent, if found.
    public var productID: Int?
    /// `USB Serial Number` from the USB parent. *Unverified:* the bar's MAC
    /// address (api.md 6.1). A hint only; `hello` decides.
    public var serialNumber: String?
    /// The IORegistry entry ID, to tell a replugged device from the old one.
    public var registryID: UInt64

    public init(calloutPath: String, vendorID: Int?, productID: Int?, serialNumber: String? = nil, registryID: UInt64) {
        self.calloutPath = calloutPath
        self.vendorID = vendorID
        self.productID = productID
        self.serialNumber = serialNumber
        self.registryID = registryID
    }

    /// Espressif's USB Serial/JTAG (VID 0x303A, PID 0x1001). The app opens
    /// nothing else (criterion 16).
    public var isEspressifSerialJTAG: Bool {
        vendorID == TinyBarAPI.USB.vendorID && productID == TinyBarAPI.USB.productID
    }
}

public enum SerialDeviceEvent: Hashable, Sendable {
    case appeared(SerialDevice)
    case disappeared(SerialDevice)
}

/// Watches serial ports arriving and leaving (macOS: IOKit,
/// `IOServiceAddMatchingNotification` on `kIOSerialBSDServiceValue` with
/// `kIOFirstMatchNotification` and `kIOTerminatedNotification`).
public protocol SerialDeviceWatcher: AnyObject, Sendable {
    /// Starts watching. Reports every Espressif USB Serial/JTAG device already
    /// present as `.appeared`, then arrivals and removals. Other serial devices
    /// are never reported.
    func start(onEvent: @escaping @Sendable (SerialDeviceEvent) -> Void) throws
    func stop()
}

// MARK: - Bonjour

/// A bar found on the network (api.md 3).
public struct DiscoveredBar: Hashable, Sendable {
    /// The Bonjour instance name, which is the bar's name: "MiniBar 2A1C".
    public var name: String
    /// TXT `id`: the bar's `device_id`. How the app finds its bar.
    public var deviceID: String?
    /// TXT `api`, `fw`, `path`, `auth`.
    public var api: String?
    public var fw: String?
    public var path: String?
    public var auth: AuthMode?
    /// Where to send HTTP: the resolved host name (`minibar.local`) or IPv4
    /// address, and port.
    public var endpoint: BarEndpoint

    public init(name: String, deviceID: String?, api: String? = nil, fw: String? = nil, path: String? = nil,
                auth: AuthMode? = nil, endpoint: BarEndpoint) {
        self.name = name
        self.deviceID = deviceID
        self.api = api
        self.fw = fw
        self.path = path
        self.auth = auth
        self.endpoint = endpoint
    }
}

public enum DiscoveryError: Hashable, Sendable {
    /// macOS refused local network access (macOS 15 and later).
    case localNetworkDenied
    /// Anything else, for logs.
    case failed(String)
}

/// Browses `_minibar._tcp`, and `_tinybar._tcp` for a bar on firmware 1.0.2
/// or earlier (`TinyBarAPI.Bonjour.serviceTypes`; macOS: one `NWBrowser` with
/// `.bonjourWithTXTRecord` per type, then resolves each result to a host and port).
/// Starting it is what brings up the Local Network prompt on macOS 15, so the
/// engine starts it only when Wi-Fi is allowed and the UI has explained why.
public protocol BarDiscovery: AnyObject, Sendable {
    /// Calls `onChange` with every bar currently found, whenever the set
    /// changes, and `onError` when browsing fails. After `.localNetworkDenied`,
    /// calls `onChange` again (with the bars found, possibly none) once
    /// browsing works, so the app knows access was allowed.
    func start(
        onChange: @escaping @Sendable ([DiscoveredBar]) -> Void,
        onError: @escaping @Sendable (DiscoveryError) -> Void
    )
    func stop()
}

// MARK: - Tokens

/// Where bar tokens are kept (macOS: the Keychain, api.md 16: one
/// generic-password item per bar, service `MiniBar`, account `device_id`,
/// `kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`). Never UserDefaults or a
/// file (criterion 20).
public protocol TokenStore: AnyObject, Sendable {
    func token(for deviceID: String) throws -> String?
    /// Adds or replaces.
    func setToken(_ token: String, for deviceID: String) throws
    /// Removing a token that isn't there isn't an error.
    func removeToken(for deviceID: String) throws
}

/// Tokens in memory, for tests.
public final class InMemoryTokenStore: TokenStore, @unchecked Sendable {
    private let tokens: Locked<[String: String]>

    public init(_ tokens: [String: String] = [:]) {
        self.tokens = Locked(tokens)
    }

    public func token(for deviceID: String) throws -> String? {
        tokens.withLock { $0[deviceID] }
    }

    public func setToken(_ token: String, for deviceID: String) throws {
        tokens.withLock { $0[deviceID] = token }
    }

    public func removeToken(for deviceID: String) throws {
        tokens.withLock { _ = $0.removeValue(forKey: deviceID) }
    }

    /// Every stored device ID, for tests.
    public var deviceIDs: [String] {
        tokens.withLock { Array($0.keys).sorted() }
    }
}

// MARK: - Login item

/// What macOS did with the login item (macOS: `SMAppService.Status`).
public enum LoginItemStatus: Hashable, Sendable {
    case enabled
    case notRegistered
    /// Allow it in System Settings › General › Login Items.
    case requiresApproval
    /// macOS can't find the app, or it isn't in Applications.
    case notFound
    /// Registering threw; the organization may manage login items.
    case failed(String)
}

/// Start at login (macOS: `SMAppService.mainApp`).
public protocol LoginItemService: AnyObject, Sendable {
    var status: LoginItemStatus { get }
    /// Registers; returns the resulting status rather than throwing.
    func register() -> LoginItemStatus
    func unregister() -> LoginItemStatus
    /// Opens System Settings › General › Login Items.
    func openSystemSettings()
}

// MARK: - Sleep and wake

public enum PowerEvent: Hashable, Sendable {
    /// The Mac is about to sleep. The handler sends `leaving` before returning.
    case willSleep
    /// The Mac is shutting down or restarting.
    case willPowerOff
    /// The Mac woke up.
    case didWake
}

/// Sleep, wake and power-off (macOS: `NSWorkspace` notifications, and
/// `IORegisterForSystemPower` so sleep can wait for the `leaving` message).
public protocol PowerEventSource: AnyObject, Sendable {
    /// Calls `handler` for each event. For `.willSleep` the adapter delays
    /// sleep until the handler returns or about 1 second has passed
    /// (`IOAllowPowerChange`), whichever is first.
    func start(handler: @escaping @Sendable (PowerEvent) async -> Void)
    func stop()
}
