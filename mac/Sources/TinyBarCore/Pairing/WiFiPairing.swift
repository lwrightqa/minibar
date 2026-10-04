import Foundation
import Observation

/// The 6-digit code the bar shows (api.md 4.2).
public enum PairingCode {
    /// The digits from what the user typed or pasted, ignoring spaces and
    /// dashes ("482 913", "482-913"). `nil` unless that leaves exactly 6 digits.
    public static func normalize(_ input: String) -> String? {
        unimplemented()
    }

    /// "482913" → "482 913", as the field shows it. Partial input is grouped
    /// the same way ("4829" → "482 9").
    public static func display(_ digits: String) -> String {
        unimplemented()
    }
}

/// Why pairing over Wi-Fi didn't work, with the Connect window's messages
/// (mac-app-ux.md 5.5).
public enum PairingProblem: Hashable, Sendable {
    /// No TinyBar found after 10 seconds.
    case noneFound
    case localNetworkBlocked
    /// Nothing answered at a typed address.
    case nothingAt(String)
    /// A typed address answered, but not as a TinyBar.
    case notATinyBar
    /// `wrong_code` with tries left.
    case wrongCode(attemptsLeft: Int)
    /// `wrong_code` with none left: the bar canceled pairing.
    case codeUsedUp
    /// `not_pairing`: expired, canceled on the bar, or already used.
    case expired
    /// `pairing_busy`: someone else is pairing.
    case busy(retryAfter: Int)
    /// `rate_limited`: too many failed pairings.
    case rateLimited(retryAfter: Int)
    /// `token_limit`: 10 paired devices already.
    case tokenLimit
    /// `in_setup`: the bar is setting up Wi-Fi.
    case inSetup
    /// No reply in time.
    case noAnswer

    /// The message under the field or button (mac-app-ux.md 5.5), with the
    /// bar's name filled in.
    public func message(barName: String) -> String {
        unimplemented()
    }
}

/// Pair Over Wi-Fi, step by step (mac-app-ux.md 5.4; api.md 4.6, 4.7). The
/// Connect window shows `step` and calls the methods; the flow does the
/// network work and, on success, stores the token and hands the bar to the
/// connection.
@MainActor
@Observable
public final class WiFiPairingFlow {
    public enum Step: Hashable, Sendable {
        /// macOS 15 and later, first time: explain the Local Network prompt.
        case explainLocalNetwork
        /// Browsing for bars.
        case looking
        /// Bars found; choose one (one bar: it's chosen already).
        case choose([DiscoveredBar], chosen: DiscoveredBar?)
        /// Asking the chosen bar to show a code.
        case requestingCode(DiscoveredBar)
        /// The bar shows a code until `expiresAt`; type it.
        case enterCode(DiscoveredBar, pairingID: String, expiresAt: Date)
        /// Sending the code.
        case pairing(DiscoveredBar)
        /// Done.
        case paired(KnownBar)
        /// The bar has `auth: none`: nothing to pair (mac-app-ux.md 5.5, last row).
        case notNeeded(KnownBar)
        /// Something went wrong; `retryAt` is when the button works again
        /// (`busy`, `rateLimited`).
        case failed(PairingProblem, bar: DiscoveredBar?, retryAt: Date?)
    }

    public private(set) var step: Step

    /// - Parameters:
    ///   - needsLocalNetworkExplanation: start at `.explainLocalNetwork`
    ///     (macOS 15 or later, and not explained before).
    public init(
        clientID: String,
        macName: String?,
        clock: any TinyClock,
        discovery: any BarDiscovery,
        transports: any TransportFactory,
        tokens: any TokenStore,
        needsLocalNetworkExplanation: Bool,
        onPaired: @escaping @MainActor (KnownBar) -> Void
    ) {
        self.step = needsLocalNetworkExplanation ? .explainLocalNetwork : .looking
        self.clientID = clientID
        self.macName = macName
        self.clock = clock
        self.discovery = discovery
        self.transports = transports
        self.tokens = tokens
        self.onPaired = onPaired
    }

    private let clientID: String
    private let macName: String?
    private let clock: any TinyClock
    private let discovery: any BarDiscovery
    private let transports: any TransportFactory
    private let tokens: any TokenStore
    private let onPaired: @MainActor (KnownBar) -> Void

    /// Continue after the explanation: starts browsing (which brings up the
    /// macOS prompt). Gives up with `.noneFound` after 10 seconds.
    public func startLooking() {
        unimplemented()
    }

    /// Choose among several bars.
    public func choose(_ bar: DiscoveredBar) {
        unimplemented()
    }

    /// Enter Address…: checks the address with `GET /api/v1/info`.
    public func useAddress(_ text: String) async {
        unimplemented()
    }

    /// Show Code on TinyBar / Show a New Code: `POST /api/v1/pair/start` with
    /// kind `mac`, scope `call`, this install's `client`, and the Mac's name if set.
    public func requestCode() async {
        unimplemented()
    }

    /// Pair: `POST /api/v1/pair` with the digits. Called by the Pair button and
    /// as soon as the sixth digit is typed or pasted.
    public func submit(code: String) async {
        unimplemented()
    }

    /// Back, or the window closed: stops browsing. A code on the bar simply
    /// expires (there's no cancel endpoint).
    public func cancel() {
        unimplemented()
    }
}
