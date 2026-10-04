# TinyBar for Mac

A small menu-bar app that shows **On a call** on your TinyBar whenever this Mac's microphone or camera is in use: Slack huddles, Google Meet, Zoom, FaceTime, or any other app. It never listens or records. It only reads whether the mic and camera are in use, and tells the bar "on a call: yes or no", plus the call app's short name ("Slack") if you allow it.

It talks to the bar over the USB cable when the bar is plugged into the Mac, and otherwise over Wi-Fi (plain HTTP to the bar on your local network).

- What it does and why: [`docs/mac-app.md`](../docs/mac-app.md) (the spec and its acceptance criteria).
- How it looks and what it says: [`docs/mac-app-ux.md`](../docs/mac-app-ux.md).
- What goes over the wire: [`docs/api.md`](../docs/api.md), the contract with the bar's firmware. Where these disagree about the wire format, `api.md` wins.

**Status (2026-10-04):** the package is set up. The API messages, USB line framing, JSON and time handling, and the test clock are written and tested against every example in `docs/api.md`. Everything else has its interface, doc comments and a stub body (`unimplemented()`), with a skipped test for each acceptance criterion it has to meet. Nothing has been built or run on a Mac yet.

## Requirements

- **To run:** macOS 14 Sonoma or later, on Apple silicon or Intel. macOS 14 is the minimum because the per-app microphone list the app reads is new in macOS 14.
- **To build:** Xcode 16 or later (Swift 6), or the Command Line Tools for Xcode 16. A universal (Apple silicon and Intel) build needs full Xcode.

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

With a Developer ID, the script signs with the hardened runtime and prints the commands to notarize (`xcrun notarytool submit … --wait`, then `xcrun stapler staple`).

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

Skipped tests (shown as "skipped" in the output) are acceptance criteria that haven't been written yet. Each is named after its criterion in `docs/mac-app.md` (`test_C14_…`).

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
| `Support/` | `Clock` (`TinyClock`, `SystemClock`, `ManualClock`), `RFC3339`, `JSONValue`, `Identifiers`, `Locked`, `Unimplemented` | Done, tested |
| `Detection/` | `Activity` (`MicProcess`, `ActivitySnapshot`, `AppPaths`), `AppCatalog` (call apps, ignored apps, matching, the default lists), `DetectionSettings` (`PauseState`, `PauseChoice`), `CallDetector` | Types done; logic stubbed |
| `Link/` | `Transport` (protocols, `BarEndpoint`, `TransportFactory`), `SerialPort` (POSIX termios, `PseudoTerminal` for tests), `USBTransport`, `HTTPTransport`, `LinkPolicy` (`RetrySchedule`, `ReachabilityTracker`, `LinkChooser`), `BarConnection` (`ReportedCall`, `ConnectionState`) | Interfaces; stubbed (except `ReportedCall.request`) |
| `Pairing/` | `WiFiPairing` (`PairingCode`, `PairingProblem`, `WiFiPairingFlow`) | Interfaces; stubbed |
| `Settings/` | `AppSettings` (`KnownBar`, `SettingsError`), `SettingsStore` (`InMemorySettingsStore`, `UserDefaultsSettingsStore`), `NameRules` | Interfaces; in-memory store done |
| `Platform/` | `PlatformAdapters`: the protocols the macOS target implements (`MicActivitySource`, `CameraActivitySource`, `SerialDeviceWatcher`, `BarDiscovery`, `TokenStore`, `LoginItemService`, `PowerEventSource`) and `InMemoryTokenStore` | Done |
| `Presentation/` | `MenuContent` (`IconState`, `FixItem`, `CountingItem`), `MenuPresenter` (`ConnectionSummary`, `Formatting`) | Types done; logic stubbed |
| `Engine/` | `EngineState`, `TinyBarEngine` (`@MainActor`, `@Observable`) | Interfaces; stubbed |

### `Sources/TinyBarMac` (macOS only)

| File | Uses | Status |
|---|---|---|
| `Main.swift` | AppKit entry point, `.accessory` policy | Done (unverified) |
| `App/AppDelegate.swift` | Owns the engine, status item, windows; quit sends `leaving` | Stub; `Dependencies.live()` written |
| `App/StatusItemController.swift` | `NSStatusItem`, `NSMenu`, SF Symbols | Stub |
| `App/WindowCoordinator.swift` | SwiftUI views in `NSWindow`s | Stub |
| `App/SettingsView.swift`, `App/ConnectView.swift` | SwiftUI | Stub (`UnimplementedView`) |
| `Adapters/CoreAudioMicMonitor.swift` | CoreAudio process objects, `proc_pidpath` | Stub |
| `Adapters/CoreMediaIOCameraMonitor.swift` | CoreMediaIO | Stub |
| `Adapters/IOKitSerialDeviceWatcher.swift` | IOKit matching notifications | Stub |
| `Adapters/BonjourBarBrowser.swift` | `NWBrowser`, `NWConnection` | Stub |
| `Adapters/KeychainTokenStore.swift` | `SecItem…` | Stub |
| `Adapters/MainAppLoginItem.swift` | `SMAppService.mainApp` | Stub |
| `Adapters/SystemPowerEvents.swift` | `NSWorkspace`, `IORegisterForSystemPower` | Stub |

Each stub's doc comment says which Apple APIs to use and what is unverified. `grep -rn "unimplemented(\|UnimplementedView" Sources` lists what's left.

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

- No microphone, camera or audio-capture prompt from reading CoreAudio's per-process list and CoreMediaIO, without a sandbox (criterion 30).
- Which process holds the mic for each call app (the Chrome and Slack helpers, FaceTime's `avconferenced`, Safari's shared WebKit process, Teams, Zoom) and for Siri, Dictation and Voice Control. The default lists in `AppCatalog.defaults` are educated guesses until then.
- AirPods and other Bluetooth mics; which call apps let go of the mic when you mute.
- That opening and closing the bar's serial port doesn't reset it (needs the bar).
- Local Network permission, the Keychain and `SMAppService` with an ad-hoc signature, including after a rebuild.
- How the menu looks: SF Symbol size, the status lines, Option-click, opening Local Network settings.

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
