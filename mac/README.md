# TinyBar for Mac

A small menu-bar app that shows **On a call** on your TinyBar whenever this Mac's microphone or camera is in use: Slack huddles, Google Meet, Zoom, FaceTime, or any other app. It never listens or records. It only reads whether the mic and camera are in use, and tells the bar "on a call: yes or no", plus the call app's short name ("Slack") if you allow it.

It talks to the bar over the USB cable when the bar is plugged into the Mac, and otherwise over Wi-Fi (plain HTTP to the bar on your local network).

- What it does and why: [`docs/mac-app.md`](../docs/mac-app.md) (the spec and its acceptance criteria).
- How it looks and what it says: [`docs/mac-app-ux.md`](../docs/mac-app-ux.md).
- What goes over the wire: [`docs/api.md`](../docs/api.md), the contract with the bar's firmware. Where these disagree about the wire format, `api.md` wins.

**Status (2026-10-05):** the core (`TinyBarCore`) is complete: detection, the API messages, both links, pairing, settings, the engine, and every string the menu, the icon and Settings › Connection show. It follows the mock-up's pairing round: the Wi-Fi page takes a code it gave up off the bar (`pair/cancel`, and quitting waits for a request still on its way), only the bar decides when a code has expired, a `pair` refused for the one-a-second limit is sent once more, every pairing reply has its message, `403 wrong_client` is read like a `401`, and Forget This TinyBar reaches a plugged-in bar over USB. 282 tests run on Linux (Swift 6.0.3), including QA's edge cases; one is skipped there because it needs a non-root user. The macOS app target (`TinyBarMac`: the adapters, the menu and the windows) is written and passes a scratch check against stand-ins for the Apple frameworks, but has never been compiled on a Mac. Nothing has been built or run on a Mac yet; see "Not verified until it runs on a Mac".

## Requirements

- **To run:** macOS 14 Sonoma or later, on Apple silicon or Intel. macOS 14 is the minimum because the per-app microphone list the app reads is new in macOS 14.
- **To build:** Xcode 16.2 or later (Swift 6.0.3), or its Command Line Tools. Every compile so far used Swift 6.0.3, and the code uses typed throws in closures, an area that got compiler fixes during 6.0.x, so Xcode 16.0 and 16.1 are untested. A universal (Apple silicon and Intel) build needs full Xcode.

## Build and run on a Mac

```sh
cd mac
swift build                    # debug build of everything
swift test                     # the core's tests
scripts/build-app.sh           # build/TinyBar.app, ad-hoc signed
cp -R build/TinyBar.app /Applications/
open /Applications/TinyBar.app
```

`scripts/build-app.sh` builds the release executable, wraps it in `TinyBar.app` with `Resources/Info.plist` (no Dock icon, the bundle ID, the version, the Local Network text and Bonjour service), and signs it. Options, as environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `VERSION` | `1.0` | The version people see |
| `BUILD` | number of git commits | The build number |
| `BUNDLE_ID` | `com.tinybar.TinyBarMac` | Use your own reverse domain if you sign with a Developer ID |
| `SIGN_IDENTITY` | `-` (ad hoc) | For example `"Developer ID Application: Your Name (TEAMID)"` |
| `UNIVERSAL` | off | `1` builds for Apple silicon and Intel (needs Xcode) |
| `STABLE_ADHOC_REQUIREMENT` | off | `1` signs an ad-hoc build with the designated requirement `identifier "<BUNDLE_ID>"` instead of its hash, so the Keychain keeps trusting rebuilds (see Troubleshooting). Weaker: any ad-hoc app claiming that identifier would be trusted with TinyBar's tokens. *Unverified.* |

With a Developer ID, the script signs with the hardened runtime and prints the commands to notarize (`xcrun notarytool submit … --wait`, then `xcrun stapler staple`).

**The first time on a Mac,** build both `swift build` (debug) and `swift build -c release`: release turns on optimization passes the debug build doesn't run.

