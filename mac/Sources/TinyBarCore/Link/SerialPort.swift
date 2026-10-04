import Foundation
#if canImport(Darwin)
import Darwin
#elseif canImport(Glibc)
import Glibc
#endif

/// Why a serial port operation failed.
public enum SerialPortError: Error, Hashable, Sendable {
    /// `open` failed with this `errno` (`ENOENT`: gone; `EACCES`: no permission).
    case openFailed(errno: Int32)
    /// Another program has the port open exclusively (`EBUSY`), for example a
    /// firmware flasher.
    case busy
    /// `tcgetattr`, `tcsetattr` or `ioctl(TIOCEXCL)` failed with this `errno`.
    case configureFailed(errno: Int32)
    /// `read` or `write` failed with this `errno`.
    case ioFailed(errno: Int32)
    /// The port was closed, or the device went away (`ENXIO`, end of file).
    case closed
    /// A write didn't finish in time.
    case timedOut
}

/// A serial port opened with POSIX calls, set up as api.md 6.3 asks.
///
/// - Opens the path with `O_RDWR | O_NOCTTY | O_NONBLOCK`. Use `/dev/cu.*` on
///   macOS, never `/dev/tty.*` (which can wait for a carrier signal).
/// - **Proposed** (api.md 6.3): takes exclusive access with `ioctl(TIOCEXCL)`,
///   so a firmware flasher fails clearly instead of sharing the port.
/// - Raw mode (`cfmakeraw`), 8 data bits, no parity, 1 stop bit, no flow
///   control, `CLOCAL | CREAD`, 115200 baud. Clears `HUPCL` so closing the port
///   doesn't drop DTR.
/// - **Never touches DTR or RTS** after opening (no `TIOCMSET`, `TIOCMBIS`,
///   `TIOCMBIC`, `TIOCSDTR`, `TIOCCDTR`): on the ESP32-S3's USB Serial/JTAG
///   port they drive the chip's reset. *Unverified on the board:* whether
///   macOS's own open and close reset the bar (api.md 6.3, criterion 16).
///
/// Works the same on Linux, which is how it's tested (against a pseudo-terminal,
/// `PseudoTerminal`).
public final class SerialPort: @unchecked Sendable {
    public struct Options: Hashable, Sendable {
        /// Take exclusive access with `TIOCEXCL`.
        public var exclusive: Bool
        public var baudRate: Int

        public init(exclusive: Bool = true, baudRate: Int = TinyBarAPI.USB.baudRate) {
            self.exclusive = exclusive
            self.baudRate = baudRate
        }
    }

    /// The device path, for example `/dev/cu.usbmodem1101`.
    public let path: String
    public let options: Options

    private struct State {
        var fd: Int32
        /// `close()` was called; the descriptor closes once no read or write uses it.
        var closing = false
        /// Reads and writes in progress.
        var users = 0
    }

    private let state: Locked<State>
    /// A pipe that `close()` writes to, so a `read` or `write` waiting in
    /// `poll` wakes at once instead of at its time-out.
    private let wakeRead: Int32
    private let wakeWrite: Int32

    /// Opens and configures the port. Throws `SerialPortError`.
    public init(path: String, options: Options = Options()) throws {
        self.path = path
        self.options = options
        let fd = Posix.open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard fd >= 0 else {
            let code = errno
            throw code == EBUSY ? SerialPortError.busy : SerialPortError.openFailed(errno: code)
        }
        do {
            if options.exclusive, Posix.ioctl(fd, Posix.tiocexcl) != 0 {
                throw errno == EBUSY ? SerialPortError.busy : SerialPortError.configureFailed(errno: errno)
            }
            try SerialPort.configure(fd, baudRate: options.baudRate)
        } catch {
            _ = Posix.close(fd)
            throw error
        }
        var pipeFDs: [Int32] = [-1, -1]
        guard Posix.pipe(&pipeFDs) == 0 else {
            let code = errno
            _ = Posix.close(fd)
            throw SerialPortError.openFailed(errno: code)
        }
        for pipeFD in pipeFDs {
            _ = Posix.fcntl(pipeFD, F_SETFL, Posix.fcntl(pipeFD, F_GETFL, 0) | O_NONBLOCK)
            _ = Posix.fcntl(pipeFD, F_SETFD, FD_CLOEXEC)
        }
        _ = Posix.fcntl(fd, F_SETFD, FD_CLOEXEC)
        wakeRead = pipeFDs[0]
        wakeWrite = pipeFDs[1]
        state = Locked(State(fd: fd))
    }

