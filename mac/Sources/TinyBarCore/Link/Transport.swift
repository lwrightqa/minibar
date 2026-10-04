import Foundation

/// The two links (api.md 1).
public enum LinkKind: String, Hashable, Sendable, Codable {
    case usb
    case wifi

    /// The `via` value the bar reports for messages over this link.
    public var via: LinkVia { self == .usb ? .usb : .wifi }
}

/// Where a bar answers over Wi-Fi: a host name (`tinybar.local`,
/// `tinybar-2.local`) or an IPv4 address, and a port.
public struct BarEndpoint: Hashable, Sendable, Codable, CustomStringConvertible {
    public var host: String
    public var port: Int

    public init(host: String, port: Int = TinyBarAPI.Bonjour.port) {
        self.host = host
        self.port = port
    }

    /// The default address for finding a bar the first time (api.md 3).
    public static let defaultHost = BarEndpoint(host: TinyBarAPI.Bonjour.defaultHost)

    /// `http://host` or `http://host:port`. The bar serves plain HTTP (api.md 4.10).
    public var baseURL: URL? {
        URL(string: port == 80 ? "http://\(host)" : "http://\(host):\(port)")
    }

    /// The Remote page (mac-app-ux.md 4.4, Open TinyBar Remote…).
    public var remotePageURL: URL? {
        baseURL.flatMap { URL(string: "/", relativeTo: $0)?.absoluteURL }
    }

    /// Parses what the user types in Settings › Advanced › Address:
    /// `tinybar.local`, `10.0.4.42`, `10.0.4.42:8080`, optionally with
    /// `http://` and a trailing `/`. Returns `nil` for anything else.
    public init?(userInput: String) {
        var text = userInput.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.lowercased().hasPrefix("http://") { text.removeFirst("http://".count) }
        if text.hasSuffix("/") { text.removeLast() }
        guard !text.isEmpty else { return nil }

        var host = Substring(text)
        var port = TinyBarAPI.Bonjour.port
        if let colon = text.lastIndex(of: ":") {
            host = text[..<colon]
            let digits = text[text.index(after: colon)...]
            guard !digits.isEmpty, digits.count <= 5, digits.allSatisfy(\.isASCIIDigit),
                  let number = Int(digits), (1...65_535).contains(number)
            else { return nil }
            port = number
        }
        // A trailing dot (an absolute DNS name) is fine.
        if host.hasSuffix(".") { host = host.dropLast() }
        let labels = host.split(separator: ".", omittingEmptySubsequences: false)
        guard !host.isEmpty, host.utf8.count <= 253,
              labels.allSatisfy({ label in
                  !label.isEmpty && label.utf8.count <= 63 && label.first != "-" && label.last != "-"
                      && label.allSatisfy { $0.isASCIIHostCharacter }
              })
        else { return nil }
        // All digits and dots must be a dotted IPv4 address.
        if labels.allSatisfy({ $0.allSatisfy(\.isASCIIDigit) }) {
            guard labels.count == 4, labels.allSatisfy({ Int($0).map { (0...255).contains($0) } ?? false }) else { return nil }
        }
        self.init(host: host.lowercased(), port: port)
    }

    public var description: String { port == 80 ? host : "\(host):\(port)" }
}

extension Character {
    var isASCIIDigit: Bool {
        guard let ascii = asciiValue else { return false }
        return (0x30...0x39).contains(ascii)
    }

    /// Letters, digits and hyphens, as in DNS host names.
    var isASCIIHostCharacter: Bool {
        guard let ascii = asciiValue, self != "\r\n" else { return false }
        return (0x30...0x39).contains(ascii) || (0x41...0x5A).contains(ascii) || (0x61...0x7A).contains(ascii) || ascii == 0x2D
    }
}

