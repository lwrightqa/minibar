# MiniBar decisions

The running record of what has been decided, and why. The product manager keeps it current; everyone on the team reads it before starting work. When a decision changes, edit the entry and note the date. Don't delete history.

## Product

- **What it is:** a status bar for a desk in an **open office** that tells the people nearby whether it's a good time to interrupt.
- **Name: MiniBar (decided 2026-10-04, applied 2026-10-05).** The user renamed the project from TinyBar to **MiniBar**, written "MiniBar" wherever people read it (the splash screen, the Remote, the Mac app, the default bar name "MiniBar 2A1C", the setup network "MiniBar-Setup") and lowercase "minibar" in code, file names and network names (`minibar.local`, `_minibar._tcp`). The GitHub repository is `lwrightqa/minibar` (renamed by the user in GitHub's settings).
  - *(Applied 2026-10-05, once the pairing alignment round had finished, so the two didn't edit the same files at once.)* **What changed:** every screen, page, toast, log line and document; the host name `minibar.local` (`minibar-2.local` after a clash), the setup network `MiniBar-Setup`, the Bonjour service `_minibar._tcp`, the `device` value "MiniBar" in `info` and `hello`, the 401 realm, the Mac app's User-Agent `MiniBarMac/<version> (api 1.0)`, its Keychain service, settings key and bundle id (`com.minibar.MiniBarMac`; the app had never shipped); the firmware builds `minibar.bin` (`dist/minibar-<version>.bin`, from 1.0.3 on) and the Mac app builds `MiniBar.app` and `MiniBar.zip`; the mock-up moves a saved look and theme to its new browser keys once (`minibar-style-3`, `minibar-theme`). **What keeps the old name, and why:** identifiers that reach nobody but developers, or that stored data depends on: the `tb_` and `TB_` C names, the `@tb ` USB line marker, the `tb1_` token prefix and the `tb_token` cookie (every issued token stays valid), the NVS namespace "tinybar" (changing it would wipe every bar's settings), the `CONFIG_TINYBAR_*` build symbols (only the menu titles people see change), the Swift module, target and directory names (`TinyBarCore`, `TinyBarMac`, `TinyBarCoreTests`) and the internal type names, the frozen fonts' file names, the images already built as `dist/tinybar-1.0.0.bin` and `tinybar-1.0.2.bin`, and the checkout path. Dated entries in this file that quote the old name are history and stay as written. **For the user's bar:** firmware 1.0.3 changes a stored name that is still the old default ("TinyBar" plus the last four characters of the ID) to "MiniBar" plus the same four, once at start-up; a name a person typed is left alone, and no other setting is touched. The new host name signs paired phones out of the Remote (its cookie is per host): the phone pairs again once, and the old row can be removed on Paired devices. For one release the Mac app accepts both `device` values and browses both service types, because a bar on 1.0.2 still says TinyBar (`docs/api.md` 14.6).
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
- **Proposed (2026-10-05):** since the screen has no haptics, a short, quiet click confirms a tap, swipe or hold that the bar acts on. It never plays during a call or meeting. See Sound, Tap sound.

## Help and the controls tour (Proposed 2026-10-05)

- **The request (2026-10-05):** the user wrote: "We may want a screen that tells the user about the controls and how to use it." The bar has seven controls (tap, swipe, hold, flip, BOOT, a PWR press and a 3-second PWR hold), and some of them do different things on different screens. The hint line that explains them is on the mock-up page, not on the bar. The help is for the person who has just set up a MiniBar and for anyone in the office who walks up to one.
- **Mock-up first.** It's built in `docs/mockup.html` and checked against the round's acceptance criteria before the firmware follows. New copy says **MiniBar** (see Product); since the rename pass of 2026-10-05 the rest of the mock-up does too.
- **Proposed (2026-10-05, waiting for the user's OK): the coordinator's plan, refined by the product manager.**
  1. **A short tour on the bar.** It has one card for each control: Tap, Swipe, BOOT, Hold, Flip and PWR, in that order, followed by a More help card. Each card has a small drawing of the gesture (the drawings follow the ones in `docs/controls-guide.html`) and one line saying what the control does.
     - **Learn by doing, where it's safe.** On the Tap, Swipe and BOOT cards, the first time you use that control the card shows what it would have done (for example "Busy → In a meeting"). It changes nothing real: not the status, the timer, a call or meeting you've set aside, or the alarm.
     - **Hold, flip and PWR do their real jobs, and that ends the tour.** A hold opens the menu for the screen underneath, which is how you leave the tour (the coordinator's "a hold leaves it"). A flip ends the tour and then does its three things, as the decided Flip rule requires (see the conflict below). A PWR press ends the tour and turns the screen dark. Holding PWR for 3 seconds powers the bar off; let go early and the tour carries on. The Hold, Flip and PWR cards explain their control and say that trying it ends the tour. That's why they come after the practice cards.
     - **Moving through it:** a tap, BOOT or a swipe left goes to the next card, and a swipe right goes back. On the last card, a tap, BOOT or a swipe left finishes the tour.
     - **When it shows:** by itself only once for each bar, the first time setup ends, whether the Wi-Fi was set up (after Connected) or skipped. After that, only from the **Help** tile.
     - **Never a trap:** after 30 seconds without a touch or a button press the tour ends by itself. Every way out returns to the screen underneath. The tour never fills the screen with a status color, so nobody reads a card as a status. It makes no sound.
  2. **A help page served by the bar** at `minibar.local/help`. It fits a phone screen and covers every control and the screens where they differ. It's linked from the Remote (including its pairing prompt) and from a QR code on the tour's last card. It works without internet: nothing comes from outside the bar, and it has no outside links. It's open without pairing because it controls nothing and shows nothing private.
- **Proposed details (product manager, 2026-10-05):**
  - **The Help tile** goes in the quick menu, as a sixth tile before Done, and in the timer menu's Settings, before Back. It's not in Setup options. With the theme round's proposed Display tile (see Look), the quick menu would read Display, Calendar, Wi-Fi, Power, Help, Done. That round kept the quick menu at five tiles so the theme names fit, but those names sit in the Display menu, so a sixth tile doesn't affect them.
    - *Measured in the mock-up, 2026-10-05:* six equal tiles are 91.7 px wide, leaving 66.1 px inside 12.8 px of padding. In Bold Signal, the "CALENDAR" label (70.1 px) and the "Sync…" value (68.8 px) don't fit. The mock-up's Low Glare Pixel card as it stands (option G, before the Handjet theme lands) fits; check again in the Handjet theme. With 10 px of padding there would be 71.7 px, which only just fits. The UX designer fits the row, for example with less padding or a narrower Done tile.
  - **Interruptions are handled the way an open menu handles them.**
    - A call or calendar meeting that starts during the tour waits underneath: the Pomodoro pauses and there's no sound, as decided. It shows when the tour ends.
    - A Pomodoro phase that ends (the alarm, or an auto-start), a status or Pomodoro change made on the Remote, and a pairing code each end the tour and show at once.
    - Wi-Fi dropping leaves the tour up. The last card swaps its QR code for "reconnecting".
    - The first-run tour waits while something else holds the screen: a menu, an alarm, a pairing code, the power screens or a dark screen.
    - On the first run it shows even if a call or meeting is on, because the person who just set the bar up is standing at it. The call or meeting shows when the tour ends.
  - **"Tour seen"** is saved in flash as soon as the first-run tour appears, so an interrupted tour doesn't come back by itself; the Help tile is always there instead. It survives restarts, power off, Set up again and Forget all. A bar that's already set up when it first gets firmware with the tour (the user's bar) shows the tour once at its next start-up. In the mock-up, which starts set up and paired, the tour counts as seen; the "First-time Wi-Fi setup" demo resets it so the first-run tour can be tried.
  - **The last card's QR code** opens the help page at the bar's IP address, which works on phones that can't look up `.local` names. The line under it shows the bar's real address (`minibar.local/help`, or `minibar-2.local/help` after a name clash). Offline (Wi-Fi skipped) there's no QR code; the card says the guide needs Wi-Fi and how to set it up. The setup network isn't opened just for help, as decided under Wi-Fi.
  - **The Remote and the API:** while the tour is up, the Remote says the bar is showing its controls tour, and still shows the status underneath. `GET /api/v1/status` keeps reporting the real status, with a new screen value for the tour. The lead developer adds this to `docs/api.md`.
  - **A Wi-Fi drop in the mock-up:** the Simulate panel gets "Wi-Fi drops" and "Wi-Fi is back" (mock-up only), using the firmware's proposed copy for a dropped Wi-Fi (see Firmware). Until now this case wasn't simulated.
- **Conflict, flagged (2026-10-05): learning the flip by doing.** The coordinator's plan practices every control "without changing the real status or timer". But the decided Flip rule says a flip always turns the layout, silences any alarm and starts whatever the Pomodoro is waiting for, and is "never swallowed anywhere". A practice flip would break that rule. **Proposed:** keep the rule: in the tour a flip ends the tour and then does all three things, as it does on a pairing code. Making the flip a practice only, so that during the tour it just turns the layout, would be an exception that only the user can approve.
- **Open, for the user (2026-10-05): where BOOT and PWR are.** The tour's drawings have to point at the real buttons. This round's brief says the user keeps BOOT and PWR **on top**. `docs/controls-guide.html` draws them on the **right edge** (BOOT the upper button, PWR the lower), moving to the left when the bar is turned over. Nothing about it is recorded here yet.
- **Follow-ups (open):** the UX designer specifies the cards, their copy and drawings, the Help tile and the phone page in both looks. The mock-up builds them, along with its notes, controls table (a tour column) and menus diagram. Then:
  - **Firmware:** the tour, the "tour seen" flag in NVS, and `web/help.html` under a size budget (**Proposed:** 100 KB or less, compressed).
  - **`docs/api.md`:** the tour's screen value.
  - **The controls guide:** mention the tour and the Help tile.
- *(2026-10-05, mock-up round 2, lead developer: built in `docs/mockup.html`, still Proposed.)* It follows this entry where the UX designer's spec (`team/help/ux-spec.md`) differs: **seven cards** (Tap, Swipe, BOOT, Touch and hold, Flip, PWR, More help; the spec merged BOOT and PWR into one card that a PWR press doesn't end), and a PWR press ends the tour. From the spec it takes the cards' layout (the Wi-Fi setup screen's QR panel and text column, in the bar's own theme, never a status color), their copy and drawings, the 700 ms pause after a practice result, the finish toast that says how to open it again ("Tour done · hold, then Help to see it again"), and the phone help page. BOOT and PWR are on top (Hardware notes), so the open question above is settled; the tour's arrows point at them upright and turned over. The quick menu fits six tiles with less padding inside them (8 px instead of 12.8 in Bold Signal). Its Wi-Fi tile is a few pixels wider than the spec's even 92 px, so the dropped-Wi-Fi foot "reconnecting" (82.5 px) fits on one line; Power, Help and Done give the room (Bold Signal 92, 92, 100, 89, 90, 89 px; Low Glare Pixel 92, 92, 99, 89, 90, 88 px). "Tour seen" is kept in the browser in the mock-up, and the Simulate panel can forget it. `GET /api/v1/status` reports `"screen": "tour"` while it's up (`docs/api.md` 7.3, Proposed).
  - **Firmware notes:** a tour struct in core and "tour seen" as one NVS key, written once when the first-run tour appears (one flash write per bar, so no wear concern). The drawings are LVGL primitives (rounded rectangles, lines, arcs, labels) in the 132 × 132 panel the setup QR code uses, so no images in flash. The QR code is a version 3 code of `http://<ip>/help`, made by the same encoder as the setup code. `web/help.html` is static and gzipped like `remote.html`, under the 100 KB budget.

## Pomodoro

- 25-minute focus, 5-minute short break, 15-minute long break after every 4th session. All adjustable from the Remote.
- When a phase ends, the alarm flashes and chimes every few seconds until answered, for at most a minute. Optional auto-start skips the alarm screens.
- The timer keeps running when another status is shown (a countdown pill appears in the corner).
- **Proposed (2026-10-04):** calls and calendar meetings pause a running timer and mute the chime. See Automatic status.
- **Ticking during focus (decided 2026-10-04):** optional and **off by default**. The Remote's Pomodoro section has a "Ticking during focus" switch and a volume choice (Soft or Medium); on the bar it's under Settings in the timer menu (Off, Soft, Medium). One gentle tick per second, only while a focus session is actually running: silent while paused, during breaks, while an alarm rings, during a call or calendar meeting (it picks up again afterwards), and when the bar is off. Each change shows a toast. Firmware: a short, quiet PCM click through the ES8311 codec over I²S, kept at a low level because it's an open office.
  - **Proposed (2026-10-04), waiting for the user's OK:** "when the bar is off" also covers a **dark screen** (one PWR press), not only powered off, because a dark screen often means you've stepped away. The mock-up is built this way. Both volumes stay below the alarm chime. *(Changed 2026-10-05 after the first test on the real bar: Soft was too quiet to hear from a reasonable distance, and so was Medium. A 25 ms tick sounds far quieter than its peak level suggests, so both now peak near the chime's peak: Soft about 3 dB below it (-16 dBFS) and Medium about 3 dB above it (-10 dBFS). Before, they peaked 18 and 10 dB below the chime. **Judged on the bar with 1.0.2 (2026-10-05): the user calls Soft "a sensible volume" from where they sit, so the levels stand.**)*
- Stop ends the run, keeps today's tomatoes, and returns to the previous status.
- **Tomatoes:** PixelLab pixel-art sprites with **no faces** (`assets/tomato_ripe.png`, `assets/tomato_unripe.png`, pixel-aligned). Finished sessions are red, upcoming ones faded red, and the current one **ripens like a real tomato**: from the blossom end up toward the stem, through yellow and orange to red.

## Sound

The bar has a small speaker, driven by the ES8311 codec, and no vibration motor. Its other sounds are decided elsewhere: the Pomodoro chime and the optional focus ticking under Pomodoro, and "no sound while a call is on or a calendar meeting is in progress" under Automatic status. "Tick" means the focus tick. "Click" means the touch click below.

### Tap sound (touch click; Proposed 2026-10-05, placement decided)

- **Decided (2026-10-05): the setting is called "Tap sound" and sits with the theme switch.** The user wrote: "Put the Tap sound setting with the theme switch." On the bar that's a **Tap sound** tile in the Display menu, next to Theme; on the Remote, a **Tap sound** switch in the Display section, next to Theme. Wherever the theme switch ends up, Tap sound goes with it. The rest of this entry is still Proposed.

- **The request (2026-10-05):** the user wrote: "This doesn't have haptics to confirm a tap went through. Can we use a 'click' sound?" The screen can't buzz under the finger, so a tap the bar took feels the same as one it missed. Today the only confirmation is the screen changing, which a finger or a side-on view can hide. A hold gives no sign that it has reached 550 ms.
- **Proposed (2026-10-05): the coordinator's plan, refined by the product manager.** The acceptance criteria for the mock-up and the firmware go with this round's brief.
  1. **What clicks:** a tap, swipe or hold that the bar acts on, one click per touch at most. A tap or swipe clicks when the finger lifts. A hold clicks at 550 ms, when its menu opens, while the finger is still down. This covers menu tiles, a tap off the tiles that closes a menu, the Wi-Fi setup screens and the touch that cancels a pairing code.
  2. **What never clicks:**
     - BOOT and PWR, which are physical buttons that click on their own.
     - A flip.
     - Touches the bar ignores: a swipe on an open menu, or a touch that began before a pairing code appeared.
     - The finger going down.
     - Anything not done by touching the bar: the Remote, the API, the Mac app, the calendar, the timer.
  3. **When it's silent:**
     - While a call is on or a calendar meeting is in progress, even one you set aside, because your mic may be live. This is the same rule as the chime and ticking.
     - On a dark screen. The touch that wakes it doesn't click, since the screen lighting up is the confirmation.
     - While starting up, powering off and powered off.
     - A touch made while the bar was silent never clicks later.
  4. **The sound:** short and dry, for the person at the bar rather than the room. It's no louder than Soft ticking and clearly different from the focus tick, so a tap made during ticking still stands out. The UX designer specifies it, and it's tuned on the real bar.
  5. **Latency:** under about 30 ms from the finger lifting (or from the hold reaching 550 ms) to the sound.
  6. **A setting, Tap sound,** on or off, on the bar and on the Remote, next to the theme switch (decided). It's kept like every other setting. In the API it's `sound.tap_sound` (`docs/api.md` 10.4, Proposed).
- **Proposed defaults for the open choices (product manager, 2026-10-05):**
  - **On by default.** The user asked for it, it only sounds when someone touches the bar, and it's silent during calls and meetings. The coordinator's "quiet by default" is read as a quiet level with the click on. *The other choice:* off by default, like ticking.
  - **On the bar, a Tap sound tile in the Display menu, next to Theme** (decided 2026-10-05: with the theme switch; the menu would read Light, Theme, Tap sound, Back). Like Theme, it's reached from every screen except the Pomodoro screen, where holding opens the timer menu. If the theme switch moves, Tap sound moves with it.
  - **On the Remote, a Tap sound switch in the Display section, next to Theme** (decided 2026-10-05).
  - **On or off only,** at one level tuned on the bar. If a volume choice is ever wanted, it would be a separate field, as `pomodoro.tick_volume` is.
  - **The hold gets its own short sound** from the UX designer, so you can hear the difference between a hold that has opened the menu and a tap. *The other choice:* the same click for both.
  - **A touch that answers a ringing alarm clicks** like that touch would at any other time. During a call or meeting it's silent anyway. *The other choice:* no click, since the alarm stopping is the confirmation.
  - **The tap that turns Tap sound on clicks**, as a sample of the sound. The tap that turns it off doesn't. The toasts are "Tap sound on" and "Tap sound off". During a call or meeting the toast says the click is silent for now. A change made on the Remote makes no sound.
  - **Whether a touch clicks** depends on the state when the gesture is recognized, before its action runs. So the Restart and Power off tiles click, and the waking touch doesn't.
- **Conflict with a proposal, flagged (2026-10-05): the controls tour "makes no sound"** (see Help, Proposed). **Proposed:** the tour adds no sounds of its own, but the click still confirms touches during it, because the tour teaches the controls as they really behave. Otherwise the tour would be the one place where a tap doesn't click. Not a conflict with any decision.
- **Known limit:** the bar only knows about calls that the Mac app reports, after its 3-second start delay, and about meetings in the calendar. A phone call, a call on another computer, or the first seconds of a call can still pick up a click. For those, turn Tap sound off.
- **Firmware risk (for the lead developer):** with the amplifier gate on, the amplifier switches off after 1.5 s of silence and needs 40 ms to come back on (`BRD_AMP_LEAD_MS`). That delay alone is over the 30 ms target, and the touch read period and the audio task's 10 ms chunks add to it. Measure it on the bar.
- **Follow-ups (open):**
  - **UX designer:** the tap and hold sounds and their levels, plus the copy for the tile, the Remote switch and the toasts.
  - **Mock-up:** the click through Web Audio, the tile, the Remote switch, and updates to the notes, the controls table, the menus diagram, the hint line and the API section. Then republish the Artifact.
  - **Firmware (after the mock-up):** a click effect from core, the sound in `board_audio`, the setting in the store and the API, the tile, and the Remote page. Then measure latency and loudness on the user's bar.
  - **The controls guide and the help page:** mention the click.
  - **`docs/api.md`:** 10.4 was written by the product manager; the lead developer reviews it.
- *(2026-10-05, mock-up round 2, lead developer: built in `docs/mockup.html` with the defaults above, still Proposed.)* The Tap sound tile is in the Display menu after Theme (Light, Theme, Tap sound, Back), the Remote's switch is in its Display section, and the API section lists `sound.tap_sound`. Reviewed api.md 10.4: its "On the bar" row said "A Click tile" and now names the Tap sound tile. The UX designer's spec (`team/click/ux-spec.md`) put the setting in a new Settings tile replacing Power in the quick menu; the decided placement above wins. The sounds follow that spec: the tap is a 2.4 kHz sine of 15 ms (1 ms rise), the hold a 1.6 to 1.0 kHz sweep of 30 ms, synthesized, with no sound files. In the mock-up the tap peaks at about -20 dBFS and the hold at -21 dBFS, which A-weighted come to about 1 dB under Soft ticking for the tap and level with it for the hold, so neither is louder than Soft and the hold is about 1 dB louder than the tap. The Simulate panel's Sounds box plays each sound once, side by side. Tune on the bar.
  - **Firmware notes:** core decides each click as an effect, as it does for the chime, and `board_audio` mixes it in from a buffer made at start-up (360 samples for the tap and 720 for the hold at the bar's 24 kHz, about 2 KB together). Within the 30 ms latency target the amplifier gate is the risk: either keep the amplifier on while the screen is lit, or measure the gate's 40 ms and accept it.

## Wi-Fi

- Setup by QR code: the bar shows a QR code that joins the phone to its own `MiniBar-Setup` network, where a page lets the user choose the office Wi-Fi.
- Supports password and work-login (username plus password) networks. Guest networks with a sign-in web page are not supported; the setup page says so.
- **Decided (2026-10-05): the bar remembers up to 5 Wi-Fi networks.** The user uses one bar at home and at work, and the bar kept only one network: setting up at work replaced home, and back home it retried the work network forever without offering setup. Now Set up **adds** a network (the same network name updates its password), the bar keeps up to 5, and when a sixth is added the one used longest ago goes. At power-on and whenever the link drops it scans and joins the strongest saved network in range. The Remote lists the saved networks with a Remove button on each. When none is in range the bar says so and points to hold, Wi-Fi, Set up to add this place. Mock-up first, then firmware (the design is in progress; the open questions are below). The single saved network of today migrates as the first entry, so nobody sets up again.
  - **Decided (2026-10-06, the user took the defaults in the UX spec, `scratchpad/team/wifi5/ux-spec.md`):** removing the network the bar is connected to disconnects it at once; with networks saved, Skip on the setup menu becomes Cancel (it rejoins a saved network) and setup closes by itself after 10 minutes if nobody finishes it; with five saved, the setup page names the network that will be replaced; no "forget networks" tile on the bar for now; strongest network wins only when the bar joins (it never hops while connected, and there's no manual choice in version 1); the Remote can't add a network (only someone at the bar can open the setup network), so its Add a network panel shows the bar's own steps; nothing is replaced unless the new network joins; "None in range" is a 6-second toast, never a screen.
  - **Shipped first, cut down (2026-10-06, firmware 1.0.5):** the bar keeps up to 5 networks and tries them in order of last use (not the strongest), so the home-and-work problem is solved without any screen change. A join that works adds the network (the same name updates its password; a failed join changes nothing; a sixth replaces the one used longest ago), the old single network moves in as the first entry when the app is updated alone, and a network that gives no address in about 15 s hands over to the next, wrapping around. The setup page says "This adds a network. MiniBar keeps up to 5." **Still to build:** the scan-and-choose join, the Remote's list with Remove, the API, the Cancel and 10-minute close, and the screens (designs in `scratchpad/team/wifi5/`). The one visible cost: joins are slower at the "other" place, because the bar works through the list.
- Skip uses the bar offline: statuses and the Pomodoro still work; the calendar and Remote don't.
  - *(2026-10-04, firmware, lead developer):* the skip is **remembered**: after Restart or power-on an offline bar
    starts offline, with its radio off and no open MiniBar-Setup network, as the mock-up's `powerOn()` keeps offline
    mode. Before this fix it came back on the QR code with the open setup network up, where anyone nearby could set its
    Wi-Fi and calendar address. Set up (hold, Wi-Fi, Set up) and a join that works end it. If a network was saved and
    you chose Set up again and then Skip, the bar stays offline too, rather than quietly rejoining the old network.
- **Proposed (2026-10-05, firmware 1.0.1, lead developer): the setup network's address is 4.3.2.1, not 192.168.4.1.**
  On the first test on the real bar, an Android phone joined TinyBar-Setup but said "Connected, no internet" and no
  "Sign in to network" sheet appeared. **The cause isn't confirmed yet.** The most likely one: Android's captive-portal
  check (NetworkMonitor in AOSP) has a rule, "a private IP DNS response means no internet". It's off in stock Android,
  but Google can turn it on with its server-side flags and phone makers can force it on. With it on, when the check's
  host (`connectivitycheck.gstatic.com`) resolves to a private address (10/8, 172.16/12, 192.168/16 or 169.254/16),
  the phone sends no HTTP check at all and reports no internet, so the bar never gets the chance to send it to the
  setup page. The bar answers every name with its own address, so that address must not be private. Against it:
  ESP-IDF's own captive-portal example uses 192.168.4.1 and works on Android, so 1.0.1 also removes the other ways
  1.0.0 differed from that example (see "Same round" below). If 1.0.1 still fails, two tests settle the cause: the
  phone's own validation log (`adb shell dumpsys network_stack`) and ESP-IDF's example on the same bar and phone (the
  firmware's `components/net/README.md`, bring-up item 3). MiniBar-Setup has no way out to the internet, so 4.3.2.1
  only stands in for the hosts phones check while they're on it; many ESP32 captive portals use it for the same reason.
  What changes for people: if the sign-in sheet doesn't open by itself, tap "Sign in to Wi-Fi network" in the
  notifications; the address to type is `http://4.3.2.1` (with mobile data off). The setup page shows it, and so does
  the mock-up's phone. *(The published mock-up Artifact still shows 192.168.4.1 until it's republished.)*
  - **The catch, part of this decision (2026-10-05, from the firmware review): 4.3.2.1 is a real, routed internet
    address** (it belongs to Lumen). A request that leaves the phone over mobile data instead of the setup network
    goes there rather than nowhere: a typed `http://4.3.2.1` with mobile data on, or, in a browser (not the sign-in
    sheet, which stays on the setup network), the setup page's next request after mobile data was turned on mid-setup,
    which could carry the Wi-Fi password in plain text to whoever answers at that address. At 192.168.4.1 those
    requests simply failed. The docs say to type the address with mobile data off.
  - **Verified on the bar (2026-10-05):** with firmware 1.0.1 the user's Android phone opens the setup page and the
    bar joins Wi-Fi. No serial log was taken, so which of 1.0.1's changes fixed it is still not confirmed.
  - **The choices** (the address is still the user's call):
    - **4.3.2.1** (the lead's pick, now working on the user's phone): widely used by ESP32 captive portals, so it's
      known to work on phones.
    - **192.0.2.1:** a documentation address (RFC 5737) that's never routed, so a stray request reaches nobody. It
      also passes Android's rule, which only looks for 10/8, 172.16/12, 192.168/16, 169.254/16 and private IPv6
      addresses. Untested on phones. Switching is one line in the firmware (`NET_SETUP_IP`) plus the setup page's
      text and the docs, best tried on the same phone once 4.3.2.1 works.
    - **192.168.4.1:** back to the old address, accepting that some Android phones need the address typed by hand.
  - Same round: the setup network now opens after one scan (so a phone on it never loses packets to a scan; the bar
    scans again while a phone is on it only if it found nothing), answers the phones' check paths (`/generate_204`,
    `/hotspot-detect.html`, `/connecttest.txt` and the like) with the redirect whatever their host, and offers no DHCP
    captive-portal option (114), which RFC 8908 reserves for an HTTPS API address. During "Set up again" the bar
    leaves the office Wi-Fi when the setup network opens (it restarts the radio for it), which matches criterion 19.
- **Once set up, the bar stops broadcasting its setup network (2026-10-05, the user's request; firmware 1.0.4, lead
  developer).** The user wrote: "Once the bar is setup, I want to make it stop broadcasting its network." What the bar
  does:
  - Once a join works, the Connected screen shows for 3 s (or until a tap, swipe, hold or BOOT), and MiniBar-Setup
    closes 15 s after that, so the setup page can still read the result: about 18 s after the join in all. It never
    comes back on its own: not after Restart, power off or unplugging (the bar starts on the saved network, with no
    access point), and not when the office Wi-Fi drops or can't be rejoined (the bar keeps trying the saved network,
    at most every 30 s). Only a person opens it again, with hold, Wi-Fi, Set up, which shows the QR code. Every
    firmware so far (1.0.0 to 1.0.3) closes it this way, so the user's bar, set up on 1.0.1 and updated app-only to
    1.0.2, shouldn't have broadcast it since, unless Set up was chosen again. Not yet checked on the bar (no serial log;
    the net bring-up checklist, item 5, says what to look for).
  - **1.0.4 makes that hold when a step goes wrong,** with no change to any screen: a close that fails is tried again
    (after three tries the radio restarts in station mode, which drops the office Wi-Fi for about a second), a close
    the busy Wi-Fi worker dropped is queued again, net finishes setup itself if core's "setup is done" was lost, a
    setup network found up outside setup is closed within a second, and the saved network is written again if saving
    it failed (without it, the next start would open the setup network).
  - **Connected is the end of setup for the page too:** from the moment a join works, the setup page's Connect (and the
    same request over USB) gets "MiniBar isn't in Wi-Fi setup anymore.", so nobody else on the open network can point
    the bar elsewhere during Connected, or keep the network up by sending again (api.md 13.2). Before 1.0.4 a second
    send during the 3 s Connected screen was taken.
  - **What brings it back, by design:** flashing the merged image at 0x0, which wipes the saved Wi-Fi (the bar starts
    like a new one, on the QR code); flash only the app at 0x30000 to update (firmware README, Flash). Also an NVS
    partition the bar can't read at start-up (it's erased so settings can be kept again), or a save of the network
    that failed six times over about 43 minutes (logged).
  - **A phone can still list it for a while:** Android drops a network that stopped broadcasting from its Wi-Fi list
    within about 15 to 25 s, an iPhone in about 20 s, and Windows can take a minute or more. A phone that joined it
    keeps it under saved (Android) or known (iPhone) networks until it's forgotten; that's the phone's memory, not the
    bar broadcasting. The firmware README's Troubleshooting says how to tell the two apart.
  - **Open, for the user (Proposed by the lead developer): a setup started on purpose and then left.** Hold, Wi-Fi,
    Set up opens the setup network and leaves the office Wi-Fi until a join works, Skip or a restart. If nobody comes
    back, it stays up indefinitely, the Remote and calendar are cut off meanwhile, and anyone nearby can set the bar's
    Wi-Fi. One tap starts it, with no confirmation. The choices: (a) keep it as it is, since a person started it;
    (b) with a network saved, close the setup network after 10 minutes with no phone on it and rejoin the saved
    network, with a toast such as "Setup timed out · back on Office-WiFi"; (c) a "Keep current network" tile in the
    setup menu, next to Skip, that goes back to the saved network at once. **Proposed:** (b) and (c) together. Both
    change the screens, so they go into the mock-up first, each with its way out, toast, controls-table row and
    diagram.

## Remote

- A web page served by the bar at `minibar.local` on the office Wi-Fi.
- Setting the status from outside the office network (an online relay) is **deferred**.
- **Decided (2026-10-04):** the Remote works only on a phone that has been **paired** with a code shown on the bar. It also lists the paired devices, each with Remove. See Pairing.
- **Proposed (2026-10-04): characters a message can't show.** The Remote checks the custom message as you type. Before anything is sent, it names any characters the bar can't draw, right under the field: "MiniBar can't show X. Remove it to show this message.", where X is the character itself, for example an emoji. Show stays disabled until they're gone. The check uses exactly the bar's own character set (printable ASCII, Latin-1, the en and em dashes, and the ellipsis, after the curly-quote mapping in `docs/api.md` 2.3), which is the firmware's `tb_text_drawable()`. This is the Remote's side of the `unsupported_chars` error in api.md 8.2.
  - *(2026-10-04, pairing fix round, lead developer, Proposed: characters nobody can see no longer block a message. QA found that a pasted "3:00 PM" with the narrow no-break space Apple puts before PM, a zero-width space, a byte-order mark or a thin space was refused with an invisible X ("MiniBar can't show  ."), and a decomposed "é" was refused although the bar has é. The mapping in api.md 2.3 now also turns odd-width spaces into a space, drops zero-width characters, direction marks and the variation selectors, and joins a letter and a combining accent into the Latin-1 letter. Anything invisible still left is named by where it is: "MiniBar can't show a hidden character after "Busy". Remove it to show this message." The firmware's mapping and `tb_text_drawable()` follow api.md 2.3.)*

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
- **Proposed: the secret address is write-only.** Once it's saved, the bar's screen, the Remote and the API never show it in full again, because anyone on the office Wi-Fi can open the Remote. The Remote shows a short masked form with Replace and Remove. **Proposed (2026-10-04, built by the team):** the masked form shows the host, the file name and the last four characters of the private token (for example "calendar.google.com/…/basic.ics · ending 3f2a"), so two addresses can be told apart. **Proposed (2026-10-04, security review, lead developer):** some feeds use the private token itself as the file name (".../8c1d5e2a…3f2a.ics"), so for those the file name shows as "….ics" ("cal.example.com/…/….ics · ending 3f2a"), and the four characters come from it. Google's "basic.ics" and other short word-like names still show. The paste fields on the Remote and the setup page are masked, with a Show button, and start masked every time they open. Any `https://` or `webcal://` address of an `.ics` feed is accepted, and the help text explains where to find it in Google Calendar. Pasting Google's *public* address gets a warning to use the secret one instead.
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
  - **Pairing:** an `Authorization: Bearer <token>` header is reserved for a later pairing step. It isn't checked yet. *(2026-10-04: the Mac app's spec proposes the pairing step; see Mac app. Later on 2026-10-04: `docs/api.md` section 4 proposes checking the token, with pairing codes shown on the bar. See Pairing.)*
  - *(2026-10-04: superseded by `docs/api.md`, which is the contract and wins on the wire format; see Mac app. Its section 14.1 lists the changes:*
    - *paths under `/api/v1/`*
    - *a token that's checked, with the `call` and `full` scopes (Proposed)*
    - *Calls from your Mac turned off answers `200` with `"sources": {"mac": false}` instead of `403 calls_off`*
    - *a dark screen is `"screen": "dark"`, not `showing: off`*
    - *USB lines carry the `@tb ` marker and an `id`*
    - *call messages carry `client`, `session` and `seq`*
    - *`meeting.next` is an object*
    - *errors carry `message` and `field`*

    *The mock-up's API section is brought in line in the pairing round, kept short, and points to `docs/api.md`.)*
- **Conflict to resolve:** the user's example for this round sends the app name (`"app": "Slack"`), but the decision above says only "on a call: yes or no" leaves the Mac. **Proposed:** `app` is optional. The bar shows it if it's sent, and the Mac app can leave it out. *(2026-10-04: the Mac app job's brief says "only on/off and the app name ever leave the Mac", which fits this proposal. The Mac app's spec sends a name only for listed call apps, and has a switch to send none; see Mac app.)*
- **Closed (2026-10-04): the user approved pairing as the answer (see Pairing).** Before: the Remote and the API have no PIN, so anyone on the office Wi-Fi can open `tinybar.local` and change the status. Worth deciding before the firmware. *(2026-10-04: `docs/api.md` section 4 proposes pairing as the answer, and the mock-up shows it. Approved by the user on 2026-10-04; see Pairing.)*

## Multiple calendars (2026-10-07)

**Decided by the user (2026-10-07):** up to **3 calendars**; events from all of them are **merged, soonest first**, so the bar has one Next up and one In a meeting, from whichever calendar has the next event; sources are **secret iCal addresses only** (no sign-in). The user then approved the mock-up and the design below (2026-10-07), and **firmware 1.0.8 builds it**. Everything below is **Accepted**; the notes dated 2026-10-07 say where the firmware differs from the mock-up. Today's single-calendar rules (which events count, titles off by default, sync every 10 minutes, the switch, the address being write-only) apply to every calendar.

- **Accepted: the Remote's Calendars list.** The Calendar section becomes **Calendars** (1 to 3 rows). Each row: a **name** (default "Calendar 1", "Calendar 2", the lowest number free), a **bar tag** (up to 4 letters, default C1, C2, C3; shown on the bar and the Remote, so never put anything private in it), "**Address saved**" (nothing else of the address, which replaces the earlier host and last-four masked form for these rows), a sync line ("Synced 2 min ago · 3 meetings left today"), **Edit** (name, tag, or a new address; an empty address keeps the saved one) and **Remove** (asks first, in place). **Add a calendar** opens the same form and is disabled at 3 with the note "You can add up to 3 calendars. Remove one to add another." The address field is password-style, starts masked each time, and has Show. **Sync now** reads every calendar. Names and tags must be different from each other. "Calendar meetings" and "Show meeting titles" stay single switches for all calendars.
- **Accepted: sync errors, per row.** A calendar that can't be read shows a "Can't sync" badge and "Couldn't reach this calendar. Last synced 12 min ago. Its meetings are left out of the bar until it works." The Remote's badge says "1 can't sync" (or "Can't sync" if none work). A failing calendar's meetings are **left out** rather than shown from its saved copy, so a stale meeting can't mislead the office; they come back on the next good sync.
- **Accepted: Next up on the bar** shows the merged soonest event. With **2 or more calendars saved**, a small **tag** sits right after the "Next up" label: 1 px outline in the label's own color, 3 px radius (square in Handjet), 0.9 em of the label, 4 px padding at the sides, up to 4 capitals. With one calendar there's no tag, so nothing changes for today's bar. Measured in the mock-up at 640 x 172: it fits beside "NEXT UP" in both Bold Signal and Handjet with room to spare. **Truncation:** the tag is never cut or shrunk; the title in the foot is what an ellipsis takes first (about 25 characters, as before), and the label line stays one line. The meeting screen still says CALENDAR (it isn't tagged), and the "Free until" side isn't tagged.
- **Accepted: the quick menu's Calendar tile** (still one tile). All saved calendars fine: "Sync" with "3 calendars" (one calendar keeps "synced 2m ago"). Some can't sync: the value is how many work ("2/3") and the foot names the first failing one by its tag ("C3 can't sync", two lines). None can: "Error" with "can't reach any" ("can't reach it" for one). It still runs Sync now. The clock's side shows "Calendar · Can't sync · see the Remote" when none can.
- **Accepted: edge cases** (all in the Simulate panel's "Several calendars" box).
  - **One fails, others work:** the bar keeps showing the working calendars; the tile and the Remote note the failure.
  - **Same event in two calendars:** shown **once** and counted once, matched by the event's UID (the firmware already keys meetings on `cal_instance_id(UID, start)`); the tag is the earlier calendar's in the list.
  - **Removing the calendar that supplies the current event:** its meeting ends on the bar at once with a "Calendar 2 removed" toast, and the next one in progress from another calendar (if any) takes over. A copy of the same event in another calendar carries on without a break.
  - **All calendars fail:** nothing from the calendars shows, the tile reads Error and the Remote says "Couldn't reach this calendar" on every row. *Note:* today's mock-up has no sync-failure copy for a single calendar (only the save-time "Google didn't recognize that address" and the Wi-Fi-drop "Offline" tile), so this is the first, and a single calendar uses the same words.
- **Accepted: privacy.** Each address is write-only, as now: kept in `nvs_sec`, never logged in full (a log line may name the calendar by its name or tag, never its address or token), never returned by the Remote or the API (`GET /api/v1/status` and `/calendar` report counts, names, tags and status only), never on the bar. The name and tag are not secret. **Migration:** a bar updated from today's firmware keeps its one address as "Calendar 1" with tag C1, and its saved copy of meetings; nothing needs re-entering. The setup page's address field adds a calendar (Calendar 1 on a new bar), and with 3 saved it leaves them alone and says so.
- **Accepted: costs and how the firmware meets them** (RAM and flash figures are by design, not yet measured on hardware):
  - **Sync time:** the 10-minute sync would run the calendars **one after another** in the same task (20 s per step today, so a worst case of about 3 times today's if all three time out), keeping the same ~28 KB of RAM. Sync now takes 3 fetches. Backoff for a failing calendar should be per calendar so one dead address doesn't slow the others.
  - **Storage:** `nvs` holds each calendar's packed copy (up to 4.7 KB each, usually a few hundred bytes) in a 24 KB partition shared with settings, Wi-Fi and own state, and NVS keeps the old copy while writing: three worst-case copies could take 14 KB, or about 28 KB while rewriting. Suggest a per-calendar cap of 16 meetings, or one merged saved copy (loses per-calendar tags and error state across a restart). Three addresses of up to a few hundred characters each go in `nvs_sec`.
  - **RAM:** a merged list of up to 3 x 32 meetings in RAM (a few KB), plus name and tag strings; no extra fetch buffers if the calendars are read in sequence.
  - **Decided by the user:** no stale copy: a failing calendar drops out at once (the 1-hour idea is not built). Still open: should a calendar's saved copy keep showing for a short while after a failure (say 1 hour) instead of dropping out at once? Should the meeting screen's CALENDAR chip carry the tag too? Is 3 enough for the Mac app's separate On a call source? (No change there.)
- **Firmware 1.0.8 notes (2026-10-07).**
  - **Sync:** the calendars sync one after another in the one `cal_sync` task, each with its own back-off (1, 2, 4, 8 minutes, then 10); Sync now queues every calendar and the last one reports ("Calendar synced", or "Couldn't sync the calendar" if any failed). A calendar that can't sync is flagged and left out of the merge at once; the next good sync brings it back. The flag isn't saved, so after a restart the saved copy shows until the first sync is tried.
  - **Storage:** each calendar's saved copy is capped at 16 meetings (`nvs` `list0` to `list2`, rewritten only when the meetings change, as before); names and tags are one small blob (`meta`), written only on an add, edit or remove. Addresses are in `nvs_sec` `calsec` `url0` to `url2`.
  - **Merge:** soonest first, ties to the earlier calendar; the same event is matched by UID plus start (the meeting id) and shown once with the earlier calendar's tag; the merged list is trimmed to 32 (over ones first, then the latest), so with three busy calendars the latest meetings of the day can fall off the bar's list.
  - **Removal:** the removed calendar's name, the list and the merged meetings reach the app task in one event, so a meeting only that calendar supplied ends at once ("Calendar 2 removed") and one another calendar also has carries on unbroken. Removing the last one behaves as removing the only calendar did.
  - **Migration:** the single address of 1.0.7 and earlier moves to the lowest free slot once, as "Calendar 1" with tag C1: written, read back and compared, then the old key is erased (the same order as the saved Wi-Fi networks); its saved copy moves with it. A cut leaves the old key and the next start finishes. An unusable old address is erased; if all three slots are taken by other addresses the old one is kept.
  - **Setup page:** its address adds a calendar; with three saved the bar says "3 calendars already · address not added".
  - **API:** `GET/POST /api/v1/calendars`, `PATCH/DELETE /api/v1/calendars/{id}` (api.md 11.5). `GET /api/v1/calendar` stays (all calendars as one) but its `address` is always null now: not even the masked form is returned. `PUT` and `DELETE /api/v1/calendar` still work with one calendar saved and answer `409 several_calendars` with more.
  - **Not decided here:** the meeting screen's CALENDAR chip is not tagged; the Mac app's On a call source is unchanged.

## Mac app

The menu-bar app that sets **On a call** (see Automatic status). The full spec, with acceptance criteria, is `docs/mac-app.md`. The wire format is `docs/api.md`, the contract between the app and the firmware; where they disagree about the wire format, `docs/api.md` wins. *(Note 2026-10-04: `docs/api.md` hasn't been written yet, so the spec follows the mock-up's API section.)*

### Proposed (2026-10-04, product manager, waiting for the user's OK)

- **Proposed: platform.** macOS 14 Sonoma or later, on Apple silicon and Intel. Menu bar only, with no Dock icon. Written in Swift and not sandboxed. Built from source, **ad-hoc signed** by default, or signed with the user's Developer ID and notarized if they have one. Not in the App Store in v1.
- **Proposed: how it detects a call.**
  - **Mic:** which apps are using the mic, read per app from CoreAudio's process list (new in macOS 14, which is why 14 is the minimum).
  - **Camera:** whether any camera is in use, from CoreMediaIO. macOS has no public way to tell which app is using it, so camera use has no app name.
  - The app only reads this state. It never opens the mic or camera, and never records.
- **Proposed: timing.** A call starts after **3 s** of continuous use (adjustable from 0 to 30 s) and ends after **10 s** without (3 to 60 s). The start delay filters blips. Dictation and Siri often last longer than 3 s, so they're kept out by the ignore list, not by the delay. Changes you make (ignoring the app, pausing) take effect at once.
- **Proposed: which apps count.**
  - **By default, any app except ignored ones,** so call apps nobody listed still count. There's also a mode, "Only the call apps on my list".
  - **Call apps** (each sends a short name): Slack, Zoom, Teams, FaceTime, Webex, Discord, WhatsApp, Signal, and the browsers Chrome, Safari, Firefox, Edge, Arc and Brave.
  - **Ignored by default:** Siri (including "Hey Siri" listening), Dictation, Voice Control and **Voice Memos**. A memo isn't a call, and the start delay alone can't filter one longer than 3 s.
  - The menu has a one-click "Don't count <app>".
- **Proposed: the app name.**
  - **Only listed call apps send a name.** Calls from other apps, or from the camera alone, send none, so the name of an arbitrary app never appears on the bar or in the bar's API.
  - **"Send the app's name to MiniBar"** is a setting, on by default.
  - **Browsers send the browser's name** ("Chrome"), never the website: the app doesn't read tabs. So "Google Meet" never comes from the Mac.
- **Proposed: the camera counts by default,** with no name. It can be turned off; Photo Booth or a webcam utility would otherwise count.
- **Proposed: Pause** for 1 hour, for the rest of today, or until resumed. There's still **no manual On a call** from the Mac, in line with "On a call is automatic only".
- **Proposed: connection.**
  - **USB first,** probing only Espressif USB serial devices (vendor ID 0x303A) with the `hello` handshake. Opening the port must not restart the bar; the firmware team confirms the safe DTR and RTS handling.
  - **Wi-Fi as the fallback,** switching within 5 s when the cable is unplugged.
  - The 30-second heartbeat is sent even when not on a call, so the bar can show its Mac icon.
  - `active: false` is sent before sleep and on quit.
  - **No alerts or notifications** about the connection; the menu says what's wrong.
- **Decided (2026-10-04): pairing.**
  - **Automatic the first time the bar answers over USB** (plugging it in is the consent), or with a **6-digit code** from a Pair a Mac button on the Remote's Connect your Mac card.
  - The bar issues a token, kept in the Mac's Keychain. No computer name, user name or serial number is sent.
  - Until the bar supports pairing, the app uses `tinybar.local` (or a typed address) with no token.
  - **Needs the API and firmware to add:** a bar ID in `hello` and `/api/status`, `{"cmd": "pair"}` over USB, `POST /api/pair` with the code, a Bonjour service (`_tinybar._tcp`) carrying the ID, and a `401` once tokens are checked.
  - *(2026-10-04: changed by `docs/api.md` 14.5. The Wi-Fi code is shown on the **bar**, not on the Remote, lasts 2 minutes rather than 5, and is asked for with `pair/start`, then sent with `pair`. The Remote's planned Pair a Mac button becomes instructions instead. USB pairing without a code stays. See Pairing.)*
- **Proposed: launch at login,** on by default, offered as a checked box in the first-run window (`SMAppService`).
- **Proposed: privacy.**
  - Only `active`, the optional short `app` name, and the protocol (the commands, the pairing code, the bar's token) leave the Mac, and only to the bar.
  - No internet connections, analytics or crash reports. No history on disk of which apps used the mic.
  - The spec says plainly that the bar shows the name to the office, that its API returns it to anyone on the Wi-Fi, and that Wi-Fi messages are plain HTTP.
- **Checked: no microphone permission needed.**
  - **Why:** the mic prompt comes when an app starts audio input (Apple, developer forums thread 743077). The app never does, and has no microphone usage text or entitlement.
  - **Shipping apps that do the same:** OverSight and Mute read the same "in use" flag without microphone permission.
  - **Not yet verified on a real Mac:**
    - The per-app list that gives names and makes the ignore list work.
    - The camera. Mute asks for camera access, but it's sandboxed; OverSight isn't and doesn't ask.
  - **Proposed fallbacks** if a prompt appears:
    - From the per-app list: device-level detection with no names or ignore list, which goes back to the user as a decision.
    - From the camera: "Count the camera" off by default.
- **Noted: the permission it does need.** Local Network, on macOS 15 and later, for Wi-Fi only. MDM can't grant it ahead of time. Apple recommends an Apple-issued signature for it to track the app reliably, so ad-hoc builds may be asked again after a rebuild.
- **Proposed: managed work Macs.** The README gets a "For IT" note: what it reads, what it never asks for, that it only talks to the bar, and how it's signed. It also covers the blockers:
  - Gatekeeper's Open Anyway for a downloaded ad-hoc app; building it yourself avoids the download quarantine.
  - App allow-listing tools, which can only allow an ad-hoc app by a hash that changes every build. A Developer ID fixes that.
  - Login items managed by MDM.
  - VPNs or client isolation that block the local network. USB still works.
- **Proposed: not in v1.**
  - Audio of any kind, reading tabs or window titles, and controlling your status from the Mac.
  - Two Macs feeding one bar.
  - iPhone calls that don't go through the Mac.
  - Notifications, auto-update, analytics and the App Store.

### Decided (2026-10-05): Download for Mac

- **The bar hands out the Mac app.** The Remote gets a **Download for Mac** button that serves the app's zip from the bar's own flash, from a small read-only partition in the free space after the two app slots (`firmware/partitions.csv`: about 2.7 MB free after `ota_1`). The SD card is not used.
- **What the bar can't do:** detecting a call still needs the app running on the Mac. The button only makes installing it easy.
- **Accepted by the user:**
  - The app is built on a Mac first (`mac/scripts/build-app.sh`); the firmware image then carries the zip. Nothing in this session can build macOS binaries.
  - macOS refuses an unsigned, un-notarized app until the person opens System Settings › Privacy & Security and chooses **Open Anyway**. The Remote's instructions say so. Notarizing would need an Apple Developer account, which isn't assumed.
  - Updating the shipped copy means flashing the bar again until the firmware has over-the-air updates. An app-only flash at 0x30000 leaves the partition alone.
- **Plan:** mock-up first (the button, its helper line with version and size, the first-run steps, the no-app state, the Simulate switches), then the firmware (the partition and its header, the download address, `mac_app` in `GET /api/v1/info`, the build tool and the flash files). The UX spec, acceptance criteria (M1 to M13 for the mock-up, F1 to F12 for the firmware) and the firmware note are in `scratchpad/team/macdl/` (2026-10-05). The free space after the app slots is `0x3D0000`, 3.8 MiB by the offsets, not the 2.7 MB the csv comment says; the comment gets corrected.

### Proposed for Download for Mac (2026-10-05, product manager, waiting for the user's OK)

The defaults the mock-up is built to. Change any of them and the team will follow.

- **Decided by the coordinator: the copy says MiniBar.** The rename runs before this mock-up round, so the button, the steps and the file name (`MiniBar-1.0.zip`, from `MiniBar.app`) use the new name from the start.
- **Proposed: the button is in two places:** in the Connect your Mac card when the Remote is paired, and as a smaller link on the pairing prompt when it isn't, with a line saying no pairing is needed. A Mac that opens `minibar.local` to download the app is almost never paired, and pairing its browser for a download would waste one of the 10 places. `GET /api/v1/info` needs no token, so the prompt can show the version and size.
- **Proposed: the button is a plain link that works everywhere,** phones included (a phone can download the zip and send it on). On anything that isn't a Mac, the line under it says this is a Mac app and that opening `minibar.local` on the Mac is the easy way. *The other choice:* instructions only on a phone.
- **Proposed: the helper line** shows the version and build as the Mac app reports them ("1.0 (12)"), the size as Finder shows it ("1.8 MB"), "macOS 14 or later", and "Apple silicon and Intel" or "Apple silicon" depending on how the app was built (`UNIVERSAL=1` is the documented way to build it, so both). All of it comes from the bar.
- **Proposed: the no-app state** (a blank or corrupt partition, or a firmware built without the zip) shows no button, and a line that says the bar isn't carrying the Mac app and where it comes from. The pairing instructions stay.
- **Proposed: the first-run steps** are an ordered list on the card, always reachable. They name the path System Settings › Privacy & Security › Open Anyway rather than quoting dialog buttons, which differ between macOS 14, 15 and 26, say to move the app to Applications (Start at login needs it there), and that the app's Connect window takes it from there.
- **Proposed: the Simulate panel** gets "App on the bar: Carried · None · Damaged copy" and "Remote opened on: Phone · Mac" in its Mac app box. A click in the mock-up downloads a harmless 22-byte stand-in zip with the real file's name, so the download can be tried and tested.
- **Proposed: the "Coming later" badge** on the Connect your Mac card goes now, and the API section's "the Mac app comes later" eyebrow with it.
- **Proposed: the API** (`docs/api.md`): `GET /mac/app.zip`, outside `/api/` because every `/api/` reply is JSON, with no product name in the path; `Content-Disposition` carries the readable name (`MiniBar-1.0.zip`). No token, like the Remote page; the host check applies. `info.mac_app` is `null` or an object with `version`, `build`, `bytes`, `archs`, `min_macos`, `file`, `path` and `sha256`.
- **Decided by the coordinator, technical:** the partition takes all of the free end (`0xC30000`, `0x3D0000`); it starts with a header the firmware checks once at start-up (magic, length, SHA-256), so a blank or damaged partition reports no app; the download is served in pieces from the memory-mapped partition by a task of its own, so the screen and the Remote's polling don't stall, one download at a time. **The usual merged image stays as it is and does not carry the partition**, so flashing at `0x0` takes the same three minutes as today: the Mac app ships as its own file, `dist/minibar-macapp-<version>.bin`, flashed once at `0xC30000` (the web flasher takes a second file and address), and `idf.py flash` carries it when the zip is present. A firmware built without the zip leaves that region of the flash alone.
- **Follow-ups (open):**
  - **Mock-up (next round, after the rename):** the card, the prompt, the Simulate switches, the notes, API section and Simulate note. Then republish the Artifact.
  - **Firmware (after the user has seen the mock-up):** `partitions.csv`, the header and its check, the build tool and CMake step, `/mac/app.zip`, `mac_app` in `info` and `hello`, `web/remote.html`, the partition file in `dist/`, and the docs (README Flash, api.md, partitions.csv, `mac/README.md`).
  - **On the user's Mac and bar:** build the app with `UNIVERSAL=1`, flash the partition file, check the screen and the phone's Remote during a download, go through Open Anyway.

### Open (2026-10-04)

- **More than one MiniBar on a network:** they can't all be `minibar.local`, which affects the Remote's address as well as the Mac app. **Proposed:** a bar keeps `minibar.local` when it's free, advertises its ID over Bonjour and shows its real address on the Wi-Fi screen. The Mac app finds its bar by ID. *(2026-10-04, pairing round: the mock-up now shows the bar's **name and real address**, for example "MiniBar 2A1C" and `minibar.local` (or `minibar-2.local` after a name clash). They appear on the Connected screen of Wi-Fi setup, the Wi-Fi menu's Network tile, and the Remote's pairing prompt. The pairing screen's foot shows the name too. The default name, "MiniBar" plus the last four characters of the bar's ID, is **Proposed** in `docs/api.md` section 3.)*
- **Two Macs, one bar:** with "latest message wins", an idle second Mac's `active: false` heartbeat would end the first Mac's call. **Proposed for later:** the bar keeps each paired Mac's state by its token. v1 is one Mac per bar.
- **Muted calls:** some apps may let go of the mic when you mute, so with the camera off, On a call would end. This is to be measured per app on a real Mac (the spec's criterion 34) before deciding whether v1 needs more.

### Follow-ups (open)

- **API contract:** fold the pairing additions above into `docs/api.md`, or record that v1 ships without pairing. *(Done 2026-10-04: `docs/api.md` section 4, Proposed. If you'd rather not have pairing, the bar ships with `"auth": "none"` and nothing else changes; see api.md 4.1.)*
- **Mock-up:**
  - Add Pair a Mac (the code) and a paired-Macs list with Forget to the Remote's Connect your Mac card. *(Changed 2026-10-04 by `docs/api.md` 14.3 and 14.5. The code shows on the bar, so the card gets instructions instead of a button. A Paired devices list with Remove covers every device, not only Macs. Both are built in the pairing round; see Pairing.)*
  - Change the Simulate controls' "Meet" app button to "Chrome".
  - Drop the card's "Coming later" badge when the app ships.
  - Then republish the Artifact.
- **Firmware:** confirm which serial line handling is safe so opening the port never resets the bar, and whether the boot log shares the USB serial port (the app ignores non-JSON lines either way).
- **UX:** draw the four menu-bar icon states, and design the menu, the Settings window and the first-run window from the spec's content.
- **Real-Mac checks** (the spec's "Unverified" list):
  - No prompts.
  - The processes that hold the mic for each call app and for Siri and Dictation.
  - AirPods.
  - Muted calls.
  - Local Network permission and login items with ad-hoc signing.

## Pairing (decided 2026-10-04)

- **Decided (2026-10-04): the user approved pairing as proposed.** That covers everything in this section, `docs/api.md` section 4 (pairing over USB without a code, the Mac app's `call` scope, `pair/cancel`, a code holding a place), and every item below that the pairing round and its fix round marked Proposed. The bar ships with pairing on; `"auth": "none"` stays a build option (api.md 4.1).

- **What it is:** `docs/api.md` section 4 proposes it, as the answer to "the Remote and the API have no PIN" (see Automatic status). Before a phone, a Mac or a script can control the bar over Wi-Fi, it pairs once with a **6-digit code shown only on the bar's screen**, so only someone who can see the bar can pair.
  - The code lasts **2 minutes**, allows **3 tries**, and only one code is shown at a time.
  - After two failed pairings in a row, the bar refuses new codes for 30 seconds. The wait doubles with each further failure, up to 1 hour.
  - **USB needs no pairing:** the cable is the proof, and the Mac app pairs over it without a code.
  - The Mac app's token can only report calls, while the Remote and scripts get full control.
  - Tokens don't expire. A bar keeps at most 10.
    - *(2026-10-04, pairing fix round, lead developer, Proposed:)* a code on the screen for a device that isn't paired yet **holds one of the 10 places** until it ends, so the code it shows can always work. QA found that pairing the Mac over USB while a phone's code was up could take the last place, and the phone then made 11. Now a USB pairing like that is refused with `token_limit` instead (the cable keeps working; only the Wi-Fi token waits), and `pair` refuses an eleventh as a safeguard. See api.md 4.3. If that safeguard ever answers, the code ends ("Pairing canceled", not counted as a failed pairing) and the Remote's prompt says "MiniBar 2A1C already has 10 paired devices. Remove one on a paired phone, or forget them all on MiniBar: hold its screen, then Wi-Fi, then Devices.", with Show a new code and Cancel.
  - The Remote removes one device at a time. Forgetting them all is done on the bar, from the Wi-Fi menu's Devices tile.
- **The mock-up shows the flow (pairing round, 2026-10-04)**, approved with the rest:
  - the bar's pairing screen
  - the Devices tile
  - the Remote's pairing prompt and Paired devices list
  - the Connect your Mac instructions
  - the Simulate panel's Pairing box

  Without pairing (a build with `"auth": "none"`), the pairing screen, the Devices tile and the Remote's prompt go away (api.md 4.1).

### Decided in the pairing round (proposed by the product manager, approved 2026-10-04)

These fill in what `docs/api.md` 4.8 and 14.3 left open. They're built into the mock-up and approved with pairing. Change any of them and the team will follow.

- **Decided: flip during pairing.** A flip cancels the pairing ("Pairing canceled"), like a tap, swipe, hold or BOOT. It then does what a flip always does: turns the layout, silences any alarm, and starts whatever the Pomodoro is waiting for. The toast adds that part, for example "Pairing canceled · Focus started". A flip is never swallowed anywhere else (on a menu, on a dark screen, during an alarm), and the decided rule says it always does those three things.
- **Decided: the pairing screen's details.**
  - The surfaces are the Clock screen's.
  - The info column's label is **"CODE EXPIRES IN"**, above an m:ss countdown, with the bar's name ("MiniBar 2A1C") as the foot. api.md says "Code expires" and the UX design says "Expires in"; "Code expires 1:52" could read as a time of day.
  - The sub line names the device: "Type it on your Mac · tap to cancel", "Type it on your phone · tap to cancel", and otherwise "Type this code on that device · tap to cancel". All of them fit.
  - The 2 minutes count from when the code appears on the screen.
  - Once pairing ends, the screen stays on, even if the code woke it.
- **Decided: what pairing does to everything else.**
  - **An alarm:**
    - A code that arrives while the alarm rings stops the chime and flash. The Pomodoro keeps waiting.
    - A phase that ends while a code is showing waits too.
    - When pairing ends, the waiting screen shows with one chime and flash and no repeats, as after a call (flash only during a call or calendar meeting).
    - A flip answers the alarm. A PWR press drops the chime, and the waiting screen shows silently when the screen is woken.
  - **A call, a calendar meeting, or a change made on the Remote** while a code is showing is handled the way an open menu handles it. It happens underneath (the Pomodoro pauses for a call or meeting, with no sound, as decided), and it shows once pairing ends, after the pairing toast.
  - A running Pomodoro keeps running, and ticking carries on.
  - **Power:**
    - PWR held all the way, Power off, or a restart ends the pairing. Paired devices stay paired, and the back-off is cleared, as api.md says.
    - Letting PWR go early returns to the code.
  - **Starting Wi-Fi setup** ends a pairing, which counts as canceled. A code is never shown during setup, including on the Connected screen.
- **Decided: the Devices tile.**
  - The Wi-Fi menu's tiles are Network, Devices, Set up again and Back.
  - The confirmation ("Forget all · 3 devices") has its own tile to go back without forgetting. The 8-second close also forgets nothing.
  - After Forget all, the toast says "Forgot 3 devices". The Remote returns to its pairing prompt ("MiniBar forgot this phone. Pair it again to use the Remote."), a call the Mac reported over Wi-Fi ends, and USB keeps working.
  - **With nothing paired, the tile stays** and reads "None", with how to pair. It's read-only like Network, so a tap on it closes the menu, as a tap on Network does.
    - *(2026-10-04, pairing fix round, lead developer, approved with pairing, from the UX review:)* the foot says **where** to pair: "pair at" over the bar's real address, each line within the tile's 86.5 px in Barlow 500 14 px. "minibar.local" is 79 px (measured again for the new name on 2026-10-05; as "tinybar.local" it was 75); a renamed host after a name clash ("minibar-2.local", 94 px) doesn't fit, so the foot then gives the IP address ("10.0.4.42"), and if that's too long too (14 characters or more, such as "192.168.100.200"), "pair at its" over "IP address", which the Network tile beside it shows. Offline it still says "set up Wi-Fi" over "to pair". It used to say "pair a phone or a Mac", which doesn't say where.
- **Decided: the Remote's pairing prompt.**
  - Until the phone is paired, the prompt replaces the Remote's controls (the bar answers nothing else without a token). It names the bar.
  - The prompt says **"Type the code shown on your MiniBar"**. "Type the code on your MiniBar" could read as typing on the bar.
  - The code field brings up a number pad, ignores spaces and dashes, and pairs as soon as the sixth digit is in, as the Mac app does.
  - Cancel goes back to the start. Pressing Pair this phone again while its own code is still on the bar returns to the field, rather than showing a "busy" message for your own code.
    - *(2026-10-04, pairing fix round, lead developer, approved with pairing, from the UX review:)* **Cancel also takes the code off the bar** with a new call, `POST /api/v1/pair/cancel` (api.md 4.7). The bar shows "Pairing canceled", as after a tap. Before, the code stayed up for up to 2 minutes, blocked every other device ("Another device is pairing"), and its time-out then counted against the person who canceled. Canceling counts as a failed pairing, like a tap on the bar, so it can't be used to get more guesses. The Mac app's Back or Cancel on its Wi-Fi page should call it too (see Follow-ups).
    - *(Same round, from QA:)* every message on the prompt follows the bar. A refusal goes as soon as its cause is over (the busy or back-off wait, setup finished, a place free, the bar back on Wi-Fi), and once the code is gone from the bar, "That code has expired or was canceled on MiniBar." replaces a wrong-code or "didn't answer" message within about 2 seconds. While the bar is busy or waiting, Pair this phone is dimmed but stays focusable, and pressing it repeats the wait.
  - The prompt notices within about 2 seconds when the code runs out or is canceled on the bar.
  - The error messages follow the Mac app's (`docs/mac-app-ux.md` 5.5), except busy: "Another device is pairing with this MiniBar. Try again in 74 seconds." "Someone else" is wrong when the other device is your own Mac.
- **Decided: the Remote's name.** The Remote sends a name for the kind of phone when the browser says what it is ("iPhone", "iPad", "Android phone"). Otherwise it sends none, and the bar says "Phone". The mock-up's phone pairs as "iPhone" ("PAIRING · IPHONE", "Paired · iPhone").
- **Decided: the Paired devices list.**
  - Each device shows its name, kind (Mac app, Remote, Automation), scope (Calls only, Full control), when it was paired (and "over USB"), when it was last used, and a Remove button.
  - Remove asks first, in place, as the calendar's Remove does. Removing This phone signs the Remote out, back to the pairing prompt.
  - *(2026-10-04, pairing fix round, lead developer, approved with pairing, from the UX review:)* the Connect your Mac card's status line agrees with the list: with a paired Mac that isn't connected now, it says "Not connected right now · paired over USB" (or "over Wi-Fi"). "Not connected yet" is only for a bar no Mac has ever paired with or connected to.
  - Removing a Mac ends a call it reported over Wi-Fi. Over USB, the call carries on.
- **Decided: Connect your Mac.** The instructions use the Mac app's real menu names from `docs/mac-app-ux.md` 5.4: "To pair your Mac, plug this MiniBar into it once. Or, in the MiniBar menu on your Mac, choose Connect…, then Pair Over Wi-Fi. This MiniBar shows the code." api.md 14.3 suggested "choose Pair with a code", but the app has no item by that name.
- **Decided: the mock-up starts paired.** This phone is already paired, along with two sample devices (a Mac paired over USB, and a script), so the Devices tile reads "3 paired" and everything that worked before works without pairing first. Removing This phone, or Forget all on the bar, shows the pairing prompt. An unpaired simulated Mac over Wi-Fi is refused, while over USB it always works.

### Follow-ups for pairing (open)

- **`docs/api.md` (lead developer):** bring 4.8 and 14.3 in line with the Proposed items above:
  - the label "Code expires in"
  - the kind-specific sub line
  - the flip
  - the Connect your Mac copy
  - "Phone" as the label for the `remote` kind when no name is sent

  *(2026-10-04: 4.8 done, with the alarm, the 2 minutes from when the code shows, USB pairing over a code, and the
  Devices tile. 14.3's Connect your Mac copy is still to do.)*
- **Firmware:** follow the mock-up once these are confirmed. A read-only check on 2026-10-04 found two differences in core:
  - a flip leaves the pairing screen up
  - the Devices tile is hidden when nothing is paired

  It also has no rule yet for an alarm or a phase ending during pairing. *(Done 2026-10-04 in the firmware's review
  round, still Proposed: the firmware now follows every item above as the mock-up draws it: the flip, the alarm and a
  phase ending under a code, the 2 minutes from when the code appears, a touch that began before the code is ignored,
  Wi-Fi setup ending a pairing, the pairing screen's label, foot, sub line and progress bar, the Connected screen's
  name, the Wi-Fi menu's tiles and five-column layout, and Forget all's three tiles with its 600 ms guard. The
  Remote checks a message's characters as you type with the copy above. Pairing the Mac over USB while another
  device's code is on the bar leaves that code up and valid; "Paired · Mac · over USB" shows once that pairing ends.)*
- **Mac app spec:** `docs/mac-app.md` still describes a Pair a Mac button on the Remote and a 5-minute code. `docs/api.md` 14.5 and `docs/mac-app-ux.md` already supersede both.
- **From the pairing fix round (2026-10-04, lead developer), approved with pairing:**
  - **Firmware:** add `POST /api/v1/pair/cancel` (api.md 4.7), the held place for a code on the screen (4.3, refusing USB `pair` with `token_limit` and `pair` as a safeguard), the extra text mapping (2.3: odd spaces, zero-width characters, Latin-1 composition) in the bar's mapping and in `web/remote.html`'s check, the Devices tile's "pair at" foot, and the Remote's hidden-character wording and Connect your Mac status line.
  - **Mac app:** the Wi-Fi page's Back or Cancel calls `pair/cancel` for the code it asked for (`docs/mac-app-ux.md` 5.4).
  - **Criterion 19 (product manager):** on the QR code, Connecting and Couldn't connect screens the bar isn't on the office Wi-Fi, so a device there gets no answer ("MiniBar 2A1C didn't answer. Make sure it's on, then try again."), not "MiniBar is setting up Wi-Fi…". That message comes over USB (`409 in_setup`, api.md 4.6 and 13) and on the Connected screen, where the bar is on the office Wi-Fi again. The mock-up does this; the criterion should say so.
- **Done (2026-10-04):** the user approved pairing as a whole, and the items above with it.

## Look

- **Alternate theme: Low Glare Pixel, set in Handjet (decided 2026-10-05).** The user asked "Is it possible to do the low glare pixel layout using Handjet?", then "I'd like to use it as an alternate theme. Not a replacement."
  - **Bold Signal stays the default look**, unchanged: the simulator's default, what a new bar shows, and what the firmware runs today.
  - **A second theme, Low Glare Pixel, that you choose** on the bar and on the Remote. It's the Low Glare layout (a warm near-black screen, a 6 px edge bar in the status color, a colored sentence-case headline, a faint side tint, warm off-white and muted text, a status-color frame for the alarm instead of the white flash, and a paused timer held still in the muted color) set **entirely in Handjet**: headlines, the timer and clock, side values and all small text. No Bitcount. It's close to the font study's option C (Handjet headlines), with Handjet instead of Micro 5 for the small text too.
  - **Option G** (Bitcount round dots with Handjet small text) **stays as history only** (its entries below). From now on, "Low Glare Pixel" means the Handjet theme.
  - **Order:** the theme is built into the mock-up first. The firmware gets it after the user has seen it there; this round doesn't touch the firmware or the Mac app.
  - **The Handjet facts it's built on** (measured 2026-10-05, wght 400 or 700, solid squares ELSH 2, element grid ELGR 1, advance widths without kerning):
    - An element is 1/17 em (480 of 8,160 units), so at a size of N px it's N/17 px. Capitals are 11 elements tall.
    - Every digit is 7 elements wide (3,360 units), so the timer and clock are already tabular.
    - As served, many elements sit on half-element offsets, which land on whole pixels only when N/17 is even (68, 102, 136 px). `tools/fonts/snap17.py` moves every element onto a whole element, so any multiple of 17 px is crisp, and the firmware can convert snapped copies.
    - Widths at 102 px (6 px elements), in capitals: AVAILABLE 333 px, BUSY 168, ON A CALL 318, MEETING 264, BREAK TIME 357, SET UP WI-FI 396, and "18:41" 186. IN A MEETING (414) and SHORT BREAK (411) are over the 408 px headline column at 102 px and fit at 85 px (345 and 342). At 119 px AVAILABLE is 388 and "18:41" 217; at 136 px "18:41" is about 248.
    - In sentence case, as the theme draws it, "In a meeting" at 102 px is 408 px of ink, exactly the column, and takes 102 px like Busy and Available: ink that fills the column fits (the firmware compares whole pixels; the mock-up's fitter allows a tenth of a pixel for the browser's 1/64 px layout rounding, which at 1:1 had dropped it to 68 px). Titles and messages wider than the column (Design review 417, Out sick today 468) take 68 px.
    - The timer, the clock and a pairing code (136 px, digits 88 px tall) sit with their first row on y 42 and their baseline on y 130, level with the side value's, as the spec places them; the AM or PM after the clock shares that baseline. The box they're centered in is the capitals' plus 2 px below the baseline, which puts them there on whole pixels (centered exactly they sat 1 px low).
  - How it's switched, stored and sent is **Proposed** below ("Proposed for the Low Glare Pixel theme").
- **Direction A, Bold Signal (decided 2026-10-04).** On 2026-10-04 the user switched the bar's look to **Direction A, Bold Signal**, the first direction on `docs/design-directions.html` (not to be confused with option A of the font study): **solid, saturated status-color fields**, white **Barlow Condensed 700** uppercase headlines, **Barlow** for all other text, and a darker **tinted info column** on the right. It **replaces** the earlier pick, the Low Glare layout in Bitcount Prop Single round dots with Handjet, which is kept below as history.
  - Bold Signal is the look that goes to the firmware, the simulator's default, and the design card with the "pick" badge. The **Low Glare Pixel** card (Bitcount and Handjet) stays on the mock-up page and can still be applied, but it's no longer the default. *(2026-10-05: that card now shows the new Low Glare Pixel theme, set in Handjet, which is the bar's alternate theme; see above.)*
  - The switch changes only the look. Every other decision and open proposal stands, including focus ticking going silent on a dark screen, meeting titles hidden by default, the offline behavior and the Mac app's API.
  - `tools/fonts/` (the Bitcount and Handjet tools `equalize_digits.py` and `snap17.py`, with their README and the fonts' licenses) is **kept for reference**, but the current look doesn't use it. *(2026-10-05: the Low Glare Pixel theme uses `snap17.py` and Handjet's license again.)*
  - The spec, proposals and follow-ups for Bold Signal are below. Option G's type spec, trade-offs and proposals are kept after them as history.
- **Low Glare layout in a pixel font (decided 2026-10-04; superseded the same day by Bold Signal).** The user reviewed the four design directions on a separate page (`docs/design-directions.html`) and prefers the **Low Glare** layout: a warm near-black background, a 6 px edge bar in the status color, a colored sentence-case headline, a faint side tint, warm off-white and muted text, a status-color frame instead of the white alarm flash, and a paused timer shown steadily in the muted color instead of blinking. It is to be set in a **pixel font**.
- **Pixel font (decided 2026-10-04; superseded the same day by Bold Signal): option D, with Handjet for the small text.** The user picked option D of the font study (`docs/pixel-fonts.html`): **Bitcount Prop Single in round dots** for the headlines, the timer and the clock, with **Handjet** replacing option D's Tiny5 for the small text. This is the user's choice over the team's recommendation: the UX designer, lead developer and product manager had all ranked option A (Jersey 15 with Micro 5) first, for its bold solid word across the room, real lowercase and perfectly sharp pixels. The team set the pick up as **option G**, first on the font study page, and fixed what the study had flagged (see the type spec and trade-offs below).
  - *History:* until 2026-10-04 this was open. The mock-up showed the choice as the **Low Glare Pixel** direction with Jersey 15 headlines and Jersey 10 small text as a stand-in, and the simulator's default look stayed as it was, to switch once the font was chosen.
  - Now that it's chosen, the Low Glare Pixel direction and the simulator's default switch to option G. **Not done yet:** `docs/mockup.html` still shows the Jersey stand-in (see Follow-ups). *(Built later on 2026-10-04, then replaced as the default by Bold Signal.)*
- In the Style panel, the **dark** screen mode is the Low Glare treatment: choosing it starts from Low Glare's colors (brightened so they read on near-black), tints and sentence-case headlines, and keeps the fonts already chosen.

### Proposed for the Low Glare Pixel theme (2026-10-05, waiting for the user's OK)

The product manager's defaults for what the user hasn't said yet. The mock-up is built to them this round. Change any of them and the team will follow.

- **Proposed: on the bar, the quick menu's Light tile becomes Display.** Its value stays the light level ("70%") with "light and theme" as its foot, and a tap opens a Display menu, built like the timer menu's Settings:
  - **Light:** 40, 70 and 100% as now.
  - **Theme:** the current theme's full name; a tap switches to the other theme.
  - **Back:** returns to the quick menu.

  The menu stays open after a change, so you see the new look at once and can switch back, and it closes after 8 s like every menu. The light takes one more tap than now, and the quick menu keeps five tiles, so the themes' full names fit. *The other choice:* a sixth tile on the quick menu, which only has room for short names ("Signal", "Pixel"). On the Pomodoro screen, holding still opens the timer menu, so the theme is changed from any other screen.
- **Proposed: on the Remote, a Display section** with a **Theme** choice, Bold Signal or Low Glare Pixel, and one line under it: "Bold Signal fills the screen with the status color. Low Glare Pixel is dark, with the color in the words and a thin edge, so it gives off less light." The Remote page itself keeps its own look.
- **Proposed: the confirmation** is a toast, "Theme · Low Glare Pixel" or "Theme · Bold Signal", drawn in the new theme. Choosing the theme the bar already has changes nothing and shows no toast.
  - *(2026-10-05, fix round, lead developer, from QA:)* the toast waits while the screen isn't free, as an automatic change's does: during the splash (it shows after "Ready"), on a Wi-Fi setup screen (after setup), on a dark screen (when woken), under a pairing code (after the pairing toast) and while PWR is held (when it's let go; a hold that powers the bar off drops it, and the bar starts in the new theme). It shows over an open menu, as the Display menu's own switch needs. Before, a switch during the splash drew the toast over the splash.
- **Proposed: a switch changes only the look,** at once, on every screen and overlay, an open menu and a pairing code included. The status, the Pomodoro, an alarm, a call or meeting and whether it's set aside, ticking, the light and the flip stay as they are, and nothing makes a sound. On a dark screen, or while a pairing code shows, the toast waits, as it does for any change from the Remote.
- **Proposed: it's kept like every other setting:** across Restart and power off and on (the first frame after power-on is already in the theme), and Wi-Fi setup, Skip and Forget all don't change it. A new bar starts in Bold Signal. The setup page doesn't offer it.
- **Proposed: the API.** A new setting, `display.theme`, `"bold_signal"` (the default) or `"low_glare_pixel"`, read and changed through `/api/v1/settings` like the others, and over USB through `request`. See `docs/api.md` 10.3 (Proposed). **The Mac app needs nothing:** it doesn't read or change settings, and ignores fields it doesn't know.
- **Proposed: the same words, and the bar's drawing rules.**
  - Every screen says exactly what it says in Bold Signal; only the look differs.
  - The headline is in sentence case, as in the Low Glare layout and option G (the widths above are measured in capitals).
  - The colors, tints and pre-mixed fills are Low Glare's (the Low Glare spec in `docs/design-directions.html`, which option G also used).
  - Every Handjet size is a whole multiple of 17 px, drawn on whole pixels, and every line, ring and frame is a whole number of pixels wide (Low Glare's 1.5 px rings become 1 or 2 px), since LVGL can't draw half pixels either.
  - All text meets 4.5:1 against what's behind it, as in Bold Signal.
- **Proposed: the mock-up page.**
  - The Bold Signal and Low Glare Pixel design cards are the bar's two themes. Each says so, the one on the bar is marked, and Apply on either switches the bar's theme, as the Remote does.
    - *(2026-10-05, fix round, lead developer, from QA:)* all the way: while the bar is off or offline the Remote can't reach it, so the card is refused too and says why ("MiniBar is off · press PWR to turn it on, then Apply"; offline it points to the bar's own Display menu, which works offline). Before, the card switched the theme in both cases, and a bar turned on afterwards showed only "Ready".
  - The other directions (Low Glare, Soft Light, Pixel Arcade) and any change in the Style panel stay mock-up-only previews, and the page says so. Switching the theme on the bar or the Remote puts that theme back, unedited.
  - Reset to defaults drops edits and previews and shows the bar's theme, without changing it.
  - The mock-up remembers the theme in the browser, standing in for the bar's flash.
- **Proposed: option G is no longer a card** on the mock-up page. It stays in the font study (`docs/pixel-fonts.html`) and in the history below, and Bitcount stays among the Style panel's pixel fonts. If you'd like option G kept as a card beside the new theme, say so.

### Follow-ups for the Low Glare Pixel theme (open)

- **Mock-up (this round):** the theme on every screen, the Display menu, the Remote's Display section, the cards and Style panel, the Google Fonts link (Handjet with every axis value the theme uses), and the notes, controls table, menu diagram and API section. Then the published Artifact needs updating.
- **Firmware (after the user has seen the mock-up):** `display.theme` in the settings store and the API, the Display menu, the second look in `ui`, Handjet converted at the theme's sizes from snapped static instances (measure the flash; with every element on a whole pixel, 1 bpp should lose nothing), and the Theme choice on the firmware's Remote page.
- **`tools/fonts/README.md`:** say that the Low Glare Pixel theme uses `snap17.py`, at which sizes and weights, and stop calling option G the user's pick (alongside its Bold Signal follow-up).
- **Mac app:** nothing.

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
  - *(Corrected 2026-10-04, Proposed: the baselines below are the mock-up as drawn, which the user approved and the firmware matches line for line. The designer's list said 152, 113 and 107; the browser floors a fractional half-leading, so it draws 151, 112 and 108.)*
- **Kicker:** Barlow 700, 15 px, capitals, 1.5 px letter-spacing, baseline y 30.
- **Headline:** Barlow Condensed **700** (not 800, which closes the counters of A, B and E at a distance; 800 stays in the Style panel only to try), white, capitals (meeting titles and messages in mixed case). Each headline takes the largest size that fits 404 px:
  - **112 px** (cap height 78, baseline y 125): the timer, the clock and BUSY.
  - **100 px** (cap 70, baseline 121): AVAILABLE and ON A CALL.
  - **78 px** (cap 55, baseline 112): IN A MEETING and BACK AT 1:30.
  - **62 px** (baseline 108): meeting titles and the message marquee.
  - The cap centers all sit near y 86.
  - **Tabular digits:** the timer and clock use Barlow Condensed's tabular figures, so they keep the same width every second ("18:42" is about 254 px at 112 px).
- **Sub line:** Barlow 500, 19 px, white, baseline y 151.
- **Info column:**
  - **Status row:** the time in Barlow 600, 15 px, baseline y 30 (level with the kicker), and 16 px icons on the right, 8 px apart: **Wi-Fi** (crossed out when offline) and a **Mac icon while a Mac is connected**. **No battery icon**, since the bar runs on USB with no battery (see Product).
  - **Label:** Barlow 700, 12 px, capitals, 1.2 px letter-spacing, muted, baseline y 92.
  - **Value:** Barlow Condensed 700, 46 px, baseline y 130, with AM or PM at 17 px. Values that are words ("Design review") are 28 px, baseline y 129. Durations fit without an hours-only rule: "10h 20m" is about 152 px of the column's 160.
  - **Foot:** Barlow 500, 14 px, muted, baseline y 151 (level with the sub line).
- **Tomatoes:** 32 × 32 px at 1:1 (the sprites' own size, so they stay sharp) with 8 px gaps; four fit exactly in 160 px. The end screen's tomato is 64 px (2x).
- **Progress bar:** 6 px high at y 166 to 172, a white fill on a track of `lv_color_darken(status, 90)`.
- **Corners:** tiles, the corner pill and toasts have an 8 px radius.
- **Alarm:** Bold Signal uses the solid look's white flash. The status-color frame was Low Glare's.
- **LVGL 9:** solid fills only, no opacity on text, no gradients.

### Proposed for Bold Signal (2026-10-04, waiting for the user's OK)

- **Decided (2026-10-04): keep the current call wording.** The On a call screen keeps what the mock-up shows: a MAC chip followed by the app name when it's sent (for example "MAC · SLACK"), headline ON A CALL, and the sub line "Please keep voices low nearby", with durations written like every other one on the bar ("12m"). The designer's suggestion ("From your Mac · Slack", "Message me instead") was declined.
- **Decided (2026-10-04): the tomato row shows the current set of four.** After Stop or a restart it starts a fresh set, while the foot under it ("1 done · 31m focused") keeps counting the whole day.
- **Proposed: a paused timer holds still in white** at full opacity, with the kicker ("Paused · …") and the sub line ("Flip or tap to resume") saying it's paused. The solid look in the mock-up blinks it down to 35% opacity, which breaks the full-opacity rule (white at 35% on a status field is far below 4.5:1). Low Glare also held it still, so this is what the user last saw.
- **Proposed: the Pomodoro corner pill's tomato** is a 16 px sprite drawn for that size, or a plain dot if that doesn't read well. A 15 px downscale of the 32 px sprite blurs.
- **Proposed: check the smallest text** (the 12 px label and the 14 px foot) on the real bar before the firmware's screens are final.

### Follow-ups for Bold Signal (open)

- **Done (2026-10-04), mock-up:** Bold Signal is the simulator's default and the published Artifact is updated. The original plan: Bold Signal becomes the simulator's default (`DEFAULT_STYLE`) and what Reset to defaults brings back, its card carries the "pick" badge and comes first, the copy near "Your pick comes first" and "The simulator opens with your pick" describes it, and every screen follows the spec above (white at full opacity on the fields, weight 700, the fixed baselines, the status row without a battery icon and with the Mac icon). The Low Glare Pixel card stays, without the badge. Then the published Artifact needs updating.
- **Firmware fonts:** convert with lv_font_conv at 4 bpp:
  - Barlow Condensed Bold at **112, 100 and 78 px** (A to Z, 0 to 9, colon, space and middle dot), **62 px** (printable ASCII), and **46 and 28 px** for the info column.
  - Barlow Medium (500) at 19 and 14 px, SemiBold (600) at 15 px, and Bold (700) at 15 and 12 px.
  - Measure the flash they take once converted.
- **Tabular digits baked into the fonts:** lv_font_conv ignores OpenType features, so before converting, freeze the tabular figures into the default digits with fonttools' `pyftfeatfreeze -f tnum` (from the opentype-feature-freezer package). Barlow's default digits are proportional: in Barlow Condensed Bold the 1 is 284 units wide against 498 for every tabular digit. The product manager checked Barlow Condensed Bold and Barlow SemiBold on 2026-10-04: the tabular digits and the colon have no kerning with each other, so unlike Bitcount no kerning needs removing. Check again in the converted fonts (every digit the same width, no digit kerning pairs).
- **Font names:** **Proposed:** like the Bitcount copies, the frozen Barlow files get a TinyBar family name and a modification note in their name tables, keeping the Barlow Project's copyright and the OFL text (in `tools/fonts/licenses/`). Barlow's copyright notice names no Reserved Font Name.
- **Sizes not in the designer's list yet:** AM or PM on the clock (about 36 px in the mock-up) and in the info column (17 px), menu tiles, toasts, the corner pill, the source chips (MAC, CALENDAR), the Wi-Fi setup screens and the hold screen. Measure them from the mock-up once it's switched, and convert extra sizes or reuse the ones above. *(Done 2026-10-04, measured from the mock-up and built into the firmware: the clock's AM/PM 36 px with 2 px tracking; tile values Barlow Condensed 700 28 px; tile labels and the source chips Barlow 700 12 px; tile feet 14 px; toasts and the pill Barlow 600 15 px; the setup steps Barlow 500 16 px, the network's name 700; the hold screen's title 46 px and its line 14 px.)*
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
  - **Updated 2026-10-05 (alternate theme):** the Low Glare Pixel card now shows the Handjet theme, and the Bold Signal and Low Glare Pixel cards become the bar's two themes. **Proposed:** Apply on either sets the bar's theme; the other cards stay mock-up only (see Look, "Proposed for the Low Glare Pixel theme").
  - **Proposed:** every direction keeps all screen text at a contrast of 4.5:1 or better on every status color. It uses only what LVGL 9 can draw: solid fills, simple gradients, and Google Fonts under the OFL or Apache license. (The current default misses 4.5:1 with white text on Available, about 4.0:1; on Focus, about 3.2:1; and on Short break, about 4.2:1.) *Note 2026-10-04: those figures are for the original solid look. Bold Signal, the default now, meets the rule: white is 4.91:1 or better on every status field.*
- **Simulate controls** (decided 2026-10-04): buttons that start and end a call or a meeting, so the automatic statuses can be tried before the Mac app exists. They're clearly labeled mock-up only, and they sit outside the Remote panel.
  - **Updated 2026-10-04 (pairing round):** a Pairing box joins them.
    - Mac app asks to pair shows a code on the bar.
    - A field takes the code as the Mac app would, and shows the Mac app's answers.
    - A wrong code is any other 6 digits.
    - The demo speed (60× faster) makes the 2-minute code and the back-off waits quick to see, and it can be reached even while the Remote is unpaired.
    - The code itself only ever shows on the simulated bar.
    - *(2026-10-04, pairing fix round:)* **Mac app asks to pair always asks over Wi-Fi**, whatever the Mac app box's Link says, so the code shows on the first try. Before, Link started on USB and the button paired over the cable with no code. A new **Plug in over USB** button stands in for plugging the bar into the Mac: Link becomes USB, and a Mac that isn't paired yet pairs at once. Send a wrong code and Jump to 0:05 left say in the box when there's no code to act on. The Phone shortcut and Fill to 10 devices keep the 10-device limit.

## Firmware (decided while building, 2026-10-04 review round, lead developer)

Where the mock-up has no answer, the firmware picked one. The ones marked **Proposed** wait for the user's or the product manager's OK; change any and the firmware follows.

- **Proposed: Message with nothing set.** On a new bar the Message status shows the mock-up's "Hello". The first time Message is shown by a tap, swipe or BOOT, the bar stores "Hello" as the message, so the API and the Remote report what the bar shows (`POST /status` "message" then works too). The other choice, leaving Message out of the cycle until a message is set, is a product decision.
- **Proposed: a dropped office Wi-Fi.** When the link drops after setup (not skipped), the quick menu's Calendar tile reads "Offline" with when it last synced ("synced 5m ago"), since Sync now can't run ("No Wi-Fi, can't sync") but the bar keeps following its saved copy; the Wi-Fi tile's foot says "reconnecting"; the Wi-Fi menu's Network tile says "not connected" under the bar's name.
- **Proposed: new copy.** A brightness change from the API says "Light 70%", like the Light tile. The Wi-Fi failure screen for a network that gives no address says "No IP address" (was "No address").
- **Decided: the setup menu closes once setup is over.** A setup menu left open on Connecting goes when the join works and setup moves on, so its Skip can't undo a join that worked ("Connected is the end of setup").
- **Decided (api.md 9.1):** a Pomodoro action from the API silences a ringing alarm first, even "pause" when nothing runs (it still answers `409 not_running`).
- **Open, for the product manager: two rules the mock-up has that don't fit "every control does something".**
  - A swipe on an open menu is ignored (the mock-up's own rule: "Only taps act on a menu", so a swipe can't run the tile the finger lifts on). **Proposed:** keep it and record it here as the one exception. The other choice is a swipe that closes the menu.
  - Skip (the timer menu's tile, and the Remote's) shows no toast, unlike Stop ("Pomodoro stopped") and +5 ("+5 min"). **Proposed:** add "Skipped to Short break" (or "to Focus", "to Long break") to the mock-up's `skip()`; the firmware will follow.
- **Open, waiting for the user: encrypting stored secrets.** The office Wi-Fi password, a work login's username and password (often the person's company sign-in) and the secret calendar address sit in the bar's flash as plain text. **Anyone in the open office with a laptop and a USB-C cable can read them in about a minute:** the flashing tool resets the chip into download mode through the same USB-C port, with no button press and without opening the case. The choices, all of them **irreversible** on that board:
  - **NVS encryption** (an HMAC key burned into the chip): stops a plain read of the flash, but not someone who flashes their own firmware to read the secrets with the chip's own key, then flashes MiniBar back.
  - **Flash encryption with secure boot**, or **turning off USB download mode and JTAG**: full protection, but the bar then only takes signed updates, and the web flasher at 0x0 no longer works as it does now.
  - **Neither**, and don't use a work login on the bar (use a password network).

  This should be decided before the bar joins an office network with work-login credentials.
- **Hardware (lead):** if the picture is upside down on the real panel, `CONFIG_TINYBAR_LCD_TURN_180` turns it (the "upright" rotation comes from a path in Waveshare's example that never ran as shipped). The boot no longer tests the whole PSRAM and the bootloader logs only warnings, so the power hold comes on sooner; on a battery, a PWR press still has to last until the power hold (to be timed on the board).
  - *(2026-10-05, firmware 1.0.2, lead developer:)* settled on the bar. With 1.0.1 the user saw the picture upside down when the bar started, and then it righted itself without being turned over. The user stands the bar with its side buttons on top, so that's upright (see Hardware notes). 1.0.2 makes buttons on top "upright" in the firmware (the IMU's up axis −Y and `CONFIG_TINYBAR_LCD_TURN_180` on, which cancel once the IMU has a reading, so steady pictures and flips are unchanged), waits for settled IMU samples before the first frame instead of a fixed 40 ms, and remembers the last steady orientation for a start lying flat. Started lying flat with nothing remembered, the bar draws buttons on top. What the IMU's first samples held on 1.0.1 isn't known (no serial log); 1.0.2's start-up log line reports it.

## Mock-up fixes found by the controls guide (2026-10-05, built in the mock-up the same day)

QA checked every control on every screen of the mock-up while building `docs/controls-guide.html` (published at https://claude.ai/artifact/RB39woa2tw36BTYhLH3ZnW). These differ from the intended behavior and go into the next mock-up round:

- **Swipe after a flip:** the mock-up reverses swipe direction on a flipped bar; it shouldn't (the firmware doesn't).
- **Power screens:** while Keep holding or Powering off shows, ignore touches, BOOT and the hold timer, and close any menu when power-off completes.
- **Flip during start-up:** keep the flip rather than dropping it.
- **Message on a dark screen:** decide whether Show from the Remote wakes the screen (the mock-up does) or waits (the theme round's Proposed rule).
- **The device frame:** the mock-up draws BOOT and PWR on the right edge; on the real bar they're on the top long edge.
- **The guide's menu pictures** show the old Light tile; re-render them once the theme round's Display tile lands.

*(2026-10-05, mock-up round 2, lead developer: the first five are built in `docs/mockup.html`, with its notes, controls table and diagrams.)*
- **Swipe:** left and right are as the layout faces you, turned over or not.
- **Power screens:** Keep holding, Powering off and Starting up ignore touches and BOOT, a hold that began before them opens no menu, and a menu is closed when the power goes off. **Proposed, new:** once powering off has begun, a PWR press is ignored too (it used to darken the screen during the second it takes); the next press after it's off starts the bar.
- **Flip during start-up (and powering off):** the layout turns, since the motion sensor's reading is absolute and the first frame after start-up is drawn the right way up, as firmware 1.0.2 does. Its other jobs (an alarm, a Pomodoro waiting) can't apply then, so nothing else happens.
- **Dark screen:** it waits. Show from the Remote, and every other Remote change (a status, the Pomodoro, set aside, Show again), no longer wakes a dark screen; the toast shows when it's woken, as the theme round proposed.
- **Device frame:** BOOT and PWR sit on the top long edge, slightly inset, at the screen x positions the tour's arrows use (placeholders 452 and 541 of 640 px, to be measured on the bar as `TB_BTN_BOOT_X` and `TB_BTN_PWR_X`).
- **The guide's menu pictures** belong to the guide's own renderer and weren't touched; it needs re-rendering for the six-tile quick menu and the four-tile Display menu.

## Hardware notes for the firmware (V2)

- **Which way is up:** the user stands the bar with the buttons on top; that's upright (verified 2026-10-05). Turned over, buttons at the bottom, the layout turns with it. On the V2 board that's the QMI8658's −Y axis pointing up and LVGL rotation 270 (firmware 1.0.2: `CONFIG_TINYBAR_IMU_UP_Y_NEG`, `CONFIG_TINYBAR_LCD_TURN_180`), inferred from 1.0.1's steady pictures, which were right with +Y and no turn; 1.0.2's start-up log line will confirm it.

- Display: AXS15231B over QSPI. Backlight on **GPIO 42** plus the expander's BL_EN (EXIO1). LCD reset on the expander (EXIO5); TE on GPIO 21.
- Touch: AXS15231B over I²C (SDA GPIO 17, SCL GPIO 18).
- TCA9554 I/O expander: EXIO0 touch interrupt, EXIO1 backlight enable, EXIO2/3 IMU interrupts, EXIO4 RTC interrupt, EXIO5 LCD reset, **EXIO6 SYS_EN (power hold on battery)**, EXIO7 amplifier mode.
- PWR button read on **GPIO 16** (SYS_OUT); BOOT on GPIO 0; battery voltage on GPIO 4.
- RTC PCF85063, IMU QMI8658, audio codec ES8311.
- On USB the board is always powered; "power off" on USB means deep sleep woken by PWR.
- Battery connector MX1.25, **pin 2 positive**. Charger ETA6098 at 1.2 A. The board has battery protection and reverse-polarity protection.
- Flashing: merged images go at **0x0**; use **115200** baud (a faster write once left the screen showing noise).
- **Verified on the user's bar (2026-10-05, firmware 1.0.0 to 1.0.2):**
  - It powers on, holds its own power, and draws the screen. The Wi-Fi setup QR code shows on first start.
  - Wi-Fi setup works from an Android phone with 1.0.1 (see Automatic status, the setup address).
  - Sound works: the focus ticking is audible through the ES8311 and the amplifier, and the chime at a phase's end sounds right at the current speaker volume (75), so the volume stays. Soft and Medium ticking were both too quiet at first; both were raised, and with 1.0.2 the user finds Soft a sensible volume (see Pomodoro, Ticking). The tap click (Proposed) is specified relative to Soft, so it inherits this level.
  - Orientation: the user stands the bar with the **buttons on top**. The motion sensor's steady reading is right (the picture rights itself), but with 1.0.1 the first frame at power-on came up upside down. **Fixed in 1.0.2 and confirmed by the user (2026-10-05): the picture is the right way up straight away.** That also confirms the −Y up axis the firmware inferred. (The user flashed the app-only file at 0x30000, keeping Wi-Fi and pairings.)
  - The Remote opens from a Windows PC on the office Wi-Fi once the bar is set up (2026-10-05).
  - Windows note: joining TinyBar-Setup from a PC that's also online another way (a cable or a dock) opens msn.com instead of the setup page, because Windows' sign-in check goes out the other connection. Typing `http://4.3.2.1` works, since that address always goes through the bar's Wi-Fi.
  - BOOT and PWR are on the edge the user keeps on top, slightly inset in the case: turned over, the bar rests on that edge without pressing them (checked by the user, 2026-10-05). So flipping can't press PWR or power the bar off by accident.
  - Pairing works on the bar: the Remote paired with the 6-digit code shown on the bar (2026-10-05).
  - Not yet reported: touch and BOOT, PWR off and on, the clock from the internet, the calendar.

## 2026-10-07: a short drag no longer acts as a tap (firmware 1.0.7)

- **Reported:** on the Pomodoro screen a swipe often started the timer.
- **Cause:** a press that moved 10 to 39 px (too far for a tap, too short for a swipe, or mostly vertical) was treated as a tap on status screens, so on Pomodoro it started the timer and elsewhere it advanced the status.
- **Decision (Accepted, firmware):** such a drag now does nothing on every screen; it already did nothing on menus. A swipe is still more than 40 px sideways. If real taps now get lost to finger jitter, raise the 10 px slop instead of bringing the old behavior back.
- **Open:** the mock-up (`docs/mockup.html`) still treats a short drag as a tap; align it after the multiple-calendars mock-up round, which is editing that file.

## Time format (2026-10-07)

**Accepted (the user approved a 12-hour default plus a 24-hour choice, 2026-10-07): the bar has a Time format setting, 12-hour (the default, today's behavior, 3:30 PM) or 24-hour (15:30).** It is a device setting like Theme and Tap sound: switched on the bar (hold, then Display, then the Time tile) or on the Remote (Display, then Time format), kept through Restart and power off and on, and left alone by Wi-Fi setup and Forget all. Built in `docs/mockup.html` only; nothing under `firmware/` or in `docs/api.md` changes yet.

- **Accepted: where it applies.** Every time the bar shows: the clock screen and the status row's clock, a meeting's start and end span, Next up, Free until, Posted, Away's "Back at", "since", "left at", "at" times, and any AM/PM chip. The Remote's times follow it too, so the bar and the Remote never disagree.
- **Accepted: 24-hour drops AM and PM everywhere and zero-pads the hour** (09:05, 00:15, 23:59; midnight is 00:00, never 24:00). In 12-hour nothing changes (3:30 PM, no leading zero).
- **Accepted: Away "Back at".** A time typed or sent for Away stays **HH:MM, 24-hour, on the wire** (`docs/api.md`), whatever the setting; only how the bar shows it follows the setting ("Back at 3:30" or "Back at 15:30"). The mock-up has no typed Away time yet (its Away screen uses a sample time), so only the display side is built.
- **Proposed: the Display menu tile.** Five tiles now (Light, Theme, Tap sound, Time, Back), each 112 px wide with 92 px inside. A tile's label can't be "Time format" (85 px in Barlow Condensed's label, 78 px in Handjet's: no margin), so the label is **Time**, the value **12-hr** or **24-hr**, the foot "tap to switch"; the toast and the Remote say the full name ("Time format · 24-hour"). Theme's value no longer fits as a full name on two lines in five tiles, so it reads **Bold** or **Pixel** with the full theme name in the foot ("Bold Signal", "Low Glare Pixel"). A tap switches at once, the menu stays open, a toast confirms, and the tap clicks like any tile (Tap sound permitting); a change from the Remote or the API never clicks.
- **Proposed: fit at 640 x 172 (measured in the mock-up, both themes, light and dark, both formats).**
  - **Clock:** 24-hour "03:08" is 5 characters with no chip, about the same width as 12-hour "3:08" plus its AM or PM chip, so it fits the 112 px (Bold) and 136 px (Handjet) clock sizes with room to spare; the clock keeps its size and left edge.
  - **Meeting span** ("03:08-03:38"): shorter than 12-hour "3:08-3:38 AM" or "11:30 AM-12:15 PM", so it fits beside the CALENDAR chip in both themes.
  - **Next up / Free until / Posted:** with no AM or PM there is no small chip after the value and no "PM · " in front of the foot; the value is "14:30" and the foot starts with "then ..." (or the title). Nothing to re-fit.
  - **Away, Handjet:** one size, 68 px, for every time; "Back at 03:40" fits as "Back at 3:40" did.
  - **Away, Bold Signal: the one change.** 12-hour "Back at 3:40" takes 78 px (376 px of the 404 px column), but a zero-padded 24-hour "Back at 03:40" is 412 px at 78 px, 8 px too wide, so it steps down to the next size in the ladder, **62 px**, at every hour (today the same step-down already happens for 12:xx). It fits, and it stays one size in 24-hour. If 62 px proves too small across the room, a 72 px digits-and-"BACK AT" size (about 380 px wide; a few glyphs) would fit; not proposed unless asked.
  - **The Display menu:** see the tile above; nothing cut off at 1100 or 390 px wide.
- **Proposed (for the firmware step): the setting and its field.** `display.time_format`: `"12h"` (the default) or `"24h"`, next to `display.theme`: read as `settings.display.time_format` and changed with `PATCH /api/v1/settings` (scope `full`) as `{"display": {"time_format": "24h"}}`; anything else is `400 bad_value` with `"field": "display.time_format"`. If the firmware prefers a flat key, `settings.time_format` works the same. `docs/api.md` needs a section beside 10.3 (theme) and 10.4 (Tap sound) when this is built.
- **Proposed: firmware details.** One shared formatter (hour, minute, setting) returns the text for every screen, so no screen formats a time itself; the setting lives in `nvs` with the other display settings and defaults to 12-hour for a bar updated from today's firmware. 24-hour needs no new glyphs (digits and the colon are there); the AM and PM glyphs stay for 12-hour. The new tile needs its label "TIME" and the value "12-hr"/"24-hr" in the converted fonts (Barlow Condensed 700 and Handjet): check that "h" and "r" are in the 34 px value sets. The Mac app is not changed; it never shows the bar's times.
- **Open:** (1) Today's Away "Back at" shows no AM or PM in 12-hour ("Back at 3:30"); left as is. (2) Should the Mac app follow the bar's setting? (3) Whether Bold's Away at 62 px in 24-hour is acceptable, or the 72 px size is worth adding.
