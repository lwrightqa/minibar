import Foundation
#if canImport(Glibc)
import Glibc
#elseif canImport(Darwin)
import Darwin
#endif
@testable import TinyBarCore

/// A tiny HTTP/1.1 server on 127.0.0.1, playing the bar's Wi-Fi API for
/// `HTTPTransport`'s tests. Each connection is served on its own thread;
/// every response says `Connection: close` unless told to keep the
/// connection and then drop it.
final class TestHTTPServer: @unchecked Sendable {
    struct Request: Sendable {
        var method: String
        var path: String
        /// Header names in lower case.
        var headers: [String: String]
        var body: Data

        var bodyText: String { String(decoding: body, as: UTF8.self) }
    }

    enum Behavior: Sendable {
        /// Answer with this status, body and extra headers.
        case respond(status: Int, body: String, headers: [String: String] = [:])
        /// Read the request and never answer (the client times out).
        case noResponse
        /// Read the request and close the connection without a word (a bar
        /// that dropped an idle connection).
        case closeWithoutResponse
    }

    let port: UInt16
    private let listenFD: Int32
    private let state = Locked<(requests: [Request], handler: @Sendable (Request) -> Behavior, held: [Int32], stopped: Bool)>(
        ([], { _ in .respond(status: 404, body: #"{"ok": false, "error": "not_found", "message": "No handler.", "field": null}"#) }, [], false)
    )

    var endpoint: BarEndpoint { BarEndpoint(host: "127.0.0.1", port: Int(port)) }

    /// Every request received, in order.
    var requests: [Request] { state.withLock { $0.requests } }

    init() throws {
        #if canImport(Glibc)
        let fd = socket(AF_INET, Int32(SOCK_STREAM.rawValue), 0)
        #else
        let fd = socket(AF_INET, SOCK_STREAM, 0)
        #endif
        guard fd >= 0 else { throw POSIXError(.EIO) }
        var yes: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, socklen_t(MemoryLayout<Int32>.size))
        var address = sockaddr_in()
        #if canImport(Darwin)
        address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        #endif
        address.sin_family = sa_family_t(AF_INET)
        address.sin_port = 0
        address.sin_addr = in_addr(s_addr: inet_addr("127.0.0.1"))
        let bound = withUnsafePointer(to: &address) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { bind(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size)) }
        }
        guard bound == 0, listen(fd, 16) == 0 else { throw POSIXError(.EADDRINUSE) }
        var length = socklen_t(MemoryLayout<sockaddr_in>.size)
        _ = withUnsafeMutablePointer(to: &address) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { getsockname(fd, $0, &length) }
        }
        listenFD = fd
        port = UInt16(bigEndian: address.sin_port)
        let thread = Thread { [self] in acceptLoop() }
        thread.start()
    }

    /// Decides how each request is answered.
    func handle(_ handler: @escaping @Sendable (Request) -> Behavior) {
        state.withLock { $0.handler = handler }
    }

    /// Answers every request with this status and body.
    func respond(status: Int = 200, _ body: String) {
        handle { _ in .respond(status: status, body: body) }
    }

    /// Closes the listening socket and any held connections, once. A second
    /// call does nothing: `deinit` calls this again, on whichever thread lets
    /// go of the server last (its accept thread), possibly after the next
    /// test's server has been given the same descriptor number, and closing
    /// that would refuse the next test's connections.
    func stop() {
        let held: [Int32]? = state.withLock { state in
            guard !state.stopped else { return nil }
            state.stopped = true
            defer { state.held.removeAll() }
            return state.held
        }
        guard let held else { return }
        held.forEach { _ = shutdown($0, Int32(SHUT_RDWR)); _ = close($0) }
        _ = shutdown(listenFD, Int32(SHUT_RDWR))
        _ = close(listenFD)
    }

    deinit {
        stop()
    }

    private func acceptLoop() {
        while true {
            let connection = accept(listenFD, nil, nil)
            if connection < 0 {
                if errno == EINTR { continue }
                return
            }
            if state.withLock({ $0.stopped }) {
                _ = close(connection)
                return
            }
            #if canImport(Darwin)
            var one: Int32 = 1
            setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE, &one, socklen_t(MemoryLayout<Int32>.size))
            #endif
            let thread = Thread { [self] in serve(connection) }
            thread.start()
        }
    }

    private func serve(_ connection: Int32) {
        var buffer = Data()
        while true {
            guard let request = readRequest(connection, buffer: &buffer) else {
                _ = close(connection)
                return
            }
            let behavior: Behavior = state.withLock { state in
                state.requests.append(request)
                return state.handler(request)
            }
            switch behavior {
            case .respond(let status, let body, let headers):
                var head = "HTTP/1.1 \(status) \(Self.reason(status))\r\n"
                if !body.isEmpty || status != 204 {
                    head += "Content-Type: application/json; charset=utf-8\r\n"
                }
                head += "Content-Length: \(body.utf8.count)\r\nConnection: close\r\n"
                for (name, value) in headers { head += "\(name): \(value)\r\n" }
                head += "\r\n"
                let bytes = Array((head + body).utf8)
                var offset = 0
                while offset < bytes.count {
                    let written = bytes[offset...].withUnsafeBytes { send(connection, $0.baseAddress!, $0.count, Self.sendFlags) }
                    if written <= 0 { break }
                    offset += written
                }
                _ = shutdown(connection, Int32(SHUT_WR))
                _ = close(connection)
                return
            case .noResponse:
                state.withLock { $0.held.append(connection) }
                return
            case .closeWithoutResponse:
                _ = shutdown(connection, Int32(SHUT_RDWR))
                _ = close(connection)
                return
            }
        }
    }

    /// Reads one request: the head up to a blank line, then `Content-Length` bytes.
    private func readRequest(_ connection: Int32, buffer: inout Data) -> Request? {
        let separator = Data("\r\n\r\n".utf8)
        while buffer.range(of: separator) == nil {
            guard readMore(connection, into: &buffer) else { return nil }
        }
        let headEnd = buffer.range(of: separator)!
        let head = String(decoding: buffer[buffer.startIndex..<headEnd.lowerBound], as: UTF8.self)
        var lines = head.components(separatedBy: "\r\n")
        let requestLine = lines.removeFirst().split(separator: " ")
        guard requestLine.count >= 2 else { return nil }
        var headers: [String: String] = [:]
        for line in lines {
            guard let colon = line.firstIndex(of: ":") else { continue }
            headers[line[..<colon].lowercased()] = line[line.index(after: colon)...].trimmingCharacters(in: .whitespaces)
        }
        let length = Int(headers["content-length"] ?? "0") ?? 0
        var rest = Data(buffer[headEnd.upperBound...])
        while rest.count < length {
            guard readMore(connection, into: &rest) else { return nil }
        }
        buffer = Data(rest[(rest.startIndex + length)...])
        return Request(method: String(requestLine[0]), path: String(requestLine[1]), headers: headers,
                       body: Data(rest[rest.startIndex..<(rest.startIndex + length)]))
    }

    private func readMore(_ connection: Int32, into data: inout Data) -> Bool {
        var chunk = [UInt8](repeating: 0, count: 4096)
        let count = chunk.withUnsafeMutableBytes { recv(connection, $0.baseAddress!, $0.count, 0) }
        guard count > 0 else { return false }
        data.append(contentsOf: chunk[..<count])
        return true
    }

    /// Never SIGPIPE the test process when a client has gone.
    #if canImport(Glibc)
    private static let sendFlags = Int32(MSG_NOSIGNAL)
    #else
    private static let sendFlags: Int32 = 0
    #endif

    private static func reason(_ status: Int) -> String {
        switch status {
        case 200: return "OK"
        case 202: return "Accepted"
        case 400: return "Bad Request"
        case 401: return "Unauthorized"
        case 403: return "Forbidden"
        case 404: return "Not Found"
        case 409: return "Conflict"
        case 429: return "Too Many Requests"
        case 431: return "Request Header Fields Too Large"
        case 503: return "Service Unavailable"
        default: return "Status"
        }
    }
}
