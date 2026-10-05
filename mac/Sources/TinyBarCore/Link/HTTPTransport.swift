import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

/// The Wi-Fi link: plain HTTP to the bar on the local network (api.md 2, 4, 5).
///
/// How it should work:
/// - One `URLSession` per transport, ephemeral (no cookies, no cache), with a
///   5-second request time-out (`TinyBarAPI.Timeouts.http`) and at most one
///   connection per host (api.md 2.5: clients keep at most one open).
/// - Requests are built by `makeRequest`; replies are decoded with
///   `WireJSON.decodeReply(_:from:httpStatus:)`.
/// - Errors map to `BarError`: time-out → `.timedOut`; DNS, refused, no route
///   → `.unreachable`; on macOS 15, a refusal by Local Network privacy →
///   `.localNetworkDenied` (*unverified:* URLSession reports it as
///   `NSURLErrorNotConnectedToInternet` or a `kCFStreamErrorDomainNetwork`
///   error; check on a Mac, or detect it with `NWConnection`'s
///   `.waiting` state and `NWError.dns`/`.posix(.ENETUNREACH)` — see
///   Apple TN3179).
/// - When a request fails because the bar closed an idle connection, retry it
///   once on a new connection (api.md 2.6). Only `GET`, `DELETE`,
///   `POST /api/v1/call` and `POST /api/v1/pair/cancel` may be repeated otherwise.
/// - Builds and runs on Linux (FoundationNetworking), where it's tested
///   against a small HTTP server in the tests.
public final class HTTPTransport: WiFiLinkTransport, @unchecked Sendable {
    public let kind: LinkKind = .wifi
    public let endpoint: BarEndpoint
    private let token: String?
    private let appVersion: String
    private let timeout: TimeInterval
    private let session: URLSession
    private let isClosed = Locked(false)

    /// - Parameter timeout: per request; 5 seconds (api.md 2.6). Tests use less.
    public init(endpoint: BarEndpoint, token: String?, appVersion: String, timeout: TimeInterval = TinyBarAPI.Timeouts.http) {
        self.endpoint = endpoint
        self.token = token
        self.appVersion = appVersion
        self.timeout = timeout
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = timeout
        configuration.timeoutIntervalForResource = timeout
        configuration.httpMaximumConnectionsPerHost = 1
        configuration.httpCookieAcceptPolicy = .never
        configuration.httpShouldSetCookies = false
        configuration.httpCookieStorage = nil
        configuration.urlCache = nil
        configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        #if os(macOS)
        // Fail at once when there's no route, rather than wait for one: the
        // connection retries on its own schedule.
        configuration.waitsForConnectivity = false
        #endif
        session = URLSession(configuration: configuration)
    }

    deinit {
        session.invalidateAndCancel()
    }

    /// `tinybar.local`, or `10.0.4.42:8080`. (URLSession doesn't say which
    /// address a name resolved to on every platform, so it's left out.)
    public var endpointDescription: String {
        endpoint.description
    }

