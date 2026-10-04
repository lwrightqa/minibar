import Foundation

/// How a list entry recognizes a process (docs/mac-app.md, "Which apps count").
public enum AppMatcher: Hashable, Sendable, Codable {
    /// The process's bundle ID, or its outermost app's, equals this or starts
    /// with it followed by a dot: `com.google.Chrome` matches
    /// `com.google.Chrome.helper` but not `com.google.Chromecast`.
    case bundleID(String)
    /// The executable's file name, exactly (case-sensitive). For daemons that
    /// have no bundle ID, such as `corespeechd` or `avconferenced`.
    case processName(String)
}

/// A call app: counted as a call, and the bar gets its short name (when
/// "Send the app's name" is on).
public struct CallApp: Hashable, Sendable, Codable, Identifiable {
    /// Stable ID: a short slug for the built-in entries ("slack"), the bundle
    /// ID for apps the user adds.
    public var id: String
    /// The app's name on this Mac, for Settings ("zoom.us", "Google Chrome").
    public var displayName: String
    /// What the bar shows, 1 to 24 printable ASCII characters ("Zoom", "Chrome").
    /// See `AppNameRules`.
    public var shortName: String
    public var matchers: [AppMatcher]

    public init(id: String, displayName: String, shortName: String, matchers: [AppMatcher]) {
        self.id = id
        self.displayName = displayName
        self.shortName = shortName
        self.matchers = matchers
    }
}

/// An ignored app: never starts or prolongs a call.
public struct IgnoredApp: Hashable, Sendable, Codable, Identifiable {
    public var id: String
    public var displayName: String
    public var matchers: [AppMatcher]
    /// Built-in system entries (Siri, Dictation, Voice Control) show a system
    /// icon in Settings, since they aren't apps you can pick in Finder.
    public var isSystem: Bool

    public init(id: String, displayName: String, matchers: [AppMatcher], isSystem: Bool = false) {
        self.id = id
        self.displayName = displayName
        self.matchers = matchers
        self.isSystem = isSystem
    }
}

/// An app as the Mac sees it, for the menu, the "Used the mic since TinyBar
/// opened" list and "Don't Count …". Never sent to the bar.
public struct AppIdentity: Hashable, Sendable, Codable {
    /// Stable key: the matching list entry's `id`; else the outer app's bundle
    /// ID; else the process's bundle ID; else the process name; else `pid:<n>`.
    public var key: String
    /// For the menu: "Slack", "GarageBand", "Siri".
    public var displayName: String
    /// The bundle ID to put in a new list entry (outer app's first), if known.
    public var bundleID: String?
    /// The process name, for a new entry when there's no bundle ID.
    public var processName: String?

    public init(key: String, displayName: String, bundleID: String? = nil, processName: String? = nil) {
        self.key = key
        self.displayName = displayName
        self.bundleID = bundleID
        self.processName = processName
    }
}

/// Which group an app falls into. When an app matches both lists, Ignored wins.
public enum AppCategory: Hashable, Sendable {
    /// Counted; the bar may get `shortName`.
    case callApp(shortName: String)
    /// Never counted.
    case ignored
    /// Counted without a name in "Any app except ignored ones", not counted in
    /// "Only the call apps on my list".
    case other
}

/// A process matched to an app.
public struct AppClassification: Hashable, Sendable {
    public var identity: AppIdentity
    public var category: AppCategory

    public init(identity: AppIdentity, category: AppCategory) {
        self.identity = identity
        self.category = category
    }
}

/// A change to the lists that "Count … Again" can undo (mac-app-ux.md 4.5).
public struct CatalogEdit: Hashable, Sendable {
    public var catalogBefore: AppCatalog
    public var app: AppIdentity

    public init(catalogBefore: AppCatalog, app: AppIdentity) {
        self.catalogBefore = catalogBefore
        self.app = app
    }
}

/// The call-app and ignored-app lists (Settings › Apps), and matching
/// processes against them.
public struct AppCatalog: Hashable, Sendable, Codable {
    public var callApps: [CallApp]
    public var ignoredApps: [IgnoredApp]