    /// api.md 6.3: raw, 8N1, no flow control, `CLOCAL | CREAD`, `HUPCL` off,
    /// reads that return at once (`VMIN` 0, `VTIME` 0; waiting is done with
    /// `poll`). Doesn't touch the modem lines.
    private static func configure(_ fd: Int32, baudRate: Int) throws {
        var settings = termios()
        guard tcgetattr(fd, &settings) == 0 else { throw SerialPortError.configureFailed(errno: errno) }
        cfmakeraw(&settings)
        settings.c_cflag |= tcflag_t(CLOCAL | CREAD | CS8)
        settings.c_cflag &= ~tcflag_t(PARENB | CSTOPB | HUPCL)
        settings.c_cflag &= ~Posix.hardwareFlowControl
        settings.c_iflag &= ~tcflag_t(IXON | IXOFF | IXANY)
        withUnsafeMutableBytes(of: &settings.c_cc) { cc in
            cc[Int(VMIN)] = 0
            cc[Int(VTIME)] = 0
        }
        let speed = Posix.speed(for: baudRate)
        _ = cfsetispeed(&settings, speed)
        _ = cfsetospeed(&settings, speed)
        guard tcsetattr(fd, TCSANOW, &settings) == 0 else { throw SerialPortError.configureFailed(errno: errno) }
    }

    deinit {
        close()
    }

    public var isOpen: Bool {
        state.withLock { !$0.closing && $0.fd >= 0 }
    }

    /// The open descriptor, counted as in use until `release()`.
    private func acquire() throws -> Int32 {
        try state.withLock { state in
            guard !state.closing, state.fd >= 0 else { throw SerialPortError.closed }
            state.users += 1
            return state.fd
        }
    }

    private func release() {
        let fdToClose: Int32? = state.withLock { state in
            state.users -= 1
            guard state.closing, state.users == 0, state.fd >= 0 else { return nil }
            defer { state.fd = -1 }
            return state.fd
        }
        if let fdToClose { finishClosing(fdToClose) }
    }

    private func finishClosing(_ fd: Int32) {
        _ = Posix.close(fd)
        _ = Posix.close(wakeRead)
        _ = Posix.close(wakeWrite)
    }

    /// Waits for `events` on `fd` (or a wake-up from `close()`). Returns the
    /// port's `revents`, or 0 on time-out. Throws `closed` when woken by `close()`.
    private func wait(_ fd: Int32, for events: Int16, until deadline: Date) throws -> Int16 {
        while true {
            let remaining = deadline.timeIntervalSinceNow
            let milliseconds = remaining <= 0 ? 0 : Int32(min(remaining * 1000, 60_000).rounded(.up))
            var fds = [
                pollfd(fd: fd, events: events, revents: 0),
                pollfd(fd: wakeRead, events: Int16(POLLIN), revents: 0),
            ]
            let result = Posix.poll(&fds, 2, milliseconds)
            if result < 0 {
                if errno == EINTR { continue }
                throw SerialPortError.ioFailed(errno: errno)
            }
            if fds[1].revents != 0 || state.withLock({ $0.closing }) { throw SerialPortError.closed }
            if result == 0 { return 0 }
            return fds[0].revents
        }
    }

