import Foundation

/// A test call from Settings › Connection: On a call for 10 seconds as "Test".
public struct TestCall: Hashable, Sendable {
    public static let duration: TimeInterval = 10
    public static let appName = "Test"

    public var startedAt: Date
    public var callID: UInt32

    public init(startedAt: Date, callID: UInt32) {
        self.startedAt = startedAt
        self.callID = callID
    }

    public var endsAt: Date { startedAt.addingTimeInterval(TestCall.duration) }
}

/// What "Count … Again" can undo, while that app (or the camera) is still in
/// use (mac-app-ux.md 4.5).
public struct UndoState: Hashable, Sendable {
    /// The last "Don't Count …" for an app.
    public var app: CatalogEdit?
    /// The camera was just stopped counting with "Don't Count the Camera".
    public var camera: Bool

    public init(app: CatalogEdit? = nil, camera: Bool = false) {
        self.app = app
        self.camera = camera
    }
}

/// A platform monitor that couldn't start. The engine tries it again every
/// minute; until then the menu says so, because calls may go unseen.
public struct MonitorProblems: OptionSet, Hashable, Sendable {
    public let rawValue: Int
    public init(rawValue: Int) { self.rawValue = rawValue }

    /// CoreAudio's process list (and its device-level fallback) couldn't be read.
    public static let mic = MonitorProblems(rawValue: 1 << 0)
    /// CoreMediaIO's camera list couldn't be read.
    public static let camera = MonitorProblems(rawValue: 1 << 1)
    /// IOKit's USB device notifications couldn't be set up.
    public static let usb = MonitorProblems(rawValue: 1 << 2)
}

/// Everything the UI shows, in one value. The engine publishes a new one
/// whenever anything changes; `MenuPresenter` turns it into the menu.
public struct EngineState: Hashable, Sendable {
    public var settings: AppSettings
    /// What the Mac sees (line 1).
    public var observation: MacObservation
    /// The call the Mac has decided on, if any.
    public var call: DetectedCall?
    public var testCall: TestCall?
    public var connection: ConnectionState
    /// Used the mic since TinyBar opened (memory only).
    public var seenApps: [SeenApp]
    public var undo: UndoState
    public var loginItem: LoginItemStatus
    /// The Bonjour browser reported Local Network denied (macOS 15+). Shown
    /// in Privacy with its button.
    public var localNetworkBlocked: Bool
    /// Monitors that couldn't start (shown as Needs you for the mic and camera).
    public var monitorProblems: MonitorProblems

    public init(
        settings: AppSettings,
        observation: MacObservation = .idle,
        call: DetectedCall? = nil,
        testCall: TestCall? = nil,
        connection: ConnectionState,
        seenApps: [SeenApp] = [],
        undo: UndoState = UndoState(),
        loginItem: LoginItemStatus = .notRegistered,
        localNetworkBlocked: Bool = false,
        monitorProblems: MonitorProblems = []
    ) {
        self.settings = settings
        self.observation = observation
        self.call = call
        self.testCall = testCall
        self.connection = connection
        self.seenApps = seenApps
        self.undo = undo
        self.loginItem = loginItem
        self.localNetworkBlocked = localNetworkBlocked
        self.monitorProblems = monitorProblems
    }

    /// The monitor problems that stop calls being seen: the mic always, the
    /// camera only while it counts.
    public var detectionProblems: MonitorProblems {
        var problems = monitorProblems.intersection(.mic)
        if monitorProblems.contains(.camera), settings.detection.cameraCounts { problems.insert(.camera) }
        return problems
    }

    /// Detection is paused at `now`.
    public func isPaused(at now: Date) -> Bool {
        settings.pause.isPaused(at: now)
    }

    /// A bar is set up (paired, or an address typed).
    public var isSetUp: Bool {
        settings.bar != nil || settings.manualAddress != nil
    }
}