    public init(callApps: [CallApp], ignoredApps: [IgnoredApp]) {
        self.callApps = callApps
        self.ignoredApps = ignoredApps
    }

    /// Matches a process to an app (docs/mac-app.md, "Which apps count"):
    /// 1. its bundle ID against both lists (exact, or prefix and a dot);
    /// 2. if neither list matches, the outermost app's bundle ID, then the
    ///    process name;
    /// 3. otherwise an unknown app (`.other`), named by `displayName`, else
    ///    its bundle ID or process name.
    /// Ignored wins when both lists match.
    ///
    /// Bundle IDs compare without regard to case, as macOS treats them.
    public func classify(_ process: MicProcess) -> AppClassification {
        let processName = process.processName
        let stages: [(bundleID: String?, processName: String?)] = [
            (process.bundleID.nonEmpty, nil),
            (process.outerAppBundleID.nonEmpty, nil),
            (nil, processName.nonEmpty),
        ]
        for stage in stages where stage.bundleID != nil || stage.processName != nil {
            let ignored = ignoredApps.first { $0.matchers.contains { $0.matches(bundleID: stage.bundleID, processName: stage.processName) } }
            if let ignored {
                return AppClassification(
                    identity: identity(key: ignored.id, displayName: ignored.displayName, process: process),
                    category: .ignored
                )
            }
            let callApp = callApps.first { $0.matchers.contains { $0.matches(bundleID: stage.bundleID, processName: stage.processName) } }
            if let callApp {
                return AppClassification(
                    identity: identity(key: callApp.id, displayName: callApp.displayName, process: process),
                    category: .callApp(shortName: callApp.shortName)
                )
            }
        }
        let bundleID = process.outerAppBundleID.nonEmpty ?? process.bundleID.nonEmpty
        let key = bundleID ?? processName.nonEmpty ?? "pid:\(process.pid)"
        let name = process.displayName.nonEmpty ?? bundleID ?? processName.nonEmpty ?? "Process \(process.pid)"
        return AppClassification(identity: identity(key: key, displayName: name, process: process), category: .other)
    }

    private func identity(key: String, displayName: String, process: MicProcess) -> AppIdentity {
        AppIdentity(
            key: key,
            displayName: displayName,
            bundleID: process.outerAppBundleID.nonEmpty ?? process.bundleID.nonEmpty,
            processName: process.processName.nonEmpty
        )
    }

    /// The group an app is in now, by its identity's key (for an app seen
    /// earlier, after the lists changed). `nil` when the key isn't on either
    /// list, which makes it an `.other` app.
    public func category(ofKey key: String) -> AppCategory? {
        if ignoredApps.contains(where: { $0.id == key }) { return .ignored }
        if let app = callApps.first(where: { $0.id == key }) { return .callApp(shortName: app.shortName) }
        return nil
    }

    /// Stops counting an app ("Don't Count Slack"): a call app moves to the
    /// ignored list, any other app is added to it. Returns the edit, for
    /// "Count Slack Again". Doing it twice changes nothing the second time.
    ///
    /// An app with neither a bundle ID nor a process name can't be listed,
    /// so nothing changes for it.
    @discardableResult
    public mutating func ignore(_ app: AppIdentity) -> CatalogEdit {
        let edit = CatalogEdit(catalogBefore: self, app: app)
        if ignoredApps.contains(where: { $0.id == app.key }) {
            callApps.removeAll { $0.id == app.key }
            return edit
        }
        if let index = callApps.firstIndex(where: { $0.id == app.key }) {
            let callApp = callApps.remove(at: index)
            ignoredApps.append(IgnoredApp(id: callApp.id, displayName: callApp.displayName, matchers: callApp.matchers))
            return edit
        }
        let matchers = AppCatalog.matchers(for: app)
        guard !matchers.isEmpty else { return edit }
        ignoredApps.append(IgnoredApp(id: app.key, displayName: app.displayName, matchers: matchers))
        return edit
    }