    /// Builds the request for `endpoint` (api.md 2.2, 4.5):
    /// - URL `http://<host>[:port]<path>`;
    /// - `Content-Type: application/json` when there's a body;
    /// - `Authorization: Bearer <token>` when `endpoint.sendsToken` and a token is set;
    /// - `User-Agent: TinyBarAPI.userAgent(appVersion:)`;
    /// - `Accept: application/json`;
    /// - `Accept-Language: en`, the same on every Mac, in place of URLSession's
    ///   default, which lists the user's preferred languages (docs/mac-app.md,
    ///   "What leaves the Mac");
    /// - time-out 5 seconds, no caching, no cookies.
    public static func makeRequest(
        to barEndpoint: BarEndpoint,
        _ endpoint: Endpoint,
        body: Data?,
        token: String?,
        appVersion: String
    ) -> URLRequest {
        let base = barEndpoint.port == 80 ? "http://\(barEndpoint.host)" : "http://\(barEndpoint.host):\(barEndpoint.port)"
        let url = URL(string: base + endpoint.path) ?? URL(string: "http://\(TinyBarAPI.Bonjour.defaultHost)\(endpoint.path)")!
        var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: TinyBarAPI.Timeouts.http)
        request.httpMethod = endpoint.method.rawValue
        request.httpShouldHandleCookies = false
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        request.setValue(TinyBarAPI.userAgent(appVersion: appVersion), forHTTPHeaderField: "User-Agent")
        request.setValue(TinyBarAPI.acceptLanguage, forHTTPHeaderField: "Accept-Language")
        if let body {
            request.httpBody = body
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        }
        if endpoint.sendsToken, let token, !token.isEmpty {
            request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        }
        return request
    }

    public func hello(_ request: HelloRequest) async throws -> InfoReply {
        try await send(Endpoints.info, as: InfoReply.self)
    }

    public func sendCall(_ request: CallRequest) async throws -> CallReply {
        try await send(Endpoints.call, body: request, as: CallReply.self)
    }

    public func status() async throws -> StatusReply {
        try await send(Endpoints.status, as: StatusReply.self)
    }

    public func pairStart(_ request: PairStartRequest) async throws -> PairStartReply {
        try await send(Endpoints.pairStart, body: request, as: PairStartReply.self)
    }

    public func pair(_ request: PairRequest) async throws -> PairReply {
        try await send(Endpoints.pair, body: request, as: PairReply.self)
    }

    public func pairCancel(_ request: PairCancelRequest) async throws -> PairCancelReply {
        try await send(Endpoints.pairCancel, body: request, as: PairCancelReply.self)
    }

    public func unpairSelf() async throws -> RevokeReply {
        try await send(Endpoints.clientsSelf, as: RevokeReply.self)
    }

    public func close() async {
        guard !isClosed.withLock({ closed in defer { closed = true }; return closed }) else { return }
        session.invalidateAndCancel()
    }

    // MARK: - Sending

    /// `GET`, `DELETE` and `POST /api/v1/call` carry the whole state they set,
    /// so they may be sent again (api.md 2.6), and so may `pair/cancel` ("Safe
    /// to repeat", api.md 4.7).
    static func isRepeatable(_ endpoint: Endpoint) -> Bool {
        endpoint.method == .get || endpoint.method == .delete || endpoint == Endpoints.call || endpoint == Endpoints.pairCancel
    }

    private func send<Reply: Decodable>(_ endpoint: Endpoint, as type: Reply.Type) async throws(BarError) -> Reply {
        try await send(endpoint, bodyData: nil, as: type)
    }

    private func send<Body: Encodable, Reply: Decodable>(
        _ endpoint: Endpoint, body: Body, as type: Reply.Type
    ) async throws(BarError) -> Reply {
        let data: Data
        do {
            data = try WireJSON.encode(body)
        } catch {
            throw .malformedReply("Couldn't encode the request: \(error)")
        }
        guard data.count <= TinyBarAPI.Limits.httpBodyBytes else { throw .tooLarge }
        return try await send(endpoint, bodyData: data, as: type)
    }

    private func send<Reply: Decodable>(_ endpoint: Endpoint, bodyData: Data?, as type: Reply.Type) async throws(BarError) -> Reply {
        guard !isClosed.get() else { throw .closed }
        var request = HTTPTransport.makeRequest(to: self.endpoint, endpoint, body: bodyData, token: token, appVersion: appVersion)
        request.timeoutInterval = timeout
        var retried = false
        while true {
            switch await perform(request) {
            case .success(let (data, status)):
                return try WireJSON.decodeReply(Reply.self, from: data, httpStatus: status)
            case .failure(let error):
                // The bar may close an idle connection at any time; a request
                // that fails because of that is sent once more (api.md 2.6).
                if !retried, HTTPTransport.isRepeatable(endpoint), HTTPTransport.isStaleConnection(error) {
                    retried = true
                    continue
                }
                throw isClosed.get() ? .closed : HTTPTransport.barError(for: error)
            }
        }
    }

    /// One request on the session, cancelled with the calling task.
    private func perform(_ request: URLRequest) async -> Result<(Data, Int), any Error> {
        let holder = DataTaskHolder()
        return await withTaskCancellationHandler {
            await withCheckedContinuation { (continuation: CheckedContinuation<Result<(Data, Int), any Error>, Never>) in
                let task = session.dataTask(with: request) { data, response, error in
                    if let error {
                        continuation.resume(returning: .failure(error))
                    } else if let http = response as? HTTPURLResponse {
                        continuation.resume(returning: .success((data ?? Data(), http.statusCode)))
                    } else {
                        continuation.resume(returning: .failure(URLError(.badServerResponse)))
                    }
                }
                holder.start(task)
            }
        } onCancel: {
            holder.cancel()
        }
    }

    /// A failure caused by reusing a connection the bar had already closed:
    /// `networkConnectionLost` on macOS; on Linux, libcurl's "empty reply"
    /// comes back as `badServerResponse`.
    static func isStaleConnection(_ error: any Error) -> Bool {
        guard let code = (error as? URLError)?.code else { return false }
        return code == .networkConnectionLost || code == .badServerResponse
    }

    /// Maps URLSession's errors to `BarError` (api.md 16).
    static func barError(for error: any Error) -> BarError {
        if let error = error as? BarError { return error }
        guard let urlError = error as? URLError else { return .unreachable(String(describing: error)) }
        switch urlError.code {
        case .timedOut:
            return .timedOut
        case .cancelled:
            return .cancelled
        case .badServerResponse, .cannotParseResponse, .zeroByteResource:
            return .malformedReply("HTTP: \(urlError.code.rawValue)")
        default:
            if isLocalNetworkDenial(urlError) { return .localNetworkDenied }
            return .unreachable("URLError \(urlError.code.rawValue)")
        }
    }

    /// Whether macOS refused the request under Local Network privacy (macOS 15).
    ///
    /// *Unverified, check on a Mac:* URLSession reports the refusal as
    /// `NSURLErrorNotConnectedToInternet` (-1009), and the error's user info
    /// carries the network path, whose description names the reason ("Local
    /// network prohibited", Apple TN3179 and developer forum reports). This
    /// reads that description rather than a private key's value, so a change
    /// in wording makes it fall back to `.unreachable`, never crash.
    static func isLocalNetworkDenial(_ error: URLError) -> Bool {
        guard error.code == .notConnectedToInternet else { return false }
        let details = error.userInfo.values.map { String(describing: $0) }.joined(separator: " ").lowercased()
        return details.contains("local network prohibited") || details.contains("localnetworkdenied")
    }
}

/// Holds a data task so cancellation that comes before it starts still stops it.
private final class DataTaskHolder: @unchecked Sendable {
    private let state = Locked<(task: URLSessionDataTask?, cancelled: Bool)>((nil, false))

    func start(_ task: URLSessionDataTask) {
        let cancelled = state.withLock { state -> Bool in
            state.task = task
            return state.cancelled
        }
        task.resume()
        if cancelled { task.cancel() }
    }

    func cancel() {
        let task = state.withLock { state -> URLSessionDataTask? in
            state.cancelled = true
            return state.task
        }
        task?.cancel()
    }
}
