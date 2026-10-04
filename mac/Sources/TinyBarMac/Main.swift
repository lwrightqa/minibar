// The app's entry point.
//
// Every file in this target is wrapped in `#if os(macOS)`. On Linux the target
// compiles to the stub below, so `swift build` works there, and the macOS
// code is still checked for syntax (Swift parses inactive `#if` blocks), but
// not for types. It has to be built on a Mac to be checked properly.

#if os(macOS)
import AppKit

/// An AppKit app whose only UI is a status item and two windows (Settings and
/// Connect). No Dock icon: `LSUIElement` in Info.plist, and the `.accessory`
/// policy here so `swift run` behaves the same.
///
/// AppKit's `NSStatusItem` and `NSMenu`, not SwiftUI's `MenuBarExtra`: the menu
/// needs Option-click details and text-like status lines (mac-app-ux.md 4).
/// The windows are SwiftUI views in `NSWindow`s.
@main
@MainActor
enum TinyBarMain {
    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        app.setActivationPolicy(.accessory)
        withExtendedLifetime(delegate) {
            app.run()
        }
    }
}
#else
@main
enum TinyBarMain {
    static func main() {
        print("TinyBar for Mac runs only on macOS. Here, build and test the core with `swift test`.")
    }
}
#endif
