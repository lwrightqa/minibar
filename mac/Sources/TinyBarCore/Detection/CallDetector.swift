import Foundation

/// A call as the Mac decided it (after the start delay).
public struct DetectedCall: Hashable, Sendable {
    /// `call_id`: 1 for the first call of a session, then +1 for each new call.
    public var callID: UInt32
    /// When the call started, which is when `active: true` is first sent (the
    /// end of the start delay). `elapsed_s` counts from here.
    public var startedAt: Date
    /// The short name to send as `app`, or `nil`: camera only, an app not on
    /// the call-app list, or "Send the app's name" off.
    public var app: String?
    /// The name the menu shows: "Slack", "GarageBand", or "Camera" for the
    /// camera alone. Never sent.
    public var localName: String
    /// The app holding the call, for "Don't Count …"; `nil` for the camera alone.
    public var identity: AppIdentity?

    public init(callID: UInt32, startedAt: Date, app: String?, localName: String, identity: AppIdentity?) {
        self.callID = callID
        self.startedAt = startedAt
        self.app = app
        self.localName = localName
        self.identity = identity
    }

    /// `elapsed_s` at `now`, whole seconds, clamped to 0…86400 (api.md 5.1).
    public func elapsedSeconds(at now: Date) -> Int {
        min(max(Int(now.timeIntervalSince(startedAt).rounded(.down)), 0), TinyBarAPI.Limits.elapsedSeconds.upperBound)
    }
}

/// What changed, for the connection to send at once.
public enum DetectorEvent: Hashable, Sendable {
    /// A call started: send `active: true`.
    case started(DetectedCall)
    /// The call's name changed (Chrome, then Zoom with no gap): send one
    /// `active: true` with the new name, same `call_id` (criterion 8).
    case changed(DetectedCall)
    /// The call ended: send `active: false`.
    case ended(DetectedCall)
}

/// One app using the mic, as line 1 of the menu describes it.
public struct MicUse: Hashable, Sendable {
    public var classification: AppClassification
    /// Counted under the current mode and lists.
    public var counted: Bool
    /// When this app was first seen using the mic in its current stretch.
    public var since: Date

    public init(classification: AppClassification, counted: Bool, since: Date) {
        self.classification = classification
        self.counted = counted
        self.since = since
    }
}

/// What the Mac sees right now: the menu's line 1 and its counting items.
public struct MacObservation: Hashable, Sendable {
    /// Apps using the mic, most recent first.
    public var micUses: [MicUse]
    public var cameraInUse: Bool
    /// Camera use counts under the current settings.
    public var cameraCounted: Bool
    /// Counted use has been going since this time, and the start delay hasn't
    /// passed yet ("Mic in use by Slack").
    public var pendingSince: Date?
    /// During a call, counted use stopped at this time, and the end delay
    /// hasn't passed yet.
    public var idleSince: Date?

    public init(micUses: [MicUse] = [], cameraInUse: Bool = false, cameraCounted: Bool = false,
                pendingSince: Date? = nil, idleSince: Date? = nil) {
        self.micUses = micUses
        self.cameraInUse = cameraInUse
        self.cameraCounted = cameraCounted
        self.pendingSince = pendingSince
        self.idleSince = idleSince
    }

    public static let idle = MacObservation()
}

/// An app seen using the mic since MiniBar opened (Settings › Apps). Kept in
/// memory only (criterion 28).
public struct SeenApp: Hashable, Sendable {
    public var identity: AppIdentity
    public var category: AppCategory
    public var lastSeen: Date

    public init(identity: AppIdentity, category: AppCategory, lastSeen: Date) {
        self.identity = identity
        self.category = category
        self.lastSeen = lastSeen
    }
}