/// One link to one bar. Implemented by `USBTransport`, `HTTPTransport`, and
/// fakes in tests.
///
/// - Every method throws only `BarError`.
/// - The app makes one request at a time (api.md 16). Implementations may
///   rely on that, but must not crash if it's broken.
/// - After `close()`, every method throws `BarError.closed`.
public protocol Transport: AnyObject, Sendable {
    var kind: LinkKind { get }

    /// For the menu's Option-click details: `cu.usbmodem1101`, or
    /// `tinybar.local (10.0.4.42)`.
    var endpointDescription: String { get }

    /// Who the bar is. USB: one `hello` with these fields (3-second time-out);
    /// Wi-Fi: `GET /api/v1/info` (the fields are unused).
    func hello(_ request: HelloRequest) async throws -> InfoReply

    /// Sends the call state (api.md 5).
    func sendCall(_ request: CallRequest) async throws -> CallReply

    /// `GET /api/v1/status`, or the USB `status` command.
    func status() async throws -> StatusReply

    /// Closes the link. Idempotent.
    func close() async
}

/// Something the bar sends over USB without being asked.
public enum USBEvent: Hashable, Sendable {
    /// The bar has (re)started (api.md 6.7): send `hello`, then the call state.
    case ready(ReadyEvent)
    /// A log line from the firmware (for a debug view; normally dropped).
    case log(String)
    /// The port went away or was closed. Finishes the stream.
    case closed
}

/// The USB link (api.md 6).
public protocol USBLinkTransport: Transport {
    /// The serial device this transport has open.
    var device: SerialDevice { get }

    /// api.md 6.2, steps 2 and 3: sends `hello` every 2 seconds until a reply
    /// says `"device": "TinyBar"`, for up to 10 seconds. Log lines and other
    /// output in between are ignored. Throws `BarError.notATinyBar` on time-out,
    /// and `BarError.api` with `unsupported_api` if the bar refuses the version.
    func handshake(_ request: HelloRequest) async throws -> InfoReply

    /// The USB `pair` command (api.md 6.6): a Wi-Fi token without a code.
    func pair(_ request: USBPairRequest) async throws -> PairReply

    /// `ready` events, log lines, and `.closed` when the port goes away.
    /// One consumer only.
    var events: AsyncStream<USBEvent> { get }
}

/// The Wi-Fi link (HTTP, api.md 4 and 5).
public protocol WiFiLinkTransport: Transport {
    var endpoint: BarEndpoint { get }

    /// `POST /api/v1/pair/start`.
    func pairStart(_ request: PairStartRequest) async throws -> PairStartReply

    /// `POST /api/v1/pair`.
    func pair(_ request: PairRequest) async throws -> PairReply

    /// `DELETE /api/v1/clients/self` (Forget This TinyBar).
    func unpairSelf() async throws -> RevokeReply
}

/// Makes transports. The engine gets one through its dependencies, so tests
/// can hand it fakes.
public protocol TransportFactory: Sendable {
    /// Opens the serial port of `device` (exclusively, without touching DTR or
    /// RTS, api.md 6.3). No handshake yet.
    func openUSB(_ device: SerialDevice) throws -> any USBLinkTransport

    /// A Wi-Fi transport to `endpoint`, sending `token` as
    /// `Authorization: Bearer` when it isn't `nil`.
    func makeWiFi(endpoint: BarEndpoint, token: String?) -> any WiFiLinkTransport
}

/// The real transports: `SerialPort` + `USBTransport`, and `HTTPTransport`.
public struct DefaultTransportFactory: TransportFactory {
    public var clock: any TinyClock
    /// For the `User-Agent` header.
    public var appVersion: String

    public init(clock: any TinyClock = SystemClock(), appVersion: String) {
        self.clock = clock
        self.appVersion = appVersion
    }

    public func openUSB(_ device: SerialDevice) throws -> any USBLinkTransport {
        unimplemented()
    }

    public func makeWiFi(endpoint: BarEndpoint, token: String?) -> any WiFiLinkTransport {
        unimplemented()
    }
}
