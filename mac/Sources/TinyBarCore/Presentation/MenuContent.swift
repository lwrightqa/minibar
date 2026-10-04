import Foundation

/// The menu-bar icon's five states (mac-app-ux.md 3.1). When several apply,
/// the order of the cases is the order of precedence (3.2).
public enum IconState: String, Hashable, Sendable, CaseIterable {
    case needsYou
    case paused
    case notConnected
    case onCall
    case connected

    /// The SF Symbol (all exist on macOS 11 and later; mac-app-ux.md 3.3).
    public var symbolName: String {
        switch self {
        case .connected: return "rectangle"
        case .onCall: return "rectangle.fill"
        case .notConnected: return "rectangle.slash"
        case .paused: return "pause.rectangle"
        case .needsYou: return "exclamationmark.triangle"
        }
    }

    /// The VoiceOver label (mac-app-ux.md 3.1).
    public var accessibilityLabel: String {
        switch self {
        case .connected: return "TinyBar, connected"
        case .onCall: return "TinyBar, on a call"
        case .notConnected: return "TinyBar, not connected"
        case .paused: return "TinyBar, paused"
        case .needsYou: return "TinyBar, needs attention"
        }
    }
}

/// The one fix item under the status lines (mac-app-ux.md 4.4).
public enum FixItem: Hashable, Sendable {
    /// Opens the Connect window.
    case connect
    /// Opens System Settings › Privacy & Security › Local Network.
    case openLocalNetworkSettings
    /// Opens the Connect window on its Wi-Fi page, this bar chosen.
    case pairAgain
    /// Opens the bar's Remote page in the browser.
    case openRemote(URL)

    public var title: String {
        switch self {
        case .connect: return "Connect…"
        case .openLocalNetworkSettings: return "Open Local Network Settings…"
        case .pairAgain: return "Pair Again…"
        case .openRemote: return "Open TinyBar Remote…"
        }
    }
}

/// The counting items (mac-app-ux.md 4.5).
public enum CountingItem: Hashable, Sendable {
    /// "Don't Count Slack".
    case dontCount(AppIdentity)
    /// "Don't Count ▸" with one item per app, when two or more counted apps use the mic.
    case dontCountSubmenu([AppIdentity])
    /// "Count Slack Again" (undo), while the app is still using the mic.
    case countAgain(AppIdentity)
    /// "Don't Count the Camera", during a call from the camera alone.
    case dontCountCamera
    /// "Count the Camera Again" (undo), while the camera is still in use.
    case countCameraAgain

    public var title: String {
        switch self {
        case .dontCount(let app): return "Don't Count \(app.displayName)"
        case .dontCountSubmenu: return "Don't Count"
        case .countAgain(let app): return "Count \(app.displayName) Again"
        case .dontCountCamera: return "Don't Count the Camera"
        case .countCameraAgain: return "Count the Camera Again"
        }
    }
}

/// Pause Detection ▸ or Resume Detection (mac-app-ux.md 4.6).
public enum PauseItem: Hashable, Sendable {
    case pauseSubmenu
    case resume
}

/// Everything the status item and its menu show, worked out from the engine's
/// state by `MenuPresenter`. The macOS side only draws it.
public struct MenuContent: Hashable, Sendable {
    public var icon: IconState
    /// The tooltip, and the status item's VoiceOver value (mac-app-ux.md 3.1, 9).
    public var tooltip: String
    /// What the Mac sees (4.2).
    public var line1: String
    /// The bar and the link (4.3). `nil` when not set up.
    public var line2: String?
    public var fixItem: FixItem?
    public var countingItems: [CountingItem]
    public var pauseItem: PauseItem
    /// Option-click detail lines (4.8): address or port, firmware and API, last reply.
    public var details: [String]
    /// Option-click: show Pause USB / Resume USB.
    public var showsUSBToggle: Bool
    public var usbPaused: Bool

    public init(
        icon: IconState,
        tooltip: String,
        line1: String,
        line2: String?,
        fixItem: FixItem? = nil,
        countingItems: [CountingItem] = [],
        pauseItem: PauseItem = .pauseSubmenu,
        details: [String] = [],
        showsUSBToggle: Bool = false,
        usbPaused: Bool = false
    ) {
        self.icon = icon
        self.tooltip = tooltip
        self.line1 = line1
        self.line2 = line2
        self.fixItem = fixItem
        self.countingItems = countingItems
        self.pauseItem = pauseItem
        self.details = details
        self.showsUSBToggle = showsUSBToggle
        self.usbPaused = usbPaused
    }
}
