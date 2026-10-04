import Foundation

/// The USB link: newline-delimited JSON with the `@tb ` marker over the bar's
/// USB Serial/JTAG port (api.md 6).
///
/// How it should work:
/// - A reader runs from `init` until `close()`, **always reading**, even when
///   nothing is pending, so the bar's output never backs up (api.md 6.3). It
///   splits lines with `USBLineSplitter`, classifies them with
///   `USBFraming.classify`, matches replies to requests by `id`, and sends
///   `ready` events and log lines to `events`.
/// - Each request gets the next `id` (1 to 2147483647) and waits up to 3
///   seconds for its reply (`TinyBarAPI.Timeouts.usbReply`), then throws
///   `BarError.timedOut`. A reply with `"id": null` answers the oldest
///   outstanding request (the bar couldn't parse it).
/// - Replies are decoded with `WireJSON.decodeReply(_:from:httpStatus:)`.
/// - When the port reports `closed`, every waiting request throws
///   `BarError.closed` and `events` yields `.closed` and finishes.
/// - Counting missed replies and closing after 3 in a row (api.md 6.8) is
///   `BarConnection`'s job, not this class's.
public final class USBTransport: USBLinkTransport, @unchecked Sendable {
    public let kind: LinkKind = .usb
    public let device: SerialDevice
    public let events: AsyncStream<USBEvent>

    private let port: SerialPort
    private let clock: any TinyClock

    /// Takes over an open port and starts reading.
    public init(port: SerialPort, device: SerialDevice, clock: any TinyClock) {
        self.port = port
        self.device = device
        self.clock = clock
        self.events = AsyncStream { _ in }
        unimplemented()
    }

    /// `cu.usbmodem1101`.
    public var endpointDescription: String {
        device.calloutPath.split(separator: "/").last.map(String.init) ?? device.calloutPath
    }

    public func handshake(_ request: HelloRequest) async throws -> InfoReply {
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

    public func pair(_ request: USBPairRequest) async throws -> PairReply {
        unimplemented()
    }

    /// Any endpoint over USB (api.md 6.6). Not used by the app in v1; for
    /// debugging. The reply's `http_status` is passed to `WireJSON.decodeReply`.
    public func request(_ command: USBRequestCommand) async throws -> JSONValue {
        unimplemented()
    }

    public func close() async {
        unimplemented()
    }
}