    /// Writes all of `data`, waiting (with `poll`) up to `timeout` seconds for
    /// room. Throws `SerialPortError.timedOut` if it can't, so a bar that has
    /// stopped reading never blocks the app.
    public func write(_ data: Data, timeout: TimeInterval) throws {
        let fd = try acquire()
        defer { release() }
        let deadline = Date().addingTimeInterval(timeout)
        let bytes = [UInt8](data)
        var offset = 0
        while offset < bytes.count {
            let written = bytes.withUnsafeBytes { buffer in
                Posix.write(fd, buffer.baseAddress! + offset, bytes.count - offset)
            }
            if written > 0 {
                offset += written
                continue
            }
            let code = errno
            if written < 0, code == EINTR { continue }
            if written < 0, code == EAGAIN || code == EWOULDBLOCK {
                let revents = try wait(fd, for: Int16(POLLOUT), until: deadline)
                if revents == 0 { throw SerialPortError.timedOut }
                if revents & Int16(POLLHUP | POLLERR | POLLNVAL) != 0, revents & Int16(POLLOUT) == 0 {
                    throw SerialPortError.closed
                }
                continue
            }
            throw SerialPort.isGone(code) ? SerialPortError.closed : SerialPortError.ioFailed(errno: code)
        }
    }

    /// Waits (with `poll`) up to `timeout` seconds for data, then returns what
    /// is available, up to `maxBytes`. Returns empty `Data` on time-out.
    /// Throws `SerialPortError.closed` when the device has gone away.
    public func read(upTo maxBytes: Int = 4096, timeout: TimeInterval) throws -> Data {
        let fd = try acquire()
        defer { release() }
        let deadline = Date().addingTimeInterval(timeout)
        while true {
            let revents = try wait(fd, for: Int16(POLLIN), until: deadline)
            if revents == 0 { return Data() }
            if revents & Int16(POLLNVAL) != 0 { throw SerialPortError.closed }
            var buffer = [UInt8](repeating: 0, count: max(maxBytes, 1))
            let count = buffer.withUnsafeMutableBytes { Posix.read(fd, $0.baseAddress!, $0.count) }
            if count > 0 { return Data(buffer[..<count]) }
            if count == 0 { throw SerialPortError.closed }  // end of file: the device is gone
            let code = errno
            if code == EINTR { continue }
            if code == EAGAIN || code == EWOULDBLOCK {
                // poll said readable but nothing was there: a hang-up with no data left.
                if revents & Int16(POLLHUP | POLLERR) != 0 { throw SerialPortError.closed }
                continue
            }
            throw SerialPort.isGone(code) ? SerialPortError.closed : SerialPortError.ioFailed(errno: code)
        }
    }

    /// Closes the port (without touching DTR or RTS). Idempotent; a blocked
    /// `read` returns `closed`.
    public func close() {
        let fdToClose: Int32? = state.withLock { state in
            guard !state.closing else { return nil }
            state.closing = true
            guard state.users == 0, state.fd >= 0 else { return nil }
            defer { state.fd = -1 }
            return state.fd
        }
        if let fdToClose {
            finishClosing(fdToClose)
        } else if state.withLock({ $0.fd >= 0 }) {
            // A read or write is waiting in poll: wake it; the last one closes.
            var byte: UInt8 = 1
            _ = Posix.write(wakeWrite, &byte, 1)
        }
    }

    /// `errno` values that mean the device went away (unplugged, or the other
    /// end of a pseudo-terminal closed).
    private static func isGone(_ code: Int32) -> Bool {
        code == EIO || code == ENXIO || code == ENODEV || code == EBADF
    }
}

/// A pseudo-terminal pair, for testing `SerialPort` and `USBTransport` against
/// a fake bar on Linux and macOS (criterion 14). The test opens `devicePath`
/// with `SerialPort` and plays the bar on `controllerFD`.
///
/// It keeps a descriptor of its own open on the device side, so the device
/// stays present while the port under test closes and reopens it (as a
/// plugged-in bar's port would). `close()` is the bar going away.
public final class PseudoTerminal: @unchecked Sendable {
    /// The controller side's file descriptor (`posix_openpt`), where the fake bar reads and writes.
    public private(set) var controllerFD: Int32 = -1
    /// The device path of the other side (`ptsname`), for `SerialPort(path:)`.
    public private(set) var devicePath: String = ""
    /// The device side, held open (see above).
    private var deviceFD: Int32 = -1

