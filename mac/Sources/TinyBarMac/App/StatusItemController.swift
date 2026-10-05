#if os(macOS)
import AppKit
import Observation
import TinyBarCore

/// The menu-bar icon and its menu (mac-app-ux.md 3 and 4). Draws
/// `engine.menuContent(showDetails:)`; works out nothing itself.
///
/// - The icon: `MenuBarIcon` (SF Symbol template images), the tooltip, and
///   VoiceOver's label and value ("MiniBar, on a call", "On a call · Slack ·
///   12m"). It follows `engine.state` through `withObservationTracking`,
///   re-armed after each change, and is refreshed every 20 seconds so the
///   call's minutes in the tooltip tick over.
/// - The menu is rebuilt in `menuWillOpen(_:)`, with the Option-click details
///   when Option is held (`NSEvent.modifierFlags`), and kept current while
///   open by a one-second timer in the run loop's common modes (which include
///   menu tracking). When only the text changed, the status lines are updated
///   in place, so the highlighted item doesn't jump. When items come or go
///   (a call starts or ends) while a submenu is open under the pointer (Pause
///   Detection ▸, Don’t Count ▸), the rebuild waits until it closes, so
///   AppKit doesn't tear it down and the next click doesn't land elsewhere.
/// - Groups exactly as mac-app-ux.md 4.1 and 4.7: status lines; fix item and
///   counting items (with their separator only when there are any); Pause
///   Detection ▸ (For 1 Hour, For the Rest of Today, Until I Resume) or Resume
///   Detection, and Pause USB / Resume USB with Option; About MiniBar and
///   Settings… (⌘,); Quit MiniBar (⌘Q).
/// - The status lines look like text (4.1): disabled items with attributed
///   titles in the label and secondary label colors, at the menu font. Or, with
///   `defaults write com.minibar.MiniBarMac StatusLineStyle view`, custom views
///   holding a label. *Unverified* which looks right: whether disabled items
///   keep attributed colors, and the custom view's left inset.
@MainActor
final class StatusItemController: NSObject, NSMenuDelegate {
    private let engine: TinyBarEngine
    private let windows: WindowCoordinator
    private let statusItem: NSStatusItem
    private let menu = NSMenu()

    private var shownIcon: IconState?
    private var tickTimer: Timer?
    private var openMenuTimer: Timer?
    private var isMenuOpen = false
    private var showingDetails = false
    /// The content the open menu was built from, and its status-line items in
    /// order (line 1, line 2, details).
    private var builtContent: MenuContent?
    private var statusLineItems: [NSMenuItem] = []

