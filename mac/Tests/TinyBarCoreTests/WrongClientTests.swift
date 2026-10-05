import Foundation
import XCTest
@testable import TinyBarCore

/// `403 wrong_client` on a call (api.md 5.2): the bar knows this Mac's token,
/// but it was paired for another `client` (for example, the app's settings
/// were reset and the install ID made again). The app treats it as "this
/// Mac's token doesn't match": it drops the token and offers Pair Again, as
/// for `401` (api.md 16), and also revokes the token on the bar so it doesn't
/// keep one of the 10 places for nobody.
final class WrongClientTests: XCTestCase {
    let slack = ReportedCall(active: true, app: "Slack", callID: 1, startedAt: ManualClock.apiExampleStart)

    func test_refusesToken() {
        let unauthorized = BarError.api(APIErrorBody(error: .unauthorized), httpStatus: 401)
        let wrongClient = BarError.api(APIErrorBody(error: .wrongClient, field: "client"), httpStatus: 403)
        XCTAssertTrue(unauthorized.refusesToken)
        XCTAssertTrue(wrongClient.refusesToken)
        XCTAssertFalse(wrongClient.isUnauthorized, "a 403, not a 401")
        XCTAssertFalse(wrongClient.isConnectionProblem)
        for other: APIErrorCode in [.wrongScope, .badOrigin, .wrongCode, .badValue] {
            XCTAssertFalse(BarError.api(APIErrorBody(error: other), httpStatus: 403).refusesToken, other.rawValue)
        }
        XCTAssertFalse(BarError.timedOut.refusesToken)
    }

    func test_wrongClientOverWiFi() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        XCTAssertEqual(rig.messageLog(), ["0.0 wifi idle"])
        let token = try XCTUnwrap(rig.tokens.token(for: "f412fa3f2a1c"))

        // The bar still knows the token, but for another install ID.
        rig.bar.set {
            $0.tokens.remove(token)
            $0.foreignTokens.insert(token)
        }
        await rig.connection.report(slack)
        await rig.settle()
        var state = await rig.state
        XCTAssertEqual(state.phase, .unrecognized, "TinyBar 2A1C doesn't recognize this Mac · Pair Again…")
        XCTAssertNil(try rig.tokens.token(for: "f412fa3f2a1c"), "the token is dropped")
        XCTAssertNil(state.bar?.tokenID)

        let log = rig.bar.log
        let refusal = try XCTUnwrap(log.firstIndex(of: "wifi call wrong_client"))
        let after = Array(log[refusal...])
        XCTAssertEqual(after, ["wifi call wrong_client", "wifi info", "wifi unpair"],
                       "info first (api.md 16): it's the paired bar; then the bar forgets the token")
        XCTAssertEqual(rig.bar.state.withLock { $0.revoked }, [token])
        XCTAssertTrue(rig.bar.state.withLock { $0.foreignTokens.isEmpty }, "no place kept for nobody")

        await rig.run(for: 120)
        XCTAssertEqual(rig.bar.log.filter { $0 == "wifi call wrong_client" }.count, 1, "no token, no more tries")

        // Plugged in: it pairs again over the cable without asking (mac-app-ux.md 5.3).
        await rig.plugIn()
        state = await rig.state
        XCTAssertEqual(state.phase, .connected)
        XCTAssertNotNil(try rig.tokens.token(for: "f412fa3f2a1c"))
        XCTAssertTrue(rig.bar.log.contains("usb pair"))
        await rig.connection.stop()
    }

    /// As for `401`: the address now refuses everything, `info` included
    /// (another device took the bar's address), so it isn't known to be the
    /// paired bar. The token is kept, and nothing is revoked.
    func test_wrongClientFromSomethingThatIsntThePairedBarKeepsTheToken() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        rig.bar.set { $0.wifiFailure = .api(APIErrorBody(error: .wrongClient, field: "client"), httpStatus: 403) }
        await rig.connection.report(slack)
        await rig.settle()
        XCTAssertNotNil(try rig.tokens.token(for: "f412fa3f2a1c"))
        let state = await rig.state
        XCTAssertNotEqual(state.phase, .unrecognized)
        XCTAssertFalse(rig.bar.log.contains("wifi unpair"))
        await rig.connection.stop()
    }

    /// A `401` token is unknown to the bar already: nothing to revoke.
    func test_unauthorizedRevokesNothing() async throws {
        let rig = ConnectionRig.paired()
        rig.factory.place(rig.bar, at: ConnectionRig.barAddress)
        await rig.start()
        rig.bar.set { $0.tokens.removeAll() }
        await rig.connection.report(slack)
        await rig.settle()
        let state = await rig.state
        XCTAssertEqual(state.phase, .unrecognized)
        XCTAssertFalse(rig.bar.log.contains("wifi unpair"))
        await rig.connection.stop()
    }
}
