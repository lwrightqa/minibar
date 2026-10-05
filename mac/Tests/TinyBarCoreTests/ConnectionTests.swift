import Foundation
import XCTest
@testable import TinyBarCore

/// `BarConnection` with fake transports and a manual clock: criteria 13, 15,
/// 17 to 24, and api.md 6.8 and 16.
final class ConnectionTests: XCTestCase {
    let slack = ReportedCall(active: true, app: "Slack", callID: 1, startedAt: ManualClock.apiExampleStart)

    // MARK: - Criterion 13: heartbeat

    func test_C13_heartbeat() async throws {
        let rig = ConnectionRig.paired()
        await rig.start()
        XCTAssertEqual(rig.messageLog(), [], "no link yet: nothing sent")

        await rig.plugIn()
        XCTAssertEqual(rig.messageLog(), ["0.0 usb idle"], "at once when a link comes up, idle too")
        await rig.run(for: 75)
        XCTAssertEqual(rig.messageLog(), ["0.0 usb idle", "30.0 usb idle", "60.0 usb idle"], "every 30 s")

        await rig.connection.report(slack)
        await rig.settle()
        await rig.run(for: 40)
        XCTAssertEqual(rig.messageLog().suffix(2), ["75.0 usb Slack", "105.0 usb Slack"], "on every change, then every 30 s from it")

        // The bar asks for another interval: the app follows the latest reply.
        rig.bar.set { $0.heartbeatS = 20 }
        await rig.run(for: 45)
        XCTAssertEqual(rig.messageLog().suffix(3), ["105.0 usb Slack", "135.0 usb Slack", "155.0 usb Slack"])

        let requests = rig.bar.messages.map(\.request)
        XCTAssertEqual(requests.compactMap(\.seq), Array(1...UInt32(requests.count)), "seq goes up by 1")
        XCTAssertTrue(requests.allSatisfy { $0.client == ConnectionRig.client && $0.session == "q8Zr2Lx0" })
        let lastCall = try XCTUnwrap(requests.last)
        XCTAssertEqual(lastCall.callID, 1)
        XCTAssertEqual(lastCall.elapsedS, 155, "elapsed_s from the call's start")
        XCTAssertEqual(lastCall.problems, [])

        // The same state reported again isn't a change.
        let count = rig.bar.messages.count
        await rig.connection.report(slack)
        await rig.settle()
        XCTAssertEqual(rig.bar.messages.count, count)
        let state = await rig.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertEqual(state.link, .usb)
        XCTAssertEqual(state.endpointDescription, "cu.usbmodem1")
        await rig.connection.stop()
    }

    // MARK: - Criterion 15: link choice

    func test_C15_linkChoice() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        XCTAssertEqual(rig.messageLog(), ["0.0 wifi idle"], "Wi-Fi to the last address that worked")

        await rig.run(for: 10)
        await rig.plugIn()
        await rig.run(for: 70)
        XCTAssertEqual(rig.messageLog(), ["0.0 wifi idle", "10.0 usb idle", "40.0 usb idle", "70.0 usb idle"],
                       "with both links up, every message goes over USB")

        await rig.connection.report(slack)
        await rig.settle()
        await rig.run(for: 2)
        await rig.unplug()
        XCTAssertEqual(rig.messageLog().last, "82.0 wifi Slack", "when USB drops, Wi-Fi at once, with the current state")
        var state = await rig.state
        XCTAssertEqual(state.link, .wifi)
        XCTAssertEqual(state.phase, .connected)

