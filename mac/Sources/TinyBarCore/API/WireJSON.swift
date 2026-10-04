import Foundation

/// Reading and writing the API's JSON.
///
/// Outgoing messages are written through `JSONValue.serialized`, so every
/// message has one exact form on macOS and Linux: compact, keys in byte order
/// (with `cmd` and `id` first on USB lines), `/` unescaped. The bar doesn't
/// care about key order; the golden tests do.
public enum WireJSON {
    /// The exact bytes of `value` as the app sends it.
    public static func encode<T: Encodable>(_ value: T, leadingKeys: [String] = []) throws -> Data {
        Data(try JSONValue.from(value).serialized(leadingKeys: leadingKeys).utf8)
    }

    /// The exact text of `value` as the app sends it.
    public static func encodeString<T: Encodable>(_ value: T, leadingKeys: [String] = []) throws -> String {
        try JSONValue.from(value).serialized(leadingKeys: leadingKeys)
    }

    /// Decodes a reply body. Unknown fields are ignored (api.md 2.3).
    public static func decode<T: Decodable>(_ type: T.Type, from data: Data) throws -> T {
        try JSONDecoder().decode(T.self, from: data)
    }

    /// Decodes a reply from the bar, over either link:
    /// - `{"ok": false, …}` throws `BarError.api` with the error body;
    /// - an HTTP status of 400 or more without a JSON error body (a `431` or
    ///   `414` from the HTTP server, api.md 2.5) throws `BarError.api` with a
    ///   made-up body whose code is `bad_request`;
    /// - anything that doesn't decode as `T` throws `BarError.malformedReply`.
    ///
    /// - Parameter httpStatus: the HTTP status, or for a USB `request` reply its
    ///   `http_status`; `nil` for other USB replies.
    public static func decodeReply<T: Decodable>(_ type: T.Type, from data: Data, httpStatus: Int?) throws(BarError) -> T {
        let ok: Bool?
        do {
            ok = try JSONDecoder().decode(OKField.self, from: data).ok
        } catch {
            if let status = httpStatus, status >= 400 {
                throw .api(APIErrorBody(error: .badRequest, message: "HTTP \(status) without a JSON body"), httpStatus: status)
            }
            throw .malformedReply("Not a JSON object: \(error)")
        }
        if ok == false || (httpStatus ?? 200) >= 400 {
            if let body = try? JSONDecoder().decode(APIErrorBody.self, from: data) {
                throw .api(body, httpStatus: httpStatus)
            }
            throw .api(
                APIErrorBody(error: .badRequest, message: "Error reply without an error code"),
                httpStatus: httpStatus
            )
        }
        do {
            return try JSONDecoder().decode(T.self, from: data)
        } catch {
            throw .malformedReply("Couldn't read \(T.self): \(error)")
        }
    }

    private struct OKField: Decodable {
        var ok: Bool?
    }
}