    /// Opens a new pair (`posix_openpt`, `grantpt`, `unlockpt`, `ptsname`).
    public init() throws {
        let fd = try Posix.openPseudoTerminal()
        controllerFD = fd.controller
        devicePath = fd.devicePath
        _ = Posix.fcntl(controllerFD, F_SETFL, Posix.fcntl(controllerFD, F_GETFL, 0) | O_NONBLOCK)
        _ = Posix.fcntl(controllerFD, F_SETFD, FD_CLOEXEC)
        deviceFD = Posix.open(devicePath, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard deviceFD >= 0 else {
            let code = errno
            _ = Posix.close(controllerFD)
            throw SerialPortError.openFailed(errno: code)
        }
        _ = Posix.fcntl(deviceFD, F_SETFD, FD_CLOEXEC)
    }

    deinit {
        close()
    }

    /// Writes bytes as the bar.
    public func write(_ data: Data) throws {
        let bytes = [UInt8](data)
        var offset = 0
        let deadline = Date().addingTimeInterval(2)
        while offset < bytes.count {
            let written = bytes.withUnsafeBytes { Posix.write(controllerFD, $0.baseAddress! + offset, bytes.count - offset) }
            if written > 0 {
                offset += written
            } else if written < 0, errno == EAGAIN || errno == EINTR, Date() < deadline {
                var fds = [pollfd(fd: controllerFD, events: Int16(POLLOUT), revents: 0)]
                _ = Posix.poll(&fds, 1, 50)
            } else {
                throw SerialPortError.ioFailed(errno: errno)
            }
        }
    }

    /// Writes text as the bar.
    public func write(_ text: String) throws {
        try write(Data(text.utf8))
    }

    /// Reads what the app wrote, waiting up to `timeout` seconds. Returns
    /// empty `Data` if nothing came.
    public func read(timeout: TimeInterval) throws -> Data {
        var fds = [pollfd(fd: controllerFD, events: Int16(POLLIN), revents: 0)]
        let milliseconds = Int32(max(timeout, 0) * 1000)
        let ready = Posix.poll(&fds, 1, milliseconds)
        guard ready > 0, fds[0].revents & Int16(POLLIN) != 0 else { return Data() }
        var buffer = [UInt8](repeating: 0, count: 65_536)
        let count = buffer.withUnsafeMutableBytes { Posix.read(controllerFD, $0.baseAddress!, $0.count) }
        if count > 0 { return Data(buffer[..<count]) }
        if count < 0, errno == EAGAIN || errno == EIO { return Data() }
        if count == 0 { return Data() }
        throw SerialPortError.ioFailed(errno: errno)
    }

    /// Reads until `timeout` passes with nothing new, or until the bytes read
    /// so far end with `terminator`.
    public func read(until terminator: UInt8 = 0x0A, timeout: TimeInterval) throws -> Data {
        var collected = Data()
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            collected += try read(timeout: min(0.05, max(deadline.timeIntervalSinceNow, 0)))
            if collected.last == terminator { break }
        }
        return collected
    }

    public func close() {
        if deviceFD >= 0 { _ = Posix.close(deviceFD); deviceFD = -1 }
        if controllerFD >= 0 { _ = Posix.close(controllerFD); controllerFD = -1 }
    }
}

/// The POSIX calls, the same on macOS and Linux. Names that differ between
/// Darwin and Glibc are settled here.
enum Posix {
    #if canImport(Darwin)
    /// `_IO('t', 13)`; Swift doesn't import Darwin's function-like ioctl macros.
    static let tiocexcl: UInt = 0x2000_740D
    static let hardwareFlowControl = tcflag_t(CCTS_OFLOW | CRTS_IFLOW)
    #else
    static let tiocexcl = UInt(TIOCEXCL)
    static let hardwareFlowControl = tcflag_t(CRTSCTS)
    /// `_IOR('T', 0x30, unsigned int)` and `_IOW('T', 0x31, int)`, the same on
    /// every Linux architecture that uses the generic ioctl numbers (x86-64, arm64).
    static let tiocgptn: UInt = 0x8004_5430
    static let tiocsptlck: UInt = 0x4004_5431
    #endif

