#if os(macOS)
import AppKit
import SwiftUI
import TinyBarCore

/// The Connect window (mac-app-ux.md 5.2 to 5.5), drawn from a `ConnectModel`.
///
/// - USB page: the app icon, heading and intro on first launch; the USB box
///   with its live status line; "Not plugged into this Mac? Pair Over Wi-Fi…";
///   the login checkbox on first launch; the menu-bar hint once connected;
///   Not Now (Esc) and Done (Return, enabled once a bar is connected).
/// - Wi-Fi page, driven by `WiFiPairingFlow.step`: the Local Network
///   explanation, looking, choosing a bar, Show Code on TinyBar, the code
///   field, pairing, paired; Back (Esc) at the bottom left.
/// - The code field: 6 digits, monospaced digits at title 2 size, placeholder
///   "000 000", shown grouped by `PairingCode.display`, pairs on the sixth
///   digit. VoiceOver label "Pairing code, 6 digits".
/// - Messages come from `PairingProblem.message(barName:)`, in red with
///   `exclamationmark.circle.fill`, never color alone.
struct ConnectView: View {
    @Bindable var model: ConnectModel

    var body: some View {
        Group {
            switch model.page {
            case .usb:
                USBPage(model: model)
            case .wifi:
                if let flow = model.flow {
                    WiFiPage(model: model, flow: flow)
                }
            }
        }
        .padding(20)
        .fixedSize(horizontal: false, vertical: true)
    }
}

// MARK: - USB page

private struct USBPage: View {
    @Bindable var model: ConnectModel

