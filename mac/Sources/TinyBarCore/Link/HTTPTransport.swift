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
///   once on a new connection (api.md 2.6). Only `GET`, `DELETE` and
///   `POST /api/v1/call` may be repeated otherwise.
/// - Builds and runs on Linux (FoundationNetworking), where it's tested
///   against a small HTTP server in the tests.
public final class HTTPTransport: WiFiLinkTransport, @unchecked Sendable {
    public let kind: LinkKind = .wifi
    public let endpoint: BarEndpoint
    private let token: String?
    private let appVersion: String

    public init(endpoint: BarEndpoint, token: String?, appVersion: String) {
        self.endpoint = endpoint
        self.token = token
        self.appVersion = appVersion
        unimplemented()
    }

    /// `tinybar.local`, or `tinybar.local (10.0.4.42)` once the address is known.
    public var endpointDescription: String {
        unimplemented()
    }

    /// Builds the request for `endpoint` (api.md 2.2, 4.5):
    /// - URL `http://<host>[:port]<path>`;
    /// - `Content-Type: application/json` when there's a body;
    /// - `Authorization: Bearer <token>` when `endpoint.sendsToken` and a token is set;
    /// - `User-Agent: TinyBarAPI.userAgent(appVersion:)`;
    /// - `Accept: application/json`;
    /// - time-out 5 seconds, no caching, no cookies.
    public static func makeRequest(
        to barEndpoint: BarEndpoint,
        _ endpoint: Endpoint,
        body: Data?,
        token: String?,
        appVersion: String
    ) -> URLRequest {
        unimplemented()
    }

    public func hello(_ request: HelloRequest) async throws -> InfoReply {
        unimplemented()
    }

    public func sendCall(_ request: CallRequest) async throws -> CallReply {
        unimplemented()
    }

    public func status() async throws -> StatusReply {
        unimplemented()
    }

    public func pairStart(_ request: PairStartRequest) async throws -> PairStartReply {
        unimplemented()
    }

    public func pair(_ request: PairRequest) async throws -> PairReply {
        unimplemented()
    }

    public func unpairSelf() async throws -> RevokeReply {
        unimplemented()
    }

    public func close() async {
        unimplemented()
    }
}