**Use the app bundle for real testing.** `swift run TinyBar`, or running the `TinyBar` scheme after `open Package.swift` in Xcode, starts the bare executable without an Info.plist. Then macOS 15 refuses Bonjour and local network access (no usage text), and Start at login can't register. USB and the menu work.

## Run the tests

On a Mac or on Linux:

```sh
cd mac
swift build
swift test
```

The tests use XCTest and a manual clock, so the start and end delays, heartbeats and back-offs run instantly. The contract tests read `../docs/api.md` directly; if the package is moved out of the repository they're skipped.

**On Linux** (Swift 6.0 toolchain): `TinyBarCore` builds and its tests run. The macOS target `TinyBarMac` builds too, but only as a stub that prints a message, because every file in it is wrapped in `#if os(macOS)`. Swift still checks the syntax of the macOS code on Linux, but not its types: only a build on a Mac does that.

Tests named after an acceptance criterion in `docs/mac-app.md` carry its number (`test_C14_…`). `QA*Tests` are QA's edge cases (flapping, clock changes, a canceled logout, a port vanishing mid-call, log noise on the serial line, pairing failures), kept as regression tests. `PairCancelTests`, `PairingWireTests` and `WrongClientTests` cover the pairing round (2026-10-05): taking a code off the bar (on Back, sleep and quit, including a request still on its way and a cancel a closed window started), every pairing reply in the Mac app's words, the one-a-second `rate_limited` from `pair` sent once more, the messages on the wire against the local HTTP server, and `wrong_client`. `ConnectionTests` covers Forget This TinyBar over USB (`DELETE /api/v1/clients/{token_id}` as a USB `request`) and over Wi-Fi. One test skips when run as root, since `TIOCEXCL` doesn't stop root from opening a port twice.

## How it's put together

Three targets in one Swift package:

| Target | Platforms | What's in it |
|---|---|---|
| `TinyBarCore` | macOS and Linux | Everything that can be plain Swift: deciding when it's a call, the API messages, the USB serial and HTTP transports, the link and heartbeat logic, pairing, settings, and every string the menu shows. |
| `TinyBarMac` | macOS (stub on Linux) | The app: AppKit status item and menu, SwiftUI windows, and the adapters for CoreAudio, CoreMediaIO, IOKit, Bonjour (`Network`), the Keychain (`Security`), login items (`ServiceManagement`) and sleep and wake. |
| `TinyBarCoreTests` | macOS and Linux | XCTest tests of the core. |

How a call travels:

```text
CoreAudioMicMonitor ─┐                                   ┌─ USBTransport ── SerialPort ── /dev/cu.usbmodem…
                     ├─▶ TinyBarEngine ─▶ CallDetector    │
CoreMediaIOCamera… ──┘        │   (events)                │
                              └──────▶ BarConnection ─────┤
IOKitSerialDeviceWatcher ────────────▶ (link, heartbeat,  └─ HTTPTransport ── http://tinybar.local/api/v1/…
BonjourBarBrowser ───────────────────▶  pairing, retries)
                                             │ ConnectionState
TinyBarEngine.state ◀────────────────────────┘
      │
      └─▶ MenuPresenter ─▶ MenuContent ─▶ StatusItemController (NSStatusItem, NSMenu)
```

### `Sources/TinyBarCore`

