# TinyBar for Mac: v1 spec

The product manager's spec for the Mac app: a small menu-bar app that sets the bar's automatic **On a call** status. Written 2026-10-04. Every choice here that the user hasn't made yet is marked **Proposed**, and the same proposals are listed in `docs/decisions.md` under "Mac app".

- **Read first:** `docs/decisions.md` (Automatic status and Mac app) and the mock-up `docs/mockup.html`, which is the source of truth for the bar's side: the On a call screen, the Remote's "Connect your Mac" card, and the section "How the Mac app talks to TinyBar" (`#api`).
- **Wire format:** `docs/api.md` is the contract between this app and the firmware. *On 2026-10-04 it hasn't been written yet, so this spec uses the mock-up's API section.* Where this spec and `docs/api.md` disagree about the wire format, `docs/api.md` wins and this spec gets updated. The additions this spec asks for are listed in [What the bar's API needs to add](#what-the-bars-api-needs-to-add).

## The problem

TinyBar tells the people near your desk whether it's a good time to interrupt. The calendar covers meetings that are scheduled. The ones it misses are the unplanned ones: a Slack huddle, a quick Google Meet, a FaceTime call, a Zoom call someone sends a link to. Those are exactly the times a colleague walks up and starts talking, and nobody remembers to change the bar first.

Whatever the app, your Mac already knows you're on a call: the microphone (and often the camera) is in use. The Mac app watches for that and tells the bar. It does this without listening: it never hears or records anything, and it sends the bar nothing except whether you're on a call and, optionally, the name of the app.

## What v1 does

A menu-bar app (no Dock icon) that:

1. Watches whether any app is using the Mac's **microphone**, and which app, and whether any **camera** is in use.
2. Decides when that counts as **a call**. Brief use doesn't count, and neither does dictation, Siri or Voice Memos, plus any app you add to the ignore list.
3. Tells the bar `active: true` with the app's short name when a call starts, `active: false` when it ends, and repeats the current state every 30 seconds.
4. Uses the **USB cable** when the bar is plugged into the Mac, and otherwise **Wi-Fi** (HTTP to the bar on the local network), after a one-time pairing.
5. Shows what it's doing in the menu bar, has a small Settings window, and can start at login.

## Scope

### In v1

- Mic detection per app (macOS 14 and later) and camera detection (any app, no app name).
- Start and end delays, the call-app list, the ignore list, and the "only call apps" mode.
- Sending on/off and the app's short name over USB serial or Wi-Fi, with a heartbeat, retries and the USB-first rule.
- Pairing: automatic over USB, or with a code over Wi-Fi (this needs the bar's support; see Pairing).
- The menu-bar icon and menu, the Settings window, a first-run window, launch at login, Pause.
- A build from source with Swift (Swift Package Manager or Xcode), **ad-hoc signed** by default, or signed with the user's Developer ID and notarized if they have one.
- A short "For IT" note in the app's README.

### Not in v1

- **No audio, ever:** no recording, no levels, no voice detection, no transcription.
- **No reading browser tabs or window titles.** A call in a browser shows as the browser ("Chrome"), never the website ("Google Meet").
- **No status control from the Mac.** You can't set Busy, Away, a message or a manual "On a call" from the app. This matches the decision that On a call is automatic only, and that the bar and the Remote own your own status.
- No calendar on the Mac (the bar reads the calendar itself), and no meeting titles or participants.
- **One Mac per bar.** Two Macs feeding one bar is a later step (see Open questions).
- Calls on your iPhone that aren't routed through the Mac.
- Notifications, sounds, auto-update, analytics and crash reporting. The app never contacts the internet.
- The Mac App Store, a DMG, Homebrew and Windows or Linux versions.
- The online relay for when you're out of the office (deferred in decisions).

## How it decides you're on a call

### Signals

| Signal | How it's read (macOS API) | What it tells us |
|---|---|---|
| **Mic in use, per app** | CoreAudio's process objects: `kAudioHardwarePropertyProcessObjectList`, then each process's `kAudioProcessPropertyIsRunningInput`, `kAudioProcessPropertyBundleID` and `kAudioProcessPropertyPID`. These are new in macOS 14; they aren't in the macOS 13.3 SDK. | Which processes have audio input running right now, on any input device (built-in, USB, Bluetooth or virtual). |
| **Camera in use** | CoreMediaIO: each device in `kCMIOHardwarePropertyDevices`, and its `kCMIODevicePropertyDeviceIsRunningSomewhere`. | Whether any camera is running. There's **no public API for which app is using it**, so camera use has no name. |

The app listens for changes to these properties and also re-reads them once a second as a safety net. It doesn't open an input stream, create an IO proc, start an `AVCaptureSession` or create a process tap. It only reads state.

**Why not just the device's "running somewhere" flag for the mic?** It's what simpler apps use, but it can't tell which app is using the mic, so dictation and Siri couldn't be ignored. On a combined headset device it also reads as running while music plays to the headset, and a developer reported on Apple's forums (thread 741026, 2024) that it always reads as inactive for Bluetooth mics. The per-process flag is about input only, and it follows the app, not the device. So the mic signal is the per-process flag alone, and that's the reason for macOS 14 as the minimum.

### Which apps count

Each process using the mic is matched to an **app**:

1. Its bundle ID, matched exactly or as a prefix followed by a dot (so `com.google.Chrome.helper` matches `com.google.Chrome`).
2. If that doesn't match a list, the **outermost `.app` bundle** in the process's executable path is used instead (from `proc_pidpath`). This maps helper processes to their app: Chrome's audio service runs in "Google Chrome Helper" inside `Google Chrome.app`, and Slack's runs inside `Slack.app`.
3. Otherwise the process is an **unknown app**, shown in the menu by its bundle ID or process name.

Each app then falls into one of three groups. When an app is in two, **Ignored wins.**

**Proposed: Call apps** (counted, and the bar gets the short name). The bundle IDs are the ones the apps are known to use and must be confirmed on a real Mac, including which helper process actually holds the mic.

| Name sent to the bar | Matches |
|---|---|
| Slack | `com.tinyspeck.slackmacgap` |
| Zoom | `us.zoom.xos` |
| Teams | `com.microsoft.teams2`, `com.microsoft.teams` |
| FaceTime | `com.apple.FaceTime`, and the system process that runs FaceTime audio (expected: `avconferenced`; to confirm) |
| Webex | `Cisco-Systems.Spark` |
| Discord | `com.hnc.Discord` |
| WhatsApp | `net.whatsapp.WhatsApp` |
| Signal | `org.whispersystems.signal-desktop` |
| Chrome | `com.google.Chrome` (Google Meet in Chrome shows as Chrome) |
| Safari | `com.apple.Safari`. Safari's media runs in WebKit's shared GPU process (`com.apple.WebKit.GPU`), which other apps also use, so this mapping needs checking. If it can't be told apart, the name is left out. |
| Firefox | `org.mozilla.firefox` |
| Edge | `com.microsoft.edgemac` |
| Arc | `company.thebrowser.Browser` |
| Brave | `com.brave.Browser` |

Names are short because the bar's kicker reads "From your Mac · Slack", and the bar shows at most 24 characters. You can add any app (Settings › Apps › Add app…, which picks an app from Applications) and edit the name it sends.

**Proposed: Ignored apps** (never start or prolong a call):

- **Siri**, including "Hey Siri" listening.
- **Dictation**, including the dictation key.
- **Voice Control.**
- **Voice Memos** (`com.apple.VoiceMemos`). A memo isn't a call, and a memo longer than the start delay would otherwise count. Remove it from the list if you want recordings to show On a call.

The processes behind Siri, Dictation and Voice Control are system daemons whose names must be found on a real Mac for macOS 14, 15 and 26. The expected ones are `corespeechd`, `assistantd` and Siri's own process, and the `SpeechRecognitionCore` helpers. The app's defaults list them by bundle ID or process name once confirmed. You can add any app to the ignore list, either from Settings or with one click in the menu ("Don't count Zoom" while Zoom is using the mic).

**Proposed: Other apps** (anything not in either list, such as GarageBand, an unknown call app or a new one):

- **Default mode, "Any app except ignored ones":** other apps count as a call, **without a name**. This catches call apps nobody listed, which is the point of the app.
- **"Only the call apps on my list":** other apps don't count, and neither does the camera, because camera use can't be tied to an app. Settings dims "Count the camera" in this mode and says why.

**Proposed: Camera.** "Count the camera" is on by default. Camera use on its own counts as a call (with no name) after the start delay. Almost every call also uses the mic, so the camera mostly matters when you're muted on video and the call app has let go of the mic. The known downside is that Photo Booth, a webcam utility or a camera preview also counts. Turn the switch off if that happens.

### Timing

- **Proposed: start delay 3 s** (adjustable from 0 to 30 s, in whole seconds). Activity has to be continuous for this long before the app sends `active: true`. It filters blips: apps that open the mic for a moment at launch, a browser checking a device, a notification sound tool. It does **not** filter dictation or a Siri request, which often last longer than 3 s. The ignore list does that.
- **Proposed: end delay 10 s** (adjustable from 3 to 60 s). Activity has to be gone this long before the app sends `active: false`. It bridges short gaps inside a call: switching to AirPods, moving from a lobby into the call, or an app restarting its audio. The bar's own 90-second timeout is separate and only matters if the app goes silent.
- **Changes you make take effect at once.** If a change leaves nothing that counts, the call ends immediately, with no end delay. That happens when you ignore the app that's on the call, turn off the camera during a camera-only call, pause, or switch to "only call apps" during a call from an unlisted app.

Examples, with the defaults:

| What happens | What the bar shows |
|---|---|
| Slack huddle: mic on at 2:04:00 PM, off at 2:31:12 PM | On a call from 2:04:03 to 2:31:22 |
| Dictation for 40 s | Nothing (ignored) |
| An app opens the mic for 1.5 s at launch | Nothing (shorter than the start delay) |
| A Zoom call, AirPods connect mid-call and the mic is idle for 2 s | One call, not two |
| Meet in Chrome, then straight into a Zoom call with no gap | One call; the name changes from Chrome to Zoom |

### The app name

- **Proposed: names go only with call apps.** A call from a listed call app sends its short name. A call from any other app, or from the camera alone, sends no `app` at all, so the bar shows "From your Mac" with no name. That way the name of an unlisted app (which could be anything) is never displayed to the office or returned by the bar's API.
- **Proposed: "Send the app's name to TinyBar"** is a setting, **on by default** (it matches the bar's design, "From your Mac · Slack"). Off, the app never sends `app`.
- **Which name, when several apps are using the mic:** a call app beats other apps. Among call apps, the one that started using the mic most recently wins. If the name changes during a call, the app sends one `active: true` with the new name; the call doesn't end and restart. If a call carries on with only the camera, it keeps the last name it sent.

### Other situations

- **Pause.** The menu's Pause offers **For 1 hour**, **For the rest of today** (until midnight) and **Until I resume**. Pausing during a call sends `active: false` at once. Nothing counts while paused, and the heartbeat carries on with `active: false`. Detection resumes on its own at the end of the pause, and a call already going then counts after the start delay, measured from when the pause ended.
- **Sleep, log out, restart, quit.** Before the Mac sleeps, and when the app quits, the app sends `active: false` (best effort) if a call is on. After waking, it starts fresh. If the app crashes or the Mac loses power, the bar ends the call on its own after 90 seconds (decided).
- **Screen locked.** Detection continues. You can still be on a call, for example on AirPods.
- **Set aside on the bar.** If you tap the bar during a call, the bar replies `"showing": "own"` while the app is sending `active: true`. That's your choice, not an error: the menu says "Set aside on the bar", and the app keeps sending its heartbeat without trying to take over again. The bar shows the next call when it starts (decided).
- **Calls from your Mac switched off on the bar.** The bar replies `calls_off`. The menu says "Calls from your Mac are off on TinyBar (turn them on in the Remote)". The app keeps its heartbeat going, so the bar knows about the call the moment the switch goes back on.
- **Muted calls.** Some apps release the mic when you mute. If the camera is off too, the Mac sees no activity, and the call ends after the end delay. *Unverified:* which of the call apps above do this has to be measured on a real Mac (acceptance criterion 34). Until then it's a known limitation, and the end delay is the only bridge.

## Talking to the bar

### USB (preferred)

- **Finding the bar:** the app looks only at USB serial devices whose vendor is **Espressif (USB vendor ID 0x303A)**, using IOKit, and opens their `/dev/cu.*` node (never `/dev/tty.*`). It never sends anything to other serial devices, such as a 3D printer, a microcontroller board or a modem.
- **Handshake:** it sends `{"cmd": "hello"}` and expects `{"ok": true, "device": "TinyBar", …}` within 2 seconds. It ignores any line that isn't JSON, because the bar's boot log may print on the same port. If no TinyBar answers, it closes the port and leaves it alone until the device is plugged in again.
- **Messages:** one JSON object per line, UTF-8, ending in `\n`, with `"cmd": "call"` added: `{"cmd": "call", "active": true, "app": "Slack"}`. The bar answers every line with one line.
- **Plugging in and out:** the app watches IOKit for USB devices arriving and leaving, and also rescans every 5 seconds.
- **Don't restart the bar.** The ESP32-S3's built-in USB port can reset the chip on certain DTR and RTS line changes; that's how flashing tools reset it. The app must open and close the port without resetting the bar. The firmware team says which line settings are safe, and acceptance criterion 16 tests it.
- **No permission needed:** macOS doesn't ask before an app (outside the App Store sandbox) opens a serial port.
- **"Powered from the Mac"** means a USB data connection. A bar on a phone charger, or on a charge-only cable, has no serial port, so the app uses Wi-Fi.

### Wi-Fi (fallback)

- `POST http://<bar>/api/call` with `{"active": true, "app": "Slack"}` or `{"active": false}`, `Content-Type: application/json`, and `Authorization: Bearer <token>` once paired. The reply is `{"ok": true, "showing": "call"}`, or `403 {"ok": false, "error": "calls_off"}`.
- `<bar>` is the paired bar's address: `tinybar.local` until bars have names of their own (see Pairing), or an address typed in Settings.
- The app uses URLSession with a 3-second timeout. Its Info.plist allows plain HTTP to local addresses (`NSAllowsLocalNetworking`) and sets a plain `User-Agent: TinyBar-Mac/<version>`, so the request doesn't carry the macOS and Darwin versions that URLSession adds by default.
- **macOS 15 and later ask for Local Network permission** the first time the app reaches the bar over Wi-Fi ("TinyBar would like to find and connect to devices on your local network"). Resolving a `.local` name and opening a TCP connection to the local network both need it (Apple TN3179). The system may fail the first request before you answer, so the app retries. If you choose Don't Allow, Wi-Fi can't work: the menu says so, with a button that opens System Settings › Privacy & Security › Local Network. USB doesn't need this permission.

### Choosing the link

- **USB first** (decided). If a TinyBar answers on USB, every message goes over USB, even if Wi-Fi works too.
- When USB goes away, the app switches to Wi-Fi within 5 seconds and sends the current state at once. When the bar is plugged back in, it moves back to USB within 5 seconds.
- The bar accepts either link, and the latest message wins (decided).

### Heartbeat, retries and errors

- The app sends its state **on every change**, **again every 30 seconds** (decided), and **at once whenever a link comes up**. It sends `active: false` heartbeats too, so the bar can show its Mac icon while a Mac is connected.
- On a failure, it retries after 2, 5, 10 and 30 seconds, then every 30 seconds. The menu shows **Not connected** once nothing has gotten through for 15 seconds.
- **No alerts and no notifications** for connection problems. The menu-bar icon and the menu say what's wrong.

### Pairing

Pairing does two things. It makes sure the app talks to **your** bar when there's more than one TinyBar in the office, and it gives the app a token for the `Authorization` header that the bar reserves (decided: reserved, not checked yet).

- **Proposed: pair automatically over USB.** The first time the bar answers on USB, the app asks it for its ID and a token (`{"cmd": "pair"}`) and saves both. Plugging the bar into your Mac is the consent, and it needs no codes. The first-run window says: "Plug TinyBar into this Mac once to pair it. After that it works over Wi-Fi too."
- **Proposed: or pair with a code over Wi-Fi.** For a bar that's never plugged into the Mac: on the Remote's Connect your Mac card, Pair a Mac shows a **6-digit code** that lasts 5 minutes and works once. You type it into the app's Settings, and the app sends it to the bar to get a token. The Remote has no PIN yet (open in decisions), so this is only as protected as the Remote itself, and it gets stronger when the Remote does.
- **The token** is random, issued by the bar, kept in the macOS **Keychain**, and never shown. "Forget this TinyBar" deletes it. If the bar answers `401` (once the bar checks tokens), the menu says "Pair TinyBar again".
- **Proposed: until the bar supports pairing,** the app talks to `tinybar.local` (or the address typed in Settings), sends no token, and Settings says "This TinyBar doesn't support pairing yet."
- **No Mac details are sent when pairing:** not the computer's name, the user's name, a serial number or a hardware address. The bar knows a paired Mac only by the token it issued.

### What the bar's API needs to add

These are for `docs/api.md` and the firmware, and are **Proposed**:

1. **A bar ID** (stable, for example from the Wi-Fi MAC address) in the `hello` reply and in `GET /api/status`, plus a short form for display ("TinyBar 3F2A").
2. **`{"cmd": "pair"}` over USB**, answered with `{"ok": true, "id": "…", "token": "…"}`.
3. **`POST /api/pair` with `{"code": "123456"}`** over Wi-Fi, answered with the same `id` and `token`, or `{"ok": false, "error": "bad_code"}`. The Remote's Connect your Mac card gets a Pair a Mac button that shows the code, and a list of paired Macs with Forget. *This changes the mock-up.*
4. **A Bonjour service** (`_tinybar._tcp`, with the ID in its TXT record), so the app can find its bar by ID. Every bar in an office can't be `tinybar.local`: mDNS gives a second bar a different name, so the app shouldn't depend on the name.
5. **`401 {"ok": false, "error": "unauthorized"}`** once the bar checks tokens.

## Menu bar

- **Proposed: the icon** is a monochrome template image (macOS tints it for light and dark menu bars) of a small bar, in four states:
  - **Connected, not on a call:** outline.
  - **On a call:** filled.
  - **Not connected or not paired:** outline with a small slash.
  - **Paused:** outline with a pause mark.

  The state never relies on color alone, and it's always spelled out in the menu. The UX designer draws the icon.
- **The menu**, top to bottom (Proposed copy):
  1. **What the Mac sees:** "On a call · Slack · 12m", "Not on a call", "Mic in use by Voice Memos (ignored)", "Camera in use", or "Paused until 3:15 PM".
  2. **The connection:** "TinyBar 3F2A · USB", "TinyBar 3F2A · Wi-Fi", "Looking for TinyBar…", "Not connected · retrying" or "Not paired".
  3. **What the bar shows**, from its last reply: "Bar: On a call", "Bar: set aside on the bar", "Bar: calls from your Mac are off", "Bar: in Wi-Fi setup" or "Bar: switched off".
  4. **"Don't count Zoom"**, only while a counted app is using the mic. It adds the app to the ignore list.
  5. **Pause** ▸ For 1 hour, For the rest of today, Until I resume. While paused, this item is **Resume** instead.
  6. **Settings…** (⌘,), **About TinyBar**, **Quit TinyBar** (⌘Q).
- **No Dock icon** (`LSUIElement`), and the app doesn't take focus except when its windows are open.
- Durations follow the bar's style: "12m" and "1h 5m".

## Settings

One window with four sections. Settings are saved with UserDefaults; the token is in the Keychain. **Reset to defaults** is at the bottom of each section.

- **General:**
  - **Start TinyBar at login** (on by default).
  - **Start a call after** [3] s of mic or camera use (0 to 30).
  - **End a call after** [10] s without (3 to 60).
  - **Count the camera** (on).
  - **Send the app's name to TinyBar** (on).
- **Apps:**
  - **Count calls from:** "Any app except ignored ones" (default) or "Only the call apps below".
  - The **call apps** list: name, the short name sent, an on switch, and Add app… / Remove.
  - The **ignored apps** list, with Add app… / Remove.
  - **"Used the mic since TinyBar started":** the apps seen this session, each with Add to call apps and Ignore buttons. It's kept in memory only and forgotten on quit.
- **TinyBar:**
  - The paired bar, its ID and the link in use.
  - **Pair with a code…** and **Forget this TinyBar.**
  - **Address** (advanced; empty means automatic).
  - **Send a test call**, which shows On a call on the bar for 10 seconds with the name "Test", then ends it. It's dimmed while a real call is on.
- **Privacy** (read-only text): what leaves the Mac, what the app reads, and that it records nothing (see Privacy), with a link to Local Network settings when Wi-Fi is blocked.

## First launch

A single window, in three steps:

1. **What it does:** "TinyBar shows On a call on your bar when your Mac's mic or camera is in use. It never listens or records. Only 'on a call: yes or no' and the app's name go to your bar."
2. **Connect:** "Plug TinyBar into this Mac with a USB cable to pair it," with live status, or **Use Wi-Fi instead…** (the code). The Local Network prompt only appears here, at the moment Wi-Fi is chosen, after the window explains why.
3. **Start at login**, already checked. Then **Done**, and the window doesn't come back.

## Launch at login

- **Proposed: on by default,** offered as a checked box in the first-run window. Uses `SMAppService.mainApp` (macOS 13 and later). macOS shows its own "Background item added" notice.
- If registering fails (some managed Macs block it, or the app isn't in Applications), the Settings switch turns itself off and a line explains why. *Unverified:* that `SMAppService` registers an ad-hoc-signed app reliably.

## Privacy

### What leaves the Mac

Only these, and only to the bar, over the USB cable or the local network:

- `active`: true or false.
- `app`: a short name from the call-app list, and only if "Send the app's name" is on.
- The protocol itself: `cmd`, `hello`, `status`, `pair`, the pairing code you type, the token the bar issued, and plain HTTP headers.

**Never:** audio, sound levels, window titles, browser tabs or URLs, meeting names or participants, contacts, the computer's or user's name, a serial number or hardware address, or the names of unlisted or ignored apps. The app never contacts anything except the bar: no analytics, no crash reports, no update checks. It keeps no history on disk of which apps used the mic.

Two things to know, said plainly in the Privacy section of Settings:

- **Whatever the bar shows, the office can see.** The app's name appears on the bar, and the bar's `GET /api/status` returns it to anyone on the office Wi-Fi (the Remote has no PIN yet). Turn off "Send the app's name" to keep it off the bar.
- **Wi-Fi messages are plain HTTP** on the office network, so someone watching that network could see on/off and the app's name. USB messages stay on the cable.

### It doesn't need microphone or camera permission: checked

The claim is that the app needs no microphone permission, because it only reads whether the mic is in use. Here's how it was checked, and what's still open.

- **Why it should hold:** the app only reads CoreAudio and CoreMediaIO **state properties**. macOS's microphone permission check is at the point where an app starts audio **input**. On Apple's developer forums (thread 743077, macOS 14), Apple's reply is that "the permission dialog is always shown when calling `AudioDeviceCreateIOProcID` for devices that have inputs" for an app that has the audio-input entitlement. The app never calls it, opens no input stream, and has neither the audio-input entitlement nor an `NSMicrophoneUsageDescription`. Without that description, macOS couldn't grant it the mic even if it asked.
- **Shipping apps that do the same:**
  - **OverSight** (Objective-See, not sandboxed) watches the mic with `kAudioDevicePropertyDeviceIsRunningSomewhere` and the camera with CoreMediaIO's equivalent. Its Info.plist has no `NSMicrophoneUsageDescription` or `NSCameraUsageDescription`.
  - **Mute** (a Mac App Store app for macOS 14 and later that turns on Do Not Disturb during calls) reads the same mic property and has no `NSMicrophoneUsageDescription`.
- **Verdict for the mic:** confirmed for the device-level flag. That's two shipping apps, and Apple's statement on what triggers the prompt.
- **Not yet verified: the per-process list** (`kAudioHardwarePropertyProcessObjectList` and the `kAudioProcessProperty…` flags) that gives app names and makes the ignore list work. Neither reference app uses it for detection. AudioCap uses it, but only after asking for the separate "audio capture" permission, which is for recording other apps' sound through a process tap; this app doesn't do that. These are read-only properties of the same kind, so no prompt is expected, but it has to be checked on a clean Mac (acceptance criterion 30). **If a prompt does appear**, the fallback is the device-level flag: no app names, and an ignore list that can't work. That would come back to the user as a product decision.
- **Camera:** the sandboxed Mute asks for camera permission before watching the camera and treats a denial as breaking detection, while OverSight, which isn't sandboxed, watches the camera without asking. **Proposed:** the app is **not sandboxed**, and no camera prompt is expected. If one appears on a real Mac, "Count the camera" turns off by default.
- **Never on the indicators:** because the app doesn't capture, it should never show the orange mic dot or the green camera light, and it shouldn't be listed under Privacy & Security › Microphone or Camera.

### Permissions it does need

- **Local Network** (macOS 15 and later), for Wi-Fi only. The usage text (Proposed): "TinyBar connects to your TinyBar on this network to show when you're on a call."
- **Login item**, if you leave Start at login on.
- Nothing else: no Accessibility, Screen Recording, Input Monitoring, Automation, Contacts or Calendar access.

## Managed work Macs

Many people will run this on a work Mac that IT manages. What can get in the way:

- **Signing:**
  - **Proposed:** builds are **ad-hoc signed** by default (`codesign --sign -`), and signed with a Developer ID and notarized if the user has one.
  - An ad-hoc app that was **downloaded** is blocked by Gatekeeper. Since macOS 15, Control-click › Open no longer gets past this: you allow it under System Settings › Privacy & Security › **Open Anyway**, which may need an administrator. A managed Mac can turn that override off.
  - **Building it yourself** from source on the Mac avoids the download quarantine.
  - With a Developer ID and notarization, it opens normally.
- **Apps that IT has to allow:** tools such as Santa, or the allow lists in endpoint security products, can block unknown apps.
  - An ad-hoc app has no Team ID, so IT can only allow it by its hash, and the hash changes with every build.
  - A Developer ID lets IT allow it once, by Team ID or signing ID.
- **Local Network permission can't be granted ahead of time by MDM** (Apple TN3179), so each person answers the prompt themselves. On macOS 15.5 and later, an admin can exempt a subnet for every app (the `AllowedWiFiLocalNetworkAddresses` and `AllowedEthernetLocalNetworkAddresses` settings), which helps if bars live on a known subnet. TN3179 also says local network privacy tracks an app by its code signature and recommends an Apple-issued signing identity. **Proposed risk note:** with ad-hoc signing, a rebuilt app may be asked again or may show up twice in Settings.
- **Login items** can be managed or blocked by MDM. The app reports a failure instead of failing silently.
- **Networks that block the bar:**
  - A full-tunnel VPN that cuts off the local network, Wi-Fi client isolation, or bars on a separate network all stop the Wi-Fi link (and the Remote).
  - **USB still works**, so the app suggests it when Wi-Fi fails.
- **USB device control:** some security tools block unknown USB devices. If the serial port never appears, the app falls back to Wi-Fi.
- **For IT** (a paragraph in the README):
  - What the app reads: CoreAudio and CoreMediaIO state only.
  - What it never asks for: microphone, camera, screen or Accessibility access.
  - What it talks to: only the TinyBar, over USB serial or plain HTTP on the local network. It makes no internet connections.
  - What it needs: Local Network permission for Wi-Fi.
  - **Proposed bundle ID:** `com.tinybar.TinyBarMac`; anyone with a Developer ID changes it to their own reverse domain.
  - How it's signed.

## Build requirements

These are for the developers, and are **Proposed:**

- **Platforms:** macOS 14 Sonoma or later, on Apple silicon and Intel. Tested on macOS 14, 15 and 26.
- **Language:** Swift 6, with the app written in AppKit and SwiftUI.
- **Not sandboxed.** The hardened runtime is on when it's signed with a Developer ID, with **no** audio-input or camera entitlements.
- **Split the code so most of it is tested off the Mac.** A plain-Swift **core** that builds and passes `swift test` on Linux, holding:
  - The call detector: the start and end delays and pause, run on an injected clock and fed activity snapshots.
  - App matching: bundle-ID prefixes, the outermost `.app` in a path, the lists, the mode and how names are picked.
  - The JSON messages: encoding exactly per `docs/api.md`, and parsing replies.
  - Line framing, which ignores non-JSON lines.
  - Choosing the link, the heartbeat and the retry backoff.
  - Validating settings.
  - The serial transport (POSIX termios) and the HTTP transport (URLSession), which also build on Linux and can be tested there against a fake bar on a pseudo-terminal and a local HTTP server.
- **macOS-only adapters**, behind `#if os(macOS)` or in a macOS-only target:
  - The CoreAudio and CoreMediaIO readers.
  - IOKit USB discovery.
  - The Keychain.
  - `SMAppService`.
  - The menu-bar UI and the Settings and first-run windows.

  These can't be compiled in the Linux container. They're written against Apple's documentation and listed as unverified until built on a Mac.
- **Resource use:** under 1% CPU on average and under 50 MB of memory, with an Energy Impact of Low in Activity Monitor. It never polls faster than once a second.

## Unverified until it runs on a Mac

- No microphone, camera or audio-capture prompt from reading the per-process list and CoreMediaIO, without a sandbox (criterion 30).
- Which process holds the mic for each call app: the Chrome and Slack helpers, FaceTime (expected `avconferenced`), Safari (WebKit's shared GPU process), Teams and Zoom. Also which processes run Siri, Dictation, "Hey Siri" listening and Voice Control on macOS 14, 15 and 26.
- That the per-process flag works for Bluetooth mics such as AirPods, which the device-level flag reportedly doesn't.
- Which call apps let go of the mic when you mute.
- That opening and closing the bar's USB serial port doesn't reset the ESP32-S3.
- How Local Network permission behaves for an ad-hoc-signed app across rebuilds, and that `SMAppService` works for it.
- That the bundle IDs in the call-app table are current.

## Acceptance criteria

Each criterion names how it's checked:

- **Core:** an automated test of the plain-Swift core, which runs on Linux with an injected clock and a fake bar.
- **Mac:** a manual or scripted test on a real Mac.
- **Bar:** needs the firmware.

Default settings unless stated.

### Deciding a call (Core)

1. Mic activity from a counted app that lasts 3.0 s sends exactly one `active: true`, at 3.0 s. Activity that stops at 2.9 s sends nothing.
2. During a call, a gap of 9.9 s sends nothing, and the call continues. A gap of 10.0 s sends exactly one `active: false`, at 10.0 s after the activity stopped.
3. Ignored apps never start or prolong a call: Siri for 20 s, Dictation for 60 s and Voice Memos for 5 minutes, alone or overlapping the end delay of a real call.
4. Helper processes map to their app: `com.google.Chrome.helper` → Chrome. A process at `/Applications/Slack.app/Contents/Frameworks/Slack Helper.app/Contents/MacOS/Slack Helper` with an unlisted bundle ID → Slack (outermost `.app`).
5. In "Any app except ignored ones", an unlisted app starts a call that has no `app` field. In "Only the call apps below", it doesn't, and neither does the camera.
6. With Count the camera on, camera use alone starts a call after 3 s, with no `app`. With it off, it doesn't.
7. Names: a call from Slack sends `"app": "Slack"`. With "Send the app's name" off, no message has an `app` field. An edited name longer than 24 characters is refused in Settings.
8. Slack, then Zoom joining with no gap: one call, one extra `active: true` with `"app": "Zoom"`, and no `active: false` in between. Camera only after the mic stops: the call keeps the last name.
9. Settings changes apply at once. Ignoring the current call's app, turning off the camera during a camera-only call, or switching to "only call apps" during an unlisted-app call each sends `active: false` immediately. Delays outside 0–30 s and 3–60 s are refused.
10. Pause during a call sends `active: false` at once. No call starts while paused. Heartbeats continue with `active: false`. Pause "for 1 hour" ends by itself after 1 hour, and a mic already in use then starts a call 3 s later.
11. Start delay 0 starts a call on the first snapshot that shows activity.

### Messages and links (Core, unless marked)

12. Messages match `docs/api.md` byte for byte in golden tests: `{"active":true,"app":"Slack"}` and `{"active":false}` over HTTP, and the same with `"cmd":"call"` over USB, one object per line ending in `\n`. No other fields are sent.
13. Heartbeat: while connected, the current state is sent every 30 s (±1 s), on every change, and at once when a link comes up. `active: false` heartbeats are sent when not on a call.
14. USB handshake against a fake bar on a pseudo-terminal: the app sends `hello` first and nothing else until the reply says `"device": "TinyBar"`. Non-JSON lines before or between replies are ignored. A port that doesn't answer within 2 s is closed and isn't written to again.
15. Link choice: with both links up, every message goes over USB. When USB drops, the next message goes over Wi-Fi within 5 s, carrying the current state. When USB comes back, messages move back within 5 s.
16. **(Mac + Bar)** Opening and closing the bar's serial port 20 times never restarts the bar. Only devices with USB vendor ID 0x303A are ever opened (checked with another USB serial device plugged in).
17. Replies:
    - `"showing": "own"` while sending `active: true` shows "Set aside on the bar", and the app doesn't send extra messages.
    - `403 calls_off` shows "Calls from your Mac are off", and the heartbeat continues.
    - `401` shows "Pair TinyBar again".
18. Failures retry after 2, 5, 10 and 30 s, then every 30 s. "Not connected" shows after 15 s without success. No alert or notification is ever shown for it.
19. **(Mac)** Sleeping the Mac or quitting the app during a call sends `active: false` before it goes. **(Bar)** Force-quitting the app during a call ends On a call on the bar within 90 s.

### Pairing (Mac + Bar, where the bar supports it)

20. The first USB connection pairs automatically, and the menu shows "TinyBar" with its short ID. The token is in the Keychain, not in UserDefaults or any file.
21. Wi-Fi requests carry `Authorization: Bearer <token>`. Forget this TinyBar removes the token and stops all messages until you pair again.
22. A code from the Remote's Pair a Mac pairs over Wi-Fi. A wrong or expired code shows "That code didn't work" and pairs nothing.
23. With two TinyBars on the network, the app only ever talks to the paired one.
24. Against a bar without pairing, the app works with `tinybar.local` and no token, and Settings says "This TinyBar doesn't support pairing yet."

### Menu bar, Settings, first run and login (Mac)

25. The icon has four distinguishable states (connected, on a call, not connected, paused), readable in light and dark menu bars and with Increase Contrast on. The menu shows the three status lines from the spec, so no state depends on color.
26. "Don't count Zoom" appears only while a counted app is using the mic, and it moves that app to the ignored list.
27. There's no Dock icon. ⌘, opens Settings and ⌘Q quits. Every setting survives a relaunch, and Reset to defaults restores the values in this spec.
28. "Used the mic since TinyBar started" lists the apps seen this session and is empty after a relaunch.
29. The first-run window appears once, pairs over USB, and asks for Local Network permission only after Use Wi-Fi is chosen. Start at login is checked by default. After a restart, the app is running. Turning the switch off removes the login item, and a registration failure turns the switch off and explains why.

### Privacy (Mac)

30. On a new user account on macOS 14, 15 and 26:
    - A full day of use (calls in Slack, Meet in Chrome, Zoom, Teams and FaceTime, plus Siri and Dictation) causes **no** microphone, camera, audio-capture, screen-recording or accessibility prompt.
    - TinyBar isn't listed under Privacy & Security › Microphone or Camera.
    - The orange mic dot and the camera light never name TinyBar.

    The built app has no `NSMicrophoneUsageDescription` or `NSCameraUsageDescription`, and no audio-input or camera entitlement. The code creates no IO proc, `AVCaptureSession` or process tap (checked by a search of the source).
31. A capture of all network traffic, and a log of the serial port, during that day shows:
    - Messages go only to the bar's address (plus mDNS on the local network) and the serial port.
    - The only content is the fields listed under "What leaves the Mac".
    - Names are sent only for listed call apps.
    - There are no connections to the internet.
32. The app's data folder and preferences hold settings and lists only, with no record of which apps used the mic or when.

### Real calls (Mac + Bar)

33. On a call in each of Slack (huddle), Google Meet in Chrome, Zoom, Microsoft Teams and FaceTime:
    - The bar shows On a call within the start delay plus 2 s, with the right name (Chrome for Meet).
    - It ends within the end delay plus 2 s after hanging up.
    - This holds with the built-in mic, a wired headset and AirPods.
    - Switching to AirPods mid-call doesn't end the call.
34. Muted-call report (a measurement, not pass or fail): for each app in criterion 33, the report records whether the call stays on while muted with the camera off, and while muted with the camera on.
35. These don't show On a call:
    - Dictation for 60 s, a Siri request, and "Hey Siri" listening with no request.
    - Voice Memos for 2 minutes.
    - Music through AirPods, a YouTube video in Chrome, and a notification sound.
36. Over an 8-hour day: under 1% average CPU, under 50 MB of memory, and an Energy Impact of Low.

## Open questions

Each has a Proposed default, so work can go ahead:

- **The pairing API** (ID, `pair`, the code, Bonjour, `401`) needs agreeing in `docs/api.md` and the firmware, and adds a Pair a Mac button and a paired-Macs list to the Remote's Connect your Mac card in the mock-up. **Proposed** as above.
- **Bars' names:** with more than one TinyBar on a network, not all of them can be `tinybar.local`, which affects the Remote's address too. **Proposed:** the bar keeps `tinybar.local` when it's free, advertises its ID over Bonjour, and shows its actual address on the Wi-Fi screen. The Mac app finds it by ID.
- **Two Macs, one bar:** today the latest message wins, so an idle second Mac's `active: false` heartbeat would end the first Mac's call. **Proposed for later:** the bar keeps each paired Mac's state by its token and shows On a call while any of them is on a call. v1 supports one Mac per bar.
- **Muted calls with the camera off** may end On a call (criterion 34). If that's common, a later version could keep the call on while a known call app is still playing call audio. Not in v1.
- **The mock-up's Simulate buttons** offer "Meet" as an app name, which the Mac app will never send. **Proposed:** change it to "Chrome".
