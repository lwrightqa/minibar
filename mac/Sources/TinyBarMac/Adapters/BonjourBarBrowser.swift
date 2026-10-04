#if os(macOS)
import Foundation
import Network
import TinyBarCore

/// Browses `_tinybar._tcp` and resolves each bar to an IPv4 address and port
/// (api.md 3).
///
/// Starting it is what makes macOS 15 and later ask for Local Network access,
/// so the engine starts it only when Wi-Fi is allowed and the UI has
/// explained the prompt (mac-app-ux.md 5.4, 8). Info.plist lists
/// `_tinybar._tcp` under `NSBonjourServices` and has
/// `NSLocalNetworkUsageDescription`; without them browsing fails on macOS 15.
///
/// How:
/// - `NWBrowser(for: .bonjourWithTXTRecord(type: "_tinybar._tcp", domain: "local."))`,
///   on `queue`. Each result's instance name is the bar's name ("TinyBar 2A1C");
///   its TXT record gives `id`, `api`, `fw`, `path` and `auth`.
/// - Resolving: a UDP `NWConnection` to the result's endpoint, restricted to
///   IPv4 (api.md 3: IPv4 only), goes `.ready` once the service is resolved,
///   and `currentPath.remoteEndpoint` is then the bar's address and port. UDP
///   sends nothing to the bar, so its small HTTP server never sees a stray
///   connection. Given up after 5 seconds and retried after 30; every bar is
///   resolved again every 5 minutes, and whenever its TXT record changes, in
///   case its address changed.
/// - `onChange` gets every resolved bar, sorted by name, whenever that list
///   changes.
/// - Local Network denied: the browser (or a resolving connection) waits with
///   `NWError.dns(kDNSServiceErr_PolicyDenied)` (-65570); reported once as
///   `.localNetworkDenied`. If the person later allows it, the browser carries
///   on by itself. *Unverified:* the exact error on macOS 15 and 26 (Apple
///   TN3179), and that UDP resolution behaves as described.
/// - Other failures: reported as `.failed`, and the browser starts again
///   after 10 seconds.
final class BonjourBarBrowser: BarDiscovery, @unchecked Sendable {
    /// `kDNSServiceErr_PolicyDenied` from `<dns_sd.h>` (a `DNSServiceErrorType`,
    /// which is an `Int32`; spelled out to avoid importing `dnssd`).
    static let policyDenied: Int32 = -65570

    private struct TXT: Hashable {
        var id: String?
        var api: String?
        var fw: String?
        var path: String?
        var auth: String?
    }

    private struct Found {
        var endpoint: NWEndpoint
        var txt: TXT
    }

    private struct Resolution {
        var connection: NWConnection
        var token: UUID
    }

    private let work = AdapterQueue(label: "TinyBar.bonjour")

    // Everything below is touched only on `work.queue`.
    private var running = false
    private var browser: NWBrowser?
    private var onChange: (@Sendable ([DiscoveredBar]) -> Void)?
    private var onError: (@Sendable (DiscoveryError) -> Void)?
    private var found: [String: Found] = [:]
    private var resolved: [String: DiscoveredBar] = [:]
    private var resolving: [String: Resolution] = [:]
    private var lastReported: [DiscoveredBar]?
    private var reportedDenied = false
    private var refreshTimer: DispatchSourceTimer?

    init() {}

    deinit {
        stop()
    }

    func start(
        onChange: @escaping @Sendable ([DiscoveredBar]) -> Void,
        onError: @escaping @Sendable (DiscoveryError) -> Void
    ) {
        work.sync {
            guard !running else { return }
            running = true
            self.onChange = onChange
            self.onError = onError
            startBrowser()
            refreshTimer = work.repeatingTimer(every: 300) { [weak self] in
                self?.resolveAll()
            }
        }
    }

    func stop() {
        work.sync {
            guard running else { return }
            running = false
            refreshTimer?.cancel()
            refreshTimer = nil
            browser?.cancel()
            browser = nil
            for resolution in resolving.values {
                resolution.connection.cancel()
            }
            resolving = [:]
            found = [:]
            resolved = [:]
            lastReported = nil
            reportedDenied = false
            onChange = nil
            onError = nil
        }
    }

    // MARK: - On the queue

    private func startBrowser() {
        let parameters = NWParameters()
        parameters.includePeerToPeer = false
        let browser = NWBrowser(
            for: .bonjourWithTXTRecord(type: TinyBarAPI.Bonjour.serviceType, domain: TinyBarAPI.Bonjour.domain),
            using: parameters
        )
        browser.stateUpdateHandler = { [weak self] state in
            self?.browserStateChanged(state)
        }
        browser.browseResultsChangedHandler = { [weak self] results, _ in
            self?.resultsChanged(results)
        }
        self.browser = browser
        browser.start(queue: work.queue)
    }