    init(engine: TinyBarEngine, windows: WindowCoordinator) {
        self.engine = engine
        self.windows = windows
        self.statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        super.init()
        menu.delegate = self
        menu.autoenablesItems = false
        statusItem.menu = menu
        statusItem.button?.imagePosition = .imageOnly
        updateIcon()
        observeEngine()
        let timer = Timer(timeInterval: 20, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                self?.updateIcon()
            }
        }
        timer.tolerance = 5
        RunLoop.main.add(timer, forMode: .common)
        tickTimer = timer
    }

    // MARK: - The icon

    private func observeEngine() {
        withObservationTracking {
            _ = engine.state
        } onChange: { [weak self] in
            // Called before the change lands; read it on the next turn.
            Task { @MainActor in
                self?.engineChanged()
            }
        }
    }

    private func engineChanged() {
        updateIcon()
        if isMenuOpen {
            refreshOpenMenu()
        }
        observeEngine()
    }

    private func updateIcon() {
        let content = engine.menuContent(showDetails: false)
        guard let button = statusItem.button else { return }
        if content.icon != shownIcon {
            button.image = MenuBarIcon.image(for: content.icon)
            shownIcon = content.icon
        }
        if button.toolTip != content.tooltip {
            button.toolTip = content.tooltip
        }
        button.setAccessibilityLabel(content.icon.accessibilityLabel)
        button.setAccessibilityValue(content.tooltip)
    }

    // MARK: - NSMenuDelegate

    func menuWillOpen(_ menu: NSMenu) {
        guard menu === self.menu else { return }
        isMenuOpen = true
        showingDetails = NSEvent.modifierFlags.contains(.option)
        rebuild(engine.menuContent(showDetails: showingDetails))
        let timer = Timer(timeInterval: 1, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                self?.refreshOpenMenu()
            }
        }
        RunLoop.main.add(timer, forMode: .common)
        openMenuTimer = timer
    }

    func menuDidClose(_ menu: NSMenu) {
        guard menu === self.menu else { return }
        isMenuOpen = false
        openMenuTimer?.invalidate()
        openMenuTimer = nil
        // The next open rebuilds from fresh state anyway.
        builtContent = nil
    }

    // MARK: - Building the menu

    private func refreshOpenMenu() {
        let content = engine.menuContent(showDetails: showingDetails)
        guard content != builtContent else { return }
        if let builtContent, builtContent.layout == content.layout {
            updateStatusLines(content)
            self.builtContent = content
        } else if menu.highlightedItem?.hasSubmenu == true {
            // A submenu is open: keep the items, update the lines that
            // still match, and rebuild on a later tick once it's closed.
            if let builtContent, builtContent.statusLineCount == content.statusLineCount {
                updateStatusLines(content)
            }
        } else {
            rebuild(content)
        }
    }

    private func updateStatusLines(_ content: MenuContent) {
        for (item, text) in zip(statusLineItems, content.statusLines) {
            StatusLine.update(item, text: text)
        }
    }

    private func rebuild(_ content: MenuContent) {
        menu.removeAllItems()
        statusLineItems = []

        // Status: what the Mac sees, then the bar and the link, then details.
        addStatusLine(content.line1, secondary: false)
        if let line2 = content.line2 {
            addStatusLine(line2, secondary: true)
        }
        for detail in content.details {
            addStatusLine(detail, secondary: true)
        }
        menu.addItem(.separator())

        // Action: at most one fix, then the counting items.
        var actions: [NSMenuItem] = []
        if let fix = content.fixItem {
            actions.append(item(fix.title) { [weak self] in self?.perform(fix) })
        }
        actions += content.countingItems.map { countingItem($0) }
        if !actions.isEmpty {
            for action in actions {
                menu.addItem(action)
            }
            menu.addItem(.separator())
        }

        // Pause.
        switch content.pauseItem {
        case .pauseSubmenu:
            let pause = NSMenuItem(title: "Pause Detection", action: nil, keyEquivalent: "")
            let submenu = NSMenu(title: "Pause Detection")
            submenu.autoenablesItems = false
            submenu.addItem(item("For 1 Hour") { [weak self] in self?.engine.pause(.oneHour) })
            submenu.addItem(item("For the Rest of Today") { [weak self] in self?.engine.pause(.restOfToday) })
            submenu.addItem(item("Until I Resume") { [weak self] in self?.engine.pause(.untilResumed) })
            pause.submenu = submenu
            menu.addItem(pause)
        case .resume:
            menu.addItem(item("Resume Detection") { [weak self] in self?.engine.resume() })
        }
        if content.showsUSBToggle {
            let paused = content.usbPaused
            menu.addItem(item(paused ? "Resume USB" : "Pause USB") { [weak self] in
                self?.engine.setUSBPaused(!paused)
            })
        }
        menu.addItem(.separator())

        // App.
        menu.addItem(item("About MiniBar") { AboutPanel.show() })
        menu.addItem(item("Settings…", key: ",") { [weak self] in self?.windows.showSettings() })
        menu.addItem(.separator())

        // Quit.
        menu.addItem(item("Quit MiniBar", key: "q") { NSApp.terminate(nil) })

        builtContent = content
    }

    private func addStatusLine(_ text: String, secondary: Bool) {
        let line = StatusLine.make(text, secondary: secondary)
        menu.addItem(line)
        statusLineItems.append(line)
    }

    private func countingItem(_ counting: CountingItem) -> NSMenuItem {
        switch counting {
        case .dontCount(let app):
            return item(counting.title) { [weak self] in self?.engine.dontCount(app.identity) }
        case .dontCountSubmenu(let apps):
            let parent = NSMenuItem(title: counting.title, action: nil, keyEquivalent: "")
            let submenu = NSMenu(title: counting.title)
            submenu.autoenablesItems = false
            for app in apps {
                submenu.addItem(item(app.name) { [weak self] in self?.engine.dontCount(app.identity) })
            }
            parent.submenu = submenu
            return parent
        case .countAgain(let app):
            return item(counting.title) { [weak self] in self?.engine.countAgain(app.identity) }
        case .dontCountCamera:
            return item(counting.title) { [weak self] in self?.engine.setCountCamera(false) }
        case .countCameraAgain:
            return item(counting.title) { [weak self] in self?.engine.setCountCamera(true) }
        }
    }

    private func perform(_ fix: FixItem) {
        switch fix {
        case .connect:
            windows.showConnect(welcome: false)
        case .openLocalNetworkSettings:
            SystemSettingsLinks.openLocalNetwork()
        case .pairAgain:
            windows.showConnect(welcome: false, wifiPage: true)
        case .openRemote(let url):
            NSWorkspace.shared.open(url)
        }
    }

    // MARK: - Items that run a closure

    private func item(_ title: String, key: String = "", action: @escaping @MainActor () -> Void) -> NSMenuItem {
        let item = NSMenuItem(title: title, action: #selector(performMenuAction(_:)), keyEquivalent: key)
        item.target = self
        item.representedObject = MenuAction(action)
        return item
    }

    @objc private func performMenuAction(_ sender: NSMenuItem) {
        (sender.representedObject as? MenuAction)?.run()
    }
}

