#if os(macOS)
import SwiftUI
import TinyBarCore

/// Settings: four tabs, each a grouped `Form` (`.formStyle(.grouped)`), as in
/// mac-app-ux.md 6. Switches apply at once; there's no Save button.
///
/// - General (`gearshape`): Start TinyBar when you log in (with the reasons
///   from `LoginItemStatus` when macOS refuses), Start a call after / End a
///   call after (pop-ups with `DetectionSettings.startDelayPresets` and
///   `endDelayPresets`), Count the camera (dimmed in "Only the call apps"
///   mode), Send the app's name to TinyBar. Restore Defaults.
/// - Apps (`square.grid.2x2`): Count calls from; Call apps table with an
///   editable "Shown on TinyBar as" column (`NameRules.appNameProblem`); Ignored
///   apps; Used the mic since TinyBar opened (`state.seenApps`). Restore
///   Defaults asks first. Add App… uses `NSOpenPanel` in /Applications and
///   reads the bundle ID with `Bundle(url:)`.
/// - Connection (`cable.connector`): `engine.connectionSummary()`, Send Test
///   Call, Forget This TinyBar… (confirmation alert), Use Wi-Fi when TinyBar
///   isn't plugged in, Advanced (Address, Name for this Mac, Pause USB).
/// - Privacy (`hand.raised`): the read-only text of mac-app-ux.md 6.5, plus the
///   Local Network line and button when `state.localNetworkBlocked`.
struct SettingsView: View {
    let engine: TinyBarEngine
    @State var tab: WindowCoordinator.SettingsTab

    var body: some View {
        UnimplementedView(name: "Settings")
    }
}
#endif
