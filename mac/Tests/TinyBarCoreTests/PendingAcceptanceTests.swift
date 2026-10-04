import XCTest
@testable import TinyBarCore

// The acceptance criteria from docs/mac-app.md (as updated by api.md 14.5 and
// mac-app-ux.md 10.1) that the core can check on Linux. Each is skipped until
// its builder writes it; replace the `XCTSkip` with the test, and keep the
// criterion number in the name so the QA report can map them.
//
// Owners: "core" is the TinyBarCore builder, "mac" the TinyBarMac builder
// (who also owns Presentation/ in the core).

private func pending(_ owner: String, _ what: String) throws -> Never {
    throw XCTSkip("Not written yet (\(owner)): \(what)")
}

// Deciding a call (criteria 1 to 11, pause choices): written, in DetectionTests.

/// Messages and links: `BarConnection`, the transports, the policies.
final class PendingLinkTests: XCTestCase {
    func test_C13_heartbeat() throws { try pending("core", "while connected, the full state every heartbeat_s (30 s ±1, then the latest reply's value), on every change, at once when a link comes up; idle heartbeats too; seq +1 per message") }
    func test_C14_usbHandshakeOnAPseudoTerminal() throws { try pending("core", "fake bar on a PseudoTerminal: hello first and nothing else until device TinyBar; log lines ignored; hello every 2 s; no answer in 10 s → closed and never written again") }
    func test_C15_linkChoice() throws { try pending("core", "both links up → USB; USB drops → next message over Wi-Fi within 5 s with the current state; USB back → moves back within 5 s") }
    func test_C17_replies() throws { try pending("core+mac", "showing own while active → Set aside line, no extra messages; sources.mac false → Paused icon and ignoring line, heartbeat continues; 401 → unrecognized, Pair Again") }
    func test_C18_retriesAndNotConnected() throws { try pending("core", "retries after 2, 5, 10, 30 s then every 30 s; Not connected after 15 s without success") }
    func test_C19_leaving() throws { try pending("core", "sendLeaving sends leaving:true with active:false over the current link, within about 1 s, never throws") }
    func test_usbMissedReplies() throws { try pending("core", "3 USB replies missed in a row → port closed, Wi-Fi used at once, port reopened after 10 s or on the next IOKit arrival") }
    func test_readyEvent() throws { try pending("core", "a ready event → hello, then the call state at once") }
    func test_wrongDevice() throws { try pending("core", "a reply from another device_id is not accepted; on 401 info is checked first (api.md 16)") }
    func test_apiVersionMismatch() throws { try pending("core", "unsupported_api or another major in info → barNeedsUpdate / appNeedsUpdate") }
    func test_serialPortOnAPseudoTerminal() throws { try pending("core", "SerialPort opens a pty raw, 8N1, reads and writes, times out, reports closed; exclusive open makes a second open fail with busy") }
    func test_httpTransportAgainstALocalServer() throws { try pending("core", "HTTPTransport: path, Content-Type, Bearer only where Endpoint.sendsToken, User-Agent, 5 s time-out → timedOut, refused → unreachable, error bodies → api") }
    func test_barEndpointParsesUserInput() throws { try pending("core", "BarEndpoint(userInput:) accepts tinybar.local, 10.0.4.42, :8080, http:// and a trailing slash; refuses paths and spaces") }
}

/// Pairing, the token store and the settings.
final class PendingPairingAndSettingsTests: XCTestCase {
    func test_C20_usbPairsAutomatically() throws { try pending("core", "first USB connection sends pair, stores the token in the TokenStore (never in AppSettings), and the bar's name shows") }
    func test_C21_bearerAndForget() throws { try pending("core", "Wi-Fi requests carry Authorization: Bearer; Forget removes the token, sends DELETE clients/self, stops messages, ignores the plugged-in port until replugged") }
    func test_C22_codePairing() throws { try pending("core", "WiFiPairingFlow: pair/start then pair; wrong_code with tries left, codeUsedUp, not_pairing, pairing_busy, rate_limited, token_limit, in_setup, time-out; each maps to its PairingProblem") }
    func test_C23_onlyThePairedBar() throws { try pending("core", "with two bars discovered, only the one whose id matches the paired device_id is used") }
    func test_C24_authNone() throws { try pending("core", "auth none: no pairing, no token, messages still sent; Connect says no pairing needed") }
    func test_differentBarOnUSB() throws { try pending("core", "plugging in a different TinyBar pairs it and forgets the old token (mac-app-ux.md 5.3)") }
    func test_pairingCode() throws { try pending("core", "PairingCode.normalize ignores spaces and dashes and needs 6 digits; display groups 3+3") }
    func test_C27_settingsPersist() throws { try pending("core", "UserDefaultsSettingsStore round-trips every field; unreadable data → nil; Restore Defaults per tab restores the spec's values and keeps the bar") }
    func test_C32_noHistoryOnDisk() throws { try pending("core", "the saved settings contain no record of which apps used the mic or when") }
    func test_nameRules() throws { try pending("core", "app names 1–24 printable ASCII after trimming; Mac names 1–32; messages per mac-app-ux.md 6.3") }
}

/// What the menu says: `MenuPresenter` and `Formatting` (owned by the mac builder).
final class PendingPresentationTests: XCTestCase {
    func test_menusOfUX47() throws { try pending("mac", "each menu drawn in mac-app-ux.md 4.7 comes out of MenuPresenter exactly (lines, fix item, counting items, pause item)") }
    func test_iconPrecedence() throws { try pending("mac", "Needs you > Paused (incl. sources.mac false) > Not connected > On a call > Connected; 15 s grace; a test call shows On a call") }
    func test_C26_dontCountAndUndo() throws { try pending("mac+core", "Don't Count appears only while a counted app uses the mic; Count Again undoes it while still in use; camera equivalents") }
    func test_C28_seenAppsInMemory() throws { try pending("core", "Used the mic since TinyBar opened lists this session's apps and starts empty") }
    func test_formatting() throws { try pending("mac", "durations 0m, 12m, 1h 5m; times 2:04 PM; waits 45 seconds, 4 minutes, singulars") }
    func test_connectionSummary() throws { try pending("mac", "Settings › Connection status and buttons for each row of mac-app-ux.md 6.4") }
}
