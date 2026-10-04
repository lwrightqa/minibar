import Foundation

/// Where `AppSettings` live between launches.
public protocol SettingsStore: AnyObject, Sendable {
    /// The saved settings, or `nil` on first launch (or if they can't be read,
    /// in which case the app starts fresh rather than crash).
    func load() -> AppSettings?
    func save(_ settings: AppSettings) throws
}

/// Settings in memory, for tests and previews.
public final class InMemorySettingsStore: SettingsStore, @unchecked Sendable {
    private let stored: Locked<AppSettings?>

    public init(_ settings: AppSettings? = nil) {
        stored = Locked(settings)
    }

    public func load() -> AppSettings? {
        stored.get()
    }

    public func save(_ settings: AppSettings) throws {
        stored.set(settings)
    }
}

/// Settings in `UserDefaults`, as one JSON value under `key`, so a new
/// version can add fields with defaults. Builds on Linux too, so it's tested
/// there (against a `UserDefaults(suiteName:)`).
public final class UserDefaultsSettingsStore: SettingsStore, @unchecked Sendable {
    public static let defaultKey = "TinyBarSettings.v1"

    private let defaults: UserDefaults
    private let key: String

    public init(defaults: UserDefaults = .standard, key: String = UserDefaultsSettingsStore.defaultKey) {
        self.defaults = defaults
        self.key = key
    }

    public func load() -> AppSettings? {
        unimplemented()
    }

    public func save(_ settings: AppSettings) throws {
        unimplemented()
    }
}
