# TinyBar decisions

The running record of what has been decided, and why. The product manager keeps it current; everyone on the team reads it before starting work. When a decision changes, edit the entry and note the date. Don't delete history.

## Product

- **What it is:** a status bar for a desk in an **open office** that tells the people nearby whether it's a good time to interrupt.
- **Hardware:** Waveshare ESP32-S3-Touch-LCD-3.49, **V2** board (V2 sticker on the case, "Rev1.1" silkscreen). The 3.49" 172 × 640 screen is used in landscape as 640 × 172.
- **Power:** runs on **USB**. No battery for now (the 1,000 mAh cell the user tried is rated below the board's 1.2 A charge current).
- **Firmware:** **ESP-IDF with LVGL 9**, started once the mock-up is final.
- **The mock-up comes first.** `docs/mockup.html` is the source of truth for screens and behavior, and is published as an Artifact (https://claude.ai/artifact/NDTZFnXwKTypeGx3zEnDes). **Keep it up to date with every change**, including its notes, controls table and diagrams.
- **Language:** American English. **Copy:** nothing that implies a private office (no "come in", "knock", "door").

## Statuses

- Manual: Available, Busy, In a meeting, Pomodoro, Away, Message, Clock (idle).
- Automatic: **In a meeting** from the calendar, and **On a call** from the Mac app (see Automatic status).

## Controls

- Tap: next status (on the Pomodoro screen, start or pause). Swipe: previous or next status. Hold: quick menu (timer menu on the Pomodoro screen).
- BOOT: next status. PWR press: screen off or on. PWR hold 3 s: power off; a press turns it back on.
- Flip the bar over: turns the layout the right way up, silences any alarm, and starts whatever the Pomodoro is waiting for. A running timer is left alone; a flip never skips anything.
- **No dead ends:** every control does something in every state, every screen has a way back, menus have a Done tile and close after 8 s, and every action shows a short confirmation.
- A dark screen wakes on the first tap or button press. A ringing alarm is answered by any tap, hold, swipe, button or flip.
- **Proposed (2026-10-04):** on an automatic screen (On a call, or In a meeting from the calendar), a tap or BOOT goes back to your own status. See Automatic status.

## Pomodoro

- 25-minute focus, 5-minute short break, 15-minute long break after every 4th session. All adjustable from the Remote.
- When a phase ends, the alarm flashes and chimes every few seconds until answered, for at most a minute. Optional auto-start skips the alarm screens.
- The timer keeps running when another status is shown (a countdown pill appears in the corner).
- **Proposed (2026-10-04):** calls and calendar meetings pause a running timer and mute the chime. See Automatic status.
- **Ticking during focus (decided 2026-10-04):** optional and **off by default**. The Remote's Pomodoro section has a "Ticking during focus" switch and a volume choice (Soft or Medium); on the bar it's under Settings in the timer menu (Off, Soft, Medium). One gentle tick per second, only while a focus session is actually running: silent while paused, during breaks, while an alarm rings, during a call or calendar meeting (it picks up again afterwards), and when the bar is off. Each change shows a toast. Firmware: a short, quiet PCM click through the ES8311 codec over I²S, kept at a low level because it's an open office.
  - **Proposed (2026-10-04), waiting for the user's OK:** "when the bar is off" also covers a **dark screen** (one PWR press), not only powered off, because a dark screen often means you've stepped away. The mock-up is built this way. Both volumes stay well below the alarm chime (peaks about 18 dB below the chime for Soft and 10 dB for Medium).
- Stop ends the run, keeps today's tomatoes, and returns to the previous status.
- **Tomatoes:** PixelLab pixel-art sprites with **no faces** (`assets/tomato_ripe.png`, `assets/tomato_unripe.png`, pixel-aligned). Finished sessions are red, upcoming ones faded red, and the current one **ripens like a real tomato**: from the blossom end up toward the stem, through yellow and orange to red.

## Wi-Fi

- Setup by QR code: the bar shows a QR code that joins the phone to its own `TinyBar-Setup` network, where a page lets the user choose the office Wi-Fi.
- Supports password and work-login (username plus password) networks. Guest networks with a sign-in web page are not supported; the setup page says so.
- Skip uses the bar offline: statuses and the Pomodoro still work; the calendar and Remote don't.

## Remote

- A web page served by the bar at `tinybar.local` on the office Wi-Fi.
- Setting the status from outside the office network (an online relay) is **deferred**.

## Automatic status (decided 2026-10-04)

- Two sources, usable **separately or together**:
  1. **Calendar:** the bar reads a **secret iCal address** (Google Calendar → Settings → the calendar → Integrate calendar → "Secret address in iCal format"). The user pastes it on the **Remote web page**, and can also paste it during Wi-Fi setup. Scheduled meetings show as **In a meeting** and fill in "Next up".
  2. **Mac app (built later):** a small menu-bar app that watches whether the Mac's microphone or camera is in use and sets **On a call**, catching unplanned Slack huddles and Google Meet calls. It talks to the bar over USB if the bar is powered from the Mac, otherwise over Wi-Fi. Only "on a call: yes or no" leaves the Mac.
- Not pursued: a Slack app (it would need workspace admin approval).
- The mock-up already reserves the bar's side of the Mac app (the On a call screen, the rules for which source wins, and the Remote's "Connect your Mac" section), so the app can be added later without redesigning anything. *(Note 2026-10-04: none of this was in the mock-up yet. It's being built in the 2026-10-04 round, together with the calendar.)*

### Proposed (2026-10-04, waiting for the user's OK)

These are the product manager's suggested defaults for questions the user hasn't answered yet. The mock-up is being built to them this round. Change any of them and the team will follow.

- **Proposed: which source wins.** Highest first:
  1. A choice you make on the bar or the Remote while a call or meeting is on. It wins until that call or meeting ends.
  2. **On a call** from the Mac app.
  3. **In a meeting** from the calendar.
  4. Your own status: the manual status or the Pomodoro you last picked.

  When a call or meeting ends, the bar drops to the next one down. A call that ends during a meeting goes back to In a meeting, and when both are over the bar returns to your own status. Back-to-back meetings flow straight from one to the next. A call or meeting never interrupts the power screens, the Wi-Fi setup screens or an open menu (it shows once they close). A dark screen stays dark and shows the change when it's woken.
- **Proposed: controls on an automatic screen** (On a call, or In a meeting from the calendar). Nothing is a dead end, and you can always override:
  - **Tap or BOOT:** back to your own status. The call or meeting is **set aside** until it ends, and a toast confirms it.
  - **Swipe:** previous or next status, counted from your own status. This also sets the call or meeting aside.
  - **Hold:** the quick menu, as on any status screen (even when your own status is the Pomodoro).
  - **Flip:** works as it does everywhere else. It turns the layout and starts whatever the Pomodoro is waiting for, so the Pomodoro shows and the call or meeting is set aside.
  - **Remote:** any status, message or Pomodoro control there also sets the call or meeting aside.
  - **Undo:** while a set-aside call or meeting is still on, the quick menu's Calendar tile becomes **Show again**, and the Remote has a Show again button. The next call or meeting always takes over again.
    - **Proposed (2026-10-04, built by the team):** on the Pomodoro screen, where holding opens the timer menu, Show again takes the Settings tile's place, so that menu stays at five tiles. The confirmation toast says how to undo it ("Call set aside · hold to show it again"), since the device has no hint line.
  - A first tap on a dark screen only wakes it, as it does today.
- **Proposed: the Pomodoro during calls and meetings.** This is an exception to "the timer keeps running when another status is shown", which stays true for statuses you pick yourself.
  - When a call or meeting takes over, a running session or break **pauses**, and the corner pill says Paused.
  - When the call or meeting ends, the bar returns to your own status with the timer still paused. A tap or flip resumes it.
  - The bar makes **no sound while a call is on or a calendar meeting is in progress**, even if you set it aside. Alarms flash without the chime.
  - A call or meeting that starts while the alarm is ringing silences it, and the Pomodoro keeps waiting. When the call or meeting ends, the bar shows the waiting screen with one chime and flash, with no repeats.
- **Proposed: On a call is automatic only.** It isn't in the tap and swipe cycle or among the Remote's status buttons: a manual On a call that someone forgets to clear would mislead, and Busy and In a meeting already cover the manual case. The screen says the status comes from the Mac, names the app if one was sent, and shows how long the call has lasted. If a calendar meeting is also on, it shows when that meeting ends. It gets its own color token, `--s-call`, clearly different from Busy and Meeting.
- **Proposed: which calendar events count.** Timed events that show you as busy and that you haven't declined. All-day events, events marked "Free" and declined events are ignored. "Next up" covers the rest of today only.
- **Proposed: syncing.** The bar reads the calendar address about every 10 minutes, and whenever you choose Sync now (on the Remote or in the quick menu). It switches into and out of In a meeting at each event's exact start and end time, using its saved copy, rather than waiting for the next sync.
- **Proposed: the secret address is write-only.** Once it's saved, the bar's screen, the Remote and the API never show it in full again, because anyone on the office Wi-Fi can open the Remote. The Remote shows a short masked form with Replace and Remove. **Proposed (2026-10-04, built by the team):** the masked form shows the host, the file name and the last four characters of the private token (for example "calendar.google.com/…/basic.ics · ending 3f2a"), so two addresses can be told apart. The paste fields on the Remote and the setup page are masked, with a Show button, and start masked every time they open. Any `https://` or `webcal://` address of an `.ics` feed is accepted, and the help text explains where to find it in Google Calendar. Pasting Google's *public* address gets a warning to use the secret one instead.
- **Proposed: Show meeting titles,** a switch on the Remote, **off by default.**
  - Off: the bar shows "In a meeting" with times only, "Next up" shows times only, and locations are hidden. The Remote doesn't show titles either.
  - On: titles and locations show on the bar and on the Remote.
  - Events marked Private in Google Calendar never show a title.
- **Proposed: offline.**
  - With Wi-Fi skipped, the calendar is off (as decided), so no meeting data shows. The Mac app still works over USB, but not over Wi-Fi.
  - If Wi-Fi drops unexpectedly, the bar keeps following the last synced copy of today's calendar, and the quick menu shows when it last synced. This case isn't simulated in the mock-up.
  - If the Mac goes quiet (no message for 90 seconds), the bar ends On a call on its own, so it can't get stuck.
  - After a restart or power-on, the bar picks up a call or meeting that's still going.
- **Proposed: sources and defaults.** The Remote's Automatic status section has a separate switch for each source:
  - **Calendar meetings:** available once an address is saved, and then on.
  - **Calls from your Mac:** on. It does nothing until a Mac connects.

  Turning a source off ends its status at once. Turning it back on during a call or meeting shows that call or meeting at once.
- **Proposed: the Mac app's API.** The app is built later; the API is documented on the mock-up page.
  - **Wi-Fi:** `POST http://tinybar.local/api/call` with `{"active": true, "app": "Slack"}`, or `{"active": false}` when the call ends. The reply is `{"ok": true, "showing": "call"}`, or `{"ok": false, "error": "calls_off"}` when that source is turned off. `GET /api/status` returns what the bar is showing and which sources are on. It never returns the calendar address.
  - **USB serial:** the same JSON, one object per line, with `"cmd": "call"` added: `{"cmd": "call", "active": true, "app": "Slack"}`. A `{"cmd": "hello"}` line lets the app find the bar. The bar answers every line with one JSON line.
  - **Heartbeat:** the app repeats its current state every 30 seconds, and the bar ends a call after 90 seconds without a message.
  - **Pairing:** an `Authorization: Bearer <token>` header is reserved for a later pairing step. It isn't checked yet.
- **Conflict to resolve:** the user's example for this round sends the app name (`"app": "Slack"`), but the decision above says only "on a call: yes or no" leaves the Mac. **Proposed:** `app` is optional. The bar shows it if it's sent, and the Mac app can leave it out.
- **Open, not proposed this round:** the Remote and the API have no PIN, so anyone on the office Wi-Fi can open `tinybar.local` and change the status. Worth deciding before the firmware.

## Look

- **Low Glare layout in a pixel font (decided 2026-10-04).** The user reviewed the four design directions on a separate page (`docs/design-directions.html`) and prefers the **Low Glare** layout: a warm near-black background, a 6 px edge bar in the status color, a colored sentence-case headline, a faint side tint, warm off-white and muted text, a status-color frame instead of the white alarm flash, and a paused timer shown steadily in the muted color instead of blinking. It is to be set in a **pixel font**.
- **Open: which pixel font.** It's being compared in `docs/pixel-fonts.html`. Until the user picks one, the mock-up shows the choice as the **Low Glare Pixel** direction, with Jersey 15 headlines and Jersey 10 small text as a stand-in, and the simulator's default look stays as it is. The default switches once the font is chosen.
- In the Style panel, the **dark** screen mode is the Low Glare treatment: choosing it starts from Low Glare's colors (brightened so they read on near-black), tints and sentence-case headlines, and keeps the fonts already chosen.

## Mock-up-only tools

- The **Style panel** (fonts, colors, screen mode, headline case, ripening slider with the USDA stages) exists only on the mock-up page as a design tool. None of it goes on the device.
  - Since 2026-10-04 its font lists include **pixel fonts**: Jersey 10, Jersey 15, Jersey 20, Silkscreen, Pixelify Sans, VT323, Press Start 2P, DotGothic16, Tiny5, Micro 5 and Handjet, with only the weights Google Fonts serves and no synthesized bold. Text in a pixel font snaps to whole multiples of that font's own pixel grid, and each headline takes the largest such size that fits, so every font can be judged fairly. With a pixel headline font other than Jersey 10 or 15, the line under the headline uses the text font, since the others are too wide or too big for it.
- **Design directions** (decided 2026-10-04): four complete looks (fonts, colors and treatment) side by side on the mock-up page. Each has a live preview and an **Apply** button that puts it on the simulator. Applying one also sets the Style panel, so the look can be fine-tuned and copied. The direction the user picks goes to the firmware.
  - **Updated 2026-10-04:** a fifth card, **Low Glare Pixel** (the user's pick, with the font still to be chosen), comes first across the full row. It uses Jersey 15 and Jersey 10, like Pixel Arcade, as a stand-in, and Bold Signal and Low Glare share Barlow Condensed; the "no shared fonts" rule for directions is waived now that the user has picked.
  - **Proposed:** every direction keeps all screen text at a contrast of 4.5:1 or better on every status color. It uses only what LVGL 9 can draw: solid fills, simple gradients, and Google Fonts under the OFL or Apache license. (The current default misses 4.5:1 with white text on Available, about 4.0:1; on Focus, about 3.2:1; and on Short break, about 4.2:1.)
- **Simulate controls** (decided 2026-10-04): buttons that start and end a call or a meeting, so the automatic statuses can be tried before the Mac app exists. They're clearly labeled mock-up only, and they sit outside the Remote panel.

## Hardware notes for the firmware (V2)

- Display: AXS15231B over QSPI. Backlight on **GPIO 42** plus the expander's BL_EN (EXIO1). LCD reset on the expander (EXIO5); TE on GPIO 21.
- Touch: AXS15231B over I²C (SDA GPIO 17, SCL GPIO 18).
- TCA9554 I/O expander: EXIO0 touch interrupt, EXIO1 backlight enable, EXIO2/3 IMU interrupts, EXIO4 RTC interrupt, EXIO5 LCD reset, **EXIO6 SYS_EN (power hold on battery)**, EXIO7 amplifier mode.
- PWR button read on **GPIO 16** (SYS_OUT); BOOT on GPIO 0; battery voltage on GPIO 4.
- RTC PCF85063, IMU QMI8658, audio codec ES8311.
- On USB the board is always powered; "power off" on USB means deep sleep woken by PWR.
- Battery connector MX1.25, **pin 2 positive**. Charger ETA6098 at 1.2 A. The board has battery protection and reverse-polarity protection.
- Flashing: merged images go at **0x0**; use **115200** baud (a faster write once left the screen showing noise).
