import Foundation
#if canImport(os)
import os
#endif

/// Problems worth finding later in Console.app (subsystem
/// `com.tinybar.TinyBarMac`). Never anything about which apps used the mic
/// (criterion 32): only which part of the app failed and why.
enum Log {
    static func error(_ category: String, _ message: String) {
        #if canImport(os)
        Logger(subsystem: "com.tinybar.TinyBarMac", category: category).error("\(message, privacy: .public)")
        #else
        FileHandle.standardError.write(Data("TinyBar [\(category)] \(message)\n".utf8))
        #endif
    }
}
