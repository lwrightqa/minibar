# TinyBar for Mac: interface design

The UX designer's design for the Mac app's interface, 2026-10-04: the menu-bar icon, the menu, connecting and pairing, the Settings window, notifications, and every piece of copy.

- **What the app does** is in `docs/mac-app.md` (the product manager's spec). **The wire format** is `docs/api.md` (the contract with the firmware). This file only decides what you see and read. Where it changes something in either, [section 10](#10-changes-to-other-documents-and-follow-ups) lists it.
- Everything here is **Proposed**, waiting for the user's OK, like the rest of the Mac app in `docs/decisions.md`.
- American English. Copy in this file is final wording unless it's in *italics*.

## Contents

1. [Principles](#1-principles)
2. [Words and style](#2-words-and-style)
3. [The menu-bar icon](#3-the-menu-bar-icon)
4. [The menu](#4-the-menu)
5. [Connecting and pairing](#5-connecting-and-pairing)
6. [The Settings window](#6-the-settings-window)
7. [Notifications](#7-notifications)
8. [Other moments: system prompts, About, reopening, the app icon](#8-other-moments)
9. [Accessibility](#9-accessibility)
10. [Changes to other documents, and follow-ups](#10-changes-to-other-documents-and-follow-ups)
11. [Unverified until it's built on a Mac](#11-unverified-until-its-built-on-a-mac)

---

## 1. Principles

- **The bar is the feedback.** TinyBar sits on your desk, in view. The Mac app stays quiet and only has to answer one question when you look at it: *is my bar showing what my Mac sees, and if not, why?*
- **Small and native.** An `NSStatusItem` with a standard `NSMenu`, one Settings window in the System Settings style, and one Connect window that's only needed once. No popover, no Dock icon, no custom chrome, no colors of its own.
- **Quiet on calls.** Nothing moves, blinks, sounds or pops up, ever, and the menu bar never shows an app name or a timer. You may be sharing your screen.
- **Say what's wrong where you look.** Problems show in the icon and the menu, each with the one action that fixes it. No alerts, no notifications (section 7).
- **Never color alone.** The icon is a template image in one color; states differ in shape. Every state is spelled out in words in the menu and the icon's tooltip.
- **No dead ends.** Every state in the menu offers its way forward, and every window has Back or Cancel.
- **Few prompts.** The only system prompt the app causes is Local Network, the first time it uses Wi-Fi, and the app explains it on screen first (section 8).

## 2. Words and style

**Names.**

- The app's name is **TinyBar** (as in the menu's About TinyBar and Quit TinyBar). The device is **TinyBar** too. Copy avoids the clash by saying **your TinyBar** or the bar's own name for the device, and by rarely naming the app at all.
- A bar's name comes from the bar (`name` in `api.md`): "TinyBar 2A1C" by default, or whatever the user renamed it to on the Remote. Use it wherever one bar has to be told from another. Examples here use "TinyBar 2A1C".
- **On a call** with capitals is the bar's status. In running text it's "on a call".
- The switch on the Remote is **Calls from your Mac**, as named in the mock-up.

**Capitalization**, following Apple's Human Interface Guidelines:

| Where | Style | Example |
|---|---|---|
| Menu items, buttons, window titles, tab names, alert buttons | Title style | Pause Detection, For the Rest of Today, Show Code on TinyBar |
| Status lines in the menu, labels, switches, checkboxes, help text, alert titles and text | Sentence style | Can't reach TinyBar 2A1C since 2:04 PM |

**Formats.**

- **Durations** are written like the bar's: `1m`, `12m`, `1h 5m`, `10h`. A call shows `1m` from its first minute, as on the bar.
- **Times** follow the Mac's own format settings (`3:15 PM`, or `15:15` for a 24-hour Mac).
- **Separators:** a middle dot with spaces, ` · `, as on the bar.
- **Ellipsis** `…` (one character) on every menu item and button that opens a window or asks for more before acting.
- Curly apostrophes and quotation marks in the app (’ “ ”). Text sent to the bar is plain (`api.md` 2.3 maps it anyway).

**Voice.** Short, plain and friendly. Say what happened and what to do, not whose fault it is: "That code didn't match", not "Invalid code". No "please", no exclamation marks, no jargon on screen (no "token", "mDNS", "HTTP", "serial", "401"). Nothing that implies a private office.

---

## 3. The menu-bar icon

### 3.1 States

One monochrome template image, so macOS tints it for light and dark menu bars and for the highlighted state. Five states, told apart by shape:

| State | When | SF Symbol | Custom glyph (fallback) | VoiceOver label | Tooltip |
|---|---|---|---|---|---|
| **Connected** | A bar is reachable and you're not on a call (or the Mac is still inside the start delay) | `rectangle` | The bar in outline | TinyBar, connected | TinyBar 2A1C · not on a call |
| **On a call** | The Mac counts a call and the bar is being told | `rectangle.fill` | The bar filled | TinyBar, on a call | On a call · Slack · 12m |
| **Not connected** | No bar has answered for 15 seconds, or TinyBar isn't set up yet | `rectangle.slash` | The bar with a slash | TinyBar, not connected | Can't reach TinyBar 2A1C |
| **Paused** | You paused detection, or the bar is ignoring calls from your Mac | `pause.rectangle` | The bar with a pause mark | TinyBar, paused | Paused until 3:15 PM |
| **Needs you** | Something only you can fix (below) | `exclamationmark.triangle` | The bar with an exclamation mark | TinyBar, needs attention | Wi-Fi is blocked in Privacy settings |

The tooltip is the menu's first status line, or the second when that one says what's wrong. It updates live.

**Needs you** covers:

- macOS is blocking Local Network access and USB isn't connected (Wi-Fi can't work).
- Your bar no longer recognizes this Mac (its pairing was removed) and USB isn't connected.
- The bar and the app speak different API versions (`unsupported_api`).

Not connected is deliberately calm: a laptop away from the office is normal, not an error.

### 3.2 Which state shows

When several apply, the first one in this list wins:

1. **Needs you.**
2. **Paused**, including "TinyBar 2A1C is ignoring calls from your Mac" (`sources.mac` is `false`). That switch is a deliberate choice made on the Remote, so it's shown as a pause rather than an error.
3. **Not connected.** A call the Mac can't deliver shows Not connected, not On a call: the filled icon must never claim the bar is showing something it isn't.
4. **On a call.** Shown the moment the Mac decides it's a call and sends it (not after the reply), so it's instant. A call set aside on the bar, or waiting behind a dark screen or Wi-Fi setup, still shows On a call: the bar knows about it, and the menu explains.
5. **Connected.**

**Grace period.** The icon moves to Not connected only after **15 seconds** without a successful reply (the 15 seconds after which `mac-app.md` shows Not connected). Launching, waking from sleep, and switching between USB and Wi-Fi don't flicker the icon: once a bar has been set up, during the first 15 seconds after launch or wake the icon shows Connected and the menu says "Looking for TinyBar…". Before any bar is set up, the icon shows Not connected from the start.

**No animation.** The icon never blinks, pulses or animates, and no text sits next to it.

A **test call** (Settings › Connection) shows On a call for its 10 seconds.

### 3.3 Drawing it

**SF Symbols (default).** All five exist in SF Symbols 1 to 2, so they're available on macOS 11 and later; none is deprecated or restricted. Names checked against the SFSafeSymbols catalog (generated from Apple's SF Symbols metadata) on 2026-10-04.

- Load with `NSImage(systemSymbolName:accessibilityDescription:)`, passing the VoiceOver label above. Symbol images are templates already.
- Size: start at `NSImage.SymbolConfiguration(pointSize: 14, weight: .regular)` and tune on a real Mac so the glyph's height matches the Wi-Fi and Control Center icons. *Unverified.*
- The status item uses `NSStatusItem.variableLength`, since the glyphs are wider than tall.

**Custom template glyphs (fallback).** For if the plain rectangle turns out too generic among other menu-bar items on a real Mac, or if a symbol renders badly at menu-bar size. They draw the actual bar: a wide body with the info column on the right, like Bold Signal's layout.

- Canvas **22 × 16 pt** (44 × 32 px at 2x). Body 20 × 10 pt (x 1 to 21, y 3 to 13), corner radius 3 pt outside. Strokes 1.5 pt, placed so they land on whole pixels at 2x. Divider at x 15.25.
- Ship as one PDF per state in the asset catalog, with **Render As: Template Image** and **Preserve Vector Data** on, or draw the same paths in code with `NSImage(size:flipped:drawingHandler:)` and set `isTemplate = true`.
- Rendered at 1x and 2x on light and dark menu bars on 2026-10-04; all five read apart at both scales, with the pause and exclamation marks softest at 1x.
- The fallback keeps the same state rules, labels and tooltips.

```svg
<!-- Connected -->
<svg xmlns="http://www.w3.org/2000/svg" width="22" height="16" viewBox="0 0 22 16"><path d="M1.75 6a2.25 2.25 0 0 1 2.25-2.25h14a2.25 2.25 0 0 1 2.25 2.25v4a2.25 2.25 0 0 1-2.25 2.25h-14a2.25 2.25 0 0 1-2.25-2.25z" fill="none" stroke="#000" stroke-width="1.5"/><path d="M15.25 3.75v8.5" stroke="#000" stroke-width="1.5"/></svg>

<!-- On a call: filled, with the column divider cut out -->
<svg xmlns="http://www.w3.org/2000/svg" width="22" height="16" viewBox="0 0 22 16"><mask id="k"><rect width="22" height="16" fill="#fff"/><rect x="14.75" y="3" width="1" height="10" fill="#000"/></mask><g mask="url(#k)"><rect x="1" y="3" width="20" height="10" rx="3" fill="#000"/></g></svg>

<!-- Not connected: slash with a clear gap around it -->
<svg xmlns="http://www.w3.org/2000/svg" width="22" height="16" viewBox="0 0 22 16"><mask id="s"><rect width="22" height="16" fill="#fff"/><path d="M3 1.25l16 13.5" stroke="#000" stroke-width="4" stroke-linecap="round"/></mask><g mask="url(#s)"><path d="M1.75 6a2.25 2.25 0 0 1 2.25-2.25h14a2.25 2.25 0 0 1 2.25 2.25v4a2.25 2.25 0 0 1-2.25 2.25h-14a2.25 2.25 0 0 1-2.25-2.25z" fill="none" stroke="#000" stroke-width="1.5"/><path d="M15.25 3.75v8.5" stroke="#000" stroke-width="1.5"/></g><path d="M3 1.25l16 13.5" stroke="#000" stroke-width="1.5" stroke-linecap="round"/></svg>

<!-- Paused -->
<svg xmlns="http://www.w3.org/2000/svg" width="22" height="16" viewBox="0 0 22 16"><path d="M1.75 6a2.25 2.25 0 0 1 2.25-2.25h14a2.25 2.25 0 0 1 2.25 2.25v4a2.25 2.25 0 0 1-2.25 2.25h-14a2.25 2.25 0 0 1-2.25-2.25z" fill="none" stroke="#000" stroke-width="1.5"/><path d="M15.25 3.75v8.5" stroke="#000" stroke-width="1.5"/><rect x="6.5" y="5.5" width="1.5" height="5" rx=".5" fill="#000"/><rect x="9" y="5.5" width="1.5" height="5" rx=".5" fill="#000"/></svg>

<!-- Needs you -->
<svg xmlns="http://www.w3.org/2000/svg" width="22" height="16" viewBox="0 0 22 16"><path d="M1.75 6a2.25 2.25 0 0 1 2.25-2.25h14a2.25 2.25 0 0 1 2.25 2.25v4a2.25 2.25 0 0 1-2.25 2.25h-14a2.25 2.25 0 0 1-2.25-2.25z" fill="none" stroke="#000" stroke-width="1.5"/><path d="M15.25 3.75v8.5" stroke="#000" stroke-width="1.5"/><rect x="7.5" y="5" width="2" height="3.25" rx="1" fill="#000"/><circle cx="8.5" cy="9.85" r="1" fill="#000"/></svg>
```

---

## 4. The menu

A standard `NSMenu` on the status item (AppKit, not SwiftUI's `MenuBarExtra`, which can't do the Option-click details or the status lines below).

### 4.1 Layout

Top to bottom. Groups are separated by a menu separator.

| Group | Items |
|---|---|
| **Status** | Line 1, what the Mac sees (4.2). Line 2, the bar and the link (4.3). Not clickable. |
| **Action** | At most one fix (4.4), then the counting items (4.5). Hidden when there's nothing to show, with its separator. |
| **Pause** | Pause Detection ▸, or Resume Detection (4.6). |
| **App** | About TinyBar · Settings… ⌘, |
| **Quit** | Quit TinyBar ⌘Q |

The status lines look like text, not commands: line 1 in the primary label color, line 2 in the secondary label color, both at the menu's regular font size, with no highlight on hover. *(Implementation: a custom `view` on two `NSMenuItem`s, or disabled items with attributed titles if those keep their colors. Unverified which looks right.)*

The menu keeps its lines current while it's open (the call duration ticks over each minute, a call starting or ending changes the lines), and reads its state fresh each time it opens.

### 4.2 Line 1: what the Mac sees

| Situation | Line 1 |
|---|---|
| Not set up yet (never connected, no address) | Not set up yet |
| Nothing using the mic or camera | Not on a call |
| A counted app is using the mic, inside the start delay | Mic in use by Slack |
| A call from a call app | On a call · Slack · 12m |
| A call from an app that isn't on the call-app list ("Any app" mode) | On a call · GarageBand · 12m |
| A call from the camera alone | On a call · Camera · 12m |
| Several apps on a call | The name the bar is sent (`mac-app.md`: a call app beats others, the most recent wins) |
| Mic in use by an ignored app | Mic in use by Voice Memos · not counted |
| Mic in use by an unlisted app in "Only the call apps on my list" mode | Mic in use by GarageBand · not counted |
| Camera in use but not counted | Camera in use · not counted |
| Test call running | Sending a test call |
| Paused for 1 hour | Paused until 3:15 PM |
| Paused for the rest of today | Paused for the rest of today |
| Paused until resumed | Paused |

- Line 1 shows the app's local name even when the bar gets no name (GarageBand above). It's the Mac's own view and never leaves the Mac; it's what makes "Don't Count GarageBand" possible.
- While paused, line 1 says only that it's paused, whatever the mic is doing.

### 4.3 Line 2: your TinyBar

| Situation | Line 2 |
|---|---|
| Not set up yet | *(no line 2)* |
| Looking, in the first 15 seconds after launch, wake or losing the link | Looking for TinyBar… |
| Connected over USB | TinyBar 2A1C · USB |
| Connected over Wi-Fi | TinyBar 2A1C · Wi-Fi |
| Connected, USB paused for flashing | TinyBar 2A1C · Wi-Fi · USB paused |
| A call, set aside on the bar (`call.aside`) | Set aside on TinyBar 2A1C for this call |
| A call while the bar's screen is dark (`screen` is `dark`) | TinyBar 2A1C · screen off |
| A call while the bar is in Wi-Fi setup (`showing` is `setup`) | TinyBar 2A1C is setting up Wi-Fi |
| The bar is ignoring calls (`sources.mac` is `false`) | TinyBar 2A1C is ignoring calls from your Mac |
| No reply for 15 seconds | Can't reach TinyBar 2A1C since 2:04 PM |
| Never reached since launch | Can't reach TinyBar 2A1C |
| Wi-Fi turned off in Settings, bar not plugged in | TinyBar 2A1C isn't plugged in |
| Local Network blocked, no USB | Wi-Fi is blocked in Privacy settings |
| Not plugged in, and while it was plugged in the app found the bar on Wi-Fi but couldn't reach its address from this Mac (client isolation, a VPN) | Wi-Fi can't reach TinyBar 2A1C here · plug it in |
| The bar no longer recognizes this Mac (`401`), no USB | TinyBar 2A1C doesn't recognize this Mac |
| The bar's API is older than the app's | TinyBar 2A1C needs a firmware update |
| The bar's API is newer than the app's | This app needs an update for TinyBar 2A1C |

- "Since 2:04 PM" is the time of the last successful reply, kept in memory only.
- "Wi-Fi can't reach … here" needs one check: while connected over USB with the bar reporting Wi-Fi, the app tries the bar's Wi-Fi address once (`info`). If that fails, it remembers it for the current network only, and the Connect window says so too (5.2).
- Set aside, screen off and Wi-Fi setup show only during a call. When you're not on a call they don't matter, and line 2 shows the normal "TinyBar 2A1C · USB".
- A bar that answers `401` over USB never shows the "doesn't recognize" line: the app pairs again over the cable on its own (5.3).

### 4.4 The fix item

At most one, directly under the status lines:

| Situation | Item | What it does |
|---|---|---|
| Not set up, can't reach, not plugged in, Wi-Fi can't reach it here | **Connect…** | Opens the Connect window (5.2). |
| Local Network blocked | **Open Local Network Settings…** | Opens System Settings › Privacy & Security › Local Network. |
| The bar doesn't recognize this Mac | **Pair Again…** | Opens the Connect window on its Wi-Fi page, with this bar chosen (5.4). |
| The bar is ignoring calls from your Mac | **Open TinyBar Remote…** | Opens the bar's Remote page (`http://<host>/`) in the default browser, where you can turn Calls from your Mac back on. |
| Firmware or app needs an update | *(none)* | |

### 4.5 Counting items

So a false alarm can be fixed in one click, and undone in the same place:

| Situation | Item | What it does |
|---|---|---|
| A counted app is using the mic | **Don't Count Slack** | Moves Slack to Ignored apps. If it was on a call, the call ends at once. |
| Two or more counted apps are using the mic | **Don't Count ▸** Slack, Chrome | The same, one item per app. |
| An app you just stopped counting is still using the mic | **Count Slack Again** | Moves it back where it was (Call apps with its name, or off the Ignored list). |
| A call from the camera alone | **Don't Count the Camera** | Turns off Count the camera in Settings. |
| The camera is in use and you just stopped counting it | **Count the Camera Again** | Turns Count the camera back on. |

The undo items show only while that app or the camera is still in use. After that, Settings › Apps (or General, for the camera) is the place to change it back.

### 4.6 Pause

- **Pause Detection ▸**
  - **For 1 Hour**
  - **For the Rest of Today** (until midnight)
  - **Until I Resume**
- While paused, the item is **Resume Detection** instead.
- Pausing during a call ends it on the bar at once, as `mac-app.md` says. The pause ends on its own at the chosen time, with no notification; the icon simply changes back.
- Pause is always there, even when not connected (detection is local, and the pause still applies when the bar comes back).

### 4.7 The menu in each state

```text
Not set up yet
┌──────────────────────────────────────────────┐
│ Not set up yet                               │
│──────────────────────────────────────────────│
│ Connect…                                     │
│──────────────────────────────────────────────│
│ Pause Detection                            ▸ │
│──────────────────────────────────────────────│
│ About TinyBar                                │
│ Settings…                                 ⌘, │
│──────────────────────────────────────────────│
│ Quit TinyBar                              ⌘Q │
└──────────────────────────────────────────────┘

Connected, not on a call
┌──────────────────────────────────────────────┐
│ Not on a call                                │
│ TinyBar 2A1C · USB                           │
│──────────────────────────────────────────────│
│ Pause Detection                            ▸ │
│──────────────────────────────────────────────│
│ About TinyBar                                │
│ Settings…                                 ⌘, │
│──────────────────────────────────────────────│
│ Quit TinyBar                              ⌘Q │
└──────────────────────────────────────────────┘

On a call
┌──────────────────────────────────────────────┐
│ On a call · Slack · 12m                      │
│ TinyBar 2A1C · Wi-Fi                         │
│──────────────────────────────────────────────│
│ Don't Count Slack                            │
│──────────────────────────────────────────────│
│ Pause Detection                            ▸ │
│──────────────────────────────────────────────│
│ About TinyBar                                │
│ Settings…                                 ⌘, │
│──────────────────────────────────────────────│
│ Quit TinyBar                              ⌘Q │
└──────────────────────────────────────────────┘

On a call, set aside on the bar
┌──────────────────────────────────────────────┐
│ On a call · Slack · 12m                      │
│ Set aside on TinyBar 2A1C for this call      │
│──────────────────────────────────────────────│
│ Don't Count Slack                            │
│──────────────────────────────────────────────│
│ Pause Detection                            ▸ │
│──────────────────────────────────────────────│
│ About TinyBar                                │
│ Settings…                                 ⌘, │
│──────────────────────────────────────────────│
│ Quit TinyBar                              ⌘Q │
└──────────────────────────────────────────────┘

On a call, bar can't be reached
┌──────────────────────────────────────────────┐
│ On a call · Zoom · 3m                        │
│ Can't reach TinyBar 2A1C since 2:04 PM       │
│──────────────────────────────────────────────│
│ Connect…                                     │
│ Don't Count Zoom                             │
│──────────────────────────────────────────────│
│ Pause Detection                            ▸ │
│──────────────────────────────────────────────│
│ About TinyBar                                │
│ Settings…                                 ⌘, │
│──────────────────────────────────────────────│
│ Quit TinyBar                              ⌘Q │
└──────────────────────────────────────────────┘

Paused
┌──────────────────────────────────────────────┐
│ Paused until 3:15 PM                         │
│ TinyBar 2A1C · USB                           │
│──────────────────────────────────────────────│
│ Resume Detection                             │
│──────────────────────────────────────────────│
│ About TinyBar                                │
│ Settings…                                 ⌘, │
│──────────────────────────────────────────────│
│ Quit TinyBar                              ⌘Q │
└──────────────────────────────────────────────┘

Needs you: Local Network blocked
┌──────────────────────────────────────────────┐
│ Not on a call                                │
│ Wi-Fi is blocked in Privacy settings         │
│──────────────────────────────────────────────│
│ Open Local Network Settings…                 │
│──────────────────────────────────────────────│
│ Pause Detection                            ▸ │
│──────────────────────────────────────────────│
│ About TinyBar                                │
│ Settings…                                 ⌘, │
│──────────────────────────────────────────────│
│ Quit TinyBar                              ⌘Q │
└──────────────────────────────────────────────┘

The bar is ignoring calls from your Mac
┌──────────────────────────────────────────────┐
│ On a call · Slack · 5m                       │
│ TinyBar 2A1C is ignoring calls from your Mac │
│──────────────────────────────────────────────│
│ Open TinyBar Remote…                         │
│ Don't Count Slack                            │
│──────────────────────────────────────────────│
│ Pause Detection                            ▸ │
│──────────────────────────────────────────────│
│ About TinyBar                                │
│ Settings…                                 ⌘, │
│──────────────────────────────────────────────│
│ Quit TinyBar                              ⌘Q │
└──────────────────────────────────────────────┘
```

### 4.8 Option-click: details

Holding **Option** while opening the menu (the macOS convention, as in the Wi-Fi menu) adds detail lines under line 2, in the secondary color, and one item:

- Address: tinybar.local (10.0.4.42) *(over Wi-Fi)*, or Port: cu.usbmodem1101 *(over USB)*
- Firmware 1.0.0 · API 1.0
- Last reply 2:31:04 PM
- **Pause USB** (or **Resume USB**), shown when a TinyBar is or was on USB. It lets go of the USB port so a firmware flasher can use it (`api.md` 6.3), and the app uses Wi-Fi meanwhile. It resumes when chosen again or when the bar is unplugged and plugged back in.

The same Pause USB control is in Settings › Connection › Advanced, so it isn't only for people who know the Option trick.

---

## 5. Connecting and pairing

### 5.1 Where it starts

- **First launch:** the Connect window opens by itself, as **Welcome to TinyBar**. It never opens by itself again.
- **The menu's Connect…** and **Pair Again…**, and **Settings › Connection**, open the same window as **Connect TinyBar**, without the welcome text and the login checkbox.
- **Plugging the bar in** needs no window at all: the app connects and pairs on its own (5.3).

The window is 440 pt wide, fixed size, centered, and brought to the front (the app activates, since it has no Dock icon). It's built with the system font and standard controls. ⌘W, Esc and the close button act like Not Now (or Back, on the Wi-Fi page). Return presses the default button.

### 5.2 The Connect window

```text
┌───────────────────── Welcome to TinyBar ─────────────────────┐
│                         [app icon]                           │
│                     Welcome to TinyBar                       │
│   TinyBar shows On a call on your bar whenever this Mac's    │
│   mic or camera is in use, in Slack, Zoom, Google Meet or    │
│   any other app. It never listens or records.                │
│ ┌──────────────────────────────────────────────────────────┐ │
│ │ [cable]  Plug TinyBar into this Mac with a USB cable.    │ │
│ │          ◌ Looking for TinyBar…                          │ │
│ └──────────────────────────────────────────────────────────┘ │
│   Not plugged into this Mac?  Pair Over Wi-Fi…               │
│                                                              │
│   ☑ Start TinyBar when you log in                            │
│     macOS will show a notice that it was added.              │
│                                          [Not Now]  [Done]   │
└──────────────────────────────────────────────────────────────┘
```

| Part | Spec |
|---|---|
| App icon | 64 pt, centered. First launch only. |
| Heading | "Welcome to TinyBar" (first launch) or "Connect your TinyBar". System font, title 2 size, semibold, centered. |
| Intro | First launch only, body size, secondary color, centered: "TinyBar shows On a call on your bar whenever this Mac's mic or camera is in use, in Slack, Zoom, Google Meet or any other app. It never listens or records." |
| USB box | A rounded group box. `cable.connector` symbol at 28 pt in the secondary color, then "Plug TinyBar into this Mac with a USB cable." Under it, a live status line (below). |
| Wi-Fi link | "Not plugged into this Mac?" in the secondary color, then a link-style button **Pair Over Wi-Fi…**, which opens the Wi-Fi page (5.4). |
| Login checkbox | First launch only, checked: "Start TinyBar when you log in". Under it, in the secondary color: "macOS will show a notice that it was added." The choice is applied when the window closes, by either button. If macOS refuses it, the window stays open and the line under the checkbox gives the reason from 6.2, with its button; pressing Done or Not Now again closes the window. |
| Menu-bar hint | Once connected: "TinyBar is in your menu bar: [icon]" with the Connected glyph inline, above the buttons. |
| Buttons | **Not Now** (cancel) and **Done** (default). Done is enabled once a bar is connected. |

The USB status line:

| Situation | Status line |
|---|---|
| Waiting | ◌ Looking for TinyBar… *(small spinner)* |
| Still waiting after 20 seconds | Adds: "Still looking? Some USB cables only charge. Try another cable, or pair over Wi-Fi." |
| Connected and paired | ✓ Connected to TinyBar 2A1C over USB. *(`checkmark.circle.fill` in the system green, with the words, so it isn't color alone)* |
| …and the bar is on Wi-Fi (`wifi` is `connected`) | Adds: "When it isn't plugged in, it uses Wi-Fi." On macOS 15 and later, the first time: "When it isn't plugged in, it uses Wi-Fi. If your Mac asks whether TinyBar can find devices on your local network, choose Allow." The app then checks Wi-Fi (4.3), which is what brings up the prompt, while this line explains it. |
| …and the bar has no Wi-Fi (`offline` or `setup`) | Adds: "TinyBar isn't on Wi-Fi, so it works only while plugged in." |
| …and the bar is on Wi-Fi, but this Mac can't reach it there (4.3) | Adds: "This Mac can't reach TinyBar over this Wi-Fi network, so it works only while plugged in." |
| …and the bar has 10 paired devices (`token_limit`) | Adds: "It works over USB. To use Wi-Fi too, remove a device on TinyBar's Remote; it can keep 10." |
| Connected, bar doesn't use pairing (`auth` is `none`) | ✓ Connected to TinyBar 2A1C over USB. |

### 5.3 USB: plug in and it's done

- When a TinyBar answers on USB and the app has no token for it, the app pairs over the cable at once (`pair`, `api.md` 6.6). Plugging in is the consent, so there's no code, no question and no window. The bar confirms on its screen ("Paired · Mac · over USB").
- If the bar later answers `401` over Wi-Fi and is then plugged in, the app pairs again over the cable without asking.
- **A different TinyBar plugged in** becomes your TinyBar: the app pairs with it, uses it from then on, and forgets the previous one (deleting its token, and telling it to forget this Mac if it can be reached). One bar per Mac in v1. Plugging the old bar back in switches back the same way. Line 2 shows the new name, so the switch is visible without a notification.

### 5.4 Wi-Fi: the code shown on the bar

For a bar that's never plugged into this Mac. The app asks the bar to show a code (`pair/start`), you type what the bar shows (`pair`). The code appears only on the bar's screen, never on the Mac or the Remote, so only someone who can see the bar can pair. Codes last 2 minutes and allow 3 tries (`api.md` 4.9).

The page replaces the window's content, with **Back** at the bottom left.

```text
┌───────────────────── Connect TinyBar ────────────────────────┐
│ Pair over Wi-Fi                                              │
│                                                              │
│ TinyBar:  [ TinyBar 2A1C          ▾ ]                        │
│           Hold a TinyBar's screen and tap Wi-Fi to see       │
│           its name.                                          │
│                                                              │
│           [ Show Code on TinyBar ]                           │
│                                                              │
│ Type the code shown on TinyBar 2A1C:                         │
│           [ 482 913 ]                                        │
│           The code works for 2 minutes.                      │
│                                                              │
│ [Back]                                              [Pair]   │
└──────────────────────────────────────────────────────────────┘
```

Step by step:

1. **Before the system prompt** (macOS 15 and later, the first time Wi-Fi is used): "Your Mac will ask whether TinyBar can find devices on your local network. Choose Allow, so this Mac can reach your bar over Wi-Fi." Button **Continue** (default). Then the app starts browsing, which makes macOS ask. The app can't read the permission's state (Apple's TN3179), so it shows this once, remembered in its settings.
2. **Looking:** spinner and "Looking for TinyBar on this network…".
3. **Choosing a bar:**
   - One bar found: "Found TinyBar 2A1C." as text.
   - Several: a pop-up button listing them by name, with the help line "Hold a TinyBar's screen and tap Wi-Fi to see its name." (needs the bar to show its name on the Network tile, `api.md` 14.3).
   - Then **Show Code on TinyBar**. The chosen bar wakes and shows the code.
4. **Typing the code:** the code field appears and takes focus.
   - One text field, 6 digits, in a large monospaced-digit font (title 2 size), placeholder "000 000". It accepts typing or pasting, ignores spaces and dashes, and shows the digits as "482 913".
   - Help line: "The code works for 2 minutes."
   - Links under it: **Show a New Code**, and with several bars, **Didn't see a code? Choose another TinyBar.**
   - **Pair** (default) is enabled at 6 digits. The app also pairs as soon as the sixth digit is typed or pasted.
5. **Pairing:** spinner and "Pairing…".
6. **Paired:** "✓ Paired with TinyBar 2A1C." and "It shows On a call whenever this Mac is on one." The bar confirms on its screen ("Paired · Mac"). The button becomes **Done**.

**Entering an address.** When nothing is found, an **Enter Address…** link shows a field ("Address", placeholder "tinybar.local or 10.0.4.42") and a **Connect** button. The app checks the address with `info` and goes on to step 3 with that bar.

### 5.5 Messages on the Wi-Fi page

Shown in place of the help line under the field or button they're about, in the system's red text with an `exclamationmark.circle.fill` symbol (never color alone). The field keeps what you typed, except after a code is used up.

| Situation (`api.md` code) | Message | Then |
|---|---|---|
| No TinyBar found after 10 seconds | Can't find a TinyBar on this network. Make sure it's on and on the same Wi-Fi as this Mac. Some office networks keep devices apart; if yours does, plug TinyBar into this Mac instead. | **Try Again**, **Enter Address…** |
| Local Network blocked | macOS is blocking TinyBar from your local network, so Wi-Fi can't work. | **Open Local Network Settings…** |
| Nothing at a typed address | Nothing answered at 10.0.4.42. | Field stays, **Connect** again |
| A typed address isn't a TinyBar | That address isn't a TinyBar. | |
| Wrong code, tries left (`wrong_code`) | That code didn't match. 2 tries left. / That code didn't match. 1 try left. | Field cleared and focused |
| Wrong code, none left | That code didn't match, so TinyBar canceled pairing. Show a new code to try again. | **Show a New Code** |
| Code expired, canceled on the bar or already used (`not_pairing`) | That code has expired or was canceled on TinyBar. Show a new code to try again. | **Show a New Code** |
| Someone else is pairing (`pairing_busy`) | Someone else is pairing with this TinyBar. Try again in 45 seconds. | Button enabled again when the time is up |
| Too many failed pairings (`rate_limited`) | Too many tries. You can try again in 4 minutes. | The same |
| 10 devices already (`token_limit`) | TinyBar 2A1C already has 10 paired devices. Remove one on its Remote, then try again. | **Open TinyBar Remote…** |
| The bar is setting up Wi-Fi (`in_setup`) | TinyBar 2A1C is setting up Wi-Fi. Finish setup on the bar, then try again. | |
| No reply (time-out) | TinyBar 2A1C didn't answer. Make sure it's on, then try again. | |
| The bar doesn't use pairing (`auth` is `none`) | TinyBar 2A1C doesn't need pairing, so you're all set. | **Done** |

Waiting times round up: under a minute in seconds ("45 seconds"), otherwise in minutes ("4 minutes").

### 5.6 What the bar shows (for the mock-up and the firmware)

`api.md` 4.8 proposes the bar's pairing screen and leaves its drawing to the UX designer. In Bold Signal:

- **Surface:** the main field #0E1013 (the dark surface of the Clock screen and menus), the info column #1C1F24 (the menu tiles' color). It's a system screen, not a status, so it has no status color.
- **Kicker** (Barlow 700, 15 px, capitals, 1.5 px spacing, baseline y 30): "PAIRING · MAC", or the name the Mac was given.
- **Headline:** the code, "482 913", in Barlow Condensed 700 at **112 px** with tabular digits, baseline y 125, white. About 357 px wide, inside the 404 px field. Only digits and a space, which the 112 px font already has.
- **Sub line** (Barlow 500, 19 px, baseline y 152): "Type it on your Mac · tap to cancel" for a Mac, and `api.md`'s "Type this code on that device · tap to cancel" for anything else.
- **Info column:** label "EXPIRES IN", value the countdown "1:52" (Barlow Condensed 700, 46 px, tabular), foot the bar's own name, "TinyBar 2A1C", so you can check you're pairing the right bar.
- Behavior, cancel and the toasts ("Paired · Mac", "Paired · Mac · over USB", "Pairing canceled", "Pairing timed out", "Pairing canceled · wrong code") are as `api.md` 4.8 says.

### 5.7 Forgetting a bar

- **Settings › Connection › Forget This TinyBar…** asks first:
  - Title: "Forget TinyBar 2A1C?"
  - Text: "This Mac will stop showing calls on it. To use it again, plug it into this Mac or pair over Wi-Fi."
  - Buttons: **Forget** (destructive) and **Cancel** (default).
- Forgetting deletes the token from the Keychain and tells the bar (`DELETE /api/v1/clients/self`, best effort).
- **If the bar is plugged in** when you forget it, the app leaves its USB port alone until it's unplugged and plugged in again. Otherwise plugging-in-pairs-automatically would undo the Forget at once.
- **To switch to another TinyBar over Wi-Fi,** forget this one, then choose Connect… (one bar per Mac in v1). Over USB, just plug the other one in (5.3).

---

## 6. The Settings window

### 6.1 The window

- A SwiftUI `Settings` scene: a toolbar with four tabs, each a grouped `Form` (`.formStyle(.grouped)`), so it looks like System Settings. 500 pt wide; the height fits each tab. The window title is the tab's name.
- Opened by **Settings…** (⌘,) and by reopening the app (section 8). It comes to the front.
- On/off settings are **switches** (Apple's choice for settings in a grouped form). They apply at once; there's no Save button.
- Each tab except Privacy ends with **Restore Defaults**. On Apps it asks first (6.3); on the others it acts at once, since it's one click to change back.

| Tab | SF Symbol |
|---|---|
| General | `gearshape` |
| Apps | `square.grid.2x2` |
| Connection | `cable.connector` |
| Privacy | `hand.raised` |

### 6.2 General

| Setting | Control | Default | Help text (secondary color, under the control) |
|---|---|---|---|
| Start TinyBar when you log in | Switch | On | *(none, unless it failed; see below)* |
| **Calls** *(section header)* | | | |
| Start a call after | Pop-up: Right away, 1 second, 2 seconds, 3 seconds, 5 seconds, 10 seconds, 15 seconds, 30 seconds | 3 seconds | How long the mic or camera has to be in use first. Filters out apps that open the mic for a moment. |
| End a call after | Pop-up: 3, 5, 10, 15, 30 or 60 seconds | 10 seconds | How long they have to be idle first. Bridges short gaps, like switching to AirPods. |
| Count the camera | Switch | On | The camera alone counts as a call, without an app name. Turn this off if a webcam app or Photo Booth shows you as on a call. |
| Send the app's name to TinyBar | Switch | On | TinyBar shows it, like "From your Mac · Slack". Only apps on your call app list send a name, and anyone near your desk can read it. |

- **Delays are pop-ups, not number fields,** so an out-of-range value can't be typed. The core still accepts any whole number in range (`mac-app.md`); if a value set some other way isn't one of the presets, the pop-up adds it as an extra item.
- **Count the camera in "Only the call apps on my list" mode** is dimmed and off, with: "Not available with “Only the call apps on my list”, because macOS doesn't say which app is using the camera."
- **When starting at login fails**, the switch turns itself off and the help line says why, with a button where one helps (from `SMAppService.Status`):

  | Status | Help line | Button |
  |---|---|---|
  | `requiresApproval` | Allow TinyBar in Login Items to start it when you log in. | **Open Login Items Settings…** (`SMAppService.openSystemSettingsLoginItems()`) |
  | `notFound`, or the app isn't in Applications | Move TinyBar to your Applications folder, then turn this on again. | |
  | Registering threw an error | macOS didn't allow it. Your organization may manage login items. | |

### 6.3 Apps

```text
Count calls from   (•) Any app, except ignored apps
                   ( ) Only the call apps on my list
                   Any app catches call apps that aren't on your list.
                   They show on TinyBar without a name.

Call apps
┌──────────────────────────────────────────────────┐
│ App                      Shown on TinyBar as     │
│ [icon] Slack             Slack                   │
│ [icon] zoom.us           Zoom                    │
│ [icon] Google Chrome     Chrome                  │
│ …                                                │
├──────────────────────────────────────────────────┤
│ [+] [−]                                          │
└──────────────────────────────────────────────────┘
Calls from these apps show their name on TinyBar.
Double-click a name to change it.

Ignored apps
┌──────────────────────────────────────────────────┐
│ [icon] Siri                                      │
│ [icon] Dictation                                 │
│ [icon] Voice Control                             │
│ [icon] Voice Memos                               │
├──────────────────────────────────────────────────┤
│ [+] [−]                                          │
└──────────────────────────────────────────────────┘
These never count as a call, even while they use the mic.

Used the mic since TinyBar opened
  [icon] GarageBand   counted, no name   [Add to Call Apps] [Ignore]
  [icon] Voice Memos  ignored
This list is kept only until TinyBar quits.

                                              [Restore Defaults]
```

- **Count calls from:** radio group, default **Any app, except ignored apps**. Help as drawn.
- **Call apps:** a two-column table, app (icon and name as in Finder) and **Shown on TinyBar as** (editable on double-click). There's **no on/off switch per row**: to stop counting an app, move it to Ignored apps (each row's context menu has **Ignore Slack**), and to stop naming it, remove it (−), which makes it an ordinary app.
- **The name** is 1 to 24 characters (the bar shows 24, `api.md` 5.2). As you type, the field refuses more than 24 with "Use 24 characters or fewer." Characters outside printable ASCII get "TinyBar can't show “é”." (the bar's fonts, `api.md` 2.3). An empty name puts back the app's default.
- **+ (Add App…)** opens a standard open panel in Applications: title "Choose an App", button **Add**, applications only. If the app is already on the other list: "Zoom is in Ignored Apps. Move it to Call Apps?" with **Move** and **Cancel**. An app with no bundle identifier gets "TinyBar can't tell when this app uses the mic."
- **Ignored apps:** one column, + and −, and a context menu **Count Voice Memos**. Built-in entries (Siri, Dictation, Voice Control) show their name and a system icon, since they aren't apps you can pick in Finder.
- **Used the mic since TinyBar opened:** one row per app the Mac has seen using the mic this session, with its state (counted, counted, no name, or ignored) and **Add to Call Apps** and **Ignore** buttons where they apply. Empty: "No apps have used the mic since TinyBar opened." Kept in memory only, as `mac-app.md` requires.
- **Restore Defaults** asks first, since lists can hold real work: "Restore the default app lists?" / "Your changes to call apps and ignored apps will be lost." / **Restore** and **Cancel**. It also resets Count calls from.

### 6.4 Connection

```text
TinyBar 2A1C                       ● Connected over USB
                                   [Send Test Call]
                                   Shows On a call on TinyBar for 10 seconds, as “Test”.
                                   [Forget This TinyBar…]

Use Wi-Fi when TinyBar isn't plugged in              [on]
Off, this Mac talks to TinyBar only over the USB cable,
and never asks for Local Network access.

▸ Advanced
    Address               [ Automatic                    ]
    Leave empty to find TinyBar automatically, or type its
    address, like tinybar.local or 10.0.4.42.
    Name for this Mac     [ Mac                          ]
    Shown in TinyBar's list of paired devices. TinyBar never
    sends your Mac's own name.
    USB                   [Pause USB]
    Lets another app, like a firmware flasher, use TinyBar's
    USB port. Wi-Fi is used meanwhile.

                                              [Restore Defaults]
```

**Your TinyBar** (top section):

| Situation | Status | Buttons |
|---|---|---|
| Not set up | Not set up yet | **Connect…** |
| Connected over USB | ● Connected over USB | **Send Test Call**, **Forget This TinyBar…** |
| Connected over Wi-Fi | ● Connected over Wi-Fi | The same |
| Can't reach | ○ Can't reach it since 2:04 PM | **Connect…**, **Forget This TinyBar…** |
| Paired, but the bar refuses this Mac | ○ It doesn't recognize this Mac | **Pair Again…**, **Forget This TinyBar…** |
| The bar doesn't use pairing | ● Connected over Wi-Fi · no pairing needed | **Send Test Call** |

The dot is filled (●) when connected and hollow (○) when not, in the system green or secondary color: the shape and the words carry it, not the color.

**Send Test Call.** Shows On a call on the bar for 10 seconds with the name "Test", then ends it. While it runs, the button reads **Sending Test Call…** and is dimmed, and line 1 of the menu says "Sending a test call". It's dimmed with a reason when it can't run: "Not available during a call." or "Connect TinyBar first."

**Use Wi-Fi when TinyBar isn't plugged in.** Switch, on by default. Off: USB only, no browsing, no Local Network prompt; the menu says "TinyBar 2A1C isn't plugged in" when it isn't. A new setting: some people and IT departments will want calls to stay on the cable.

**Advanced** (a disclosure group, closed by default):

- **Address:** text field, placeholder "Automatic". Help as drawn. Checked with `info` when you press Return or leave the field: "That address isn't a TinyBar." or "Nothing answered at 10.0.4.42." under the field.
- **Name for this Mac:** text field, empty by default, placeholder "Mac", 1 to 32 characters. Sent as `name` (`api.md` 4.6 and 6.6) so the Remote's list can tell your Macs apart. Help as drawn. The bar learns a new name the next time the Mac pairs or is plugged in (there's no rename over Wi-Fi), so the help line adds, once it's changed: "TinyBar will show the new name after you plug it in or pair again."
- **USB: Pause USB / Resume USB.** As in 4.8.

**Restore Defaults** turns Wi-Fi back on and clears Advanced. It doesn't forget the bar.

### 6.5 Privacy

Read-only text, no controls except the Local Network button. Headings in semibold.

> **What TinyBar sends**
>
> Only to your TinyBar, over the USB cable or your local network:
>
> - Whether you're on a call: yes or no.
> - The call app's name, like “Slack”, if Send the app's name is on. Names of other apps are never sent.
> - A random ID for this copy of TinyBar, so your bar can tell your Mac apart from others. It isn't your Mac's name or serial number.
>
> It never sends audio, sound levels, window titles, websites, meeting names, contacts, or your name or your Mac's. It never connects to the internet.
>
> **What TinyBar reads**
>
> Whether the mic and camera are in use, and which app is using the mic. It never turns them on, listens or records, so it doesn't ask for microphone or camera access. It keeps no record of which apps used the mic.
>
> **Good to know**
>
> - Anyone near your desk can see what TinyBar shows, including the app's name. TinyBar's Remote shows it too.
> - Over Wi-Fi, messages aren't encrypted, so someone watching the office network could see them. Over USB, they stay on the cable.

When the app has seen Local Network blocked (macOS 15 and later), a line and a button follow: "macOS is blocking TinyBar from your local network, so Wi-Fi can't work." **Open Local Network Settings…**

*The third bullet under "What TinyBar sends" depends on the user's OK for `client` (`api.md` 14.2). If the Mac's time is approved for `hello` too, add: "Over USB, your Mac's time, so TinyBar's clock is right without Wi-Fi."*

---

## 7. Notifications

**TinyBar for Mac shows no notifications in v1, and never asks for permission to.** This follows `decisions.md` ("No alerts or notifications about the connection", and notifications not in v1), and it's the right design, not just a scope cut:

- **The bar is on your desk.** If calls aren't showing, you can see that on the bar itself, and the icon says why.
- **Calls are exactly when a banner hurts.** It interrupts you, and it can appear in a screen share.
- **Nothing here is urgent.** Every problem waits safely until you next look at the menu bar.
- **It would add a permission prompt,** and the app's promise is no prompts beyond Local Network.

Where each event shows instead:

| Event | Where you see it |
|---|---|
| A call starts or ends | The bar. The icon fills or empties. |
| Call set aside on the bar | The bar (you did it). Menu line 2. |
| Can't reach the bar | Icon (after 15 s), menu line 2, Settings › Connection. |
| Paired, over USB or Wi-Fi | The bar's toast, the Connect window, menu line 2. |
| A different TinyBar plugged in | Menu line 2 names it. |
| Pause ended | The icon changes back. |
| Local Network blocked, or pairing refused | The Needs-you icon, menu line 2 with its fix item. |
| Bar ignoring calls from your Mac | The Paused icon, menu line 2, **Open TinyBar Remote…** |
| Starting at login failed | Settings › General, when you change the switch. |
| Firmware or app needs an update | The Needs-you icon, menu line 2. |

**If a later version adds notifications,** each one has to pass all of these: it needs you to act; you can't see it on the bar or in the icon; it's never shown during a call or while the screen is shared; it's shown at most once per cause per day; and it can be turned off in Settings. Nothing in v1 passes.

**macOS's own notices** still appear, and the app prepares you for them: "Background Items Added" when the login item is registered (the first-run checkbox says so), and the Local Network prompt (explained on the Wi-Fi page first).

---

## 8. Other moments

**The Local Network prompt** (macOS 15 and later). macOS shows the app's usage text, `NSLocalNetworkUsageDescription`: "TinyBar connects to your TinyBar on this network to show when you're on a call." The app makes its first Wi-Fi request, which is what makes macOS ask, while the Connect window explains it: on the Wi-Fi page (5.4), or right after a USB connection in the Connect window (5.2). The one exception is a bar first plugged in with no window open (after Not Now): the prompt then comes right after the plug-in, when the app checks Wi-Fi, so it still follows something you just did. With Wi-Fi turned off in Settings, it never appears.

**Reopening the app.** With no Dock icon, people who can't find the menu-bar icon (a full menu bar, a hidden item, the MacBook notch) open the app again from Finder, Spotlight or Launchpad. When that happens while it's running (`applicationShouldHandleReopen`), it opens **Settings** on the General tab, or the **Connect window** if TinyBar isn't set up.

**About TinyBar.** The standard About panel (`orderFrontStandardAboutPanel`): the app icon, name, "Version 1.0 (12)", and the credits line "Shows On a call on your TinyBar when this Mac's mic or camera is in use. It never listens or records."

**Quit TinyBar.** Quits at once, with no confirmation, like other menu-bar apps; during a call it tells the bar first (`leaving`, `api.md` 5.2).

**The app icon** (Finder, About, the Connect window, Login Items). A standard macOS app icon on Apple's icon grid: the TinyBar seen from the front, a wide rounded bar with the main field in On a call blue #1450B4 and the info column in its tint #0F3D89, on a dark #0E1013 rounded-square background. Two short white bars stand in for the text (real words turn to mush at 16 px). Flat fills, no glass or gloss. *For macOS 26, the same artwork can go through Icon Composer for the dark and tinted styles.*

---

## 9. Accessibility

- **VoiceOver:** the status item's label is "TinyBar, " plus the state (3.1), and its value is the tooltip text, so "TinyBar, on a call, On a call · Slack · 12m" is read in full. Status lines in the menu are read in order. Every control in the windows has a label; the code field is "Pairing code, 6 digits".
- **Color is never the only signal:** icon states differ in shape; checkmarks and dots come with words; errors have a symbol and words.
- **Increase Contrast and Reduce Transparency:** handled by template images and standard controls. The custom glyphs' 1.5 pt strokes stay visible with Increase Contrast. *Check on a real Mac (`mac-app.md` criterion 25).*
- **Keyboard:** every window works with Tab and Full Keyboard Access; Return and Esc do what the default and cancel buttons do; ⌘, and ⌘Q work while the menu is open; menu items can be reached with the arrow keys as in any menu.
- **Motion:** nothing animates except standard spinners.
- **Text size:** the windows use system text styles, so they follow the Mac's text size where macOS applies it.

---

## 10. Changes to other documents, and follow-ups

### 10.1 Changes to `docs/mac-app.md`

The product manager's spec should be brought in line with these (none changes what the app detects or sends):

1. **Five icon states, not four:** adds **Needs you** (Local Network blocked, pairing refused, API mismatch). "Calls from your Mac" off on the bar shows as **Paused**, not as an error. Not connected waits 15 seconds before it shows.
2. **Two status lines, not three.** What the bar is showing is folded into line 2, and only when it differs from what you'd expect (set aside, screen off, Wi-Fi setup, ignoring calls).
3. **"Calls from your Mac are off on TinyBar (turn them on in the Remote)"** becomes "TinyBar 2A1C is ignoring calls from your Mac" with **Open TinyBar Remote…** (now a `200` with `sources.mac: false`, `api.md` 14.1).
4. **Don't Count <app> has an undo,** Count <app> Again, while the app is still using the mic, and a camera equivalent, Don't Count the Camera. Criterion 26 should cover both.
5. **Pause** keeps its three durations, in a submenu named **Pause Detection**, with **Resume Detection** while paused.
6. **Connect…** joins the menu whenever the bar isn't connected.
7. **Settings tabs:** General, Apps, **Connection** (was "TinyBar") and Privacy. **Restore Defaults** (Apple's wording) replaces "Reset to defaults", and Privacy has none.
8. **Delays are pop-ups** with preset values (Right away to 30 seconds; 3 to 60 seconds), so criterion 9's "refused" becomes impossible to reach from the UI. The core still validates.
9. **Call apps have no per-row on switch:** moving an app between Call apps and Ignored apps is the control.
10. **New settings:** **Use Wi-Fi when TinyBar isn't plugged in** (on), **Name for this Mac** (empty, from `api.md`), and **Pause USB** (from `api.md` 6.3), the last two under Advanced. Pause USB is also in the Option-click menu.
11. **Pairing over Wi-Fi** is started from the app, and the code is shown on the bar, as `api.md` 14.5 already says. The Connect window replaces the first-run window's three steps with one page plus the Wi-Fi page.
12. **"This TinyBar doesn't support pairing yet"** becomes "TinyBar 2A1C doesn't need pairing, so you're all set." (`auth: none`).
13. **A different TinyBar on USB** becomes your TinyBar and the old one is forgotten (5.3). **Forget** while plugged in ignores the port until it's replugged (5.7).
14. **Reopening the app** opens Settings, or the Connect window if not set up (section 8).
15. **Privacy text** lists the random install ID, pending the user's OK in `api.md` 14.2.

### 10.2 For `docs/api.md`

Nothing needs changing. This design uses: `name` from `info`, `hello` and pairing replies; `wifi` from `info` (to say whether Wi-Fi will work after unplugging); `showing`, `screen`, `call.aside` and `sources.mac` from every `call` reply; and the pairing error codes and `attempts_left` and `retry_after_s`. **One request:** the bar's own Wi-Fi address in `info` (it's in `status.wifi.ip` today), so the app can check whether Wi-Fi reaches the bar (4.3) right after `hello`, without a second request. *Optional; the app can send `status` instead.*

### 10.3 For the mock-up and the firmware

- Draw the **pairing screen** (5.6) and its toasts in the mock-up, and add it to the firmware's screens.
- Show the bar's **name** on the Wi-Fi menu's **Network** tile, as `api.md` 14.3 asks: the Wi-Fi page's help line depends on it.
- The Remote's **Connect your Mac** card: replace the planned Pair a Mac button with the instructions in `api.md` 14.3. Suggested copy: "To pair your Mac, plug this TinyBar into it once. Or, in the TinyBar menu on your Mac, choose Connect…, then Pair Over Wi-Fi; this TinyBar shows the code." Plus a list of paired devices with **Remove**.
- The card's link to "How the Mac app talks to TinyBar" stays; its "Coming later" badge goes when the app ships.

### 10.4 For `docs/decisions.md`

The follow-up "UX: draw the four menu-bar icon states, and design the menu, the Settings window and the first-run window" is answered by this file. The proposals in 10.1 should be listed for the user's OK, especially: the fifth icon state, the Wi-Fi switch, switching bars by plugging one in, and no notifications at all.

---

## 11. Unverified until it's built on a Mac

The Linux container can't build AppKit or SwiftUI, so none of this has been seen on screen. To check on macOS 14, 15 and 26:

- **SF Symbol size and weight** in the status item (3.3), and whether `rectangle` reads as TinyBar among other menu-bar icons; if not, switch to the custom glyphs.
- **The status lines' look** (4.1): whether disabled items keep attributed colors, or a custom view is needed, and that it doesn't highlight on hover.
- **The menu updating while open** (4.1): timers in the menu's event-tracking run-loop mode.
- **Option-click** (4.8): reading `NSEvent.modifierFlags` in `menuWillOpen(_:)`.
- **Opening Local Network settings** directly. The URL `x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork` is widely used but not documented by Apple; if it doesn't land on the pane, open Privacy & Security and say "Choose Local Network".
- **`SMAppService.Status` values** and `openSystemSettingsLoginItems()` for an ad-hoc-signed app (6.2).
- **Opening the Settings scene** from an app with no Dock icon and bringing it to the front (`SettingsLink`, or activating the app first).
- **`applicationShouldHandleReopen`** firing for an app with `LSUIElement` (section 8).
- **The custom glyphs** at 1x on a non-Retina display (rendered in a browser only).