/// Decides when mic and camera use is a call (docs/mac-app.md, "How it
/// decides you're on a call"; criteria 1 to 11).
///
/// Pure logic: it never reads the clock or the hardware. The engine feeds it
/// snapshots and the time, calls `tick(at:)` at `nextDeadline`, and sends the
/// events it returns.
///
/// Rules, with the defaults:
/// - Counted activity (mic from a counted app, or the camera when it counts)
///   that lasts `startDelay` seconds without a break starts a call; at 0 the
///   first snapshot with activity starts it.
/// - During a call, `endDelay` seconds without counted activity end it. A gap
///   shorter than that is bridged (AirPods switching).
/// - Name: a call app beats other apps; among call apps the one that started
///   using the mic most recently wins. A name change sends `.changed`, not an
///   end and a new start. A call carrying on with the camera alone keeps the
///   last name.
/// - A settings change, `ignore`, or a pause that leaves nothing counted ends
///   the call at once, with no end delay (criterion 9, 10).
/// - When a pause ends, activity already going counts after the start delay,
///   measured from when the pause ended.
public struct CallDetector: Sendable {
    public private(set) var settings: DetectionSettings
    public private(set) var pause: PauseState
    /// The current call, if any.
    public private(set) var call: DetectedCall?
    public private(set) var observation: MacObservation
    /// Apps seen using the mic since launch, most recent first.
    public private(set) var seenApps: [SeenApp]

    /// - Parameter firstCallID: the `call_id` of the first call.
    public init(settings: DetectionSettings = .defaults, pause: PauseState = .notPaused, firstCallID: UInt32 = 1) {
        self.settings = settings
        self.pause = pause
        self.call = nil
        self.observation = .idle
        self.seenApps = []
        self.nextCallID = max(firstCallID, 1)
    }

    private var nextCallID: UInt32

    /// The latest snapshot.
    private var snapshot = ActivitySnapshot.idle
    /// When each app (by `MicProcess.activityKey`) started its current
    /// stretch of mic use.
    private var micStarts: [String: Date] = [:]
    /// Counted use has been going since then; the start delay runs from here.
    private var pendingSince: Date?
    /// During a call, counted use stopped then; the end delay runs from here.
    private var idleSince: Date?
    /// Every app seen using the mic since launch, by `activityKey`. Memory only.
    private var seen: [String: SeenRecord] = [:]
    private var seenCounter = 0

    private struct SeenRecord: Sendable {
        var process: MicProcess
        /// Higher is more recent: when the app last started using the mic.
        var order: Int
        var lastSeen: Date
    }

    /// Dates within this of a deadline count as reaching it, so rounding in
    /// `Date` arithmetic can't make a tick at exactly the deadline miss it.
    private static let tolerance: TimeInterval = 0.0005

    private static func reached(_ now: Date, _ deadline: Date) -> Bool {
        now.timeIntervalSince(deadline) >= -tolerance
    }

    /// When `tick(at:)` must next be called: the end of a start or end delay,
    /// or of a timed pause. `nil` when nothing is waiting.
    public var nextDeadline: Date? {
        var deadlines: [Date] = []
        if call == nil, let pendingSince {
            deadlines.append(pendingSince.addingTimeInterval(TimeInterval(settings.startDelay)))
        }
        if call != nil, let idleSince {
            deadlines.append(idleSince.addingTimeInterval(TimeInterval(settings.endDelay)))
        }
        if let end = pause.endsAt {
            deadlines.append(end)
        }
        return deadlines.min()
    }

    /// Takes a fresh snapshot of the mic and camera.
    @discardableResult
    public mutating func observe(_ snapshot: ActivitySnapshot, at now: Date) -> [DetectorEvent] {
        // Whatever was due before this snapshot happened with the old one.
        var events = advance(to: now)

        let previousKeys = Set(self.snapshot.mic.map(\.activityKey))
        let currentKeys = Set(snapshot.mic.map(\.activityKey))
        micStarts = micStarts.filter { currentKeys.contains($0.key) }
        for process in snapshot.mic {
            let key = process.activityKey
            if micStarts[key] == nil { micStarts[key] = now }
            if var record = seen[key] {
                if !previousKeys.contains(key) {
                    seenCounter += 1
                    record.order = seenCounter
                }
                record.process = process
                record.lastSeen = now
                seen[key] = record
            } else {
                seenCounter += 1
                seen[key] = SeenRecord(process: process, order: seenCounter, lastSeen: now)
            }
        }
        self.snapshot = snapshot
        events += evaluate(at: now)
        return events
    }

