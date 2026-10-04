import Foundation
import TinyBarCore

/// Every JSON example in docs/api.md, so the contract tests check the app's
/// message types against the contract itself rather than a copy of it.
struct APIDocExample: CustomStringConvertible {
    enum Direction {
        /// Sent to the bar: an HTTP request body, or a `mac → bar` USB line.
        case toBar
        /// Sent by the bar: an HTTP response body, or a `bar → mac` USB line.
        case fromBar
        /// A ```json block: decided by its content (responses have `ok`).
        case unknown
    }

    var direction: Direction
    /// Line number in api.md, for failure messages.
    var line: Int
    var text: String
    var over: String

    var description: String { "api.md line \(line) (\(over)): \(text.prefix(140))" }

    /// The example as JSON.
    func value() throws -> JSONValue {
        try JSONValue.parse(text)
    }

    /// `true` for responses: bar-to-Mac lines and HTTP responses, and JSON
    /// blocks with an `ok` field.
    var isResponse: Bool {
        switch direction {
        case .toBar: return false
        case .fromBar: return true
        case .unknown: return (try? value())?["ok"] != nil
        }
    }
}

enum APIDoc {
    /// docs/api.md, found relative to this file (mac/Tests/TinyBarCoreTests/Support/).
    static var url: URL {
        URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()  // Support
            .deletingLastPathComponent()  // TinyBarCoreTests
            .deletingLastPathComponent()  // Tests
            .deletingLastPathComponent()  // mac
            .deletingLastPathComponent()  // repository root
            .appendingPathComponent("docs/api.md")
    }

    static func text() throws -> String {
        try String(contentsOf: url, encoding: .utf8)
    }

    /// Extracts the examples the same way the doc's checker script does:
    /// ```json blocks whole, the body of ```http blocks (after the blank
    /// line), and every `@tb ` line in ```text blocks.
    static func examples() throws -> [APIDocExample] {
        let lines = try text().components(separatedBy: "\n")
        var examples: [APIDocExample] = []
        var index = 0
        while index < lines.count {
            let line = lines[index]
            guard line.hasPrefix("```"), line.count > 3 else {
                index += 1
                continue
            }
            let language = String(line.dropFirst(3)).trimmingCharacters(in: .whitespaces)
            let start = index + 1
            var end = start
            while end < lines.count, !lines[end].hasPrefix("```") { end += 1 }
            let body = Array(lines[start..<min(end, lines.count)])
            switch language {
            case "json":
                examples.append(APIDocExample(direction: .unknown, line: start + 1,
                                              text: body.joined(separator: "\n"), over: "json"))
            case "http":
                if let blank = body.firstIndex(where: { $0.trimmingCharacters(in: .whitespaces).isEmpty }) {
                    let json = body[(blank + 1)...].joined(separator: "\n").trimmingCharacters(in: .whitespacesAndNewlines)
                    if !json.isEmpty {
                        let isResponse = body.first?.hasPrefix("HTTP/") ?? false
                        examples.append(APIDocExample(direction: isResponse ? .fromBar : .toBar,
                                                      line: start + blank + 2, text: json,
                                                      over: body.first ?? "http"))
                    }
                }
            case "text":
                for (offset, textLine) in body.enumerated() {
                    guard let range = textLine.range(of: USBFraming.marker) else { continue }
                    let prefix = textLine[..<range.lowerBound]
                    let direction: APIDocExample.Direction =
                        prefix.contains("mac → bar") ? .toBar : prefix.contains("bar → mac") ? .fromBar : .unknown
                    examples.append(APIDocExample(direction: direction, line: start + offset + 1,
                                                  text: String(textLine[range.upperBound...]), over: "usb"))
                }
            default:
                break
            }
            index = end + 1
        }
        return examples
    }
}

extension JSONValue {
    /// The object without the given keys (USB `id`, `cmd`, `http_status`).
    func removingKeys(_ keys: Set<String>) -> JSONValue {
        guard case .object(var object) = self else { return self }
        for key in keys { object.removeValue(forKey: key) }
        return .object(object)
    }

    var keySet: Set<String> {
        Set(objectValue?.keys.map { $0 } ?? [])
    }
}