    private func browserStateChanged(_ state: NWBrowser.State) {
        guard running else { return }
        switch state {
        case .ready:
            reportedDenied = false
        case .waiting(let error):
            report(error)
        case .failed(let error):
            report(error)
            browser?.cancel()
            browser = nil
            work.queue.asyncAfter(deadline: .now() + 10) { [weak self] in
                guard let self, self.running, self.browser == nil else { return }
                self.startBrowser()
            }
        default:
            break
        }
    }

    private func report(_ error: NWError) {
        if case .dns(let code) = error, code == Self.policyDenied {
            guard !reportedDenied else { return }
            reportedDenied = true
            onError?(.localNetworkDenied)
        } else {
            onError?(.failed(String(describing: error)))
        }
    }

    private func resultsChanged(_ results: Set<NWBrowser.Result>) {
        guard running else { return }
        var names: Set<String> = []
        for result in results {
            guard case .service(let name, _, _, _) = result.endpoint else { continue }
            names.insert(name)
            var txt = TXT()
            if case .bonjour(let record) = result.metadata {
                txt = TXT(
                    id: record[TinyBarAPI.Bonjour.TXT.deviceID],
                    api: record[TinyBarAPI.Bonjour.TXT.api],
                    fw: record[TinyBarAPI.Bonjour.TXT.firmware],
                    path: record[TinyBarAPI.Bonjour.TXT.path],
                    auth: record[TinyBarAPI.Bonjour.TXT.auth]
                )
            }
            let previous = found[name]
            found[name] = Found(endpoint: result.endpoint, txt: txt)
            if previous?.txt != txt {
                resolved[name] = nil
                resolve(name)
            } else if resolved[name] == nil && resolving[name] == nil {
                resolve(name)
            }
        }
        for name in Array(found.keys) where !names.contains(name) {
            found[name] = nil
            resolved[name] = nil
            resolving.removeValue(forKey: name)?.connection.cancel()
        }
        reportBars()
    }

    private func resolveAll() {
        guard running else { return }
        for name in found.keys where resolving[name] == nil {
            resolve(name)
        }
    }

    private func resolve(_ name: String) {
        guard let entry = found[name] else { return }
        resolving.removeValue(forKey: name)?.connection.cancel()
        let parameters = NWParameters.udp
        if let ip = parameters.defaultProtocolStack.internetProtocol as? NWProtocolIP.Options {
            ip.version = .v4
        }
        let connection = NWConnection(to: entry.endpoint, using: parameters)
        let token = UUID()
        resolving[name] = Resolution(connection: connection, token: token)
        connection.stateUpdateHandler = { [weak self] state in
            self?.resolutionStateChanged(name: name, token: token, state: state)
        }
        connection.start(queue: work.queue)
        work.queue.asyncAfter(deadline: .now() + 5) { [weak self] in
            self?.resolutionFinished(name: name, token: token, endpoint: nil)
        }
    }

    private func resolutionStateChanged(name: String, token: UUID, state: NWConnection.State) {
        guard let resolution = resolving[name], resolution.token == token else { return }
        switch state {
        case .ready:
            resolutionFinished(name: name, token: token, endpoint: resolution.connection.currentPath?.remoteEndpoint)
        case .waiting(let error):
            if case .dns(let code) = error, code == Self.policyDenied {
                report(error)
            }
        case .failed:
            resolutionFinished(name: name, token: token, endpoint: nil)
        default:
            break
        }
    }

    /// Ends one resolution: records the address if there is one, or tries
    /// again in 30 seconds.
    private func resolutionFinished(name: String, token: UUID, endpoint: NWEndpoint?) {
        guard let resolution = resolving[name], resolution.token == token else { return }
        resolving[name] = nil
        resolution.connection.cancel()
        guard running, let entry = found[name] else { return }
        if let barEndpoint = endpoint.flatMap(Self.barEndpoint) {
            resolved[name] = DiscoveredBar(
                name: name,
                deviceID: entry.txt.id,
                api: entry.txt.api,
                fw: entry.txt.fw,
                path: entry.txt.path,
                auth: entry.txt.auth.map { AuthMode(rawValue: $0) },
                endpoint: barEndpoint
            )
            reportBars()
        } else {
            work.queue.asyncAfter(deadline: .now() + 30) { [weak self] in
                guard let self, self.running, self.found[name] != nil,
                      self.resolved[name] == nil, self.resolving[name] == nil else { return }
                self.resolve(name)
            }
        }
    }

    private func reportBars() {
        let bars = resolved.values.sorted { $0.name < $1.name }
        guard bars != lastReported else { return }
        lastReported = bars
        onChange?(bars)
    }

    /// An IPv4 address and port as the HTTP transport wants them.
    private static func barEndpoint(_ endpoint: NWEndpoint) -> BarEndpoint? {
        guard case .hostPort(let host, let port) = endpoint else { return nil }
        switch host {
        case .ipv4(let address):
            let dotted = address.rawValue.map { String($0) }.joined(separator: ".")
            return BarEndpoint(host: dotted, port: Int(port.rawValue))
        case .name(let hostName, _):
            return BarEndpoint(host: hostName, port: Int(port.rawValue))
        case .ipv6:
            return nil
        @unknown default:
            return nil
        }
    }
}
#endif