| Folder | Files | Status |
|---|---|---|
| `API/` | `TinyBarAPI` (constants), `OpenEnum` (string values a later API may add to), `BarTime`, `APIError` (`APIErrorCode`, `APIErrorBody`, `BarError`), `WireJSON`, `CallMessages`, `InfoMessages`, `StatusMessages`, `PairingMessages`, `USBMessages`, `USBFraming` (`@tb ` lines), `Endpoints` | Done, tested |
| `Support/` | `Clock` (`TinyClock`, `SystemClock`, `ManualClock`), `RFC3339`, `JSONValue`, `Identifiers`, `Locked`, `Log` | Done, tested |
| `Detection/` | `Activity` (`MicProcess`, `ActivitySnapshot`, `AppPaths`), `AppCatalog` (call apps, ignored apps, matching, the default lists), `DetectionSettings` (`PauseState`, `PauseChoice`), `CallDetector` | Done, tested |
| `Link/` | `Transport` (protocols, `BarEndpoint`, `TransportFactory`), `SerialPort` (POSIX termios, `PseudoTerminal` for tests), `USBTransport`, `HTTPTransport`, `LinkPolicy` (`RetrySchedule`, `ReachabilityTracker`, `LinkChooser`), `BarConnection` (`ReportedCall`, `ConnectionState`) | Done, tested (USB on pseudo-terminals, HTTP against a local server) |
| `Pairing/` | `WiFiPairing` (`PairingCode`, `PairingProblem`, `WiFiPairingFlow`) | Done, tested |
| `Settings/` | `AppSettings` (`KnownBar`, `SettingsError`), `SettingsStore` (`InMemorySettingsStore`, `UserDefaultsSettingsStore`), `NameRules` | Done, tested |
| `Platform/` | `PlatformAdapters`: the protocols the macOS target implements (`MicActivitySource`, `CameraActivitySource`, `SerialDeviceWatcher`, `BarDiscovery`, `TokenStore`, `LoginItemService`, `PowerEventSource`) and `InMemoryTokenStore` | Done |
| `Presentation/` | `MenuContent` (`IconState`, `FixItem`, `CountingItem`), `MenuPresenter` (`ConnectionSummary`, `Formatting`) | Done, tested (each menu of mac-app-ux.md 4.7) |
| `Engine/` | `EngineState`, `TinyBarEngine` (`@MainActor`, `@Observable`), `BarDiscoveryHub` | Done, tested end to end with fakes |

**Time.** `TinyClock.now()` is monotonic (`ContinuousClock`, anchored to the wall clock at launch), so setting the Mac's clock never stretches a delay, a heartbeat or a pause, or changes `elapsed_s`. Times are converted to the Mac's clock (`wallClockOffset()`) only to show them and to find midnight, in the Mac's auto-updating time zone and format.

### `Sources/TinyBarMac` (macOS only)

Written, but **never compiled on a Mac** (see "Not verified until it runs on a Mac" below). It draws what the core works out: the status item shows `MenuPresenter`'s output, and the adapters feed `TinyBarEngine`.

