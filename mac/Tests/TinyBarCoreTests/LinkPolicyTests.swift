import Foundation
import XCTest
@testable import TinyBarCore

/// Retry back-off, the 15-second grace, link choice and typed addresses.
final class LinkPolicyTests: XCTestCase {
    let t0 = ManualClock.apiExampleStart

    func test_C18_retrySchedule() {
        let schedule = RetrySchedule.standard
        XCTAssertEqual((1...7).map { schedule.delay(afterFailures: $0) }, [2, 5, 10, 30, 30, 30, 30],
                       "after 2, 5, 10 and 30 s, then every 30 s")
        XCTAssertEqual(schedule.delay(afterFailures: 0), 2, "treated as the first failure")
        XCTAssertEqual(schedule.delay(afterFailures: 10_000), 30)
        XCTAssertEqual(RetrySchedule(delays: [1]).delay(afterFailures: 5), 1)
    }

    func test_C18_notConnectedAfter15Seconds() {
        var tracker = ReachabilityTracker()
        XCTAssertFalse(tracker.isUnreachable(at: t0))

        // Launch: 15 seconds of grace, then not connected.
        tracker.beginGrace(at: t0)
        XCTAssertTrue(tracker.isInGrace(at: t0.addingTimeInterval(14.9)))
        XCTAssertFalse(tracker.isUnreachable(at: t0.addingTimeInterval(14.9)))
        tracker.recordFailure(at: t0.addingTimeInterval(5))
        XCTAssertEqual(tracker.graceEnd, t0.addingTimeInterval(15), "a failure doesn't restart the grace")
        XCTAssertTrue(tracker.isUnreachable(at: t0.addingTimeInterval(15)))
        XCTAssertFalse(tracker.isInGrace(at: t0.addingTimeInterval(15)))

        // A success clears it, and heartbeats 30 s apart don't make it unreachable.
        tracker.recordSuccess(at: t0.addingTimeInterval(20))
        XCTAssertEqual(tracker.lastSuccess, t0.addingTimeInterval(20))
        XCTAssertFalse(tracker.isUnreachable(at: t0.addingTimeInterval(80)))
        XCTAssertNil(tracker.graceEnd)

        // The first failure after a success starts the 15 seconds.
        tracker.recordFailure(at: t0.addingTimeInterval(50))
        tracker.recordFailure(at: t0.addingTimeInterval(52))
        tracker.recordFailure(at: t0.addingTimeInterval(57))
        XCTAssertFalse(tracker.isUnreachable(at: t0.addingTimeInterval(64.9)))
        XCTAssertTrue(tracker.isUnreachable(at: t0.addingTimeInterval(65)))

        // Waking or switching links: a new grace period.
        tracker.beginGrace(at: t0.addingTimeInterval(100))
        XCTAssertFalse(tracker.isUnreachable(at: t0.addingTimeInterval(110)))
        XCTAssertTrue(tracker.isUnreachable(at: t0.addingTimeInterval(115)))
    }

    func test_C15_linkChoice() {
        let endpoint = BarEndpoint(host: "minibar.local")
        func choose(usb: Bool = false, paused: Bool = false, wifi: Bool = true, address: BarEndpoint? = endpoint,
                    authorized: Bool = true, setUp: Bool = true) -> LinkChoice {
            LinkChooser.choose(LinkInputs(usbReady: usb, usbPaused: paused, wifiEnabled: wifi, wifiEndpoint: address,
                                          wifiAuthorized: authorized, isSetUp: setUp))
        }
        XCTAssertEqual(choose(usb: true), .usb, "USB first, even when Wi-Fi works")
        XCTAssertEqual(choose(usb: true, wifi: false, address: nil, authorized: false, setUp: false), .usb,
                       "a MiniBar on USB works before anything is set up")
        XCTAssertEqual(choose(usb: true, paused: true), .wifi(endpoint), "Pause USB: Wi-Fi meanwhile")
        XCTAssertEqual(choose(), .wifi(endpoint))
        XCTAssertEqual(choose(setUp: false), .none(.notSetUp))
        XCTAssertEqual(choose(wifi: false), .none(.wifiOff))
        XCTAssertEqual(choose(address: nil), .none(.noAddress))
        XCTAssertEqual(choose(authorized: false), .none(.notPaired))
        XCTAssertEqual(choose(usb: true, paused: true, wifi: false), .none(.wifiOff))
    }

    func test_barEndpointParsesUserInput() {
        func parse(_ text: String) -> String? { BarEndpoint(userInput: text).map { "\($0.host):\($0.port)" } }
        XCTAssertEqual(parse("minibar.local"), "minibar.local:80")
        XCTAssertEqual(parse("  MiniBar-2.local  "), "minibar-2.local:80")
        XCTAssertEqual(parse("10.0.4.42"), "10.0.4.42:80")
        XCTAssertEqual(parse("10.0.4.42:8080"), "10.0.4.42:8080")
        XCTAssertEqual(parse("http://10.0.4.42/"), "10.0.4.42:80")
        XCTAssertEqual(parse("HTTP://minibar.local:80/"), "minibar.local:80")
        XCTAssertEqual(parse("minibar.local."), "minibar.local:80")
        XCTAssertEqual(parse("minibar"), "minibar:80")

        for bad in ["", " ", "mini bar.local", "minibar.local/api", "http://minibar.local/api/v1", "https://minibar.local",
                    "ftp://minibar.local", "minibar.local:", "minibar.local:0", "minibar.local:65536", "minibar.local:http",
                    "10.0.4.256", "10.0.4", "1.2.3.4.5", "-minibar.local", "mini_bar.local", "minibär.local", "a..b",
                    "[fe80::1]", "fe80::1", "user@minibar.local", "minibar.local?x=1"] {
            XCTAssertNil(BarEndpoint(userInput: bad), "\(bad) should be refused")
        }
    }

    func test_barEndpointURLs() {
        XCTAssertEqual(BarEndpoint.defaultHost.baseURL?.absoluteString, "http://minibar.local")
        XCTAssertEqual(BarEndpoint(host: "10.0.4.42", port: 8080).baseURL?.absoluteString, "http://10.0.4.42:8080")
        XCTAssertEqual(BarEndpoint.defaultHost.remotePageURL?.absoluteString, "http://minibar.local/")
        XCTAssertEqual(BarEndpoint(host: "10.0.4.42", port: 8080).description, "10.0.4.42:8080")
        XCTAssertEqual(LinkKind.usb.via, .usb)
        XCTAssertEqual(LinkKind.wifi.via, .wifi)
    }
}
