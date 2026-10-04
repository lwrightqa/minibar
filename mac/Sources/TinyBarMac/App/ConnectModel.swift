#if os(macOS)
import AppKit
import Observation
import TinyBarCore

/// The Connect window's state (mac-app-ux.md 5.2 to 5.5): which page shows,
/// the Wi-Fi pairing flow, the login checkbox on first launch, and what the
/// window's buttons and close button do. `ConnectView` draws it.
@MainActor
@Observable
final class ConnectModel {
    enum Page: Hashable {
        case usb
        case wifi
    }

    let engine: TinyBarEngine
    /// First launch: "Welcome to TinyBar", the intro, the login checkbox.
    let welcome: Bool

    private(set) var page: Page = .usb
    /// The Wi-Fi page's pairing flow, while that page shows.
    private(set) var flow: WiFiPairingFlow?

    /// Start TinyBar when you log in (first launch only). Applied when the
    /// window closes, by either button.
    var startAtLogin = true
    /// Why macOS refused the login item, shown under the checkbox (6.2). The
    /// window then stays open until Done or Not Now is pressed again.
    private(set) var loginProblem: LoginItemStatus?

    /// No TinyBar on USB after 20 seconds: show the cable hint.
    private(set) var stillLooking = false

    /// Wi-Fi page: the Enter Address… field.
    var showingAddressField = false
    var addressText = ""
    /// Wi-Fi page: the digits typed so far (at most 6).
    private(set) var codeDigits = ""
    /// The last search found more than one bar (shows "Choose another TinyBar").
    private(set) var foundSeveralBars = false

    private let closeWindow: @MainActor () -> Void
    @ObservationIgnored private var loginApplied = false
    /// Pair Again…: choose the paired bar by itself when several are found.
    @ObservationIgnored private var choosePairedBar = false
    /// The Local Network hint on the USB page, decided once per window so it
    /// doesn't flip to the short form the moment it's been shown.
    @ObservationIgnored private var localNetworkHintDecision: Bool?

    init(engine: TinyBarEngine, welcome: Bool, close: @escaping @MainActor () -> Void) {
        self.engine = engine
        self.welcome = welcome
        self.closeWindow = close
    }

    // MARK: - The window

    /// Done or Not Now.
    func finish() {
        if applyLoginChoice() {
            closeWindow()
        }
    }

    /// The close button, ⌘W or Esc: Back on the Wi-Fi page (unless pairing is
    /// done), else Not Now. Returns whether the window may close.
    func closeRequested() -> Bool {
        if page == .wifi && !isPairingDone {
            back()
            return false
        }
        return applyLoginChoice()
    }

    /// The window closed, however it happened.
    func windowClosed() {
        flow?.cancel()
        flow = nil
        if welcome {
            engine.markWelcomeShown()
        }
    }

    /// Applies the login checkbox the first time the window is closed. Returns
    /// `false` (keep the window open) if macOS refused it.
    private func applyLoginChoice() -> Bool {
        guard welcome, !loginApplied else { return true }
        loginApplied = true
        engine.setLaunchAtLogin(startAtLogin)
        guard startAtLogin else { return true }
        switch engine.state.loginItem {
        case .requiresApproval, .notFound, .failed:
            loginProblem = engine.state.loginItem
            return false
        case .enabled, .notRegistered:
            return true
        }
    }

    // MARK: - USB page

    var isConnected: Bool {
        engine.state.connection.phase == .connected
    }

    var barName: String {
        engine.state.connection.bar?.name ?? engine.state.connection.info?.name ?? "TinyBar"
    }

    /// Waits 20 seconds, then shows the cable hint (if still not connected).
    func waitForStillLooking() async {
        try? await Task.sleep(for: .seconds(20))
        if !Task.isCancelled && !isConnected {
            stillLooking = true
        }
    }

    /// The line under "Connected to TinyBar 2A1C over USB." (5.2), if any.
    var usbDetailLine: String? {
        let state = engine.state
        let connection = state.connection
        guard connection.link == .usb else { return nil }
        if connection.tokenLimitReached {
            return "It works over USB. To use Wi-Fi too, remove a device on TinyBar’s Remote; it can keep 10."
        }
        guard state.settings.useWiFi, let wifi = connection.info?.wifi else { return nil }
        if wifi == .connected, connection.wifiReachableHere == false {
            // Checked once after plugging in (mac-app-ux.md 4.3): client
            // isolation or a VPN keeps this Mac from the bar's address.
            return "This Mac can’t reach TinyBar over this Wi-Fi network, so it works only while plugged in."
        }
        if wifi == .connected {
            let explain = localNetworkHintDecision
                ?? (AppInfo.hasLocalNetworkPrivacy && !state.settings.didExplainLocalNetwork)
            return explain
                ? "When it isn’t plugged in, it uses Wi-Fi. If your Mac asks whether TinyBar can find devices on your local network, choose Allow."
                : "When it isn’t plugged in, it uses Wi-Fi."
        }
        if wifi == .offline || wifi == .setup {
            return "TinyBar isn’t on Wi-Fi, so it works only while plugged in."
        }
        return nil
    }