| File | Uses | What it does |
|---|---|---|
| `TinyBarApp.swift` | AppKit entry point, `.accessory` policy | `@main`; on Linux, a stub that prints a message |
| `App/AppDelegate.swift` | `NSApplicationDelegate`, `ProcessInfo.beginActivity` | Hands over to a copy that's already running and quits; opts out of App Nap for the app's lifetime (idle sleep still allowed); makes the engine with `Dependencies.live()`, the status item and windows; quit is `.terminateLater` until `engine.shutdown()` has sent `leaving` (3-second backstop); reopening opens Settings, or Connect if not set up. Also the hidden main menu (so ⌘W, ⌘, ⌘Q and copy and paste work in the windows), the About panel and the System Settings links |
| `App/StatusItemController.swift` | `NSStatusItem`, `NSMenu`, Observation | Draws `engine.menuContent(showDetails:)`: icon, tooltip, VoiceOver label and value; the menu groups of mac-app-ux.md 4.7, Option-click details, kept current while open (a rebuild waits while a submenu is open) |
| `App/MenuBarIcon.swift` | SF Symbols, `NSBezierPath` | The five icon states; the custom glyphs of mac-app-ux.md 3.3 for the whole set if any symbol is missing |
| `App/WindowCoordinator.swift` | `NSWindow`, `NSTabViewController`, `NSHostingController` | Settings (toolbar tabs, 500 pt wide, titled after the tab) and Connect (440 pt, close button acts as Not Now or Back) |
| `App/SettingsView.swift` | SwiftUI grouped `Form`s | General, Apps (call-app table with editable names, ignored apps, apps seen this session, Add App…), Connection (status, test call, Forget, Wi-Fi switch, Advanced), Privacy |
| `App/ConnectModel.swift`, `App/ConnectView.swift` | SwiftUI, `@Observable` | Welcome / Connect window: USB status, login checkbox, Pair Over Wi-Fi driven by `WiFiPairingFlow`, every message of mac-app-ux.md 5.5; Back and closing the window take a code off the bar (5.4) |
| `Adapters/AdapterQueue.swift` | Dispatch | The private serial queue each adapter's state lives on |
| `Adapters/CoreAudioMicMonitor.swift` | CoreAudio process objects, `proc_pidpath` | Per-process "running input" with listeners (on CoreAudio's own notification thread, not the main run loop) and a 1-second re-read; falls back to the device-level flag (no names) if the process list isn't there. Names apps in Desktop, Documents, Downloads, iCloud Drive and other volumes from LaunchServices, so reading them never brings up a Files & Folders prompt |
| `Adapters/CoreMediaIOCameraMonitor.swift` | CoreMediaIO | "Running somewhere" on every camera, with listeners and a 1-second re-read |
| `Adapters/IOKitSerialDeviceWatcher.swift` | IOKit matching notifications | Arrivals and removals of VID 0x303A / PID 0x1001 serial ports only, plus a 5-second rescan |
| `Adapters/BonjourBarBrowser.swift` | `NWBrowser`, `NWConnection` | Browses `_tinybar._tcp`, reads the TXT record, resolves each bar to an IPv4 address; reports Local Network denied |
| `Adapters/KeychainTokenStore.swift` | `SecItem…` | One generic-password item per bar; an item this build may not change is replaced. Never called on the main thread (a Keychain prompt blocks its caller) |
| `Adapters/MainAppLoginItem.swift` | `SMAppService.mainApp` | Register (not again when already enabled, and not from outside Applications), unregister, status, and why it failed |
| `Adapters/SystemPowerEvents.swift` | `IORegisterForSystemPower`, `NSWorkspace` | Sleep waits up to 1 second for `leaving`; wake; power-off (the app carries on if it's still running 10 seconds later, since a logout can be canceled) |

Two hidden settings help compare looks on a real Mac without rebuilding (relaunch after each):

```sh
defaults write com.tinybar.TinyBarMac IconStyle glyphs          # the custom glyphs instead of SF Symbols
defaults write com.tinybar.TinyBarMac StatusLineStyle view      # status lines as custom views instead of disabled items
defaults delete com.tinybar.TinyBarMac IconStyle                # back to the default
```

### Other files

- `Resources/Info.plist`: the app's Info.plist template (`LSUIElement`, `NSLocalNetworkUsageDescription`, `NSBonjourServices`, `NSAllowsLocalNetworking`; deliberately no microphone or camera usage text).
- `scripts/build-app.sh`: builds and signs `TinyBar.app`.

## Rules the code follows

- **The contract is `docs/api.md`.** `API/` is its Swift form, and `APIContractTests` decodes and re-encodes every example in it. Change `api.md` first, then the types.
- **One exact form for every message.** Outgoing JSON is compact, with keys in byte order, and `cmd` and `id` first on USB lines (`JSONValue.serialized`), so golden tests are byte-for-byte on macOS and Linux.
- **Never on the indicators.** No code may create an IO proc, an `AVCaptureSession` or a process tap, or add a microphone or camera usage description (criterion 30).
- **Never touch DTR or RTS** on the serial port; they can reset the bar (api.md 6.3).
- **Only Espressif's USB Serial/JTAG devices** (VID 0x303A, PID 0x1001) are ever opened (criterion 16).
- **No notifications, no alerts** about the connection. The icon and the menu say what's wrong.
- **Swift 6 language mode** with strict concurrency: core types are `Sendable`; the engine and UI are `@MainActor`; mutable shared state in classes sits behind `Locked`.

## Not verified until it runs on a Mac

From `docs/mac-app.md` and `docs/mac-app-ux.md`, still to check on macOS 14, 15 and 26:

- **That `TinyBarMac` compiles at all, with Xcode 16.2's Swift 6.0.3, in debug and release.** It has only been syntax-checked by the Linux compiler, and checked in a scratch harness (type-checking plus the SIL passes that find Swift 6 data-race, exclusivity and initialization errors) against hand-written stand-ins for the Apple frameworks, written from Apple's documentation, not from the SDK. That catches mistakes in the app's own code, its calls into the core and Swift 6 concurrency, but not a wrong guess about an Apple signature or its `Sendable` and actor annotations. The first Mac build may need small fixes.
- No microphone, camera or audio-capture prompt from reading CoreAudio's per-process list and CoreMediaIO, without a sandbox (criterion 30); and no Files & Folders prompt when an app in Downloads or on the Desktop uses the mic (`NSWorkspace.runningApplications` read from the monitor's queue, and its `bundleURL` matching the path from `proc_pidpath`).
- Which process holds the mic for each call app (the Chrome and Slack helpers, FaceTime's `avconferenced`, Safari's shared WebKit process, Teams, Zoom) and for Siri, Dictation and Voice Control. The default lists in `AppCatalog.defaults` are educated guesses until then.
- AirPods and other Bluetooth mics; which call apps let go of the mic when you mute.
- That opening and closing the bar's serial port doesn't reset it (needs the bar).
- Forget This TinyBar while the bar is plugged in: that the USB `request` for `DELETE /api/v1/clients/{token_id}` takes the Mac off the Remote's Paired devices (the firmware answers it over USB without a token; captured by QA on the fake bar only). And that quitting while the Wi-Fi page's `pair/start` or `pair` is still on its way holds the quit (at most 2 seconds) until the answer has taken the code off the bar, or kept the token it issued.
- Timing on a real Mac: that the App Nap opt-out holds (Activity Monitor's App Nap column says No after a few idle minutes with no window open), the heartbeat stays at 30 s ±1 s and the start delay within 2 s (criteria 13, 33), and Energy Impact stays Low (criterion 36). The 1-second CoreAudio re-read reads every process object; if it shows in Energy Impact, `CoreAudioMicMonitor.safetyNetInterval` can go to 2 seconds. That setting `kAudioHardwarePropertyRunLoop` to NULL moves CoreAudio's notifications off the main run loop. That `Task.sleep` on `ContinuousClock` fires on time after the Mac wakes.
- The Keychain with an ad-hoc signature: whether a rebuild brings up "TinyBar wants to use your confidential information", whether replacing the item (`SecItemDelete`, then `SecItemAdd`) prompts too, and whether `STABLE_ADHOC_REQUIREMENT=1` keeps the access list matching across rebuilds.
- `SMAppService` with an ad-hoc signature: `register()` on an item that's already enabled (skipped now), the statuses it reports, and `openSystemSettingsLoginItems()`.
- Local Network permission with an ad-hoc signature, including after a rebuild; that the refusal arrives as `NWError.dns(-65570)`; and that the browser goes `.ready` once access is allowed, which clears "blocked" in the menu and Privacy.
- App Transport Security: that `NSAllowsLocalNetworking` allows `.local` names, single-label names and IP addresses over plain HTTP, and refuses other DNS names (which Settings and the Connect window now refuse up front with "Use TinyBar's .local name or its IP address.").
- A second copy: that reopening the running copy with `NSWorkspace.openApplication(at:)` reaches its `applicationShouldHandleReopen`, and that `applicationShouldHandleReopen` fires at all for an `LSUIElement` app.
- How the menu looks: SF Symbol size (and that all five symbols exist on macOS 14, `rectangle.slash` included), the status lines, Option-click, opening Local Network settings; that `menu.highlightedItem` reports the open submenu's parent while the menu tracks, so a rebuild waits for it.
- The adapters' other Apple calls behave as their doc comments say: the CoreAudio process list on macOS 14.0 and 14.1 (the device-level fallback is used only if it's missing), IOKit notifications on a dispatch queue, resolving a Bonjour service with a UDP `NWConnection`, and `IOAllowPowerChange` delaying sleep.
- The windows: Settings' toolbar tabs and their heights, the window titled after the tab through each tab's own title, the tables in the grouped Apps form, the Connect window resizing between pages, Esc and ⌘W, and focus in the code field.

## For IT

TinyBar for Mac is meant to be safe to run on a managed work Mac. In short:

- **What it reads:** whether the microphone and camera are in use, and which app is using the mic (CoreAudio and CoreMediaIO state only). It never turns them on, listens, records or captures. It keeps no record of which apps used the mic.
- **What it never asks for:** microphone, camera, screen recording, Accessibility, Input Monitoring, Automation, Contacts or Calendar access. Its Info.plist has no microphone or camera usage text, so macOS couldn't grant it either.
- **What it talks to:** only the TinyBar on the person's desk, over the USB serial cable or plain HTTP on the local network (port 80, Bonjour service `_tinybar._tcp`). It makes no internet connections: no analytics, crash reports or update checks.
- **What it sends:** whether the person is on a call, optionally the call app's short name ("Slack"), and a random install ID so the bar can tell Macs apart. Never the computer's or the user's name, serial numbers or hardware addresses. Wi-Fi messages are unencrypted HTTP on the local network.
- **What it needs:** Local Network access on macOS 15 and later, for Wi-Fi only. MDM can't grant this ahead of time; each person answers the prompt. On macOS 15.5 and later, an administrator can exempt a subnet for all apps (`AllowedWiFiLocalNetworkAddresses`, `AllowedEthernetLocalNetworkAddresses`). Optionally a login item (`SMAppService`), which MDM can manage or block; the app reports a failure instead of failing silently.
- **Bundle ID:** `com.tinybar.TinyBarMac` by default; anyone signing with their own Developer ID changes it to their own reverse domain.
- **How it's signed:** ad hoc by default (`codesign --sign -`), or with a Developer ID and notarized. A downloaded ad-hoc app is blocked by Gatekeeper until allowed under System Settings › Privacy & Security › Open Anyway (which a managed Mac can turn off); building it on the Mac avoids the download quarantine. Allow-listing tools can only allow an ad-hoc app by its hash, which changes with every build; a Developer ID lets IT allow it once by Team ID. Local Network privacy also tracks apps by signature, so a rebuilt ad-hoc app may be asked again.
- **Networks that block it:** a full-tunnel VPN, Wi-Fi client isolation, or a bar on a separate network stop the Wi-Fi link. USB still works. Security tools that block unknown USB devices stop the USB link; the app then uses Wi-Fi.

## Troubleshooting

- **Flashing new firmware onto the bar:** the app holds the bar's USB port exclusively, so a flasher would fail with "Resource busy". Hold Option, open the TinyBar menu and choose **Pause USB** (also in Settings › Connection › Advanced), flash, then choose **Resume USB** or replug the bar.
- **Wi-Fi doesn't work on macOS 15 or later:** check System Settings › Privacy & Security › Local Network, and make sure TinyBar is on.
- **After rebuilding, macOS asks whether TinyBar may use "your confidential information stored in TinyBar":** that's the bar's pairing token in your login keychain, and an ad-hoc build's signature changes with every build. Choose **Always Allow**. (If you choose Deny, the app replaces the item with a new one the next time it pairs, over USB at once if the bar is plugged in.) `STABLE_ADHOC_REQUIREMENT=1 scripts/build-app.sh` may stop the question for later rebuilds.
- **Two copies:** if TinyBar is already running (say the one in Applications, started at login) and you open another (from `build/`), the new one hands over to the running one, which opens its Settings, and quits.
