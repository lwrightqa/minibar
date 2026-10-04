import Foundation
import Observation

/// The 6-digit code the bar shows (api.md 4.2).
public enum PairingCode {
    /// The digits from what the user typed or pasted, ignoring spaces and
    /// dashes ("482 913", "482-913"). `nil` unless that leaves exactly 6 digits.
    public static func normalize(_ input: String) -> String? {
        var digits = ""
        for character in input {
            if character.isWhitespace || PairingCode.dashes.contains(character) { continue }
            guard let ascii = character.asciiValue, (0x30...0x39).contains(ascii) else { return nil }
            digits.append(character)
        }
        return digits.count == 6 ? digits : nil
    }

    /// Hyphen, minus and the dashes a Mac types or pastes.
    static let dashes: Set<Character> = ["-", "\u{2010}", "\u{2011}", "\u{2012}", "\u{2013}", "\u{2014}", "\u{2212}"]

    /// "482913" → "482 913", as the field shows it. Partial input is grouped
    /// the same way ("4829" → "482 9").
    public static func display(_ digits: String) -> String {
        let only = String(digits.filter { $0.asciiValue.map { (0x30...0x39).contains($0) } ?? false }.prefix(6))
        guard only.count > 3 else { return only }
        return only.prefix(3) + " " + only.dropFirst(3)
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
    /// A typed DNS name macOS won't reach over plain HTTP (App Transport
    /// Security allows only `.local` names and IP addresses).
    case addressNotAllowed
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
        switch self {
        case .noneFound:
            return "Can\u{2019}t find a TinyBar on this network. Make sure it\u{2019}s on and on the same Wi-Fi as this Mac. Some office networks keep devices apart; if yours does, plug TinyBar into this Mac instead."
        case .localNetworkBlocked:
            return "macOS is blocking TinyBar from your local network, so Wi-Fi can\u{2019}t work."
        case .nothingAt(let address):
            return "Nothing answered at \(address)."
        case .notATinyBar:
            return "That address isn\u{2019}t a TinyBar."
        case .addressNotAllowed:
            return "Use TinyBar\u{2019}s .local name or its IP address."
        case .wrongCode(let left):
            return left == 1 ? "That code didn\u{2019}t match. 1 try left." : "That code didn\u{2019}t match. \(left) tries left."
        case .codeUsedUp:
            return "That code didn\u{2019}t match, so TinyBar canceled pairing. Show a new code to try again."
        case .expired:
            return "That code has expired or was canceled on TinyBar. Show a new code to try again."
        case .busy(let seconds):
            return "Someone else is pairing with this TinyBar. Try again in \(Formatting.wait(seconds: seconds))."
        case .rateLimited(let seconds):
            return "Too many tries. You can try again in \(Formatting.wait(seconds: seconds))."
        case .tokenLimit:
            return "\(barName) already has 10 paired devices. Remove one on its Remote, then try again."
        case .inSetup:
            return "\(barName) is setting up Wi-Fi. Finish setup on the bar, then try again."
        case .noAnswer:
            return "\(barName) didn\u{2019}t answer. Make sure it\u{2019}s on, then try again."
        }
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

    /// The bars found so far (or the one at a typed address).
    @ObservationIgnored private var bars: [DiscoveredBar] = []
    /// The code the chosen bar is showing: its `pairing_id` and when it expires.
    @ObservationIgnored private var pairing: (bar: DiscoveredBar, id: String, expiresAt: Date)?
    @ObservationIgnored private var isBrowsing = false
    @ObservationIgnored private var lookingTimer: Task<Void, Never>?
    /// Ignores results from an earlier round of browsing.
    @ObservationIgnored private var generation = 0

    /// How long to look before saying none was found (mac-app-ux.md 5.5).
    public static let lookingTimeout: TimeInterval = 10

    /// Continue after the explanation: starts browsing (which brings up the
    /// macOS prompt). Gives up with `.noneFound` after 10 seconds.
    public func startLooking() {
        stopBrowsing()
        generation += 1
        let generation = self.generation
        bars = []
        step = .looking
        isBrowsing = true
        discovery.start(
            onChange: { [weak self] found in
                Task { @MainActor in self?.discovered(found, generation: generation) }
            },
            onError: { [weak self] error in
                Task { @MainActor in self?.discoveryFailed(error, generation: generation) }
            }
        )
        let clock = self.clock
        lookingTimer = Task { [weak self] in
            do { try await clock.sleep(seconds: WiFiPairingFlow.lookingTimeout) } catch { return }
            self?.lookingTimedOut(generation: generation)
        }
    }

    private func discovered(_ found: [DiscoveredBar], generation: Int) {
        guard generation == self.generation, isBrowsing else { return }
        // Only TinyBars that say who they are can be paired.
        let tinyBars = found.filter { $0.deviceID.map(Identifiers.isValidDeviceID) ?? false }
        bars = tinyBars
        switch step {
        case .looking, .choose, .failed(.noneFound, _, _):
            if tinyBars.isEmpty {
                if case .choose = step { step = .looking }
                return
            }
            lookingTimer?.cancel()
            var chosen: DiscoveredBar?
            if case .choose(_, let previous?) = step {
                chosen = tinyBars.first { $0.deviceID == previous.deviceID }
            }
            if tinyBars.count == 1 { chosen = tinyBars[0] }
            step = .choose(tinyBars, chosen: chosen)
        default:
            break
        }
    }

    private func discoveryFailed(_ error: DiscoveryError, generation: Int) {
        guard generation == self.generation, error == .localNetworkDenied else { return }
        switch step {
        case .looking, .choose, .failed:
            lookingTimer?.cancel()
            step = .failed(.localNetworkBlocked, bar: nil, retryAt: nil)
        default:
            break
        }
    }

    private func lookingTimedOut(generation: Int) {
        guard generation == self.generation, case .looking = step else { return }
        step = .failed(.noneFound, bar: nil, retryAt: nil)
    }

    /// The bar chosen now, from whatever step the window is on.
    private var chosenBar: DiscoveredBar? {
        switch step {
        case .choose(_, let chosen): return chosen
        case .requestingCode(let bar), .enterCode(let bar, _, _), .pairing(let bar): return bar
        case .failed(_, let bar, _): return bar
        default: return nil
        }
    }

    /// The code on the bar can be sent now or after the wait: typing it,
    /// after a wrong code with tries left, or after `rate_limited` or no reply
    /// to `pair` while the code is still on the bar.
    public var acceptsCode: Bool {
        guard let pairing, clock.now() < pairing.expiresAt else { return false }
        switch step {
        case .enterCode, .failed(.wrongCode, _, _), .failed(.rateLimited, _, _), .failed(.noAnswer, _, _):
            return true
        default:
            return false
        }
    }

    /// The wait after `pairing_busy` or `rate_limited` is over (or there's
    /// none). Measured on the flow's clock, so a change to the Mac's clock
    /// doesn't stretch it.
    public func canRetryNow() -> Bool {
        guard case .failed(_, _, let retryAt?) = step else { return true }
        return clock.now() >= retryAt
    }

    /// Choose among several bars.
    public func choose(_ bar: DiscoveredBar) {
        pairing = nil
        step = .choose(bars.isEmpty ? [bar] : bars, chosen: bar)
    }

    /// Enter Address…: checks the address with `GET /api/v1/info`.
    public func useAddress(_ text: String) async {
        let typed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let endpoint = BarEndpoint(userInput: typed) else {
            step = .failed(.nothingAt(typed), bar: nil, retryAt: nil)
            return
        }
        guard endpoint.isAllowedOverPlainHTTP else {
            step = .failed(.addressNotAllowed, bar: nil, retryAt: nil)
            return
        }
        lookingTimer?.cancel()
        step = .looking
        let transport = transports.makeWiFi(endpoint: endpoint, token: nil)
        defer { Task { await transport.close() } }
        do {
            let info = try await transport.hello(HelloRequest(client: clientID, name: macName))
            guard info.isTinyBar else { throw BarError.notATinyBar }
            let bar = DiscoveredBar(name: info.name, deviceID: info.deviceID, api: info.api, fw: info.fw,
                                    path: TinyBarAPI.basePath, auth: info.auth, endpoint: endpoint)
            bars = [bar]
            pairing = nil
            step = .choose([bar], chosen: bar)
        } catch {
            let problem: PairingProblem
            switch error as? BarError {
            case .localNetworkDenied?: problem = .localNetworkBlocked
            case .timedOut?, .unreachable?, .closed?, .cancelled?, nil: problem = .nothingAt(endpoint.description)
            default: problem = .notATinyBar
            }
            step = .failed(problem, bar: nil, retryAt: nil)
        }
    }

    /// Show Code on TinyBar / Show a New Code: `POST /api/v1/pair/start` with
    /// kind `mac`, scope `call`, this install's `client`, and the Mac's name if set.
    public func requestCode() async {
        guard var bar = chosenBar else { return }
        pairing = nil
        step = .requestingCode(bar)
        let transport = transports.makeWiFi(endpoint: bar.endpoint, token: nil)
        defer { Task { await transport.close() } }
        do {
            // Bonjour's TXT record may not say whether the bar pairs; ask it.
            if bar.auth == nil || bar.deviceID == nil {
                let info = try await transport.hello(HelloRequest(client: clientID, name: macName))
                guard info.isTinyBar else { throw BarError.notATinyBar }
                bar.auth = info.auth
                bar.deviceID = info.deviceID
                bar.name = info.name
            }
            if bar.auth == .notRequired, let deviceID = bar.deviceID {
                // Nothing to pair (mac-app-ux.md 5.5, last row): use it as it is.
                let known = KnownBar(deviceID: deviceID, name: bar.name, host: bar.endpoint.host,
                                     lastEndpoint: bar.endpoint, auth: .notRequired)
                finish(.notNeeded(known), known)
                return
            }
            let reply = try await transport.pairStart(PairStartRequest(name: macName, client: clientID))
            let expiresAt = clock.now().addingTimeInterval(TimeInterval(reply.expiresInS))
            pairing = (bar, reply.pairingID, expiresAt)
            step = .enterCode(bar, pairingID: reply.pairingID, expiresAt: expiresAt)
        } catch {
            fail(error, bar: bar)
        }
    }

    /// Pair: `POST /api/v1/pair` with the digits. Called by the Pair button and
    /// as soon as the sixth digit is typed or pasted.
    public func submit(code: String) async {
        guard let digits = PairingCode.normalize(code), let pairing else { return }
        switch step {
        case .enterCode, .failed(.wrongCode, _, _): break
        case .failed(.rateLimited, _, _), .failed(.noAnswer, _, _):
            // The same code is still on the bar: send it again once the wait is over.
            guard canRetryNow() else { return }
        default: return
        }
        let bar = pairing.bar
        guard clock.now() < pairing.expiresAt else {
            self.pairing = nil
            step = .failed(.expired, bar: bar, retryAt: nil)
            return
        }
        step = .pairing(bar)
        let transport = transports.makeWiFi(endpoint: bar.endpoint, token: nil)
        defer { Task { await transport.close() } }
        do {
            let reply = try await transport.pair(PairRequest(pairingID: pairing.id, code: digits))
            guard let token = reply.token, Identifiers.isValidDeviceID(reply.deviceID),
                  bar.deviceID == nil || bar.deviceID == reply.deviceID
            else { throw BarError.malformedReply("pairing reply without a token for this bar") }
            // Off the main actor: the Keychain may show a dialog and block
            // the calling thread until it's answered.
            let store = tokens
            let deviceID = reply.deviceID
            do {
                try await Task.detached { try store.setToken(token, for: deviceID) }.value
            } catch {
                throw BarError.malformedReply("couldn't keep the token: \(error)")
            }
            let known = KnownBar(deviceID: reply.deviceID, name: reply.name, host: reply.host,
                                 lastEndpoint: bar.endpoint, auth: .bearer, tokenID: reply.tokenID)
            self.pairing = nil
            finish(.paired(known), known)
        } catch {
            fail(error, bar: bar, keepCode: true)
        }
    }

    /// Back, or the window closed: stops browsing. A code on the bar simply
    /// expires (there's no cancel endpoint).
    public func cancel() {
        stopBrowsing()
        pairing = nil
    }

    /// Paired (or nothing to pair). The engine's own browsing starts in
    /// `onPaired` before this flow's stops, so the shared Bonjour browser
    /// keeps running instead of stopping and starting again.
    private func finish(_ step: Step, _ known: KnownBar) {
        self.step = step
        onPaired(known)
        stopBrowsing()
    }

    private func stopBrowsing() {
        lookingTimer?.cancel()
        lookingTimer = nil
        if isBrowsing {
            isBrowsing = false
            discovery.stop()
        }
    }

    /// Shows what went wrong (mac-app-ux.md 5.5).
    ///
    /// - Parameter keepCode: the error came from sending the code. A wrong
    ///   code with tries left, `rate_limited` (one `pair` a second, api.md
    ///   4.9) and no reply leave the code on the bar, so it's kept until it
    ///   expires and can be sent again.
    private func fail(_ error: any Error, bar: DiscoveredBar, keepCode: Bool = false) {
        let problem = WiFiPairingFlow.problem(for: error)
        let codeStillShown = keepCode && pairing.map { clock.now() < $0.expiresAt } == true
        switch problem {
        case .wrongCode:
            break  // the same code is still on the bar: try again
        case .rateLimited, .noAnswer:
            if !codeStillShown { pairing = nil }
        default:
            pairing = nil
        }
        var retryAt: Date?
        switch problem {
        case .busy(let seconds), .rateLimited(let seconds):
            retryAt = clock.now().addingTimeInterval(TimeInterval(seconds))
        default:
            break
        }
        step = .failed(problem, bar: bar, retryAt: retryAt)
    }

    /// The problem for a pairing request's error.
    nonisolated static func problem(for error: any Error) -> PairingProblem {
        guard let error = error as? BarError else { return .noAnswer }
        switch error {
        case .api(let body, _):
            switch body.error {
            case .wrongCode:
                let left = body.attemptsLeft ?? 0
                return left > 0 ? .wrongCode(attemptsLeft: left) : .codeUsedUp
            case .notPairing: return .expired
            case .pairingBusy: return .busy(retryAfter: max(body.retryAfterS ?? 60, 1))
            case .rateLimited: return .rateLimited(retryAfter: max(body.retryAfterS ?? 60, 1))
            case .tokenLimit: return .tokenLimit
            case .inSetup: return .inSetup
            default: return .noAnswer
            }
        case .localNetworkDenied: return .localNetworkBlocked
        case .notATinyBar: return .notATinyBar
        default: return .noAnswer
        }
    }
}
