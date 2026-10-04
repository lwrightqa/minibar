#if os(macOS)
import AppKit
import SwiftUI
import TinyBarCore

/// Opens the Settings and Connect windows: SwiftUI views in `NSWindow`s
/// (`NSHostingController`), so an app with no Dock icon can bring them to the
/// front reliably (`NSApp.activate()` first). That avoids SwiftUI's `Settings`
/// scene, which an AppKit-lifecycle app can't open directly (mac-app-ux.md 11).
///
/// - Settings: an `NSTabViewController` in toolbar style (the System Settings
///   look of mac-app-ux.md 6.1, `toolbarStyle = .preference`), one hosting
///   controller per tab, 500 pt wide. The window is titled after the tab and
///   resized to the tab's height when the tab changes.
/// - Connect: 440 pt wide, fixed size, centered. The close button and ⌘W act
///   like Not Now, or Back on the Wi-Fi page (`ConnectModel.closeRequested`).
@MainActor
final class WindowCoordinator: NSObject, NSWindowDelegate {
    enum SettingsTab: Int, Hashable, CaseIterable {
        case general, apps, connection, privacy

        var title: String {
            switch self {
            case .general: return "General"
            case .apps: return "Apps"
            case .connection: return "Connection"
            case .privacy: return "Privacy"
            }
        }

        /// mac-app-ux.md 6.1.
        var symbolName: String {
            switch self {
            case .general: return "gearshape"
            case .apps: return "square.grid.2x2"
            case .connection: return "cable.connector"
            case .privacy: return "hand.raised"
            }
        }

        /// The tab's height. The forms scroll if their content is taller
        /// (a long app list). *Unverified:* tune on a real Mac.
        var height: CGFloat {
            switch self {
            case .general: return 470
            case .apps: return 640
            case .connection: return 500
            case .privacy: return 470
            }
        }
    }

    static let settingsWidth: CGFloat = 500
    static let connectWidth: CGFloat = 440

    private let engine: TinyBarEngine
    private var settingsWindow: NSWindow?
    private var settingsTabs: SettingsTabsController?
    private var connectWindow: NSWindow?
    private var connectModel: ConnectModel?

    init(engine: TinyBarEngine) {
        self.engine = engine
    }

    /// Settings… (⌘,): 500 pt wide, toolbar tabs, the window titled after the tab.
    func showSettings(tab: SettingsTab = .general) {
        if settingsWindow == nil {
            let tabs = SettingsTabsController()
            tabs.tabStyle = .toolbar
            for each in SettingsTab.allCases {
                let view = SettingsView(engine: engine, tab: each, windows: self)
                    .frame(width: Self.settingsWidth, height: each.height)
                let hosting = NSHostingController(rootView: view)
                // NSTabViewController copies the selected child's title to
                // the window (canPropagateSelectedChildViewControllerTitle),
                // so each child carries its tab's name.
                hosting.title = each.title
                // The window's size comes from the tab's height, set here and
                // in `SettingsTabsController`, not from SwiftUI's sizing.
                hosting.sizingOptions = []
                hosting.preferredContentSize = NSSize(width: Self.settingsWidth, height: each.height)
                let item = NSTabViewItem(viewController: hosting)
                item.label = each.title
                item.image = NSImage(systemSymbolName: each.symbolName, accessibilityDescription: each.title)
                tabs.addTabViewItem(item)
            }
            let window = NSWindow(contentViewController: tabs)
            window.styleMask = [.titled, .closable, .miniaturizable]
            window.toolbarStyle = .preference
            window.isReleasedWhenClosed = false
            window.delegate = self
            settingsTabs = tabs
            settingsWindow = window
            tabs.select(tab)
            window.center()
        } else {
            settingsTabs?.select(tab)
        }
        if let settingsWindow {
            bringToFront(settingsWindow)
        }
    }

    /// The Connect window: "Welcome to TinyBar" on first launch, "Connect
    /// TinyBar" otherwise; `wifiPage` opens it on Pair Over Wi-Fi (Pair Again…).
    /// 440 pt wide, fixed size, centered.
    func showConnect(welcome: Bool, wifiPage: Bool = false) {
        if let connectWindow, let connectModel {
            if wifiPage {
                connectModel.showWiFiPage(pairAgain: true)
            }
            bringToFront(connectWindow)
            return
        }
        let model = ConnectModel(engine: engine, welcome: welcome) { [weak self] in
            self?.connectWindow?.close()
        }
        if wifiPage {
            model.showWiFiPage(pairAgain: true)
        }
        // The view has a fixed width and its ideal height, and the hosting
        // controller's standard sizing (minimum, intrinsic and maximum size)
        // makes the window follow it when a page changes height. *Unverified.*
        let hosting = NSHostingController(rootView: ConnectView(model: model).frame(width: Self.connectWidth))
        let window = NSWindow(contentViewController: hosting)
        window.styleMask = [.titled, .closable]
        window.title = welcome ? "Welcome to TinyBar" : "Connect TinyBar"
        window.isReleasedWhenClosed = false
        window.delegate = self
        connectModel = model
        connectWindow = window
        window.center()
        bringToFront(window)
    }

    private func bringToFront(_ window: NSWindow) {
        NSApp.activate()
        window.makeKeyAndOrderFront(nil)
        window.orderFrontRegardless()
    }

    // MARK: - NSWindowDelegate

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        if sender === connectWindow, let connectModel {
            return connectModel.closeRequested()
        }
        return true
    }

    func windowWillClose(_ notification: Notification) {
        guard let window = notification.object as? NSWindow, window === connectWindow else { return }
        connectModel?.windowClosed()
        connectModel = nil
        connectWindow = nil
    }
}

/// Settings' tabs, in the window's toolbar.
@MainActor
final class SettingsTabsController: NSTabViewController {
    func select(_ tab: WindowCoordinator.SettingsTab) {
        selectedTabViewItemIndex = tab.rawValue
        titleAndResize()
    }

    override func tabView(_ tabView: NSTabView, didSelect tabViewItem: NSTabViewItem?) {
        super.tabView(tabView, didSelect: tabViewItem)
        titleAndResize()
    }

    /// The window is titled after the tab (already, through the child's
    /// title; this is a safety net), and as tall as the tab, keeping its top
    /// edge where it is.
    private func titleAndResize() {
        guard let window = view.window,
              let tab = WindowCoordinator.SettingsTab(rawValue: selectedTabViewItemIndex) else { return }
        window.title = tab.title
        let content = NSSize(width: WindowCoordinator.settingsWidth, height: tab.height)
        let frame = window.frameRect(forContentRect: NSRect(origin: .zero, size: content))
        var newFrame = window.frame
        newFrame.origin.y += newFrame.height - frame.height
        newFrame.size = frame.size
        window.setFrame(newFrame, display: true, animate: window.isVisible)
    }
}
#endif
