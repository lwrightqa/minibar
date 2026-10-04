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

- **Direction A, Bold Signal (decided 2026-10-04).** On 2026-10-04 the user switched the bar's look to **Direction A, Bold Signal**, the first direction on `docs/design-directions.html` (not to be confused with option A of the font study): **solid, saturated status-color fields**, white **Barlow Condensed 700** uppercase headlines, **Barlow** for all other text, and a darker **tinted info column** on the right. It **replaces** the earlier pick, the Low Glare layout in Bitcount Prop Single round dots with Handjet, which is kept below as history.
  - Bold Signal is the look that goes to the firmware, the simulator's default, and the design card with the "pick" badge. The **Low Glare Pixel** card (Bitcount and Handjet) stays on the mock-up page and can still be applied, but it's no longer the default.
  - The switch changes only the look. Every other decision and open proposal stands, including focus ticking going silent on a dark screen, meeting titles hidden by default, the offline behavior and the Mac app's API.
  - `tools/fonts/` (the Bitcount and Handjet tools `equalize_digits.py` and `snap17.py`, with their README and the fonts' licenses) is **kept for reference**, but the current look doesn't use it.
  - The spec, proposals and follow-ups for Bold Signal are below. Option G's type spec, trade-offs and proposals are kept after them as history.
- **Low Glare layout in a pixel font (decided 2026-10-04; superseded the same day by Bold Signal).** The user reviewed the four design directions on a separate page (`docs/design-directions.html`) and prefers the **Low Glare** layout: a warm near-black background, a 6 px edge bar in the status color, a colored sentence-case headline, a faint side tint, warm off-white and muted text, a status-color frame instead of the white alarm flash, and a paused timer shown steadily in the muted color instead of blinking. It is to be set in a **pixel font**.
- **Pixel font (decided 2026-10-04; superseded the same day by Bold Signal): option D, with Handjet for the small text.** The user picked option D of the font study (`docs/pixel-fonts.html`): **Bitcount Prop Single in round dots** for the headlines, the timer and the clock, with **Handjet** replacing option D's Tiny5 for the small text. This is the user's choice over the team's recommendation: the UX designer, lead developer and product manager had all ranked option A (Jersey 15 with Micro 5) first, for its bold solid word across the room, real lowercase and perfectly sharp pixels. The team set the pick up as **option G**, first on the font study page, and fixed what the study had flagged (see the type spec and trade-offs below).
  - *History:* until 2026-10-04 this was open. The mock-up showed the choice as the **Low Glare Pixel** direction with Jersey 15 headlines and Jersey 10 small text as a stand-in, and the simulator's default look stayed as it was, to switch once the font was chosen.
  - Now that it's chosen, the Low Glare Pixel direction and the simulator's default switch to option G. **Not done yet:** `docs/mockup.html` still shows the Jersey stand-in (see Follow-ups). *(Built later on 2026-10-04, then replaced as the default by Bold Signal.)*
- In the Style panel, the **dark** screen mode is the Low Glare treatment: choosing it starts from Low Glare's colors (brightened so they read on near-black), tints and sentence-case headlines, and keeps the fonts already chosen.

### Spec: Bold Signal (2026-10-04)

The UX designer's spec for Direction A. Positions are on the 640 × 172 screen.

- **Status colors**, with white text's contrast on each: Available #0F7F3C (5.10:1), Busy #D01B3A (5.38), In a meeting #8B3AE5 (5.48), Focus #B0590D (4.91), Short break #0B7A70 (5.21), Long break #12708F (5.61), Away #545C65 (6.78), Message #C0198C (5.59), On a call #1450B4 (7.40, the `--s-call` token).
  - **Info column tints**, in the same order: #0B612E, #9E152C, #6A2CAE, #86440A, #085D55, #0E556D, #40464D, #92136B, #0F3D89. Each is the status color about 24% toward black (`lv_color_darken(status, 61)`). White on a tint is 7.4 to 10.3:1.
  - **Dark surfaces:** the Clock (idle) screen and the menu overlay are #0E1013. Menu tiles are #1C1F24, and the Done tile is #E8EBEE with dark text. Wi-Fi setup stays #1D3557 (white 12.4:1).
  - **Muted text** #DFE5EA: 5.8 to 8.1:1 on the tints, 15:1 on #0E1013.
  - Compared with the earlier solid look's palette: Available, Busy, Focus and Short break are darker for contrast and keep their hues, Message is a more vivid magenta, In a meeting moves from indigo to purple, and Long break moves from blue to petrol, which frees cobalt blue for On a call. The closest two statuses are 15.1 apart (CIEDE2000); On a call is 43.9 from Busy and 19.2 from In a meeting, and it's darker than Meeting, so it still stands apart for red-green color-blind viewers. Busy and Focus are hard to tell apart with deuteranopia (2.6), but both mean "don't interrupt" and the words differ.
- **Contrast rules:**
  - Text on a status field is **always pure white at full opacity**, the kicker and the sub line included (the mock-up's dimming of the kicker to .82 and the sub line to .9 is off for this direction). Hierarchy comes from size, weight and capitals.
  - The muted color is used **only** on the tinted info column and on dark surfaces (Clock, menus). On a status field it would drop to about 3.9:1.
  - So Bold Signal meets the 4.5:1 rule for directions (see Mock-up-only tools): the lowest is white on Focus at 4.91:1.
- **Layout:** the main field runs from x 0 to 448, with text inset 24 px on the left and 20 px on the right (404 px wide). The info column runs from x 448 to 640 (192 px), in the solid tint, with a 16 px inset. Baselines are fixed, so the lines don't move when the headline changes size.
- **Kicker:** Barlow 700, 15 px, capitals, 1.5 px letter-spacing, baseline y 30.
- **Headline:** Barlow Condensed **700** (not 800, which closes the counters of A, B and E at a distance; 800 stays in the Style panel only to try), white, capitals (meeting titles and messages in mixed case). Each headline takes the largest size that fits 404 px:
  - **112 px** (cap height 78, baseline y 125): the timer, the clock and BUSY.
  - **100 px** (cap 70, baseline 121): AVAILABLE and ON A CALL.
  - **78 px** (cap 55, baseline 113): IN A MEETING and BACK AT 1:30.
  - **62 px** (baseline 107): meeting titles and the message marquee.
  - The cap centers all sit near y 86.
  - **Tabular digits:** the timer and clock use Barlow Condensed's tabular figures, so they keep the same width every second ("18:42" is about 254 px at 112 px).
- **Sub line:** Barlow 500, 19 px, white, baseline y 152.
- **Info column:**
  - **Status row:** the time in Barlow 600, 15 px, baseline y 30 (level with the kicker), and 16 px icons on the right, 8 px apart: **Wi-Fi** (crossed out when offline) and a **Mac icon while a Mac is connected**. **No battery icon**, since the bar runs on USB with no battery (see Product).
  - **Label:** Barlow 700, 12 px, capitals, 1.2 px letter-spacing, muted, baseline y 92.
  - **Value:** Barlow Condensed 700, 46 px, baseline y 130, with AM or PM at 17 px. Values that are words ("Design review") are 28 px. Durations fit without an hours-only rule: "10h 20m" is about 152 px of the column's 160.
  - **Foot:** Barlow 500, 14 px, muted, baseline y 152 (level with the sub line).
- **Tomatoes:** 32 × 32 px at 1:1 (the sprites' own size, so they stay sharp) with 8 px gaps; four fit exactly in 160 px. The end screen's tomato is 64 px (2x).
- **Progress bar:** 6 px high at y 166 to 172, a white fill on a track of `lv_color_darken(status, 90)`.
- **Corners:** tiles, the corner pill and toasts have an 8 px radius.
- **Alarm:** Bold Signal uses the solid look's white flash. The status-color frame was Low Glare's.
- **LVGL 9:** solid fills only, no opacity on text, no gradients.

### Proposed for Bold Signal (2026-10-04, waiting for the user's OK)

- **Proposed: call copy** (the designer's suggestion). Kicker "From your Mac · Slack" ("· Slack" left out when no app is sent), headline ON A CALL, sub line "Message me instead", and in the info column "On the call", the duration, and "since 2:04 PM", with the foot changing to "Meeting ends 2:30 PM" when a calendar meeting is also on. The designer's example writes the duration "12 min"; **Proposed:** "12m", like every other duration on the bar.
- **Proposed: a paused timer holds still in white** at full opacity, with the kicker ("Paused · …") and the sub line ("Flip or tap to resume") saying it's paused. The solid look in the mock-up blinks it down to 35% opacity, which breaks the full-opacity rule (white at 35% on a status field is far below 4.5:1). Low Glare also held it still, so this is what the user last saw.
- **Proposed: the Pomodoro corner pill's tomato** is a 16 px sprite drawn for that size, or a plain dot if that doesn't read well. A 15 px downscale of the 32 px sprite blurs.
- **Proposed: check the smallest text** (the 12 px label and the 14 px foot) on the real bar before the firmware's screens are final.

### Follow-ups for Bold Signal (open)

- **Mock-up (being switched in the 2026-10-04 round):** Bold Signal becomes the simulator's default (`DEFAULT_STYLE`) and what Reset to defaults brings back, its card carries the "pick" badge and comes first, the copy near "Your pick comes first" and "The simulator opens with your pick" describes it, and every screen follows the spec above (white at full opacity on the fields, weight 700, the fixed baselines, the status row without a battery icon and with the Mac icon). The Low Glare Pixel card stays, without the badge. Then the published Artifact needs updating.
- **Firmware fonts:** convert with lv_font_conv at 4 bpp:
  - Barlow Condensed Bold at **112, 100 and 78 px** (A to Z, 0 to 9, colon, space and middle dot), **62 px** (printable ASCII), and **46 and 28 px** for the info column.
  - Barlow Medium (500) at 19 and 14 px, SemiBold (600) at 15 px, and Bold (700) at 15 and 12 px.
  - Measure the flash they take once converted.
- **Tabular digits baked into the fonts:** lv_font_conv ignores OpenType features, so before converting, freeze the tabular figures into the default digits with fonttools' `pyftfeatfreeze -f tnum` (from the opentype-feature-freezer package). Barlow's default digits are proportional: in Barlow Condensed Bold the 1 is 284 units wide against 498 for every tabular digit. The product manager checked Barlow Condensed Bold and Barlow SemiBold on 2026-10-04: the tabular digits and the colon have no kerning with each other, so unlike Bitcount no kerning needs removing. Check again in the converted fonts (every digit the same width, no digit kerning pairs).
- **Font names:** **Proposed:** like the Bitcount copies, the frozen Barlow files get a TinyBar family name and a modification note in their name tables, keeping the Barlow Project's copyright and the OFL text (in `tools/fonts/licenses/`). Barlow's copyright notice names no Reserved Font Name.
- **Sizes not in the designer's list yet:** AM or PM on the clock (about 36 px in the mock-up) and in the info column (17 px), menu tiles, toasts, the corner pill, the source chips (MAC, CALENDAR), the Wi-Fi setup screens and the hold screen. Measure them from the mock-up once it's switched, and convert extra sizes or reuse the ones above.
- **Icons:** 16 px Wi-Fi, Wi-Fi crossed out and Mac icons for the status row.
- **Font tools README:** `tools/fonts/README.md` still introduces option G as the user's pick. It needs a note that the current look is Bold Signal and these tools are kept for reference, and a place for the Barlow build steps.

### Type spec: option G (2026-10-04; superseded by Bold Signal, kept as history)

The details, with every measurement, are in `docs/pixel-fonts.html` (option G). Colors are Low Glare's: background #111110, text #e2ddd3, muted #9a948a, and the status colors and side tints. Positions are on the 640 × 172 screen with its 6 px edge bar; text starts at x 26 in the main column (408 px wide) and x 470 in the side column (154 px wide).

- **Three font files, built by the team** rather than the downloads:
  - **TinyBarBitcount-Round.ttf:** Bitcount Prop Single at wght 400, ELSH 0, CRSV 0 (cursive a and f off), ELXP 0, slnt 0. Headlines, timer, clock, splash and Wi-Fi setup titles.
  - **TinyBarBitcount-Square.ttf:** the same font at wght 384.88, ELSH 50 (square dots that exactly fill the grid). Side value and the hold screen's title.
  - **Handjet-Snap17.ttf:** Handjet's solid squares (wght 400, ELSH 2, ELGR 1) with every element moved onto a whole pixel at 17 px. All other text, at 17 px (1x) and 34 px (2x).
  - Both Bitcount files are built with `tools/fonts/equalize_digits.py --grid 100 --colon --tnum-shapes`: all ten digits share one 700-unit width with their ink centered, the 1 is the one with a base that option D showed, the colon has a dot of space on each side, and kerning that involves a digit is removed. Checked 2026-10-04 in the built files: digits 700 units, colon 300, no digit kerning.
- **Headline** (round dots, status color; the clock in the text color). Bitcount has 10 dots per em, so sizes are multiples of 10 px. Each headline takes the largest step that fits the main column:
  - **100 px:** Busy (220 px of ink), On a call (390), short messages, and the splash's "TinyBar" (350, text color, with the tomato sprite).
  - **80 px:** Available (328; 410 at 100).
  - **60 px:** In a meeting (360), Break time and Back to it (a matching pair), meeting titles ("Design review" 402), and the full-width Wi-Fi setup titles ("Wrong password" 492 of about 588).
  - **50 px:** Back at 12:30 (350; 420 at 60) and "Scan to set up" on the QR screen.
  - **40 px:** meeting titles too long for 60, then an ellipsis. Messages use 100, 80 or 60 px, and longer ones scroll at 60.
  - **Timer and clock, 120 px, digits and colon only:** every MM:SS is 372 px wide (348 of ink) and the colon never moves. The clock adds AM or PM in Handjet 34 px in the same color, right after the last digit.
  - Placement: the headline's cap-plus-descender box is centered between the kicker's baseline (y 28) and the sub line's cap top (y 146), so Busy's baseline is at y 112, the timer's at 117 and In a meeting's at 102. Text that starts with a digit is placed so the first digit's ink sits on the column's left edge.
- **Side value:** Bitcount square at 40 px (4x) in the text color, cap top y 102, baseline 126 (one size down from option D; see Proposed). "45m" is 88 px, "1h 5m" 124, "1h 20m" and "9h 59m" 152. Values that are words ("Not set up", "Off", "Nothing", Next-up titles) use Handjet 34 px, cut with an ellipsis at 154 px. The hold screen's title ("Keep holding", "Powering off") uses the square font in sentence case.
- **Small text, Handjet Snap17 at 17 px:**
  - Kicker: capitals, 1 px letter-spacing, muted, cap top y 17, baseline 28. Side label: the same, cap top y 81.
  - Sub line (see Proposed): text color, sentence case, no letter-spacing, baseline y 157. Foot: muted, baseline y 157. Status row: muted, cap top y 17.
  - Toasts, Wi-Fi setup steps (21 px line pitch), setup kickers and feet, the line under the hold title, and the Pomodoro corner pill: 17 px.
  - Source chips (MAC, CALENDAR): 17 px capitals with 1 px letter-spacing in the status color, a 1 px status-color outline, 3 px padding above and below, 5 px at the sides and a 3 px radius, cap-aligned on the kicker line.
  - Menu tiles: values at 34 px in the text color ("Show again" on two lines), labels at 17 px in capitals with 1 px letter-spacing, feet at 17 px wrapping to two lines.
- **Firmware fonts:** lv_font_conv with `--autohint-off` and its default compression, about **105 KB** in all (under 1% of the 16 MB flash):
  - Round dots at 100, 80, 60, 50 and 40 px with the full character set, plus 120 px with digits and colon only, all at 2 bpp. 2 bpp looks the same as 4 bpp on the dots; 1 bpp turns them into octagons.
  - Square dots at 40 px, and Handjet Snap17 at 17 and 34 px, at 1 bpp.
  - Checked in the converted 120 px font: every digit is 84 px wide, the colon 36 px, and there are no kerning pairs.
- **Mock-up:** Bitcount comes from Google Fonts; the 2.9 KB Snap17 file is embedded as a data URI. Google's Bitcount still jitters with tabular figures, so each digit sits in a 0.7 em cell and each colon in a 0.3 em cell to match the device. The CSS is in `docs/pixel-fonts.html`.

### Option G's trade-offs and how they were handled (2026-10-04; superseded by Bold Signal, kept as history)

- **Bitcount's digits aren't equal width.** On the device the cause is the narrow default 1 and Bitcount's digit kerning, since lv_font_conv ignores the tabular-figure feature. **Fixed** by `equalize_digits.py`: "18:41", "18:46", "11:11" and "17:07" were 264 to 336 px wide with 4 of 5 columns moving; now all are 372 px and nothing moves.
- **Round dots are anti-aliased on the device, by design.** 23 to 27% of lit pixels are partly lit at 60 to 120 px, 48% at 50 px and 75% at 40 px. The smaller sizes are used only for Back at 12:30, the QR screen's title and long meeting titles. Kept, since it's the round-dot look the user chose. It costs flash: the round set is about 94 KB at 2 bpp (everything at 4 bpp would be 167 KB).
- **Handjet is sharp only at 34 px and up as served, and 34 px doesn't fit.** At 34 px "MEETING ENDS IN" and "1 done · 31m focused" overflow the 154 px side column. As served, Handjet at 17 px is 32.6% soft on the device because many elements sit on half pixels, which also drops muted text to about 2.6:1 contrast. **Fixed** by Handjet Snap17: 0.3% soft at 17 px (only the grave accent) and 0% at 34 px.
- **Handjet's round dots can't echo Bitcount's.** Below about 68 px they render as solid squares (17 px) or a gray stroke (34 px), so small text uses the solid squares.
- **The headline changes size a lot** (100 down to 40 px), more than in any other option, because Bitcount is wide. Shorter copy helps: "Back 12:30" fits at 60 px.
- **Small text is small:** 11 px capitals, about 1.5 mm on the 8.6 cm screen, for reading up close. There's no sharp Handjet size between 17 and 34 px.
- **Look-alike characters:** Handjet's I and l are identical, and Bitcount's B and 8 can read as 6, as in option D. Not addressed.
- **Correction to the font study's figures:** Handjet at 34 px and Bitcount's squares at 50 and 20 px are 0% soft, not 7% and 2%. The earlier scripts counted lv_font_conv's origin markers as soft pixels.

### Proposed for option G (2026-10-04; superseded by Bold Signal)

These applied only to Bitcount and Handjet. **Superseded on 2026-10-04 by the switch to Bold Signal**, so they no longer wait for the user's OK. They're kept as history, with what Bold Signal does instead.

- *Superseded:* **Proposed: the line under the headline moves to Handjet 17 px,** out of option D's square dots. D's 20 px line overflows real copy: "Please don't interrupt · break at 12:55 PM" is 416 px of 408, the Wi-Fi setup line is 562 px, and meeting details would be cut at about 29 characters. Handjet 17 fits all current copy (235 px for that line, 309 px on the full-width setup screen), with less presence from across the room. If the user wants D's line back, the copy has to be shorter ("Don't interrupt · break at 12:55 PM", about 348 px) and meeting details are cut at about 29 characters. *Bold Signal's sub line is Barlow 500 at 19 px.*
- *Superseded:* **Proposed: the side value is one size down from option D,** 40 px instead of 50. At 50 px, durations over an hour ("1h 5m", "1h 20m") don't fit the side column. *Bold Signal's value is Barlow Condensed 700 at 46 px.*
- *Superseded:* **Proposed: from 10 hours on, durations show hours only** ("10h"), since "10h 20m" is 180 px at 40 px. *Not needed in Bold Signal: "10h 20m" is about 152 px of 160.*
- *Superseded:* **Proposed: menu tiles' inner padding drops from 12.8 to 10 px,** so "QR code" (90 px), "Medium" and "Restart" (88 px) fit at 34 px. *That was for Handjet at 34 px; Bold Signal's tiles are measured in its follow-ups.*
- *Superseded:* **Proposed: check the 17 px small text on the real bar** before the firmware's screens are final. *That was Handjet Snap17; Bold Signal's smallest text gets the same check (see its proposals).*

### Follow-ups for option G (2026-10-04; closed by the switch to Bold Signal)

- **Done (2026-10-04): mock-up.** The Low Glare Pixel direction and the simulator's default use option G (fonts, sizes, digit cells, positions and the embedded Snap17), and Bitcount Prop Single and Handjet Snap17 are in the Style panel's pixel fonts. *Since the switch, option G stays on the Low Glare Pixel card, and the default moves to Bold Signal (see Follow-ups for Bold Signal).*
- **Done (2026-10-04): font tools.** `snap17.py` is in `tools/fonts/`, and the README describes option G and the Snap17 build. *Superseded:* Option G's new sizes (Bitcount round at 80, 60 and 50 px, square at 40 px, Snap17 at 17 and 34 px) still need converting with lv_font_conv and measuring for flash. *Superseded:* **Proposed:** like the Bitcount copies, the snapped Handjet gets a TinyBar family name (the README's build command does this) and a modification note in its name table, keeping Handjet's copyright and license. Handjet declares no Reserved Font Name, so its current name is allowed. *Neither Bitcount nor Handjet is built for the firmware now; the tools stay in `tools/fonts/` for reference.*
- **Done (2026-10-04): font study page.** Handjet's solid shape is 0% soft in the glance table. The dot variant's softness on option C's card is real (the lead measured 33%), so it stays.

## Mock-up-only tools

- The **Style panel** (fonts, colors, screen mode, headline case, ripening slider with the USDA stages) exists only on the mock-up page as a design tool. None of it goes on the device.
  - Since 2026-10-04 its font lists include **pixel fonts**: Jersey 10, Jersey 15, Jersey 20, Silkscreen, Pixelify Sans, VT323, Press Start 2P, DotGothic16, Tiny5, Micro 5 and Handjet, with only the weights Google Fonts serves and no synthesized bold. Text in a pixel font snaps to whole multiples of that font's own pixel grid, and each headline takes the largest such size that fits, so every font can be judged fairly. With a pixel headline font other than Jersey 10 or 15, the line under the headline uses the text font, since the others are too wide or too big for it.
- **Design directions** (decided 2026-10-04): four complete looks (fonts, colors and treatment) side by side on the mock-up page. Each has a live preview and an **Apply** button that puts it on the simulator. Applying one also sets the Style panel, so the look can be fine-tuned and copied. The direction the user picks goes to the firmware.
  - **Updated 2026-10-04:** a fifth card, **Low Glare Pixel** (the user's pick, with the font still to be chosen), comes first across the full row. It uses Jersey 15 and Jersey 10, like Pixel Arcade, as a stand-in, and Bold Signal and Low Glare share Barlow Condensed; the "no shared fonts" rule for directions is waived now that the user has picked.
  - **Updated 2026-10-04 (font chosen):** the Low Glare Pixel card is to use option G, Bitcount round dots with Handjet Snap17, instead of the Jersey stand-in, and becomes the simulator's default. Not built yet (see Look). *(Built later on 2026-10-04.)*
  - **Updated 2026-10-04 (switch to Bold Signal):** the user switched to **Direction A, Bold Signal**. It becomes the simulator's default, carries the "pick" badge and comes first, and it's the direction that goes to the firmware. The Low Glare Pixel card stays and can still be applied, but it's no longer the default. See Look.
  - **Proposed:** every direction keeps all screen text at a contrast of 4.5:1 or better on every status color. It uses only what LVGL 9 can draw: solid fills, simple gradients, and Google Fonts under the OFL or Apache license. (The current default misses 4.5:1 with white text on Available, about 4.0:1; on Focus, about 3.2:1; and on Short break, about 4.2:1.) *Note 2026-10-04: those figures are for the original solid look. Bold Signal, the default now, meets the rule: white is 4.91:1 or better on every status field.*
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
