#if os(macOS)
import Foundation
import ServiceManagement
import TinyBarCore

/// Start at login, with `SMAppService.mainApp` (macOS 13 and later).
///
/// - `status` maps `SMAppService.Status`: `.enabled`, `.notRegistered`,
///   `.requiresApproval`, `.notFound`. Unknown future cases → `.failed`.
/// - `register()`: already enabled → `.enabled` without registering again
///   (which can throw `kSMErrorAlreadyRegistered`); not in an Applications
///   folder (a first launch from `build/`, or an App Translocation path) →
///   `.notFound` without registering, so no login item points at a path that
///   moves or disappears (mac-app-ux.md 6.2: "Move MiniBar to your
///   Applications folder"). Otherwise it registers, and after a failure
///   reports `.enabled`, `.requiresApproval` or `.notFound` if the status now
///   says so, else `.failed(error.localizedDescription)`.
/// - `unregister()` catches the thrown error the same way.
/// - `openSystemSettings()`: `SMAppService.openSystemSettingsLoginItems()`.
/// - *Unverified:* that an ad-hoc-signed app registers reliably, that it
///   survives a rebuild, and which errors `register()` throws on a Mac whose
///   login items are managed (docs/mac-app.md, "Launch at login").
final class MainAppLoginItem: LoginItemService, @unchecked Sendable {
    init() {}

    var status: LoginItemStatus {
        Self.map(SMAppService.mainApp.status)
    }

    func register() -> LoginItemStatus {
        let service = SMAppService.mainApp
        if service.status == .enabled { return .enabled }
        guard Self.isInApplicationsFolder else { return .notFound }
        do {
            try service.register()
            return Self.map(service.status)
        } catch {
            return Self.failure(error, status: service.status)
        }
    }

    func unregister() -> LoginItemStatus {
        let service = SMAppService.mainApp
        guard service.status != .notRegistered else { return .notRegistered }
        do {
            try service.unregister()
            return Self.map(service.status)
        } catch {
            // Unregistering something macOS can't find leaves nothing to undo.
            if service.status == .notFound || service.status == .notRegistered {
                return .notRegistered
            }
            return .failed(error.localizedDescription)
        }
    }

    func openSystemSettings() {
        SMAppService.openSystemSettingsLoginItems()
    }

    private static func map(_ status: SMAppService.Status) -> LoginItemStatus {
        switch status {
        case .enabled: return .enabled
        case .notRegistered: return .notRegistered
        case .requiresApproval: return .requiresApproval
        case .notFound: return .notFound
        @unknown default: return .failed("Unknown login item status \(status.rawValue)")
        }
    }

    private static func failure(_ error: any Error, status: SMAppService.Status) -> LoginItemStatus {
        switch status {
        case .enabled: return .enabled
        case .requiresApproval: return .requiresApproval
        case .notFound: return .notFound
        default: break
        }
        if !isInApplicationsFolder {
            return .notFound
        }
        return .failed(error.localizedDescription)
    }

    /// `/Applications`, `~/Applications`, or a folder inside either.
    private static var isInApplicationsFolder: Bool {
        let path = Bundle.main.bundleURL.resolvingSymlinksInPath().path
        let folders = [
            "/Applications/",
            FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Applications").path + "/",
        ]
        return folders.contains { path.hasPrefix($0) }
    }
}
#endif