    /// Lets time pass: start and end delays and timed pauses expire.
    @discardableResult
    public mutating func tick(at now: Date) -> [DetectorEvent] {
        var events = advance(to: now)
        events += evaluate(at: now)
        return events
    }

    /// Applies new settings at once (criterion 9). If the change leaves
    /// nothing counted where something was, a call ends at once, without the
    /// end delay. An end delay already running carries on with the new length.
    @discardableResult
    public mutating func update(settings: DetectionSettings, at now: Date) -> [DetectorEvent] {
        var events = advance(to: now)
        let hadCounted = countsAnything(at: now)
        self.settings = settings
        events += evaluate(at: now, endAtOnce: hadCounted)
        return events
    }

    /// Pauses or resumes. Pausing during a call ends it at once (criterion 10).
    /// After a pause, activity already going counts after the start delay,
    /// measured from the end of the pause.
    @discardableResult
    public mutating func setPause(_ pause: PauseState, at now: Date) -> [DetectorEvent] {
        var events = advance(to: now)
        self.pause = pause
        events += evaluate(at: now, endAtOnce: true)
        return events
    }

    /// Starts fresh (after the Mac wakes, docs/mac-app.md "Other situations"):
    /// forgets the current call without an event (the bar was told
    /// `leaving` before the sleep), the delays in progress and the snapshot.
    /// Settings, the pause, the seen apps and the `call_id` count stay.
    public mutating func reset() {
        call = nil
        pendingSince = nil
        idleSince = nil
        micStarts = [:]
        snapshot = .idle
        observation = .idle
    }

    /// Takes the next `call_id` for a call that doesn't come from detection
    /// (Settings' test call), so it never repeats a detected call's ID.
    public mutating func reserveCallID() -> UInt32 {
        let id = nextCallID
        nextCallID = nextCallID == UInt32.max ? 1 : nextCallID + 1
        return id
    }

    // MARK: - Deciding

    /// Handles every deadline up to `now`, in order, each at its own time.
    private mutating func advance(to now: Date) -> [DetectorEvent] {
        var events: [DetectorEvent] = []
        var steps = 0
        while let deadline = nextDeadline, CallDetector.reached(now, deadline), steps < 16 {
            events += evaluate(at: min(deadline, now))
            steps += 1
        }
        return events
    }

    /// Whether anything counts at `now`, under the current settings and pause.
    private func countsAnything(at now: Date) -> Bool {
        if pause.isPaused(at: now) { return false }
        return micUses(at: now).contains(where: \.counted) || (snapshot.cameraInUse && settings.cameraCounts)
    }

    /// Works out the state at `t` from the latest snapshot.
    ///
    /// - Parameter endAtOnce: a change the user made: if nothing counts now, a
    ///   call ends at once instead of after the end delay.
    private mutating func evaluate(at t: Date, endAtOnce: Bool = false) -> [DetectorEvent] {
        var events: [DetectorEvent] = []
        if let end = pause.endsAt, CallDetector.reached(t, end) {
            pause = .notPaused
        }
        let paused = pause.isPaused(at: t)
        let uses = micUses(at: t)
        let cameraCounted = snapshot.cameraInUse && settings.cameraCounts
        let counted = !paused && (uses.contains(where: \.counted) || cameraCounted)

        if var current = call {
            if counted {
                idleSince = nil
                let name = naming(uses, previous: current)
                if name.app != current.app || name.localName != current.localName || name.identity != current.identity {
                    current.app = name.app
                    current.localName = name.localName
                    current.identity = name.identity
                    call = current
                    events.append(.changed(current))
                }
            } else if paused || endAtOnce {
                call = nil
                idleSince = nil
                events.append(.ended(current))
            } else {
                let since = idleSince ?? t
                idleSince = since
                if CallDetector.reached(t, since.addingTimeInterval(TimeInterval(settings.endDelay))) {
                    call = nil
                    idleSince = nil
                    events.append(.ended(current))
                }
            }
        }

        if call == nil {
            if counted {
                let since = pendingSince ?? t
                pendingSince = since
                let startAt = since.addingTimeInterval(TimeInterval(settings.startDelay))
                if CallDetector.reached(t, startAt) {
                    let name = naming(uses, previous: nil)
                    let started = DetectedCall(
                        callID: reserveCallID(),
                        startedAt: min(startAt, t),
                        app: name.app,
                        localName: name.localName,
                        identity: name.identity
                    )
                    call = started
                    pendingSince = nil
                    events.append(.started(started))
                }
            } else {
                pendingSince = nil
            }
        } else {
            pendingSince = nil
        }

        observation = MacObservation(
            micUses: uses,
            cameraInUse: snapshot.cameraInUse,
            cameraCounted: settings.cameraCounts,
            pendingSince: call == nil ? pendingSince : nil,
            idleSince: call == nil ? nil : idleSince
        )
        refreshSeenApps()
        return events
    }

