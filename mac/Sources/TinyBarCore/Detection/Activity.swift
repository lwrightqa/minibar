import Foundation

/// One process that has audio input running right now, as the macOS mic
/// monitor reports it (CoreAudio's process objects, docs/mac-app.md "Signals").
///
/// The monitor fills in what it can; the core matches it to an app
/// (`AppCatalog.classify`). Nothing here ever leaves the Mac.
public struct MicProcess: Hashable, Sendable {
    /// `kAudioProcessPropertyPID`.
    public var pid: Int32
    /// `kAudioProcessPropertyBundleID`. Often a helper's own ID
    /// (`com.google.Chrome.helper`); `nil` or empty for most daemons.
    public var bundleID: String?
    /// The executable's path, from `proc_pidpath`. `nil` if macOS won't say.
    public var executablePath: String?
    /// The bundle ID of the outermost `.app` in `executablePath`
    /// (`AppPaths.outermostAppPath`), read from its Info.plist by the monitor.
    /// This maps helper processes to their app (docs/mac-app.md, "Which apps count").
    public var outerAppBundleID: String?
    /// A name for the menu ("Slack", "GarageBand"): the outer app's display
    /// name, else the process's own. Only ever shown on this Mac.
    public var displayName: String?

    public init(
        pid: Int32,
        bundleID: String? = nil,
        executablePath: String? = nil,
        outerAppBundleID: String? = nil,
        displayName: String? = nil
    ) {
        self.pid = pid
        self.bundleID = bundleID
        self.executablePath = executablePath
        self.outerAppBundleID = outerAppBundleID
        self.displayName = displayName
    }

    /// The executable's file name (`corespeechd`), for matching daemons that
    /// have no bundle ID.
    public var processName: String? {
        guard let executablePath, let last = executablePath.split(separator: "/").last else { return nil }
        return String(last)
    }

    /// A key that stays the same for one app's processes across snapshots,
    /// for timing how long it has been using the mic: the outer app's bundle
    /// ID, else the process's bundle ID, else its path, else its PID.
    var activityKey: String {
        if let id = outerAppBundleID.nonEmpty { return "app:" + id.lowercased() }
        if let id = bundleID.nonEmpty { return "bundle:" + id.lowercased() }
        if let path = executablePath.nonEmpty { return "path:" + path }
        return "pid:\(pid)"
    }
}

extension Optional where Wrapped == String {
    /// The string, or `nil` when it's `nil` or empty.
    var nonEmpty: String? {
        guard let self, !self.isEmpty else { return nil }
        return self
    }
}

/// What the Mac's inputs are doing at one moment. The detector is fed one of
/// these whenever anything changes, and at least once a second (the monitors'
/// safety-net re-read).
public struct ActivitySnapshot: Hashable, Sendable {
    /// Every process with audio input running. Order doesn't matter.
    public var mic: [MicProcess]
    /// Any camera is running (`kCMIODevicePropertyDeviceIsRunningSomewhere`).
    /// macOS doesn't say which app, so camera use never has a name.
    public var cameraInUse: Bool

    public init(mic: [MicProcess] = [], cameraInUse: Bool = false) {
        self.mic = mic
        self.cameraInUse = cameraInUse
    }

    /// Nothing in use.
    public static let idle = ActivitySnapshot()
}

/// Path helpers for matching helper processes to their app.
public enum AppPaths {
    /// The outermost `.app` bundle in an executable's path, or `nil`:
    /// `/Applications/Slack.app/Contents/Frameworks/Slack Helper.app/Contents/MacOS/Slack Helper`
    /// → `/Applications/Slack.app` (docs/mac-app.md criterion 4).
    ///
    /// A path component counts only if it ends in `.app` exactly
    /// (case-insensitive), so `Foo.application` doesn't.
    public static func outermostAppPath(_ executablePath: String) -> String? {
        var components: [Substring] = []
        for component in executablePath.split(separator: "/", omittingEmptySubsequences: false) {
            components.append(component)
            // More than ".app" itself, ending in ".app" in any case.
            if component.utf8.count > 4, component.lowercased().hasSuffix(".app") {
                return components.joined(separator: "/")
            }
        }
        return nil
    }

    /// Folders macOS guards with a Files & Folders prompt for an app that
    /// isn't sandboxed: Desktop, Documents, Downloads, iCloud Drive, cloud
    /// storage providers, and other volumes. Reading an app's Info.plist in
    /// one of them would make MiniBar ask, so its name and bundle ID come from
    /// LaunchServices instead.
    public static func isInProtectedFolder(_ path: String, home: String) -> Bool {
        let home = home.hasSuffix("/") ? String(home.dropLast()) : home
        let folders = ["Desktop", "Documents", "Downloads", "Library/Mobile Documents", "Library/CloudStorage"]
            .map { home + "/" + $0 + "/" } + ["/Volumes/"]
        return folders.contains { path.hasPrefix($0) }
    }
}
