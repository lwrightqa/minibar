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
/// connection. A code it asked for and then gave up (Back, the window closed,
/// quitting, sleep, another bar) comes off the bar at once with
/// `pair/cancel`, in the background and best effort.
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

    /// A code this flow asked for: the bar, its `pairing_id` and when it expires.
    typealias Code = (bar: DiscoveredBar, id: String, expiresAt: Date)

    /// The bars found so far (or the one at a typed address).
    @ObservationIgnored private var bars: [DiscoveredBar] = []
    /// The code the chosen bar is showing, as far as this flow knows. `nil`
    /// once it's been used, has ended (expired, used up, canceled on the bar)
    /// or was given up (`releaseCode`).
    @ObservationIgnored private var pairing: Code?
    /// Counts the codes given up (Back, the window closed, quitting, sleep,
    /// another bar), so a `pair/start` or `pair` answer that arrives after its
    /// code was given up takes that code off the bar instead of showing it.
    @ObservationIgnored private var codeGeneration = 0
    /// A `pair` request is on its way. A code given up meanwhile waits for its
    /// answer: a success keeps the pairing, and a code still on the bar after
    /// any other answer is canceled then.
    @ObservationIgnored private var isSendingCode = false
    /// Back, the window closed, or quitting: no more codes are asked for.
    @ObservationIgnored private var isClosed = false
    @ObservationIgnored private var isBrowsing = false
    @ObservationIgnored private var lookingTimer: Task<Void, Never>?
    /// Ignores results from an earlier round of browsing.
    @ObservationIgnored private var generation = 0

    /// How long to look before saying none was found (mac-app-ux.md 5.5).
    public static let lookingTimeout: TimeInterval = 10

    /// How long taking a code off the bar (`pair/cancel`) may take, with one
    /// more try after `rate_limited`. Quitting and sleep wait at most this long.
    public nonisolated static let cancelTimeout: TimeInterval = 2

    /// Continue after the explanation: starts browsing (which brings up the
    /// macOS prompt). Gives up with `.noneFound` after 10 seconds. A code
    /// already on a bar (Didn't see a code? Choose another TinyBar.) is taken
    /// off it.
    public func startLooking() {
        releaseCode()
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

    /// Choose among several bars. A code on the bar chosen before is taken off it.
    public func choose(_ bar: DiscoveredBar) {
        releaseCode()
        step = .choose(bars.isEmpty ? [bar] : bars, chosen: bar)
    }

    /// Enter Address…: checks the address with `GET /api/v1/info`.
    public func useAddress(_ text: String) async {
        releaseCode()
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
    ///
    /// While this flow's own code is still on the bar, the bar answers
    /// `pairing_busy` (one code at a time), and the page goes back to typing
    /// that code instead of saying someone else is pairing, as the mock-up's
    /// Mac app does ("Asking again while its own code is still on the bar just
    /// goes back to typing it"). If that code has ended on the bar meanwhile
    /// (a tap canceled it), the bar shows a new one.
    public func requestCode() async {
        guard !isClosed, var bar = chosenBar else { return }
        let ownCode = pairing.flatMap { clock.now() < $0.expiresAt ? $0 : nil }
        pairing = ownCode
        let generation = codeGeneration
        step = .requestingCode(bar)
        let transport = transports.makeWiFi(endpoint: bar.endpoint, token: nil)
        defer { Task { await transport.close() } }
        do {
            // Bonjour's TXT record may not say whether the bar pairs; ask it.
            if bar.auth == nil || bar.deviceID == nil {
                let info = try await transport.hello(HelloRequest(client: clientID, name: macName))
                guard info.isTinyBar else { throw BarError.notATinyBar }
                // Given up meanwhile (Back, the window closed, sleep): ask for nothing.
                guard generation == codeGeneration, !isClosed else { return }
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
            guard generation == codeGeneration else {
                // Given up while the bar was being asked: the code it just
                // showed comes off again at once.
                sendCancel(for: (bar, reply.pairingID, expiresAt))
                return
            }
            pairing = (bar, reply.pairingID, expiresAt)
            step = .enterCode(bar, pairingID: reply.pairingID, expiresAt: expiresAt)
        } catch {
            // Given up meanwhile: the page has moved on.
            guard generation == codeGeneration else { return }
            if let ownCode, (error as? BarError)?.apiCode == .pairingBusy, clock.now() < ownCode.expiresAt {
                // Busy with this flow's own code: back to typing it.
                pairing = ownCode
                step = .enterCode(ownCode.bar, pairingID: ownCode.id, expiresAt: ownCode.expiresAt)
                return
            }
            fail(error, bar: bar, keepCode: ownCode != nil)
        }
    }

    /// Pair: `POST /api/v1/pair` with the digits. Called by the Pair button and
    /// as soon as the sixth digit is typed or pasted.
    public func submit(code: String) async {
        guard !isClosed, let digits = PairingCode.normalize(code), let pairing else { return }
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
        let generation = codeGeneration
        isSendingCode = true
        defer { isSendingCode = false }
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
            // Paired, even if the code was given up while it was being sent:
            // the bar has the token now, so it's kept and nothing is canceled.
            self.pairing = nil
            finish(.paired(known), known)
        } catch {
            guard generation == codeGeneration else {
                // Given up while the code was being sent (Back, the window
                // closed, quitting, sleep). Unless this answer ended it, the
                // code is still on the bar: take it off now.
                if WiFiPairingFlow.codeOutlives(WiFiPairingFlow.problem(for: error)) {
                    sendCancel(for: pairing)
                }
                return
            }
            fail(error, bar: bar, keepCode: true)
        }
    }

    /// Back, the window closed, or quitting: stops browsing, asks for no more
    /// codes, and takes this flow's code off the bar if it's still there
    /// (`POST /api/v1/pair/cancel`, api.md 4.7), so it doesn't hold up other
    /// devices for the rest of its 2 minutes. Nothing is sent after a success
    /// or once the code has ended.
    ///
    /// - Returns: the `pair/cancel` request, which quitting waits for (it
    ///   gives up after `cancelTimeout`), or `nil` when there was nothing to
    ///   cancel. The window never waits for it.
    @discardableResult
    public func cancel() -> Task<Void, Never>? {
        stopBrowsing()
        isClosed = true
        return releaseCode()
    }

    /// The Mac is going to sleep or shutting down: nobody will type the code
    /// now, so it's taken off the bar as Back does, and the page says so
    /// ("That code has expired or was canceled on TinyBar", with Show a New
    /// Code). Browsing carries on.
    ///
    /// - Returns: the `pair/cancel` request, which sleep waits for briefly,
    ///   or `nil` when no code was on the bar.
    @discardableResult
    public func macWillSleep() -> Task<Void, Never>? {
        let codeBar: DiscoveredBar?
        switch step {
        case .requestingCode(let bar), .pairing(let bar):
            codeBar = bar
        default:
            codeBar = pairing.flatMap { clock.now() < $0.expiresAt ? $0.bar : nil }
        }
        guard let codeBar else { return nil }
        let request = releaseCode()
        if !isClosed {
            step = .failed(.expired, bar: codeBar, retryAt: nil)
        }
        return request
    }

    /// Gives up the code this flow asked for, and takes it off the bar if it's
    /// still there as far as the flow knows. Nothing is sent once it has ended
    /// (used, expired, used up, canceled on the bar), nor while it's being sent
    /// (`pair`): that answer decides then (`submit`). A `pair/start` still on
    /// its way is canceled when its answer comes (`requestCode`).
    @discardableResult
    private func releaseCode() -> Task<Void, Never>? {
        codeGeneration += 1
        guard let code = pairing else { return nil }
        pairing = nil
        guard !isSendingCode, clock.now() < code.expiresAt else { return nil }
        return sendCancel(for: code)
    }

    /// Sends `pair/cancel` for `code` in the background, so the window never waits.
    @discardableResult
    private func sendCancel(for code: Code) -> Task<Void, Never> {
        let transports = self.transports
        let clock = self.clock
        let pairingID = code.id
        let endpoint = code.bar.endpoint
        return Task.detached {
            await WiFiPairingFlow.cancelCode(pairingID: pairingID, at: endpoint, transports: transports, clock: clock)
        }
    }

    /// `POST /api/v1/pair/cancel` (api.md 4.7). Best effort: never throws and
    /// gives up after `cancelTimeout`. It shares `pair`'s limit of one request
    /// a second, so `rate_limited` (a wrong code typed just before Back) is
    /// tried once more a second later. `not_pairing` means the code had ended
    /// already; after anything else the code simply runs out on the bar.
    nonisolated static func cancelCode(
        pairingID: String,
        at endpoint: BarEndpoint,
        transports: any TransportFactory,
        clock: any TinyClock
    ) async {
        let transport = transports.makeWiFi(endpoint: endpoint, token: nil)
        let request = PairCancelRequest(pairingID: pairingID)
        await BarConnection.withTimeout(cancelTimeout, clock: clock) {
            do {
                _ = try await transport.pairCancel(request)
            } catch let error as BarError where error.apiCode == .rateLimited {
                do { try await clock.sleep(seconds: 1) } catch { return }
                _ = try? await transport.pairCancel(request)
            } catch {
                // `not_pairing`, no answer: nothing more to do.
            }
        }
        await transport.close()
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
    /// - Parameter keepCode: a code of this flow's may still be on the bar
    ///   (the error came from sending it, or from Show a New Code while it
    ///   showed). A wrong code with tries left, `rate_limited` (one `pair` a
    ///   second, api.md 4.9) and no reply leave it there, so it's kept until
    ///   it expires and can be sent again (or canceled). Every other answer
    ///   means it has ended on the bar: `not_pairing`, `wrong_code` with no
    ///   tries left, `token_limit` (from `pair` a safeguard that ends the
    ///   pairing, api.md 4.7), `in_setup` (setup ends any pairing, api.md 13).
    private func fail(_ error: any Error, bar: DiscoveredBar, keepCode: Bool = false) {
        let problem = WiFiPairingFlow.problem(for: error)
        let codeStillShown = keepCode && pairing.map { clock.now() < $0.expiresAt } == true
        if !(codeStillShown && WiFiPairingFlow.codeOutlives(problem)) {
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

    /// Whether the code is still on the bar after `pair` failed with `problem`:
    /// a wrong code with tries left, `rate_limited`, or no answer.
    nonisolated static func codeOutlives(_ problem: PairingProblem) -> Bool {
        switch problem {
        case .wrongCode, .rateLimited, .noAnswer: return true
        default: return false
        }
    }

    /// The problem for a pairing request's error (api.md 4.6, 4.7, Appendix B).
    /// `token_limit` comes from `pair/start` and, as a safeguard, from `pair`;
    /// `in_setup` from both.
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
