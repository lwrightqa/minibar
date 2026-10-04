#if os(macOS)
import Foundation
import Security
import TinyBarCore

/// Bar tokens in the login Keychain (api.md 16): one generic-password item per
/// bar, `kSecAttrService` "TinyBar", `kSecAttrAccount` = `device_id`,
/// `kSecAttrAccessible` = `kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`,
/// value = the token as UTF-8 data. Never UserDefaults or a file (criterion 20).
///
/// - Read: `SecItemCopyMatching` with `kSecReturnData` and `kSecMatchLimitOne`;
///   `errSecItemNotFound` → `nil`.
/// - Write: `SecItemUpdate`; on `errSecItemNotFound`, `SecItemAdd`.
/// - Delete: `SecItemDelete`; `errSecItemNotFound` isn't an error.
/// - The file-based login keychain, not the data-protection keychain:
///   `kSecUseDataProtectionKeychain` needs a keychain-access-groups
///   entitlement, which an ad-hoc signature can't carry.
/// - *Unverified:* whether an ad-hoc-signed app that's been rebuilt (a new
///   code signature) gets a "TinyBar wants to use your confidential
///   information" prompt when reading an item a previous build stored. If the
///   read fails instead, the engine treats it as "no token" and pairs again
///   over USB.
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
            var item = query
            item[kSecValueData as String] = data
            item[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            item[kSecAttrLabel as String] = "TinyBar"
            item[kSecAttrDescription as String] = "TinyBar pairing"
            let added = SecItemAdd(item as CFDictionary, nil)
            guard added == errSecSuccess else {
                throw KeychainError(operation: "add", status: added)
            }
        default:
            throw KeychainError(operation: "update", status: status)
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