    /// Puts the lists back as they were before `edit` ("Count Slack Again").
    /// Only the entries for `edit.app` change; other edits made since stay.
    public mutating func undo(_ edit: CatalogEdit) {
        let key = edit.app.key
        AppCatalog.restore(key: key, in: &callApps, from: edit.catalogBefore.callApps)
        AppCatalog.restore(key: key, in: &ignoredApps, from: edit.catalogBefore.ignoredApps)
    }

    /// Makes the entry with `key` in `list` what it was in `before`: put back
    /// at its old place if it was there, removed if it wasn't.
    private static func restore<Entry: Identifiable>(key: String, in list: inout [Entry], from before: [Entry])
    where Entry.ID == String {
        list.removeAll { $0.id == key }
        guard let oldIndex = before.firstIndex(where: { $0.id == key }) else { return }
        // Put it back after the entries that came before it then and are still here.
        let earlierIDs = Set(before[..<oldIndex].map(\.id))
        let insertAt = (list.lastIndex { earlierIDs.contains($0.id) }).map { $0 + 1 } ?? 0
        list.insert(before[oldIndex], at: insertAt)
    }

    /// The matcher a new list entry gets for an app seen using the mic.
    static func matchers(for app: AppIdentity) -> [AppMatcher] {
        if let bundleID = app.bundleID, !bundleID.isEmpty { return [.bundleID(bundleID)] }
        if let processName = app.processName, !processName.isEmpty { return [.processName(processName)] }
        return []
    }

    /// Adds a call app (Settings › Apps › +, or "Add to Call Apps"). If the
    /// app is on the ignored list, it's moved (the UI asks first). Throws
    /// `SettingsError.badAppName` for a bad short name.
    ///
    /// The short name is trimmed of spaces. An entry with the same `id` is
    /// replaced in place.
    public mutating func addCallApp(_ app: CallApp) throws(SettingsError) {
        var app = app
        if let problem = NameRules.appNameProblem(app.shortName) { throw .badAppName(problem) }
        app.shortName = NameRules.trimmed(app.shortName)
        ignoredApps.removeAll { $0.id == app.id || $0.matchers.contains(where: app.matchers.contains) }
        if let index = callApps.firstIndex(where: { $0.id == app.id }) {
            callApps[index] = app
        } else {
            callApps.append(app)
        }
    }

    /// Changes the short name a call app sends. An empty name puts back the
    /// app's default (mac-app-ux.md 6.3): the built-in short name for the
    /// built-in apps, else the app's own name if the bar can show it.
    /// An unknown `callAppID` changes nothing.
    public mutating func rename(callAppID: String, to shortName: String) throws(SettingsError) {
        guard let index = callApps.firstIndex(where: { $0.id == callAppID }) else { return }
        var name = NameRules.trimmed(shortName)
        if name.isEmpty {
            if let builtIn = AppCatalog.defaults.callApps.first(where: { $0.id == callAppID }) {
                name = builtIn.shortName
            } else {
                name = NameRules.trimmed(callApps[index].displayName)
            }
        }
        if let problem = NameRules.appNameProblem(name) { throw .badAppName(problem) }
        callApps[index].shortName = name
    }

    public mutating func removeCallApp(id: String) {
        callApps.removeAll { $0.id == id }
    }

    /// Adds an ignored app. If the app is on the call-app list, it's moved
    /// (the UI asks first). An entry with the same `id` is replaced in place.
    public mutating func addIgnoredApp(_ app: IgnoredApp) {
        callApps.removeAll { $0.id == app.id || $0.matchers.contains(where: app.matchers.contains) }
        if let index = ignoredApps.firstIndex(where: { $0.id == app.id }) {
            ignoredApps[index] = app
        } else {
            ignoredApps.append(app)
        }
    }

    public mutating func removeIgnoredApp(id: String) {
        ignoredApps.removeAll { $0.id == id }
    }
}