    var body: some View {
        VStack(spacing: 16) {
            if model.welcome {
                Image(nsImage: NSApp.applicationIconImage)
                    .resizable()
                    .frame(width: 64, height: 64)
                    .accessibilityHidden(true)
            }
            Text(model.welcome ? "Welcome to TinyBar" : "Connect your TinyBar")
                .font(.title2)
                .fontWeight(.semibold)
                .multilineTextAlignment(.center)
            if model.welcome {
                Text("TinyBar shows On a call on your bar whenever this Mac’s mic or camera is in use, in Slack, Zoom, Google Meet or any other app. It never listens or records.")
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
            }

            GroupBox {
                HStack(alignment: .top, spacing: 12) {
                    Image(systemName: "cable.connector")
                        .font(.system(size: 28))
                        .foregroundStyle(.secondary)
                        .accessibilityHidden(true)
                    VStack(alignment: .leading, spacing: 6) {
                        Text("Plug TinyBar into this Mac with a USB cable.")
                            .fixedSize(horizontal: false, vertical: true)
                        USBStatusLine(model: model)
                    }
                    Spacer(minLength: 0)
                }
                .padding(8)
            }

            HStack(spacing: 4) {
                Text("Not plugged into this Mac?")
                    .foregroundStyle(.secondary)
                Button("Pair Over Wi-Fi…") {
                    model.showWiFiPage(pairAgain: false)
                }
                .buttonStyle(.link)
                Spacer(minLength: 0)
            }

            if model.welcome {
                VStack(alignment: .leading, spacing: 4) {
                    Toggle("Start TinyBar when you log in", isOn: $model.startAtLogin)
                        .toggleStyle(.checkbox)
                    Group {
                        if let problem = model.loginProblem, let help = LoginItemHelp(status: problem) {
                            help
                        } else {
                            Text("macOS will show a notice that it was added.")
                                .foregroundStyle(.secondary)
                        }
                    }
                    .font(.callout)
                    .padding(.leading, 20)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            if model.isConnected {
                HStack(spacing: 6) {
                    Text("TinyBar is in your menu bar:")
                    Image(systemName: IconState.connected.symbolName)
                        .accessibilityLabel(IconState.connected.accessibilityLabel)
                }
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            HStack {
                Spacer()
                Button("Not Now") {
                    model.finish()
                }
                .keyboardShortcut(.cancelAction)
                Button("Done") {
                    model.finish()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(!model.isConnected)
            }
        }
    }
}

/// The USB box's live status line (5.2).
private struct USBStatusLine: View {
    let model: ConnectModel

    var body: some View {
        let connection = model.engine.state.connection
        if model.isConnected, let link = connection.link {
            VStack(alignment: .leading, spacing: 4) {
                Label {
                    Text(verbatim: "Connected to \(model.barName) over \(link == .usb ? "USB" : "Wi-Fi").")
                } icon: {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(.green)
                        .accessibilityHidden(true)
                }
                if let detail = model.usbDetailLine {
                    Text(detail)
                        .font(.callout)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                        .onAppear { model.detailLineShown() }
                }
            }
        } else {
            VStack(alignment: .leading, spacing: 4) {
                HStack(spacing: 6) {
                    ProgressView()
                        .controlSize(.small)
                    Text("Looking for TinyBar…")
                }
                if model.stillLooking {
                    Text("Still looking? Some USB cables only charge. Try another cable, or pair over Wi-Fi.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .task {
                await model.waitForStillLooking()
            }
        }
    }
}

// MARK: - Wi-Fi page

private struct WiFiPage: View {
    @Bindable var model: ConnectModel
    let flow: WiFiPairingFlow

    @FocusState private var codeFocused: Bool
    @FocusState private var addressFocused: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Pair over Wi-Fi")
                .font(.title2)
                .fontWeight(.semibold)
            content
            if showsAddressField {
                addressField
            }
            HStack {
                Button("Back") {
                    model.back()
                }
                .keyboardShortcut(.cancelAction)
                Spacer()
                primaryButton
            }
            .padding(.top, 6)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .onChange(of: flow.step, initial: true) { _, step in
            model.stepChanged(to: step)
            switch step {
            case .enterCode, .failed(.wrongCode, _, _), .failed(.rateLimited, _, _), .failed(.noAnswer, _, _):
                codeFocused = true
            default:
                break
            }
        }
        .onChange(of: model.showingAddressField) { _, showing in
            if showing {
                addressFocused = true
            }
        }
    }

    // MARK: Steps

    @ViewBuilder private var content: some View {
        switch flow.step {
        case .explainLocalNetwork:
            Text("Your Mac will ask whether TinyBar can find devices on your local network. Choose Allow, so this Mac can reach your bar over Wi-Fi.")
                .fixedSize(horizontal: false, vertical: true)

        case .looking:
            busyLine("Looking for TinyBar on this network…")

        case .choose(let bars, let chosen):
            barChooser(bars: bars, chosen: chosen)
            showCodeButton(enabled: chosen != nil, isDefault: true)

        case .requestingCode(let bar):
            barName(bar)
            showCodeButton(enabled: false, isDefault: false)

        case .enterCode(let bar, _, _):
            barName(bar)
            codeEntry(bar: bar, message: nil, busy: false)

        case .pairing(let bar):
            barName(bar)
            codeEntry(bar: bar, message: nil, busy: true)

        case .paired(let bar):
            successLine("Paired with \(bar.name).")
            Text("It shows On a call whenever this Mac is on one.")
                .foregroundStyle(.secondary)

        case .notNeeded(let bar):
            successLine("\(bar.name) doesn’t need pairing, so you’re all set.")

        case .failed(let problem, let bar, _):
            failure(problem, bar: bar)
        }
    }

    @ViewBuilder
    private func failure(_ problem: PairingProblem, bar: DiscoveredBar?) -> some View {
        let message = problem.message(barName: bar?.name ?? "TinyBar")
        switch problem {
        case .wrongCode:
            if let bar {
                barName(bar)
                codeEntry(bar: bar, message: message, busy: false)
            } else {
                ErrorLine(message: message)
            }

        case .noneFound:
            ErrorLine(message: message)
            HStack {
                Button("Try Again") { model.lookAgain() }
                if !model.showingAddressField {
                    Button("Enter Address…") { model.showingAddressField = true }
                }
            }

        case .localNetworkBlocked:
            ErrorLine(message: message)
            Button("Open Local Network Settings…") { SystemSettingsLinks.openLocalNetwork() }

        case .nothingAt, .notATinyBar, .addressNotAllowed:
            // The address field below stays, with what was typed.
            ErrorLine(message: message)

        case .codeUsedUp, .expired:
            if let bar { barName(bar) }
            ErrorLine(message: message)
            Button("Show a New Code") { model.requestCode() }

        case .tokenLimit:
            // From asking for a code, or from sending it (a safeguard that
            // ends the pairing, api.md 4.7). Once a device is removed on the
            // Remote, Show Code on TinyBar tries again.
            if let bar { barName(bar) }
            ErrorLine(message: message)
            HStack {
                if let url = bar?.endpoint.remotePageURL {
                    Button("Open TinyBar Remote…") { _ = NSWorkspace.shared.open(url) }
                }
                if bar != nil {
                    showCodeButton(enabled: true, isDefault: false)
                }
            }

        case .rateLimited, .noAnswer:
            if let bar, flow.acceptsCode {
                // The code is still on the bar: send it again (Pair) once
                // the wait is over.
                barName(bar)
                codeEntry(bar: bar, message: message, busy: false)
            } else {
                if let bar { barName(bar) }
                ErrorLine(message: message)
                if bar != nil { retryableShowCodeButton }
            }

        case .busy, .inSetup:
            if let bar { barName(bar) }
            ErrorLine(message: message)
            if bar != nil { retryableShowCodeButton }
        }
    }

    // MARK: Pieces

    /// Show Code on TinyBar, enabled again when the wait after `pairing_busy`
    /// or `rate_limited` is over (checked each second on the flow's clock).
    private var retryableShowCodeButton: some View {
        TimelineView(.periodic(from: .now, by: 1)) { _ in
            showCodeButton(enabled: flow.canRetryNow(), isDefault: true)
        }
    }

    /// One bar: "Found TinyBar 2A1C." Several: a pop-up and the help line.
    @ViewBuilder
    private func barChooser(bars: [DiscoveredBar], chosen: DiscoveredBar?) -> some View {
        if bars.count == 1, let bar = bars.first {
            Text(verbatim: "Found \(bar.name).")
        } else {
            VStack(alignment: .leading, spacing: 4) {
                Picker("TinyBar:", selection: mainActorBinding(
                    get: { chosen?.name },
                    set: { (name: String?) in
                        if let bar = bars.first(where: { $0.name == name }) {
                            model.choose(bar)
                        }
                    }
                )) {
                    ForEach(bars, id: \.name) { bar in
                        Text(verbatim: bar.name).tag(Optional(bar.name))
                    }
                }
                .fixedSize()
                Text("Hold a TinyBar’s screen and tap Wi-Fi to see its name.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private func barName(_ bar: DiscoveredBar) -> some View {
        LabeledContent("TinyBar:") {
            Text(verbatim: bar.name)
        }
        .fixedSize()
    }

    private func showCodeButton(enabled: Bool, isDefault: Bool) -> some View {
        Button("Show Code on TinyBar") {
            model.requestCode()
        }
        .disabled(!enabled)
        .keyboardShortcut(isDefault && enabled ? .defaultAction : nil)
    }

    private func codeEntry(bar: DiscoveredBar, message: String?, busy: Bool) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(verbatim: "Type the code shown on \(bar.name):")
            TextField("000 000", text: mainActorBinding(
                get: { model.codeText },
                set: { model.codeTextChanged($0) }
            ))
            .font(.title2.monospacedDigit())
            .frame(width: 150)
            .focused($codeFocused)
            .disabled(busy)
            .onSubmit { model.submitCode() }
            .accessibilityLabel("Pairing code, 6 digits")
            if let message {
                ErrorLine(message: message)
            } else if busy {
                busyLine("Pairing…")
            } else {
                Text("The code works for 2 minutes.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
            HStack(spacing: 12) {
                Button("Show a New Code") { model.requestCode() }
                    .buttonStyle(.link)
                if model.foundSeveralBars {
                    Button("Didn’t see a code? Choose another TinyBar.") { model.chooseAnotherBar() }
                        .buttonStyle(.link)
                }
            }
            .disabled(busy)
        }
    }

    private var showsAddressField: Bool {
        switch flow.step {
        case .failed(.noneFound, _, _), .failed(.nothingAt, _, _), .failed(.notATinyBar, _, _),
             .failed(.addressNotAllowed, _, _):
            return model.showingAddressField
        default:
            return false
        }
    }

    private var addressField: some View {
        HStack {
            TextField("Address", text: $model.addressText, prompt: Text("tinybar.local or 10.0.4.42"))
                .focused($addressFocused)
                .onSubmit { model.connectToAddress() }
            Button("Connect") { model.connectToAddress() }
                .disabled(model.addressText.trimmingCharacters(in: .whitespaces).isEmpty)
        }
    }

    @ViewBuilder private var primaryButton: some View {
        switch flow.step {
        case .explainLocalNetwork:
            Button("Continue") { model.continueAfterExplanation() }
                .keyboardShortcut(.defaultAction)
        case .paired, .notNeeded:
            Button("Done") { model.finish() }
                .keyboardShortcut(.defaultAction)
        default:
            // Re-checked each second, so Pair comes back when a wait is over.
            TimelineView(.periodic(from: .now, by: 1)) { _ in
                let enabled = model.canSubmitCode
                Button("Pair") { model.submitCode() }
                    .keyboardShortcut(enabled ? .defaultAction : nil)
                    .disabled(!enabled)
            }
        }
    }

    private func busyLine(_ text: String) -> some View {
        HStack(spacing: 6) {
            ProgressView()
                .controlSize(.small)
            Text(text)
        }
    }

    private func successLine(_ text: String) -> some View {
        Label {
            Text(text)
        } icon: {
            Image(systemName: "checkmark.circle.fill")
                .foregroundStyle(.green)
                .accessibilityHidden(true)
        }
    }
}

// MARK: - Shared pieces

/// An error message: red text with `exclamationmark.circle.fill`, so it's
/// never color alone (mac-app-ux.md 5.5).
struct ErrorLine: View {
    let message: String

    var body: some View {
        Label {
            Text(message)
                .fixedSize(horizontal: false, vertical: true)
        } icon: {
            Image(systemName: "exclamationmark.circle.fill")
                .accessibilityHidden(true)
        }
        .foregroundStyle(.red)
        .accessibilityLabel(message)
    }
}

/// Why starting at login didn't work, with its button (mac-app-ux.md 6.2).
/// `nil` for the statuses that need no help.
struct LoginItemHelp: View {
    let message: String
    let showsOpenSettings: Bool

    init?(status: LoginItemStatus) {
        switch status {
        case .requiresApproval:
            message = "Allow TinyBar in Login Items to start it when you log in."
            showsOpenSettings = true
        case .notFound:
            message = "Move TinyBar to your Applications folder, then turn this on again."
            showsOpenSettings = false
        case .failed:
            message = "macOS didn’t allow it. Your organization may manage login items."
            showsOpenSettings = false
        case .enabled, .notRegistered:
            return nil
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(message)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            if showsOpenSettings {
                Button("Open Login Items Settings…") {
                    MainAppLoginItem().openSystemSettings()
                }
            }
        }
    }
}
#endif
