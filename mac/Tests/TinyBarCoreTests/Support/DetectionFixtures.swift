import Foundation
@testable import TinyBarCore

/// Processes as the macOS mic monitor would report them.
enum Proc {
    static let slack = MicProcess(
        pid: 101, bundleID: "com.tinyspeck.slackmacgap",
        executablePath: "/Applications/Slack.app/Contents/MacOS/Slack",
        outerAppBundleID: "com.tinyspeck.slackmacgap", displayName: "Slack")
    /// Slack's audio helper with an unlisted bundle ID: matched by its outermost app.
    static let slackHelper = MicProcess(
        pid: 102, bundleID: "com.github.Electron.helper",
        executablePath: "/Applications/Slack.app/Contents/Frameworks/Slack Helper.app/Contents/MacOS/Slack Helper",
        outerAppBundleID: "com.tinyspeck.slackmacgap", displayName: "Slack")
    static let zoom = MicProcess(
        pid: 201, bundleID: "us.zoom.xos",
        executablePath: "/Applications/zoom.us.app/Contents/MacOS/zoom.us",
        outerAppBundleID: "us.zoom.xos", displayName: "zoom.us")
    static let chromeHelper = MicProcess(
        pid: 301, bundleID: "com.google.Chrome.helper",
        executablePath: "/Applications/Google Chrome.app/Contents/Frameworks/Google Chrome Framework.framework/Versions/130.0/Helpers/Google Chrome Helper.app/Contents/MacOS/Google Chrome Helper",
        outerAppBundleID: "com.google.Chrome", displayName: "Google Chrome")
    static let faceTimeAudio = MicProcess(
        pid: 401, bundleID: nil, executablePath: "/usr/libexec/avconferenced", displayName: "avconferenced")
    static let siri = MicProcess(
        pid: 501, bundleID: nil,
        executablePath: "/System/Library/PrivateFrameworks/CoreSpeech.framework/corespeechd", displayName: "corespeechd")
    static let dictation = MicProcess(
        pid: 502, bundleID: "com.apple.SpeechRecognitionCore.speechrecognitiond",
        executablePath: "/System/Library/PrivateFrameworks/SpeechRecognitionCore.framework/Versions/A/speechrecognitiond",
        displayName: "speechrecognitiond")
    static let voiceMemos = MicProcess(
        pid: 503, bundleID: "com.apple.VoiceMemos",
        executablePath: "/System/Applications/VoiceMemos.app/Contents/MacOS/VoiceMemos",
        outerAppBundleID: "com.apple.VoiceMemos", displayName: "Voice Memos")
    static let garageBand = MicProcess(
        pid: 601, bundleID: "com.apple.garageband10",
        executablePath: "/Applications/GarageBand.app/Contents/MacOS/GarageBand",
        outerAppBundleID: "com.apple.garageband10", displayName: "GarageBand")
}

extension ActivitySnapshot {
    static func mic(_ processes: MicProcess...) -> ActivitySnapshot {
        ActivitySnapshot(mic: processes, cameraInUse: false)
    }

    static let camera = ActivitySnapshot(mic: [], cameraInUse: true)
}

/// Drives a `CallDetector` the way the engine does: snapshots at given
/// seconds, and `tick` at every `nextDeadline`. Records each event with the
/// second it happened at.
struct DetectorRun {
    let t0 = ManualClock.apiExampleStart
    var detector: CallDetector
    /// The time of the last call, in seconds from `t0`.
    private(set) var now: Double = 0
    private(set) var events: [(at: Double, event: DetectorEvent)] = []

    init(settings: DetectionSettings = .defaults, pause: PauseState = .notPaused) {
        detector = CallDetector(settings: settings, pause: pause)
    }

    func date(_ seconds: Double) -> Date { t0.addingTimeInterval(seconds) }

    func seconds(_ date: Date) -> Double {
        (date.timeIntervalSince(t0) * 1000).rounded() / 1000
    }

    /// Ticks at every deadline up to `seconds`.
    mutating func run(until seconds: Double) {
        while let deadline = detector.nextDeadline, deadline <= date(seconds) {
            let at = self.seconds(deadline)
            record(detector.tick(at: deadline), at: at)
        }
        now = seconds
    }

    /// Runs up to `seconds`, then takes the snapshot then.
    mutating func observe(_ snapshot: ActivitySnapshot, at seconds: Double) {
        run(until: seconds)
        record(detector.observe(snapshot, at: date(seconds)), at: seconds)
    }

    mutating func update(_ change: (inout DetectionSettings) -> Void, at seconds: Double) {
        run(until: seconds)
        var settings = detector.settings
        change(&settings)
        record(detector.update(settings: settings, at: date(seconds)), at: seconds)
    }

    mutating func setPause(_ pause: PauseState, at seconds: Double) {
        run(until: seconds)
        record(detector.setPause(pause, at: date(seconds)), at: seconds)
    }

    private mutating func record(_ new: [DetectorEvent], at seconds: Double) {
        events += new.map { (seconds, $0) }
        now = seconds
    }

    /// Events as short strings: "3.0 start Slack", "13.0 end", "10.0 change Zoom",
    /// with "-" for no `app`.
    var log: [String] {
        events.map { at, event in
            let time = String(format: "%.1f", at)
            switch event {
            case .started(let call): return "\(time) start \(call.app ?? "-")"
            case .changed(let call): return "\(time) change \(call.app ?? "-")"
            case .ended: return "\(time) end"
            }
        }
    }
}
