import XCTest
@testable import TinyBarCore

/// Lines on the USB port (api.md 6.4).
final class USBFramingTests: XCTestCase {
    private func classify(_ text: String) -> USBIncomingLine {
        USBFraming.classify(Data(text.utf8))
    }

    func testLogLinesAreLogs() {
        XCTAssertEqual(classify("I (24312) wifi: connected to Office-WiFi, ip 10.0.4.42"),
                       .log("I (24312) wifi: connected to Office-WiFi, ip 10.0.4.42"))
        XCTAssertEqual(classify("{\"ok\": true}"), .log("{\"ok\": true}"), "JSON without the marker is log output")
        XCTAssertEqual(classify(""), .log(""))
        XCTAssertEqual(classify("x@tb {\"id\": 1}"), .log("x@tb {\"id\": 1}"), "the marker counts only at the start")
    }

    func testReplyWithIDAndTrailingCR() {
        let line = "@tb {\"id\": 3, \"ok\": true, \"device_id\": \"f412fa3f2a1c\"}\r"
        guard case .reply(let id, let json) = classify(line) else { return XCTFail() }
        XCTAssertEqual(id, 3)
        XCTAssertEqual(String(decoding: json, as: UTF8.self), "{\"id\": 3, \"ok\": true, \"device_id\": \"f412fa3f2a1c\"}")
    }

    func testReplyToAnUnparsableLineHasNoID() {
        guard case .reply(let id, _) = classify(
            "@tb {\"id\": null, \"ok\": false, \"error\": \"bad_json\", \"message\": \"That line isn't a JSON object.\", \"field\": null}"
        ) else { return XCTFail() }
        XCTAssertNil(id)
    }

    func testANSIColorCodesAroundAProtocolLineAreStripped() {
        guard case .reply(let id, _) = classify("\u{1B}[0;32m@tb {\"id\": 5, \"ok\": true}\u{1B}[0m") else {
            return XCTFail()
        }
        XCTAssertEqual(id, 5)
        XCTAssertEqual(classify("\u{1B}[0;33mW (24890) cal: sync took 4.2 s\u{1B}[0m\r"),
                       .log("W (24890) cal: sync took 4.2 s\u{1B}[0m"))
    }

    func testReadyIsAnEvent() throws {
        guard case .event(let name, let json) = classify(
            "@tb {\"event\": \"ready\", \"device_id\": \"f412fa3f2a1c\", \"api\": \"1.0\", \"fw\": \"1.0.0\"}"
        ) else { return XCTFail() }
        XCTAssertEqual(name, "ready")
        XCTAssertEqual(try WireJSON.decode(ReadyEvent.self, from: json),
                       ReadyEvent(deviceID: "f412fa3f2a1c", fw: "1.0.0"))
    }

    func testMarkerLinesThatArentJSONObjectsAreInvalid() {
        XCTAssertEqual(classify("@tb {\"id\": 2, \"ok\": tru"), .invalid("@tb {\"id\": 2, \"ok\": tru"))
        XCTAssertEqual(classify("@tb [1, 2]"), .invalid("@tb [1, 2]"))
        XCTAssertEqual(classify("@tb \"text\""), .invalid("@tb \"text\""))
        XCTAssertEqual(classify("@tb {\"id\": \"three\"}"), .invalid("@tb {\"id\": \"three\"}"))
    }

    func testSplitterJoinsChunksAndSplitsAtLF() {
        var splitter = USBLineSplitter()
        XCTAssertEqual(splitter.append(Data("I (1) boot\r\n@tb {\"id\":".utf8)), [Data("I (1) boot\r".utf8)])
        XCTAssertEqual(splitter.pendingByteCount, 10)
        XCTAssertEqual(splitter.append(Data(" 1}\n\n".utf8)), [Data("@tb {\"id\": 1}".utf8), Data()])
        XCTAssertEqual(splitter.pendingByteCount, 0)
    }

    func testSplitterDropsOverlongLinesUpToTheirLF() {
        var splitter = USBLineSplitter(maxLineBytes: 10)
        XCTAssertEqual(splitter.append(Data(String(repeating: "x", count: 25).utf8)), [])
        XCTAssertEqual(splitter.droppedLines, 1)
        XCTAssertEqual(splitter.append(Data("yyy\nshort\n".utf8)), [Data("short".utf8)])
        XCTAssertEqual(splitter.droppedLines, 1)
    }

    func testDefaultSplitterKeepsTheBarsLongestLine() {
        var splitter = USBLineSplitter()
        let long = "@tb " + String(repeating: "a", count: 8192)
        XCTAssertEqual(splitter.append(Data((long + "\n").utf8)).first?.count, long.utf8.count)
    }
}
