import Foundation

/// Constants from docs/api.md, the contract between the app and the bar's
/// firmware. When the contract changes, change it here and in the tests.
public enum TinyBarAPI {
    /// The API version this app speaks (api.md 2.1). Sent in `hello`.
    public static let version = "1.0"

    /// Every endpoint is under this path (api.md 2.1).
    public static let basePath = "/api/v1"

    /// The `User-Agent` for HTTP requests (api.md 2.1): `TinyBarMac/1.0 (12) (api 1.0)`.
    /// It replaces URLSession's default, which would add the macOS and Darwin
    /// versions (docs/mac-app.md, Wi-Fi).
    public static func userAgent(appVersion: String) -> String {
        "TinyBarMac/\(appVersion) (api \(version))"
    }

    /// Defaults the bar starts with; the app always uses the values from the
    /// latest reply (api.md 5.3).
    public enum Defaults {
        public static let heartbeatSeconds = 30
        public static let timeoutSeconds = 90
    }

    /// Client time-outs (api.md 2.6, 6.2, 6.8).
    public enum Timeouts {
        /// An HTTP request.
        public static let http: TimeInterval = 5
        /// A USB reply.
        public static let usbReply: TimeInterval = 3
        /// Between `hello`s while finding out whether a port is a TinyBar.
        public static let helloInterval: TimeInterval = 2
        /// Give up on a port that hasn't answered `hello` by then.
        public static let helloGiveUp: TimeInterval = 10
        /// USB replies missed in a row before the app closes the port.
        public static let usbMissedRepliesBeforeClose = 3
        /// Reopen a closed port after this long if IOKit hasn't reported it again.
        public static let usbReopenAfter: TimeInterval = 10
    }

    /// Size and length limits (api.md 2.5).
    public enum Limits {
        public static let httpBodyBytes = 2048
        /// A USB line from the Mac, including the `@tb ` marker, not counting the line ending.
        public static let usbLineOutBytes = 2048
        /// A USB line from the bar, and any response body.
        public static let usbLineInBytes = 8192
        /// `app` in a call message, in UTF-8 bytes.
        public static let appNameBytes = 1...64
        /// How many characters of `app` the bar shows.
        public static let appNameShownCharacters = 24
        /// `name` in pairing and `hello`.
        public static let clientNameCharacters = 1...32
        /// `elapsed_s` in a call message.
        public static let elapsedSeconds = 0...86_400
    }

    /// The bar's USB Serial/JTAG port (api.md 6.1).
    public enum USB {
        /// Espressif.
        public static let vendorID = 0x303A
        /// The ESP32-S3's USB Serial/JTAG controller.
        public static let productID = 0x1001
        /// Doesn't matter for USB Serial/JTAG, but something has to be set (api.md 6.3).
        public static let baudRate = 115_200
    }

    /// Bonjour and the default address (api.md 3).
    public enum Bonjour {
        public static let serviceType = "_tinybar._tcp"
        public static let domain = "local."
        public static let defaultHost = "tinybar.local"
        public static let port = 80
        /// TXT record keys.
        public enum TXT {
            public static let api = "api"
            public static let deviceID = "id"
            public static let firmware = "fw"
            public static let path = "path"
            public static let auth = "auth"
        }
    }

    /// Where the token is kept (api.md 16): one generic-password item per bar,
    /// service `TinyBar`, account = `device_id`.
    public enum Keychain {
        public static let service = "TinyBar"
    }
}

/// An API version string such as `"1.0"` (api.md 2.1). Clients and bars with
/// the same major version can talk; minor versions only add things.
public struct APIVersion: Sendable, Hashable, Comparable, CustomStringConvertible {
    public let major: Int
    public let minor: Int

    public init(major: Int, minor: Int) {
        self.major = major
        self.minor = minor
    }

    /// Parses `"1.0"`. Returns `nil` for anything that isn't `<int>.<int>`.
    public init?(_ string: String) {
        let parts = string.split(separator: ".", omittingEmptySubsequences: false)
        guard parts.count == 2, let major = Int(parts[0]), let minor = Int(parts[1]), major >= 0, minor >= 0 else {
            return nil
        }
        self.init(major: major, minor: minor)
    }

    /// The version this app speaks.
    public static let current = APIVersion(TinyBarAPI.version)!

    /// Whether the app can talk to a bar that speaks `self`: same major version.
    public var isCompatibleWithApp: Bool {
        major == APIVersion.current.major
    }

    public var description: String { "\(major).\(minor)" }

    public static func < (lhs: APIVersion, rhs: APIVersion) -> Bool {
        (lhs.major, lhs.minor) < (rhs.major, rhs.minor)
    }
}
