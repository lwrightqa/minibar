#if os(macOS)
import Foundation
import Security
import TinyBarCore

/// Bar tokens in the login Keychain (api.md 16): one generic-password item per
/// bar, `kSecAttrService` "MiniBar", `kSecAttrAccount` = `device_id`,
/// `kSecAttrAccessible` = `kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`,
/// value = the token as UTF-8 data. Never UserDefaults or a file (criterion 20).
///
/// - Read: `SecItemCopyMatching` with `kSecReturnData` and `kSecMatchLimitOne`;
///   `errSecItemNotFound` → `nil`.
/// - Write: `SecItemUpdate`; on `errSecItemNotFound`, `SecItemAdd`. On any
///   other failure (the item's access list doesn't trust this build, or the
///   person chose Deny), the old item is deleted and a new one added, which
///   this build then owns, so the next launch doesn't pair again.
/// - Delete: `SecItemDelete`; `errSecItemNotFound` isn't an error.
/// - The file-based login keychain, not the data-protection keychain:
///   `kSecUseDataProtectionKeychain` needs a keychain-access-groups
///   entitlement, which an ad-hoc signature can't carry. The file-based
///   keychain ignores `kSecAttrAccessible` (Apple TN3137), so "this device
///   only" isn't enforced; it's set anyway, for if the item ever moves.
/// - **Blocking:** the item's access list trusts the build that created it
///   by its designated requirement, which for an ad-hoc signature is its
///   cdhash, new with every build. After a rebuild, macOS shows "MiniBar wants
///   to use your confidential information stored in “MiniBar”", and the call
///   waits until it's answered. So no method here may run on the main
///   thread: the core calls them from the connection actor and, for pairing,
///   from a detached task. `scripts/build-app.sh` can sign ad-hoc builds with
///   an identifier-based requirement instead (`STABLE_ADHOC_REQUIREMENT=1`),
///   and the README says to choose Always Allow. *Unverified* on a Mac:
///   whether `SecItemDelete` prompts too, and whether an identifier-based
///   requirement keeps the access list matching across rebuilds.
final class KeychainTokenStore: TokenStore, @unchecked Sendable {
    struct KeychainError: Error, CustomStringConvertible {
        var operation: String
        var status: OSStatus

        var description: String {
            let message = SecCopyErrorMessageString(status, nil) as String? ?? "OSStatus \(status)"
            return "Keychain \(operation) failed: \(message)"
        }
    }

    init() {}

    func token(for deviceID: String) throws -> String? {
        var query = Self.query(for: deviceID)
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        switch status {
        case errSecSuccess:
            guard let data = result as? Data else { return nil }
            return String(data: data, encoding: .utf8)
        case errSecItemNotFound:
            return nil
        default:
            throw KeychainError(operation: "read", status: status)
        }
    }

    func setToken(_ token: String, for deviceID: String) throws {
        let data = Data(token.utf8)
        let query = Self.query(for: deviceID)
        let update: [String: Any] = [kSecValueData as String: data]
        let status = SecItemUpdate(query as CFDictionary, update as CFDictionary)
        switch status {
        case errSecSuccess:
            return
        case errSecItemNotFound:
            break
        default:
            // Not allowed to change the old item (another build made it, or
            // Deny was chosen): replace it with one this build owns.
            _ = SecItemDelete(query as CFDictionary)
        }
        var item = query
        item[kSecValueData as String] = data
        item[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        item[kSecAttrLabel as String] = "MiniBar"
        item[kSecAttrDescription as String] = "MiniBar pairing"
        let added = SecItemAdd(item as CFDictionary, nil)
        guard added == errSecSuccess else {
            throw KeychainError(operation: status == errSecItemNotFound ? "add" : "update", status: added)
        }
    }

    func removeToken(for deviceID: String) throws {
        let status = SecItemDelete(Self.query(for: deviceID) as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else {
            throw KeychainError(operation: "delete", status: status)
        }
    }

    private static func query(for deviceID: String) -> [String: Any] {
        [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: TinyBarAPI.Keychain.service,
            kSecAttrAccount as String: deviceID,
        ]
    }
}
#endif