    /// The detail line appeared: if it explained the Local Network prompt,
    /// that counts as explained (mac-app-ux.md 5.2, 8).
    func detailLineShown() {
        guard localNetworkHintDecision == nil, engine.state.connection.info?.wifi == .connected else { return }
        let explain = AppInfo.hasLocalNetworkPrivacy && !engine.state.settings.didExplainLocalNetwork
        localNetworkHintDecision = explain
        if explain {
            engine.markLocalNetworkExplained()
        }
    }

    // MARK: - Wi-Fi page

    /// Pair Over Wi-Fi…, or Pair Again… (`pairAgain`: the paired bar is chosen).
    func showWiFiPage(pairAgain: Bool) {
        flow?.cancel()
        let flow = engine.makeWiFiPairingFlow()
        self.flow = flow
        page = .wifi
        choosePairedBar = pairAgain
        showingAddressField = false
        codeDigits = ""
        foundSeveralBars = false
        if flow.step == .looking {
            flow.startLooking()
        }
    }

    func back() {
        flow?.cancel()
        flow = nil
        page = .usb
    }

    var isPairingDone: Bool {
        switch flow?.step {
        case .paired?, .notNeeded?: return true
        default: return false
        }
    }

    /// Continue, after the Local Network explanation.
    func continueAfterExplanation() {
        engine.markLocalNetworkExplained()
        flow?.startLooking()
    }

    /// Try Again, after nothing was found.
    func lookAgain() {
        showingAddressField = false
        flow?.startLooking()
    }

    /// "Didn't see a code? Choose another TinyBar."
    func chooseAnotherBar() {
        choosePairedBar = false
        codeDigits = ""
        flow?.startLooking()
    }

    func choose(_ bar: DiscoveredBar) {
        flow?.choose(bar)
    }

    /// Show Code on TinyBar / Show a New Code.
    func requestCode() {
        guard let flow else { return }
        codeDigits = ""
        Task { await flow.requestCode() }
    }

    /// What the code field shows: "482 913".
    var codeText: String {
        PairingCode.display(codeDigits)
    }

    /// Typing or pasting in the code field: keeps the digits (ignoring spaces
    /// and dashes), at most 6, and pairs as soon as there are 6.
    func codeTextChanged(_ text: String) {
        let digits = String(text.filter { $0.isASCII && $0.isNumber }.prefix(6))
        guard digits != codeDigits else { return }
        codeDigits = digits
        if digits.count == 6 {
            submitCode()
        }
    }

    /// Pair is enabled: 6 digits, and the code is still on the bar (after a
    /// wrong code with tries left, or once the wait after `rate_limited` or
    /// no reply is over).
    var canSubmitCode: Bool {
        guard codeDigits.count == 6, let flow else { return false }
        return flow.acceptsCode && flow.canRetryNow()
    }

    /// Pair.
    func submitCode() {
        guard canSubmitCode, let flow else { return }
        let digits = codeDigits
        Task { await flow.submit(code: digits) }
    }

    /// Enter Address… › Connect.
    func connectToAddress() {
        let text = addressText.trimmingCharacters(in: .whitespaces)
        guard !text.isEmpty, let flow else { return }
        Task { await flow.useAddress(text) }
    }

    /// Keeps the page in step with the flow.
    func stepChanged(to step: WiFiPairingFlow.Step) {
        switch step {
        case .choose(let bars, let chosen):
            foundSeveralBars = bars.count > 1
            if chosen == nil, choosePairedBar, let paired = engine.state.settings.bar,
               let match = bars.first(where: { $0.deviceID == paired.deviceID }) {
                flow?.choose(match)
            }
        case .failed(.wrongCode, _, _), .failed(.codeUsedUp, _, _), .failed(.expired, _, _):
            // "Field cleared and focused" (wrong code), or a new code needed.
            codeDigits = ""
        case .failed(.noneFound, _, _):
            showingAddressField = false
        case .failed(.nothingAt, _, _), .failed(.notATinyBar, _, _), .failed(.addressNotAllowed, _, _):
            showingAddressField = true
        default:
            break
        }
    }
}
#endif
