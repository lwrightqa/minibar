#if os(macOS)
import AppKit
import TinyBarCore

/// The menu-bar icon and its menu (mac-app-ux.md 3 and 4). Draws
/// `engine.menuContent(showDetails:)`; works out nothing itself.
///
/// - `NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)`.
/// - Icon: `NSImage(systemSymbolName: content.icon.symbolName,
///   accessibilityDescription: content.icon.accessibilityLabel)` with
///   `NSImage.SymbolConfiguration(pointSize: 14, weight: .regular)` (size to
///   tune on a Mac), template image. `button.toolTip = content.tooltip`; the
///   VoiceOver value is the tooltip too (`setAccessibilityValue`).
/// - Rebuilds the menu in `menuWillOpen(_:)` (reading
///   `NSEvent.modifierFlags.contains(.option)` for the details), and keeps it
///   current while open with a timer added to the run loop in `.eventTracking`
///   mode (the call's minutes tick over). *Unverified:* which looks right for
///   the two status lines, disabled items with attributed titles or a custom
///   `view` (4.1).
/// - Updates the icon whenever `engine.state` changes, with
///   `withObservationTracking` re-armed after each change.
/// - Menu groups and items exactly as mac-app-ux.md 4.1 and 4.7: status lines;
///   fix item and counting items; Pause Detection ▸ (For 1 Hour, For the Rest
///   of Today, Until I Resume) or Resume Detection; About TinyBar, Settings…
///   (⌘,); Quit TinyBar (⌘Q). Option adds the detail lines and Pause USB.
/// - About: `NSApp.orderFrontStandardAboutPanel(options:)` with the credits
///   line from mac-app-ux.md 8, after `NSApp.activate()`.
/// - Open Local Network Settings…: `x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork`
///   (*unverified*, undocumented; fall back to Privacy & Security).
@MainActor
final class StatusItemController: NSObject, NSMenuDelegate {
    private let engine: TinyBarEngine
    private let windows: WindowCoordinator
    private let statusItem: NSStatusItem

    init(engine: TinyBarEngine, windows: WindowCoordinator) {
        self.engine = engine
        self.windows = windows
        self.statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        super.init()
        unimplemented()
    }

    func menuWillOpen(_ menu: NSMenu) {
        unimplemented()
    }

    func menuDidClose(_ menu: NSMenu) {
        unimplemented()
    }
}
#endif
