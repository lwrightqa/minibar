import Foundation
import XCTest
@testable import TinyBarCore

/// The rename to MiniBar (api.md 14.6): what the app says about itself, and
/// what it still accepts from a bar on firmware 1.0.2 or earlier.
final class LegacyNameTests: XCTestCase {
    private func info(device: String) throws -> InfoReply {
        let json = FakeBarPTY.infoJSON.replacingOccurrences(of: #""device": "MiniBar""#, with: #""device": "\#(device)""#)
        return try WireJSON.decodeReply(InfoReply.self, from: Data(json.utf8), httpStatus: nil)
    }

    func test_eitherDeviceNameIsABar() throws {
        XCTAssertEqual(TinyBarAPI.deviceName, "MiniBar")
        XCTAssertEqual(TinyBarAPI.legacyDeviceName, "TinyBar")
        XCTAssertEqual(InfoReply(deviceID: "f412fa3f2a1c", name: "MiniBar 2A1C", fw: "1.0.3").device, "MiniBar",
                       "what the app assumes when it builds a reply itself")
        XCTAssertTrue(try info(device: "MiniBar").isKnownBar)
        XCTAssertTrue(try info(device: "TinyBar").isKnownBar, "a bar on firmware 1.0.2 still says TinyBar")
        XCTAssertFalse(try info(device: "OtherThing").isKnownBar)
        XCTAssertFalse(try info(device: "minibar").isKnownBar, "the value is case-sensitive, as api.md 7.1 spells it")
    }

    func test_bonjourBrowsesBothTypesNewFirst() {
        XCTAssertEqual(TinyBarAPI.Bonjour.serviceTypes, ["_minibar._tcp", "_tinybar._tcp"])
        XCTAssertEqual(TinyBarAPI.Bonjour.serviceTypes.first, TinyBarAPI.Bonjour.serviceType)
        XCTAssertEqual(TinyBarAPI.Bonjour.defaultHost, "minibar.local")
    }

    func test_whatTheAppCallsItself() {
        XCTAssertEqual(TinyBarAPI.userAgent(appVersion: "1.0 (12)"), "MiniBarMac/1.0 (12) (api 1.0)")
        XCTAssertEqual(TinyBarAPI.Keychain.service, "MiniBar")
        XCTAssertEqual(UserDefaultsSettingsStore.defaultKey, "MiniBarSettings.v1")
    }
}