        await rig.run(for: 3)
        await rig.plugIn()
        XCTAssertEqual(rig.messageLog().last, "85.0 usb Slack", "back on USB as soon as it answers")
        state = await rig.state
        XCTAssertEqual(state.link, .usb)
        XCTAssertTrue(rig.bar.messages.filter { $0.via == .wifi }.allSatisfy { $0.token != nil }, "Wi-Fi always with the token")
        await rig.connection.stop()
    }

    func test_C23_onlyThePairedBar() async throws {
        let rig = ConnectionRig.paired()
        let other = FakeBar(clock: rig.clock, deviceID: "a0b1c2d3e4f5", name: "MiniBar E4F5")
        let otherAddress = BarEndpoint(host: "10.0.4.50")
        let pairedAddress = BarEndpoint(host: "10.0.4.77")
        rig.factory.place(other, at: otherAddress)
        rig.factory.place(rig.bar, at: pairedAddress)
        await rig.start()
        await rig.connection.discovered([
            DiscoveredBar(name: "MiniBar E4F5", deviceID: "a0b1c2d3e4f5", endpoint: otherAddress),
            DiscoveredBar(name: "MiniBar 2A1C", deviceID: "f412fa3f2a1c", endpoint: pairedAddress),
        ])
        await rig.settle()
        await rig.run(for: 31)
        XCTAssertEqual(other.messages.count, 0, "never the other bar")
        XCTAssertGreaterThanOrEqual(rig.bar.messages.count, 2)
        let state = await rig.state
        XCTAssertEqual(state.bar?.lastEndpoint, pairedAddress, "the address that worked is kept")
        await rig.connection.stop()
    }

    func test_wrongDevice() async throws {
        // The last address now belongs to another bar: its replies aren't accepted.
        let rig = ConnectionRig.paired()
        let other = FakeBar(clock: rig.clock, deviceID: "a0b1c2d3e4f5", name: "MiniBar E4F5")
        rig.factory.place(other, at: ConnectionRig.barAddress)
        await rig.start()
        XCTAssertEqual(other.messages.count, 0, "info said it's another bar; nothing sent")
        await rig.run(for: 20)
        var state = await rig.state
        XCTAssertEqual(state.bar?.lastEndpoint, nil, "the stale address is dropped")
        XCTAssertNotEqual(state.phase, .connected)
        XCTAssertEqual(try rig.tokens.token(for: "f412fa3f2a1c") != nil, true, "and the token is kept")

        // A call reply from another device_id (the address changed hands mid-session).
        let rig2 = ConnectionRig.paired()
        rig2.factory.place(rig2.bar, at: ConnectionRig.barAddress)
        await rig2.start()
        XCTAssertEqual(rig2.bar.messages.count, 1)
        rig2.bar.set { $0.replyDeviceID = "a0b1c2d3e4f5" }
        await rig2.connection.report(slack)
        await rig2.settle()
        state = await rig2.state
        XCTAssertEqual(state.lastReply?.deviceID, "f412fa3f2a1c", "the other bar's reply isn't taken")
        await rig.connection.stop()
        await rig2.connection.stop()
    }

    // MARK: - Criterion 17: replies

    func test_C17_replies() async throws {
        let rig = ConnectionRig.paired()
        await rig.start()
        await rig.plugIn()
        await rig.connection.report(slack)
        await rig.settle()
        var state = await rig.state
        XCTAssertEqual(state.lastReply?.showing, .call)

        // Set aside on the bar: the reply says so, and the app sends nothing extra.
        rig.bar.set { $0.aside = true }
        let before = rig.bar.messages.count
        await rig.run(for: 60)
        XCTAssertEqual(rig.bar.messages.count, before + 2, "only the heartbeats")
        state = await rig.state
        XCTAssertEqual(state.lastReply?.showing, .own)
        XCTAssertEqual(state.lastReply?.call.aside, true)
        XCTAssertEqual(state.phase, .connected)

        // Calls from your Mac switched off: a normal reply, and the heartbeat goes on.
        rig.bar.set { $0.aside = false; $0.sourcesMac = false }
        await rig.run(for: 60)
        XCTAssertEqual(rig.bar.messages.count, before + 4)
        state = await rig.state
        XCTAssertEqual(state.lastReply?.sources.mac, false)
        XCTAssertEqual(state.phase, .connected)
        await rig.connection.stop()
    }

    func test_C17_unauthorizedOverWiFi() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        XCTAssertEqual(rig.bar.messages.count, 1)

        // The pairing was removed on the Remote.
        rig.bar.set { $0.tokens.removeAll() }
        await rig.connection.report(slack)
        await rig.settle()
        var state = await rig.state
        XCTAssertEqual(state.phase, .unrecognized, "doesn't recognize this Mac: Pair Again")
        XCTAssertNil(try rig.tokens.token(for: "f412fa3f2a1c"), "the refused token is deleted")
        XCTAssertNil(state.bar?.tokenID)
        XCTAssertTrue(rig.bar.log.contains("wifi info"), "info is checked first (api.md 16)")
        let refused = rig.bar.log.filter { $0 == "wifi call refused" }.count
        await rig.run(for: 120)
        XCTAssertEqual(rig.bar.log.filter { $0 == "wifi call refused" }.count, refused, "no token, no more tries")

        // Plugged in: it pairs again over the cable without asking (mac-app-ux.md 5.3).
        await rig.plugIn()
        state = await rig.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertNotNil(try rig.tokens.token(for: "f412fa3f2a1c"))
        XCTAssertTrue(rig.bar.log.contains("usb pair"))
        await rig.connection.stop()
    }

    // MARK: - Criterion 18: retries and Not connected

    func test_C18_retriesAndNotConnected() async throws {
        let rig = ConnectionRig.paired()   // Wi-Fi only, and nothing answers at the address
        await rig.start()
        var state = await rig.state
        XCTAssertEqual(state.phase, .looking)
        await rig.run(for: 14.5)
        state = await rig.state
        XCTAssertEqual(state.phase, .looking, "15 s of grace after launch")
        await rig.run(for: 1)
        state = await rig.state
        XCTAssertEqual(state.phase, .unreachable, "Not connected after 15 s without success")
        await rig.run(for: 100)
        let tries = rig.factory.made.map { _ in 0 }.count
        XCTAssertEqual(tries, 7, "at 0, 2, 7, 17, 47, 77 and 107 s")

        // The bar comes back: connected at the next try, then every 30 s.
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.run(for: 30)
        XCTAssertEqual(rig.messageLog(), ["137.0 wifi idle"])
        state = await rig.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertEqual(state.lastSuccess, rig.start.addingTimeInterval(137))
        await rig.connection.stop()
    }

    func test_C18_retryTimesAfterFailures() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        rig.bar.set { $0.wifiFailure = .timedOut }
        await rig.run(for: 30)    // the heartbeat at 30 fails
        await rig.run(for: 130)
        let failures = rig.bar.log.enumerated().filter { $0.element == "wifi call failed" || $0.element == "wifi failed" }
        XCTAssertEqual(failures.count, 7, "at 30, 32, 37, 47, 77, 107 and 137 s")
        var state = await rig.state
        XCTAssertEqual(state.phase, .unreachable)
        rig.bar.set { $0.wifiFailure = nil }
        await rig.run(for: 30)
        state = await rig.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertEqual(rig.messageLog().last, "167.0 wifi idle")
        await rig.connection.stop()
    }

    // MARK: - Criterion 19: leaving

    func test_C19_leaving() async throws {
        let rig = ConnectionRig.paired()
        await rig.start()
        await rig.plugIn()
        await rig.connection.report(slack)
        await rig.settle()
        await rig.connection.sendLeaving()
        let last = try XCTUnwrap(rig.bar.messages.last?.request)
        XCTAssertEqual(last.leaving, true)
        XCTAssertFalse(last.active, "leaving sends active: false even during a call")
        XCTAssertNil(last.app)
        XCTAssertNil(last.callID)
        XCTAssertEqual(last.problems, [])

        // Asleep: nothing more until the Mac wakes.
        let count = rig.bar.messages.count
        await rig.run(for: 90)
        XCTAssertEqual(rig.bar.messages.count, count)
        await rig.connection.didWake()
        await rig.settle()
        XCTAssertEqual(rig.messageLog().last, "90.0 usb Slack", "the state at once after waking")
        await rig.connection.stop()
    }

    func test_C19_leavingGivesUpAfterAboutASecond() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        rig.factory.place(nil, at: ConnectionRig.barAddress)
        rig.factory.hang(at: ConnectionRig.barAddress)
        // The live transport hangs now too.
        await rig.connection.update(BarConnection.Configuration(clientID: ConnectionRig.client, sessionID: "q8Zr2Lx0", appVersion: "1.0 (12)", useWiFi: true))
        let leaving = Task { await rig.connection.sendLeaving() }
        await rig.clock.waitForSleepers(1)
        rig.clock.advance(by: 1)
        await leaving.value   // returns: best effort, never throws
        await rig.connection.stop()
    }

    // MARK: - USB: missed replies, ready, versions, devices

    func test_usbMissedReplies() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        await rig.plugIn()
        XCTAssertEqual(rig.messageLog().last, "0.0 usb idle")

        rig.bar.set { $0.usbFailure = .timedOut }
        await rig.connection.report(slack)
        await rig.settle()
        await rig.run(for: 8)   // retries at 2 and 7 s
        XCTAssertEqual(rig.bar.log.filter { $0 == "usb call failed" }.count, 1)
        XCTAssertEqual(rig.factory.opened.first?.isClosed, true, "3 replies missed in a row: port closed")
        XCTAssertEqual(rig.messageLog().last, "7.0 wifi Slack", "Wi-Fi at once")

        rig.bar.set { $0.usbFailure = nil }
        await rig.run(for: 8.5)
        XCTAssertEqual(rig.factory.opened.count, 1, "not reopened before 10 s")
        await rig.run(for: 1)
        XCTAssertEqual(rig.factory.opened.count, 2, "reopened after 10 s")
        XCTAssertEqual(rig.messageLog().last, "17.0 usb Slack")
        await rig.connection.stop()
    }

    func test_readyEvent() async throws {
        let rig = ConnectionRig.paired()
        await rig.start()
        await rig.plugIn()
        await rig.run(for: 12)
        let transport = try XCTUnwrap(rig.factory.opened.first)
        let logCount = rig.bar.log.count
        transport.emit(.ready(ReadyEvent(deviceID: "f412fa3f2a1c", fw: "1.0.0")))
        await rig.settle()
        XCTAssertEqual(Array(rig.bar.log.dropFirst(logCount)), ["usb hello", "usb call"], "hello, then the state at once")
        XCTAssertEqual(rig.messageLog().last, "12.0 usb idle")
        await rig.connection.stop()
    }

    func test_portClosedByTheBarRestarting() async throws {
        let rig = ConnectionRig.paired()
        await rig.start()
        await rig.plugIn()
        let first = try XCTUnwrap(rig.factory.opened.first)
        first.emit(.closed)
        await rig.settle()
        var state = await rig.state
        XCTAssertNotEqual(state.link, .usb)
        // IOKit reports it again (a restart gives it a new registry entry).
        await rig.unplug()
        let reborn = SerialDevice(calloutPath: "/dev/cu.usbmodem1101", vendorID: 0x303A, productID: 0x1001, registryID: 2)
        await rig.run(for: 1)
        await rig.plugIn(reborn)
        state = await rig.state
        XCTAssertEqual(state.link, .usb)
        XCTAssertEqual(rig.messageLog().last, "1.0 usb idle")
        await rig.connection.stop()
    }

    func test_C14_aDeviceThatIsntATinyBarIsLeftAlone() async throws {
        let rig = ConnectionRig.paired()
        await rig.start()
        let other = SerialDevice(calloutPath: "/dev/cu.usbmodem3101", vendorID: 0x303A, productID: 0x1001, registryID: 7)
        await rig.plugIn(other, answers: false)
        XCTAssertEqual(rig.factory.opened.count, 1)
        XCTAssertEqual(rig.factory.opened.first?.isClosed, true, "closed after the handshake failed")
        await rig.run(for: 120)
        XCTAssertEqual(rig.factory.opened.count, 1, "and never opened again")
        await rig.unplug(other)
        await rig.plugIn(other, answers: false)
        XCTAssertEqual(rig.factory.opened.count, 2, "until it's unplugged and plugged in again")

        // Criterion 16: never anything but Espressif's USB Serial/JTAG.
        let printer = SerialDevice(calloutPath: "/dev/cu.usbserial-1410", vendorID: 0x1A86, productID: 0x7523, registryID: 8)
        await rig.plugIn(printer)
        let s3Download = SerialDevice(calloutPath: "/dev/cu.usbmodem01", vendorID: 0x303A, productID: 0x0002, registryID: 9)
        await rig.plugIn(s3Download)
        XCTAssertEqual(rig.factory.opened.count, 2)
        await rig.connection.stop()
    }

    func test_busyPortIsTriedAgainLater() async throws {
        let rig = ConnectionRig.paired()
        await rig.start()
        rig.factory.failOpening(with: SerialPortError.busy)
        await rig.plugIn()
        XCTAssertEqual(rig.factory.opened.count, 0)
        rig.factory.failOpening(with: nil)
        await rig.run(for: 10.5)
        XCTAssertEqual(rig.factory.opened.count, 1, "opened 10 s later, when the flasher let go")
        XCTAssertEqual(rig.messageLog(), ["10.0 usb idle"])
        await rig.connection.stop()
    }

    func test_apiVersionMismatch() async throws {
        for (api, phase) in [("2.0", ConnectionState.Phase.appNeedsUpdate), ("0.9", .barNeedsUpdate)] {
            let rig = ConnectionRig.paired()
            rig.bar.set { $0.api = api }
            await rig.start()
            await rig.plugIn()
            let state = await rig.state
            XCTAssertEqual(state.phase, phase, "api \(api)")
            XCTAssertEqual(rig.bar.messages.count, 0)
            XCTAssertEqual(rig.factory.opened.first?.isClosed, true)
            await rig.connection.stop()
        }
        XCTAssertEqual(BarConnection.versionProblem(InfoReply(deviceID: "f412fa3f2a1c", name: "x", fw: "1", api: "1.7")), nil,
                       "minor versions only add things")
        XCTAssertEqual(BarConnection.versionProblem(InfoReply(deviceID: "f412fa3f2a1c", name: "x", fw: "1", api: "one")), .appNeedsUpdate)
    }

    func test_pauseUSB() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        await rig.plugIn()
        await rig.connection.setUSBPaused(true)
        await rig.settle()
        var state = await rig.state
        XCTAssertTrue(state.usbPaused)
        XCTAssertEqual(state.link, .wifi, "Wi-Fi meanwhile")
        XCTAssertEqual(rig.factory.opened.first?.isClosed, true, "the port is let go, for a flasher")
        await rig.run(for: 60)
        XCTAssertEqual(rig.factory.opened.count, 1)

        await rig.connection.setUSBPaused(false)
        await rig.settle()
        state = await rig.state
        XCTAssertEqual(state.link, .usb)
        XCTAssertEqual(rig.factory.opened.count, 2)

        // Unplugging and plugging in again also ends the pause.
        await rig.connection.setUSBPaused(true)
        await rig.unplug()
        await rig.plugIn(SerialDevice(calloutPath: "/dev/cu.usbmodem1101", vendorID: 0x303A, productID: 0x1001, registryID: 3))
        state = await rig.state
        XCTAssertFalse(state.usbPaused)
        XCTAssertEqual(state.link, .usb)
        await rig.connection.stop()
    }

    // MARK: - Pairing over USB, forgetting, switching bars

    func test_C20_usbPairsAutomatically() async throws {
        let rig = ConnectionRig()
        await rig.start()
        var state = await rig.state
        XCTAssertEqual(state.phase, .notSetUp)
        await rig.run(for: 60)
        XCTAssertEqual(rig.factory.made.count, 0, "not set up: never Wi-Fi on its own (no Local Network prompt)")

        await rig.plugIn()
        state = await rig.state
        XCTAssertEqual(state.bar?.deviceID, "f412fa3f2a1c")
        XCTAssertEqual(state.bar?.name, "MiniBar 2A1C", "the menu shows the bar's name")
        XCTAssertEqual(state.bar?.tokenID, "00000001")
        XCTAssertEqual(state.phase, .connected)
        XCTAssertTrue(state.usbSeen)
        XCTAssertEqual(rig.tokens.deviceIDs, ["f412fa3f2a1c"], "the token is in the token store")
        XCTAssertEqual(Array(rig.bar.log.prefix(3)), ["usb handshake", "usb pair", "usb call"])
        // The token is nowhere in the state the engine saves.
        let saved = try WireJSON.encodeString(try XCTUnwrap(state.bar))
        XCTAssertFalse(saved.contains("tb1_"))

        // The bar is on Wi-Fi: the app checked its address answers from here.
        XCTAssertTrue(rig.bar.log.contains("usb call"))
        await rig.connection.stop()
    }

    func test_C20_tokenLimitStillWorksOverUSB() async throws {
        let rig = ConnectionRig()
        rig.bar.set { $0.tokenLimit = true }
        await rig.start()
        await rig.plugIn()
        let state = await rig.state
        XCTAssertTrue(state.tokenLimitReached)
        XCTAssertEqual(state.phase, .connected)
        XCTAssertEqual(rig.messageLog(), ["0.0 usb idle"])
        XCTAssertEqual(rig.tokens.deviceIDs, [])
        await rig.connection.stop()
    }

    func test_C21_bearerAndForget() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        let token = try XCTUnwrap(try rig.tokens.token(for: "f412fa3f2a1c"))
        XCTAssertEqual(rig.bar.messages.first?.token, token, "Wi-Fi requests carry the token")
        await rig.plugIn()

        await rig.connection.forget()
        await rig.settle()
        // Plugged in: over the cable, by token_id (api.md 12.2), since USB
        // carries no token for clients/self.
        XCTAssertEqual(rig.bar.state.withLock { $0.revoked }, [token], "DELETE /api/v1/clients/{token_id}")
        XCTAssertTrue(rig.bar.log.contains("usb unpair 74d8a526"), "\(rig.bar.log)")
        XCTAssertFalse(rig.bar.log.contains("wifi unpair"))
        XCTAssertNil(try rig.tokens.token(for: "f412fa3f2a1c"))
        var state = await rig.state
        XCTAssertNil(state.bar)
        XCTAssertEqual(state.phase, .notSetUp)
        XCTAssertEqual(rig.factory.opened.first?.isClosed, true)

        let count = rig.bar.messages.count
        await rig.run(for: 120)
        XCTAssertEqual(rig.bar.messages.count, count, "no messages until it's paired again")
        XCTAssertEqual(rig.factory.opened.count, 1, "the plugged-in port is left alone")

        await rig.unplug()
        await rig.plugIn()
        state = await rig.state
        XCTAssertEqual(state.bar?.deviceID, "f412fa3f2a1c", "replugged: paired again over the cable")
        XCTAssertEqual(state.phase, .connected)
        await rig.connection.stop()
    }

    /// Forget This MiniBar for a bar only ever reached over USB (Wi-Fi
    /// skipped, or client isolation): the token still comes off the bar, as a
    /// USB `request` for `DELETE /api/v1/clients/{token_id}` (api.md 12.2,
    /// Appendix A), so it doesn't keep one of the bar's 10 places.
    func test_forgetOverUSBOnly() async throws {
        let rig = ConnectionRig()
        rig.bar.set { $0.wifi = .offline }
        await rig.start()
        await rig.plugIn()
        var state = await rig.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertEqual(state.bar?.tokenID, "00000001", "the token_id from the USB pair reply")
        let token = try XCTUnwrap(try rig.tokens.token(for: "f412fa3f2a1c"))

        await rig.connection.forget()
        await rig.settle()
        XCTAssertEqual(rig.bar.log.filter { $0.hasPrefix("usb unpair") }, ["usb unpair 00000001"])
        XCTAssertEqual(rig.bar.state.withLock { $0.revoked }, [token], "the bar forgot the Mac")
        XCTAssertTrue(rig.bar.state.withLock { $0.tokens.isEmpty }, "no place kept")
        XCTAssertFalse(rig.factory.made.contains { $0.token == token }, "nothing was tried over Wi-Fi")
        XCTAssertNil(try rig.tokens.token(for: "f412fa3f2a1c"))
        state = await rig.state
        XCTAssertNil(state.bar)
        XCTAssertEqual(state.phase, .notSetUp)
        await rig.connection.stop()
    }

    /// Not plugged in: `DELETE /api/v1/clients/self` over Wi-Fi (api.md 12.3).
    func test_forgetOverWiFi() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        let token = try XCTUnwrap(try rig.tokens.token(for: "f412fa3f2a1c"))
        await rig.connection.forget()
        await rig.settle()
        XCTAssertEqual(rig.bar.log.filter { $0.hasSuffix("unpair") || $0.hasPrefix("usb unpair") }, ["wifi unpair"])
        XCTAssertEqual(rig.bar.state.withLock { $0.revoked }, [token])
        XCTAssertNil(try rig.tokens.token(for: "f412fa3f2a1c"))
        await rig.connection.stop()
    }

    func test_differentBarOnUSB() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        let oldToken = try XCTUnwrap(try rig.tokens.token(for: "f412fa3f2a1c"))
        let newBar = FakeBar(clock: rig.clock, deviceID: "a0b1c2d3e4f5", name: "MiniBar E4F5")
        await rig.plugIn(bar: newBar)
        let state = await rig.state
        XCTAssertEqual(state.bar?.deviceID, "a0b1c2d3e4f5", "the plugged-in bar becomes your MiniBar")
        XCTAssertEqual(state.bar?.name, "MiniBar E4F5")
        XCTAssertEqual(rig.tokens.deviceIDs, ["a0b1c2d3e4f5"], "the old token is deleted")
        XCTAssertEqual(rig.bar.state.withLock { $0.revoked }, [oldToken], "and the old bar is told to forget this Mac")
        XCTAssertEqual(newBar.messages.count, 1)
        await rig.connection.stop()
    }

    func test_C24_authNone() async throws {
        let rig = ConnectionRig()
        rig.bar.set { $0.auth = .notRequired }
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        await rig.plugIn()
        XCTAssertFalse(rig.bar.log.contains("usb pair"), "nothing to pair")
        XCTAssertEqual(rig.tokens.deviceIDs, [])
        await rig.unplug()
        // No Bonjour here: the address it had over USB (minibar.local) isn't
        // where the fake answers, so give it the address as Bonjour would.
        await rig.connection.discovered([DiscoveredBar(name: "MiniBar 2A1C", deviceID: "f412fa3f2a1c", auth: .notRequired,
                                                        endpoint: ConnectionRig.barAddress)])
        await rig.settle()
        let state = await rig.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertEqual(state.link, .wifi)
        XCTAssertEqual(rig.bar.messages.last?.via, .wifi)
        XCTAssertNil(rig.bar.messages.last?.token, "no token")
        await rig.connection.stop()
    }

    func test_manualAddressWithoutPairing() async throws {
        let address = BarEndpoint(host: "10.0.4.99")
        let rig = ConnectionRig { $0.manualEndpoint = address }
        rig.factory.place(rig.bar, at: address)
        await rig.start()
        var state = await rig.state
        XCTAssertEqual(state.bar?.deviceID, "f412fa3f2a1c", "info at the typed address names the bar")
        XCTAssertEqual(state.phase, .unrecognized, "it wants pairing: Pair Again…")
        XCTAssertEqual(rig.bar.messages.count, 0)

        rig.bar.set { $0.auth = .notRequired }
        let open = ConnectionRig { $0.manualEndpoint = address }
        open.bar.set { $0.auth = .notRequired }
        open.factory.place(open.bar, at: address)
        await open.start()
        state = await open.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertEqual(open.messageLog(), ["0.0 wifi idle"])
        await rig.connection.stop()
        await open.connection.stop()
    }

    func test_adoptAfterWiFiPairing() async throws {
        let rig = ConnectionRig()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        let token = "tb1_" + String(repeating: "B", count: 43)
        rig.bar.set { $0.tokens.insert(token) }
        try rig.tokens.setToken(token, for: "f412fa3f2a1c")
        await rig.connection.adopt(KnownBar(deviceID: "f412fa3f2a1c", name: "MiniBar 2A1C", lastEndpoint: ConnectionRig.barAddress,
                                            tokenID: "74d8a526"))
        await rig.settle()
        let state = await rig.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertEqual(rig.bar.messages.first?.token, token)
        await rig.connection.stop()
    }

    // MARK: - Wi-Fi problems the menu explains

    func test_wifiOffAndNotPluggedIn() async throws {
        let rig = ConnectionRig.paired { $0.useWiFi = false }
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        var state = await rig.state
        XCTAssertEqual(state.phase, .notPluggedIn)
        XCTAssertEqual(rig.factory.made.count, 0, "Wi-Fi off: never a request")
        await rig.plugIn()
        state = await rig.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertEqual(rig.factory.made.count, 0, "not even the reachability check")
        await rig.connection.stop()
    }

    func test_localNetworkBlocked() async throws {
        let rig = ConnectionRig.paired()
        await rig.start()
        await rig.connection.discoveryFailed(.localNetworkDenied)
        await rig.settle()
        var state = await rig.state
        XCTAssertEqual(state.phase, .localNetworkBlocked)
        await rig.plugIn()
        state = await rig.state
        XCTAssertEqual(state.phase, .connected, "USB doesn't need it")
        await rig.connection.stop()
    }

    func test_wifiCantReachItHere() async throws {
        let rig = ConnectionRig.paired()   // the bar says Wi-Fi is connected, but nothing answers at its address
        await rig.start()
        await rig.plugIn()
        XCTAssertGreaterThanOrEqual(rig.factory.made.count, 1, "the app tried the bar's Wi-Fi address once")
        await rig.unplug()
        await rig.run(for: 16)
        let state = await rig.state
        XCTAssertEqual(state.phase, .wifiCantReachHere)
        await rig.connection.stop()
    }

    func test_fetchStatus() async throws {
        let rig = ConnectionRig.paired { $0.useWiFi = false }
        await rig.start()
        do {
            _ = try await rig.connection.fetchStatus()
            XCTFail("no link")
        } catch {
            XCTAssertEqual(error as? BarError, .closed)
        }
        await rig.plugIn()
        let status = try await rig.connection.fetchStatus()
        XCTAssertEqual(status.deviceID, "f412fa3f2a1c")
        await rig.connection.stop()
    }

    func test_statesStream() async throws {
        let rig = ConnectionRig.paired()
        var states = rig.connection.states.makeAsyncIterator()
        await rig.start()
        await rig.plugIn()
        var phases: [ConnectionState.Phase] = []
        while let state = await states.next() {
            phases.append(state.phase)
            if state.phase == .connected { break }
        }
        XCTAssertEqual(phases.first, .looking)
        XCTAssertEqual(phases.last, .connected)
        await rig.connection.stop()
    }
}
