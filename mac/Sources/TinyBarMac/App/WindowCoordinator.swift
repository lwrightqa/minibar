#if os(macOS)
import AppKit
import SwiftUI
import TinyBarCore

/// Opens the Settings and Connect windows: SwiftUI views in `NSWindow`s
/// (`NSHostingController`), so an app with no Dock icon can bring them to the
/// front reliably (`NSApp.activate()` first). That avoids SwiftUI's `Settings`
/// scene, which an AppKit-lifecycle app can't open directly (mac-app-ux.md 11).
@MainActor
final class WindowCoordinator {
    enum SettingsTab: Hashable {
        case general, apps, connection, privacy
    }

    private let engine: TinyBarEngine
    private var settingsWindow: NSWindow?
    private var connectWindow: NSWindow?

    init(engine: TinyBarEngine) {
        self.engine = engine
    }

    /// Settings… (⌘,): 500 pt wide, toolbar tabs, the window titled after the tab.
    func showSettings(tab: SettingsTab = .general) {
        unimplemented()
    }

    /// The Connect window: "Welcome to TinyBar" on first launch, "Connect
    /// TinyBar" otherwise; `wifiPage` opens it on Pair Over Wi-Fi (Pair Again…).
    /// 440 pt wide, fixed size, centered.
    func showConnect(welcome: Bool, wifiPage: Bool = false) {
        unimplemented()
    }
}
#endif
