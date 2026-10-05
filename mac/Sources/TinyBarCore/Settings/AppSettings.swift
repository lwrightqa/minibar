import Foundation

/// Why a setting was refused.
public enum SettingsError: Error, Hashable, Sendable {
    case startDelayOutOfRange(Int)
    case endDelayOutOfRange(Int)
    case badAppName(NameProblem)
    case badMacName(NameProblem)
    case badAddress(String)
}

/// The bar this Mac is paired with (or talks to without pairing). Saved in
/// the settings; the token itself is in the Keychain (`TokenStore`).
public struct KnownBar: Hashable, Sendable, Codable {
    /// `device_id`. The Keychain account for its token.
    public var deviceID: String
    /// "MiniBar 2A1C", from the latest `info`, `hello` or pairing reply.
    public var name: String
    /// The bar's mDNS name, "minibar.local", if it has Wi-Fi.
    public var host: String?
    /// The last Wi-Fi address that worked (api.md 3: a fallback for networks
    /// where multicast is unreliable).
    public var lastEndpoint: BarEndpoint?
    /// `"bearer"` or `"none"`, from the latest `info`.
    public var auth: AuthMode
    /// The public ID of this Mac's token, if paired.
    public var tokenID: String?

    public init(deviceID: String, name: String, host: String? = nil, lastEndpoint: BarEndpoint? = nil,
                auth: AuthMode = .bearer, tokenID: String? = nil) {
        self.deviceID = deviceID
        self.name = name
        self.host = host
        self.lastEndpoint = lastEndpoint
        self.auth = auth
        self.tokenID = tokenID
    }
}

/// Everything the app remembers between launches, apart from tokens.
/// Saved in UserDefaults. Holds settings and lists only: no record of which
/// apps used the mic or when (criterion 32).
public struct AppSettings: Hashable, Sendable, Codable {
    /// `client`: made once per install.
    public var installID: String
    public var detection: DetectionSettings
    /// Start MiniBar when you log in (the wish; `LoginItemService` says what
    /// macOS actually did).
    public var launchAtLogin: Bool
    /// Use Wi-Fi when MiniBar isn't plugged in. Off: USB only, no browsing,
    /// no Local Network prompt (mac-app-ux.md 6.4).
    public var useWiFi: Bool
    /// Advanced › Address. `nil`: find the bar automatically.
    public var manualAddress: String?
    /// Advanced › Name for this Mac, 1 to 32 characters, sent as `name`.
    /// `nil`: send none (the bar says "Mac").
    public var macName: String?
    /// The paired bar.
    public var bar: KnownBar?
    /// A pause survives a relaunch (an "Until I Resume" pause especially).
    public var pause: PauseState
    /// The Welcome window has been shown (it opens by itself only once).
    public var didShowWelcome: Bool
    /// The Local Network explanation has been shown (mac-app-ux.md 5.4, step 1).
    public var didExplainLocalNetwork: Bool

    public init(
        installID: String,
        detection: DetectionSettings = .defaults,
        launchAtLogin: Bool = true,
        useWiFi: Bool = true,
        manualAddress: String? = nil,
        macName: String? = nil,
        bar: KnownBar? = nil,
        pause: PauseState = .notPaused,
        didShowWelcome: Bool = false,
        didExplainLocalNetwork: Bool = false
    ) {
        self.installID = installID
        self.detection = detection
        self.launchAtLogin = launchAtLogin
        self.useWiFi = useWiFi
        self.manualAddress = manualAddress
        self.macName = macName
        self.bar = bar
        self.pause = pause
        self.didShowWelcome = didShowWelcome
        self.didExplainLocalNetwork = didExplainLocalNetwork
    }

    /// First-launch settings with a new install ID.
    public static func firstLaunch() -> AppSettings {
        AppSettings(installID: Identifiers.newInstallID())
    }

    /// Checks every value (detection ranges, names, the address).
    public func validate() throws(SettingsError) {
        try detection.validate()
        if let macName, let problem = NameRules.macNameProblem(macName) { throw .badMacName(problem) }
        if let manualAddress {
            let trimmed = manualAddress.trimmingCharacters(in: .whitespacesAndNewlines)
            if !trimmed.isEmpty, BarEndpoint(userInput: trimmed) == nil { throw .badAddress(manualAddress) }
        }
    }

    /// Settings › Advanced › Address, parsed. `nil` when empty (automatic)
    /// or not an address.
    public var manualEndpoint: BarEndpoint? {
        guard let manualAddress else { return nil }
        return BarEndpoint(userInput: manualAddress)
    }

    /// The name sent for this Mac: `nil` when empty, trimmed otherwise.
    public var macNameToSend: String? {
        guard let macName else { return nil }
        let trimmed = NameRules.trimmed(macName)
        return trimmed.isEmpty ? nil : trimmed
    }

    /// Restore Defaults on the General tab: launch at login, the delays, the
    /// camera and the name switches. Lists, the bar and the install ID stay.
    public mutating func restoreGeneralDefaults() {
        let defaults = DetectionSettings.defaults
        launchAtLogin = true
        detection.startDelay = defaults.startDelay
        detection.endDelay = defaults.endDelay
        detection.countCamera = defaults.countCamera
        detection.sendAppName = defaults.sendAppName
    }

    /// Restore Defaults on the Apps tab: the lists and the counting mode.
    public mutating func restoreAppsDefaults() {
        detection.catalog = DetectionSettings.defaults.catalog
        detection.mode = DetectionSettings.defaults.mode
    }

    /// Restore Defaults on the Connection tab: Wi-Fi on, Advanced cleared. The
    /// bar isn't forgotten (mac-app-ux.md 6.4).
    public mutating func restoreConnectionDefaults() {
        useWiFi = true
        manualAddress = nil
        macName = nil
    }

    enum CodingKeys: String, CodingKey {
        case installID, detection, launchAtLogin, useWiFi, manualAddress, macName, bar, pause
        case didShowWelcome, didExplainLocalNetwork
    }

    /// Reads saved settings. A field a newer version added gets its default;
    /// only the install ID must be there (without it, they aren't this app's).
    public init(from decoder: any Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        installID = try container.decode(String.self, forKey: .installID)
        detection = try container.decodeIfPresent(DetectionSettings.self, forKey: .detection) ?? .defaults
        launchAtLogin = try container.decodeIfPresent(Bool.self, forKey: .launchAtLogin) ?? true
        useWiFi = try container.decodeIfPresent(Bool.self, forKey: .useWiFi) ?? true
        manualAddress = try container.decodeIfPresent(String.self, forKey: .manualAddress)
        macName = try container.decodeIfPresent(String.self, forKey: .macName)
        bar = try? container.decodeIfPresent(KnownBar.self, forKey: .bar)
        pause = (try? container.decodeIfPresent(PauseState.self, forKey: .pause)) ?? .notPaused
        didShowWelcome = try container.decodeIfPresent(Bool.self, forKey: .didShowWelcome) ?? false
        didExplainLocalNetwork = try container.decodeIfPresent(Bool.self, forKey: .didExplainLocalNetwork) ?? false
    }
}
