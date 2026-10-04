#if os(macOS)
import SwiftUI
import TinyBarCore

/// The Connect window (mac-app-ux.md 5.2 to 5.5): the USB box with its live
/// status line, "Pair Over Wi-Fi…", the login checkbox on first launch, and
/// the Wi-Fi page driven by a `WiFiPairingFlow` (`engine.makeWiFiPairingFlow()`).
///
/// - The code field: 6 digits, monospaced digits at title 2 size, placeholder
///   "000 000", shown grouped by `PairingCode.display`, submits on the sixth
///   digit. VoiceOver label "Pairing code, 6 digits".
/// - Messages come from `PairingProblem.message(barName:)`, in red with
///   `exclamationmark.circle.fill`, never color alone.
/// - Esc, ⌘W and the close button act like Not Now (or Back on the Wi-Fi page).
struct ConnectView: View {
    let engine: TinyBarEngine
    let welcome: Bool
    @State var showingWiFiPage: Bool
    let close: () -> Void

    var body: some View {
        UnimplementedView(name: "Connect window")
    }
}
#endif