/// A menu item's action, carried in its `representedObject`.
private final class MenuAction: NSObject {
    let run: @MainActor () -> Void

    init(_ run: @escaping @MainActor () -> Void) {
        self.run = run
    }
}

private extension MenuContent {
    /// Line 1, line 2 and the details, in menu order.
    var statusLines: [String] {
        [line1] + (line2.map { [$0] } ?? []) + details
    }

    var statusLineCount: Int { statusLines.count }

    /// Everything but the text that changes with time, so the open menu can
    /// update its lines in place when nothing else changed.
    var layout: MenuContent {
        var copy = self
        copy.icon = .connected
        copy.tooltip = ""
        copy.line1 = ""
        copy.line2 = line2 == nil ? nil : ""
        copy.details = details.map { _ in "" }
        return copy
    }
}

/// The two status lines and the Option-click details: text, not commands
/// (mac-app-ux.md 4.1).
@MainActor
private enum StatusLine {
    /// `defaults write com.minibar.MiniBarMac StatusLineStyle view` to try
    /// the custom-view version on a real Mac.
    static var usesCustomView: Bool {
        UserDefaults.standard.string(forKey: "StatusLineStyle") == "view"
    }

    static func make(_ text: String, secondary: Bool) -> NSMenuItem {
        let item = NSMenuItem(title: text, action: nil, keyEquivalent: "")
        item.isEnabled = false
        item.tag = secondary ? 1 : 0
        if usesCustomView {
            item.view = StatusLineView(text: text, secondary: secondary)
        } else {
            item.attributedTitle = attributed(text, secondary: secondary)
        }
        return item
    }

    static func update(_ item: NSMenuItem, text: String) {
        guard item.title != text else { return }
        item.title = text
        if let view = item.view as? StatusLineView {
            view.text = text
        } else {
            item.attributedTitle = attributed(text, secondary: item.tag == 1)
        }
    }

    static func attributed(_ text: String, secondary: Bool) -> NSAttributedString {
        NSAttributedString(string: text, attributes: [
            .font: NSFont.menuFont(ofSize: 0),
            .foregroundColor: secondary ? NSColor.secondaryLabelColor : NSColor.labelColor,
        ])
    }
}

/// A status line drawn as a plain label, for `StatusLineStyle view`.
private final class StatusLineView: NSView {
    /// Where standard items' titles start. *Unverified:* tune on a real Mac.
    static let leadingInset: CGFloat = 21
    static let trailingInset: CGFloat = 14
    static let height: CGFloat = 22

    private let label: NSTextField

    var text: String {
        get { label.stringValue }
        set {
            label.stringValue = newValue
            fitWidth()
        }
    }

    init(text: String, secondary: Bool) {
        label = NSTextField(labelWithString: text)
        label.font = NSFont.menuFont(ofSize: 0)
        label.textColor = secondary ? .secondaryLabelColor : .labelColor
        label.lineBreakMode = .byTruncatingTail
        label.translatesAutoresizingMaskIntoConstraints = false
        super.init(frame: .zero)
        addSubview(label)
        NSLayoutConstraint.activate([
            label.leadingAnchor.constraint(equalTo: leadingAnchor, constant: Self.leadingInset),
            label.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -Self.trailingInset),
            label.centerYAnchor.constraint(equalTo: centerYAnchor),
        ])
        autoresizingMask = [.width]
        fitWidth()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) isn't used")
    }

    /// The width the menu needs for this line (it uses the widest item).
    private func fitWidth() {
        let width = ceil(label.intrinsicContentSize.width) + Self.leadingInset + Self.trailingInset
        setFrameSize(NSSize(width: max(width, frame.width), height: Self.height))
    }
}
#endif