    /// The apps using the mic now, one entry per app, most recent first.
    private func micUses(at now: Date) -> [MicUse] {
        var byKey: [String: MicUse] = [:]
        for process in snapshot.mic {
            let classification = settings.catalog.classify(process)
            let counted: Bool
            switch classification.category {
            case .callApp: counted = true
            case .ignored: counted = false
            case .other: counted = settings.mode == .anyExceptIgnored
            }
            let since = micStarts[process.activityKey] ?? now
            if let existing = byKey[classification.identity.key] {
                // Several processes of one app: its stretch began with the first.
                if since < existing.since { byKey[classification.identity.key]?.since = since }
            } else {
                byKey[classification.identity.key] = MicUse(classification: classification, counted: counted, since: since)
            }
        }
        return byKey.values.sorted {
            $0.since != $1.since ? $0.since > $1.since : $0.classification.identity.key < $1.classification.identity.key
        }
    }

    private struct Naming: Equatable {
        var app: String?
        var localName: String
        var identity: AppIdentity?
    }

    /// The call's name (docs/mac-app.md, "The app name"): a call app beats
    /// other apps, the most recent first; with the camera alone, the call keeps
    /// the name it had, unless that app no longer counts.
    private func naming(_ uses: [MicUse], previous: DetectedCall?) -> Naming {
        let counted = uses.filter(\.counted)
        for use in counted {
            if case .callApp(let shortName) = use.classification.category {
                return Naming(app: settings.sendAppName ? shortName : nil, localName: shortName,
                              identity: use.classification.identity)
            }
        }
        if let use = counted.first {
            return Naming(app: nil, localName: use.classification.identity.displayName,
                          identity: use.classification.identity)
        }
        let camera = Naming(app: nil, localName: CallDetector.cameraName, identity: nil)
        guard let previous, let identity = previous.identity else { return camera }
        switch settings.catalog.category(ofKey: identity.key) {
        case .ignored:
            return camera
        case .callApp(let shortName):
            return Naming(app: settings.sendAppName ? shortName : nil, localName: shortName, identity: identity)
        case .other, nil:
            guard settings.mode == .anyExceptIgnored else { return camera }
            return Naming(app: nil, localName: previous.localName, identity: identity)
        }
    }

    /// The menu's name for a call from the camera alone.
    public static let cameraName = "Camera"

    private mutating func refreshSeenApps() {
        var byKey: [String: (app: SeenApp, order: Int)] = [:]
        for record in seen.values {
            let classification = settings.catalog.classify(record.process)
            let key = classification.identity.key
            if let existing = byKey[key], existing.order > record.order { continue }
            byKey[key] = (SeenApp(identity: classification.identity, category: classification.category,
                                  lastSeen: max(record.lastSeen, byKey[key]?.app.lastSeen ?? record.lastSeen)),
                          record.order)
        }
        seenApps = byKey.values.sorted { $0.order > $1.order }.map(\.app)
    }
}