extension AppMatcher {
    /// Whether this matcher recognizes a bundle ID or a process name.
    func matches(bundleID: String?, processName: String?) -> Bool {
        switch self {
        case .bundleID(let listed):
            guard let bundleID, !listed.isEmpty else { return false }
            let candidate = bundleID.lowercased()
            let listed = listed.lowercased()
            return candidate == listed || candidate.hasPrefix(listed + ".")
        case .processName(let listed):
            guard let processName, !listed.isEmpty else { return false }
            return processName == listed
        }
    }
}

extension AppCatalog {
    /// The default lists (docs/mac-app.md, **Proposed**).
    ///
    /// **Unverified:** the bundle IDs are the ones the apps are known to use,
    /// and the system processes behind Siri, Dictation, Voice Control and
    /// FaceTime audio are educated guesses. All must be checked on macOS 14,
    /// 15 and 26 (docs/mac-app.md, "Unverified until it runs on a Mac").
    public static let defaults = AppCatalog(
        callApps: [
            CallApp(id: "slack", displayName: "Slack", shortName: "Slack",
                    matchers: [.bundleID("com.tinyspeck.slackmacgap")]),
            CallApp(id: "zoom", displayName: "zoom.us", shortName: "Zoom",
                    matchers: [.bundleID("us.zoom.xos")]),
            CallApp(id: "teams", displayName: "Microsoft Teams", shortName: "Teams",
                    matchers: [.bundleID("com.microsoft.teams2"), .bundleID("com.microsoft.teams")]),
            // FaceTime audio is expected to run in avconferenced (to confirm).
            CallApp(id: "facetime", displayName: "FaceTime", shortName: "FaceTime",
                    matchers: [.bundleID("com.apple.FaceTime"), .processName("avconferenced")]),
            CallApp(id: "webex", displayName: "Webex", shortName: "Webex",
                    matchers: [.bundleID("Cisco-Systems.Spark")]),
            CallApp(id: "discord", displayName: "Discord", shortName: "Discord",
                    matchers: [.bundleID("com.hnc.Discord")]),
            CallApp(id: "whatsapp", displayName: "WhatsApp", shortName: "WhatsApp",
                    matchers: [.bundleID("net.whatsapp.WhatsApp")]),
            CallApp(id: "signal", displayName: "Signal", shortName: "Signal",
                    matchers: [.bundleID("org.whispersystems.signal-desktop")]),
            CallApp(id: "chrome", displayName: "Google Chrome", shortName: "Chrome",
                    matchers: [.bundleID("com.google.Chrome")]),
            // Safari's media runs in WebKit's shared GPU process
            // (com.apple.WebKit.GPU), which other apps use too, so it isn't
            // listed here: until that can be told apart, Safari calls count
            // without a name.
            CallApp(id: "safari", displayName: "Safari", shortName: "Safari",
                    matchers: [.bundleID("com.apple.Safari")]),
            CallApp(id: "firefox", displayName: "Firefox", shortName: "Firefox",
                    matchers: [.bundleID("org.mozilla.firefox")]),
            CallApp(id: "edge", displayName: "Microsoft Edge", shortName: "Edge",
                    matchers: [.bundleID("com.microsoft.edgemac")]),
            CallApp(id: "arc", displayName: "Arc", shortName: "Arc",
                    matchers: [.bundleID("company.thebrowser.Browser")]),
            CallApp(id: "brave", displayName: "Brave Browser", shortName: "Brave",
                    matchers: [.bundleID("com.brave.Browser")]),
        ],
        ignoredApps: [
            IgnoredApp(id: "siri", displayName: "Siri",
                       matchers: [.bundleID("com.apple.Siri"), .processName("Siri"),
                                  .processName("assistantd"), .processName("corespeechd")],
                       isSystem: true),
            IgnoredApp(id: "dictation", displayName: "Dictation",
                       matchers: [.bundleID("com.apple.SpeechRecognitionCore")],
                       isSystem: true),
            IgnoredApp(id: "voicecontrol", displayName: "Voice Control",
                       matchers: [.bundleID("com.apple.VoiceControl"), .processName("VoiceControl")],
                       isSystem: true),
            IgnoredApp(id: "voicememos", displayName: "Voice Memos",
                       matchers: [.bundleID("com.apple.VoiceMemos")]),
        ]
    )
}
