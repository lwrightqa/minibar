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

    public init(
        settings: AppSettings,
        observation: MacObservation = .idle,
        call: DetectedCall? = nil,
        testCall: TestCall? = nil,
        connection: ConnectionState,
        seenApps: [SeenApp] = [],
        undo: UndoState = UndoState(),
        loginItem: LoginItemStatus = .notRegistered,
        localNetworkBlocked: Bool = false
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