    static func speed(for baudRate: Int) -> speed_t {
        #if canImport(Darwin)
        // Darwin's speed_t is the rate itself.
        return speed_t(baudRate)
        #else
        switch baudRate {
        case 9600: return speed_t(B9600)
        case 19_200: return speed_t(B19200)
        case 38_400: return speed_t(B38400)
        case 57_600: return speed_t(B57600)
        default: return speed_t(B115200)
        }
        #endif
    }

    static func open(_ path: String, _ flags: Int32) -> Int32 {
        #if canImport(Darwin)
        return Darwin.open(path, flags)
        #else
        return Glibc.open(path, flags)
        #endif
    }

    static func close(_ fd: Int32) -> Int32 {
        #if canImport(Darwin)
        return Darwin.close(fd)
        #else
        return Glibc.close(fd)
        #endif
    }

    static func read(_ fd: Int32, _ buffer: UnsafeMutableRawPointer, _ count: Int) -> Int {
        #if canImport(Darwin)
        return Darwin.read(fd, buffer, count)
        #else
        return Glibc.read(fd, buffer, count)
        #endif
    }

    static func write(_ fd: Int32, _ buffer: UnsafeRawPointer, _ count: Int) -> Int {
        #if canImport(Darwin)
        return Darwin.write(fd, buffer, count)
        #else
        return Glibc.write(fd, buffer, count)
        #endif
    }

    static func poll(_ fds: inout [pollfd], _ count: Int, _ milliseconds: Int32) -> Int32 {
        #if canImport(Darwin)
        return Darwin.poll(&fds, nfds_t(count), milliseconds)
        #else
        return Glibc.poll(&fds, nfds_t(count), milliseconds)
        #endif
    }

    static func pipe(_ fds: inout [Int32]) -> Int32 {
        #if canImport(Darwin)
        return Darwin.pipe(&fds)
        #else
        return Glibc.pipe(&fds)
        #endif
    }

    static func fcntl(_ fd: Int32, _ command: Int32, _ value: Int32) -> Int32 {
        #if canImport(Darwin)
        return Darwin.fcntl(fd, command, value)
        #else
        return Glibc.fcntl(fd, command, value)
        #endif
    }

    static func ioctl(_ fd: Int32, _ request: UInt) -> Int32 {
        #if canImport(Darwin)
        return Darwin.ioctl(fd, request)
        #else
        return Glibc.ioctl(fd, request)
        #endif
    }

    /// Opens a pseudo-terminal's controller side, unlocked, and names its device side.
    static func openPseudoTerminal() throws -> (controller: Int32, devicePath: String) {
        #if canImport(Darwin)
        let fd = posix_openpt(O_RDWR | O_NOCTTY)
        guard fd >= 0 else { throw SerialPortError.openFailed(errno: errno) }
        guard grantpt(fd) == 0, unlockpt(fd) == 0, let name = ptsname(fd) else {
            let code = errno
            _ = Darwin.close(fd)
            throw SerialPortError.openFailed(errno: code)
        }
        return (fd, String(cString: name))
        #else
        // Glibc's module doesn't import posix_openpt and friends; these are
        // what they do on Linux.
        let fd = Glibc.open("/dev/ptmx", O_RDWR | O_NOCTTY)
        guard fd >= 0 else { throw SerialPortError.openFailed(errno: errno) }
        var unlock: Int32 = 0
        var number: UInt32 = 0
        guard Glibc.ioctl(fd, tiocsptlck, &unlock) == 0, Glibc.ioctl(fd, tiocgptn, &number) == 0 else {
            let code = errno
            _ = Glibc.close(fd)
            throw SerialPortError.openFailed(errno: code)
        }
        return (fd, "/dev/pts/\(number)")
        #endif
    }
}
