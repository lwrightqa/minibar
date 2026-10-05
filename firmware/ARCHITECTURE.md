# TinyBar firmware architecture

Lead developer, 2026-10-04. Read with `../docs/decisions.md` (what), `../docs/mockup.html` (how it behaves and looks)
and `../docs/api.md` (the wire contract). Where this file and those disagree, they win; tell the lead.

**Status (2026-10-04, integration):** every module is built and wired together. The firmware compiles with no
warnings and the host tests pass (section 14), but **nothing has run on the board yet**. Each component's README has
a bring-up checklist for the first runs (`components/board`, `components/net`, `components/calendar`).

## 1. Principles

1. **The mock-up is the specification.** core is a port of the mock-up's state (`s`, `pomo`, `cal`, `mac`) and its
   handlers, function by function, with the same order of operations, toasts and timings. ui is a port of its
   `view()` family and the Bold Signal CSS. Section 8 maps each mock-up function to its firmware home.
2. **One owner for the model.** A single task, the *app task*, owns the core model, LVGL and the ui. Every other task
   talks to it through the bus. Nothing in core locks, and the HTTP and USB router reads and changes the model in one
   consistent step by running on the app task (`tb_bus_exec`).
3. **Pure logic is host-tested.** core, calendar's `src/`, net's `proto/` and ui's `view/` include no ESP-IDF,
   FreeRTOS or LVGL headers and build on Linux under `test/host/`. The device-only code around them is thin.
4. **Effects, not calls.** core never calls hardware. It queues effects (chime, backlight, power off, start Wi-Fi
   setup, save...) that the app task carries out. That keeps core testable and every side effect visible in one
   switch (`main/app_task.c`, `run_effects`).
5. **Time is passed in.** core and calendar never read a clock: every function takes a `tb_clock_t` (monotonic ms for
   durations and timers, wall time for what the screen prints), so tests drive time.

## 2. Module map

| Module | Path | Owner | Pure? | What |
|---|---|---|---|---|
| main | `main/` | lead | no | Start-up order, the app task loop, effect dispatch, NVS persistence of settings and own state |
| bus | `components/bus/` | lead | no | The app task's event queue; `tb_bus_exec` (run a function on the app task and wait) |
| core | `components/core/` | core builder | **yes** | `tb_app` (the device state machine), `tb_pomodoro`, `tb_settings`, `tb_menu`, `tb_gesture`, `tb_fmt`, `tb_text` |
| ui | `components/ui/` | ui builder | `view/` yes | LVGL screens and overlays in Bold Signal; `ui_view` (what each screen says, as data); fonts; tomato images; host snapshot tool |
| net | `components/net/` | net builder | `proto/` yes | Wi-Fi (station, WPA2-Enterprise, setup AP + DNS), mDNS, HTTP server, Remote and setup pages, USB serial, SNTP; `proto/`: the router, the Mac table, pairing |
| calendar | `components/calendar/` | calendar builder | `src/` yes | Address checks, streaming ICS reader, RRULE, time zones; `esp/`: HTTPS sync service |
| board | `components/board/` | board builder | no | Power hold, display + touch for LVGL, backlight, buttons, IMU flip, RTC, audio, device id, watchdog |

Dependencies (an arrow means "includes the headers of"):

```text
main ──► board, bus, calendar, core, net, ui
net ───► core, calendar (cal_status, cal_url; cal_sync on the device), bus
ui ────► core, lvgl
calendar ► core (tb_types), bus (device side)
board ──► lvgl, bus, core (tb_types)
bus ───► core (tb_types)
core ──► nothing
```

No cycles, and nothing depends on main. net doesn't call board: it posts `TB_EV_TIME_SET` and main writes the RTC.

## 3. Tasks

| Task | Owner | Core | Priority | Stack | Does |
|---|---|---|---|---|---|
| **app** | main | 1 | 5 | 12 KB, internal | core, ui, LVGL rendering and touch reading, the router (via `tb_bus_exec`), effects, saves |
| esp_timer (IDF) | board | 0 | 22 | IDF | The 5 ms button poll (a timer callback, no task of its own) |
| imu | board | 0 | 3 | 4 KB, internal | QMI8658 at about 25 Hz; posts `TB_EV_ORIENTATION` after 0.5 s of a steady new orientation |
| audio | board | 1 | 6 | 4 KB, internal | Mixes the chime and tick PCM into I2S; one tick a second while ticking is on |
| httpd (IDF) | net | 0 | 5 | 6 KB, **PSRAM** | HTTP handlers: parse, then `tb_bus_exec(router)`; serves the gzipped pages directly. A request has 3 s from its first byte to arrive (headers and body), so one client can't hold the task |
| usb_rx | net | 0 | 4 | 4 KB, **PSRAM** | Reads "@tb " lines (2 KB max), `tb_bus_exec(net_api_usb_line)`, writes the reply |
| net | net | 0 | 4 | 4 KB, internal | Wi-Fi worker: saves credentials and the Wi-Fi skip, scans, opens the setup network (scan first, then the access point), starts mDNS and SNTP once online, hands the setup page's calendar address over |
| dns | net | 0 | 5 | 4 KB, **PSRAM** | Setup mode only: port 53 on every address, answers the setup subnet's A queries with 4.3.2.1 (NODATA for other types); priority 5 as in ESP-IDF's captive_portal example |
| cal_sync | calendar | 0 | 2 | 10 KB, internal | HTTPS fetch streamed through the ICS reader; one at a time; blocks a tick after every 50 ms of work, from inside recurrence expansion too (the reader's tick hook) |
| tiT (lwIP tcpip) | IDF | **0** (pinned) | 18 | 4 KB | |
| sys_evt (event loop) | IDF | 0 | 20 | 5 KB | runs net's Wi-Fi and IP handlers |
| wifi, mdns, sntp | IDF | 0 | IDF defaults | IDF | |
| bringup | board | 0 | 2 | 4 KB | Only with `CONFIG_TINYBAR_BOARD_BRINGUP` (off): the hardware self-test |

- **The app loop** (`main/app_task.c`) runs every 5 to 33 ms (what `lv_timer_handler` asks for):
  1. drain the bus, at most 16 events or router jobs per loop (`tb_bus_receive` runs a job and hands back
     `TB_EV_NONE`, so jobs count against the budget too);
  2. poll the hold gesture, `tb_app_tick`, `net_api_tick` (once net is up);
  3. under `board_display_lock()`: run the effects, `ui_update`, `lv_timer_handler` (renders, flushes, reads the
     touch panel, whose samples reach core through ui's pointer callback), then the effects those touches caused, so
     a wake or a chime doesn't wait a loop;
  4. `settings_store_poll`, feed the watchdog.

  A loop that used its whole event budget yields one tick, so a flood of requests can't starve core 1's idle task.
  The log carries a health line 15 s after start, right after the first calendar fetch (the internal-RAM peak), and
  then every minute: internal heap (free, lowest, largest block), PSRAM, dropped events, frames and their rotate +
  send time, and every task's unused stack by name. The frame time (LVGL's render plus the flush) is logged every
  minute at INFO, and a loop slower than 250 ms is logged: all for the first runs on the board.
- **LVGL has no OS layer** (`CONFIG_LV_OS_NONE`): only the app task calls it, and it holds `board_display_lock()`
  while it does, so anything that ever needs LVGL from elsewhere has one lock to take (nothing does today). Start-up
  calls LVGL from the main task before the app task exists. The flush callback blocks on the DMA semaphore inside the
  app task.
- Wi-Fi and the network stack stay on core 0, the screen on core 1, so a slow TLS handshake never stalls a frame
  (`CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0`: lwIP's thread, at priority 18, would otherwise preempt the app task).
- **Watchdog:** the task watchdog (10 s, panic and restart) watches both idle tasks, the app task, usb_rx, imu and
  audio. cal_sync isn't subscribed; it relies on its own limits (20 s per network step, 3 minutes per fetch), and it
  keeps core 0's idle task fed: it blocks for a tick after every 50 ms of work, and the ICS reader calls its tick from
  inside a recurrence walk, whose budget is charged by the days examined (a hostile feed's whole expansion is about
  0.1 s at -O2 on a desktop, a few seconds on the S3 at most; to be timed on the board).
  `board_power_off()` feeds the watchdog while it waits for PWR to be let go. A crash leaves a core dump in the
  `coredump` partition (`idf.py coredump-info`).
- **Router latency:** `tb_bus_exec` waits up to 900 ms for the app task to start the job (api.md's 1 s reply rule);
  if it can't, the job is canceled and the client gets `503 busy`. A job that has started always finishes before
  `tb_bus_exec` returns, so the request and reply buffers can live on the handler's stack.

## 4. Start-up order (`main/app_main.c`)

1. `board_power_hold()`: the backlight pin to its dark level first, then the system I2C bus, TCA9554, **SYS_EN high**.
   First, before anything slow: on battery the board turns itself off again unless SYS_EN is held.
   `board_backlight_early_off()` keeps the panel dark.
2. `tb_bus_init()`, `net_usb_init()`: from here every log line goes through the protocol-safe USB writer. The first
   log line names the firmware version and why the chip started (power-on, restart, deep sleep, crash, watchdog).
3. NVS (`nvs` and `nvs_sec`; a partition that can't be read is erased and started again), the device id (Wi-Fi MAC),
   settings, and the time zone (`setenv("TZ")`, and the calendar's copy).
4. `board_imu_init()` and one reading, so the first frame is drawn the right way up.
5. `lv_init()`, `board_display_init(flipped)`, `board_touch_init()`, `ui_init()`.
6. `board_rtc_init()`: the system clock from the RTC if it holds a time TinyBar wrote. The clock counts as known if
   the RTC was trusted or the system clock already reads 2025 or later (it survives a restart and a deep sleep).
7. `tb_app_init()` (booting = true: the splash) with how the Wi-Fi starts (`net_wifi_start_mode()`: a saved network,
   a remembered Skip, or the QR code), `settings_store_restore()`, `tb_app_flip(initial)`.
8. `app_task_start()`. From here only the app task touches the model, LVGL and the ui. The backlight comes on after
   the first frame reaches the panel (the initial `TB_FX_BACKLIGHT`).
9. `board_buttons_init()`, `board_audio_init()`, `board_imu_start()`.
10. `net_init()`, `net_start()` (HTTP, then station or setup mode, then the USB reader and its `ready` line; mDNS and
    SNTP once online), then `app_net_ready()`: Wi-Fi effects core queued before this (in practice none, since the
    1.5 s splash outlasts net's start) are carried out now. `cal_sync_init()` posts the saved meetings, then waits for
    Wi-Fi.
11. `esp_ota_mark_app_valid_cancel_rollback()`. There's no OTA in v1, so it guards nothing yet. **When OTA lands:**
    mark the image valid only after a self-test (the panel and net started), and restart right after switching the
    OTA partition, before any power off can happen: power off is a deep sleep, and with
    `CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP` the wake skips the image check that IDF's help says must not be
    skipped after a partition switch.

Nothing in start-up gives up on a failing part: a missing IMU, codec, RTC, touch controller or unreadable NVS leaves
the bar working without it and says so in the log, and a failed network start leaves statuses and the Pomodoro
working. Only running out of memory for the display restarts the chip.

After 1.5 s core leaves the splash (`powerOn()`'s ending: a Pomodoro status falls back to the last status, the Wi-Fi
setup screens restart at the QR code, set-aside is cleared, "Ready" or "Ready · On a call").

## 5. The bus (`components/bus/include/tb_bus.h`)

| Event | From | Payload | The app task does |
|---|---|---|---|
| `TB_EV_BUTTON` | board | `tb_button_t`: BOOT click, PWR down, PWR up | `tb_app_button` (core times the PWR hold: 400 ms shows "Keep holding", 3 s powers off) |
| `TB_EV_ORIENTATION` | board | flipped, initial | `tb_app_flip` |
| `TB_EV_WIFI` | net | connecting / connected / failed (setup), link up / down | `tb_app_wifi_*`; `cal_sync_set_online` |
| `TB_EV_TIME_SET` | net | source (ntp, mac) | clock valid; `board_rtc_save_now()` |
| `TB_EV_USB_LINK` | net | up / down | log only (the Mac icon follows the Mac table) |
| `TB_EV_CAL_MEETINGS` | calendar | `tb_cal_meetings_t*` (malloc'ed) | `tb_app_set_meetings`, then `free` |
| `TB_EV_CAL_STATUS` | calendar | saved, checking, last sync | `tb_app_set_calendar` |
| `TB_EV_CAL_EVENT` | calendar | saved, setup failed, removed, synced | `tb_app_calendar_event` (the toasts) |
| `TB_EV_NOTIFY` | anyone | text | `tb_app_notify` |
| `TB_EV_EXEC` | `tb_bus_exec` | fn, ctx | runs fn on the app task (the router); `tb_bus_receive` returns it as `TB_EV_NONE` |

The queue holds 32 events and posts never block; a full queue drops the event and counts it (`tb_bus_dropped()`, in
the health line).

Effects core asks for, and what main does with them (`run_effects`):

| Effect | main calls |
|---|---|
| `TB_FX_CHIME`, `TB_FX_TICKING` | `board_audio_chime`, `board_audio_set_ticking` |
| `TB_FX_BACKLIGHT`, `TB_FX_ROTATE` | `board_backlight_set`, `board_display_set_flipped` |
| `TB_FX_POWER_OFF`, `TB_FX_RESTART` | save what's pending, sound off, `esp_wifi_stop()`, then `board_power_off()` or `board_restart()` |
| `TB_FX_WIFI_SETUP`, `_SKIP`, `_DONE` | `net_setup_begin`, `net_setup_skip` (and `cal_sync_set_online(false)`), `net_setup_done`; held until net is up |
| `TB_FX_CAL_SYNC` | `cal_sync_now()`; if the calendar refuses (it hasn't heard the link is up, or the address just went), core gets `TB_CALEV_SYNC_FAILED`, so the quick menu's "Sync…" ends in "Couldn't sync the calendar" |
| `TB_FX_SAVE_SETTINGS` | NVS in 2 s; a changed time zone is applied (TZ and the calendar), a changed name goes to mDNS |
| `TB_FX_SAVE_STATE` | NVS in 2 s |
| `TB_FX_PAIRING_CANCELED`, `TB_FX_FORGET_DEVICES` | `net_api_pairing_canceled` (a failed pairing for the back-off), `net_api_forget_devices` (core has already shown the toast) |
| `TB_FX_PAIRING_RESET` | `net_api_pairing_reset`: Power off, Restart or PWR held 3 s end any code without counting a failed pairing, and clear the back-off (api.md 4.9) |

## 6. Data flow

**Touch.** board's LVGL input device reads the AXS15231B → LVGL (rotation applied) → ui's screen events
(pressed, pressing, released, lost) with the menu tile under the point → `tb_app_pointer()` → `tb_gesture` (hold at
550 ms unless it moved over 10 px; a swipe is |dx| > 40) → the mock-up's tap, swipe and hold handlers in core.

**Buttons and flip.** board posts edges and orientations → core's BOOT, PWR and flip handlers.

**Effects.** core queues `tb_effect_t` → `run_effects()`: chime and ticking (board audio), backlight and rotation
(board display), power off and restart (save first), Wi-Fi setup start, skip and done (net), Sync now (calendar),
save settings and save state (debounced NVS), pairing canceled and forget devices (net's router).

**Screen.** `ui_update()` compares `ui_view_key()` (rev, the shown minute, the timer's second...) with the last one;
when it changed, `ui_view_build()` produces the words and `ui.c` lays them out in Bold Signal. Overlays (menus,
toast, hold screen, flash, pairing) come from `ui_overlay_build()` and `tb_app_t.menu`.

**API over HTTP.** esp_http_server → `net_http.c` parses headers (Host, Content-Type, Authorization, Cookie,
Origin, If-None-Match) and the body (≤ 2 KB, within the request's 3 s) into a `net_req_t` → `tb_bus_exec(net_api_handle)`
on the app task → the router checks host, rate limits, token and scope, checks the JSON's nesting (16 levels at most)
before cJSON parses it, validates, calls `tb_app_remote_*` (or the calendar and Wi-Fi ports) and builds the JSON reply
(≤ 8 KB) → `net_http.c` sends it with the headers api.md requires. A body that wasn't read (over 2 KB) closes the
connection after the reply instead of being drained.

**API over USB.** `usb_rx` assembles lines → `tb_bus_exec(net_api_usb_line)` → the same router (`hello`, `call`,
`status`, `pair`, `request`) → one "@tb " reply line, written whole through the shared writer.

**A Mac's call.** `POST /api/v1/call` or USB `call` → the router checks the token may report for that `client`
(api.md 5.2: a token paired with a client only for it; one paired without, for one Mac no one else holds; USB for
any) → `net_macs_on_call()` (per-Mac state with the token it came with, staleness, call ids, elapsed time) →
`net_macs_aggregate()` → `tb_app_set_call()` → core's `syncAuto()`: takeover, pause the Pomodoro, hold a ringing alarm,
toasts. Every app loop `net_api_tick()` runs the 90 s time-outs ("Lost contact with your Mac"). Revoking a token, and
Forget all, end the Wi-Fi calls that came with it; a call over USB carries on.

**Calendar.** `cal_sync` task: HTTPS GET with the certificate bundle → each chunk into `cal_feed_write()` (line
unfolding, VEVENTs, RRULE expansion in the window, EXDATE, overrides) → `cal_feed_finish()` → today's and tomorrow's
meetings that count → `TB_EV_CAL_MEETINGS` → core starts and ends In a meeting at each event's exact times from that
list; syncs every 10 minutes only refresh it. The list is saved in NVS (packed, `cal_store.h`) when it changes. A
fetch waits until the clock is known, since the window is "today and tomorrow". For a new address the posts come in
the order core needs: the meetings, then `TB_CALEV_SAVED` (its toast counts them), then the status. Only Sync now
reports `TB_CALEV_SYNCED` or `TB_CALEV_SYNC_FAILED`; the 10-minute syncs are silent. If the setup page's address
can't even be checked once the bar is online, net posts `TB_CALEV_SETUP_FAILED`, as a failed check does.

**Time.** RTC at boot (or the system clock kept through a restart or deep sleep) → SNTP once online (the office's own
NTP server from DHCP first, then pool.ntp.org and time.google.com) → the RTC is written after each sync → or the
Mac's `hello` time when there's been no network time for 24 h. The time zone (IANA) becomes a POSIX rule through
calendar's table. **The setup page's zone always applies** (the phone of the person setting the bar up, so setting
it up again after a move fixes the clock); the Mac's `hello` only fills one in when none is set (api.md 6.6); PATCH
`device.time_zone` changes it any time.

## 7. The core model (`components/core/include/tb_app.h`)

`tb_app_t` is the mock-up's `s`, `pomo`, `cal` and `mac` in one struct, plus the overlays (menu, toast, pending
toast, PWR hold, pairing) and the effect queue. It's transparent so ui and net can read it; only core writes it.
`rev` goes up on every visible change and is also the API's `rev` and ETag.

Firmware additions the mock-up doesn't have (each flagged in section 13):
- the wall clock may be unknown (`tb_clock_t.valid`);
- today's tomatoes and focused time reset at local midnight (`tb_pomo_roll_day`) and survive a restart (NVS);
- Wi-Fi can drop after setup (`wifi_link_up`), apart from being skipped (`TB_WIFI_OFFLINE`);
- the pairing screen and the Devices tile (api.md 4.8; approved with pairing on 2026-10-04, and aligned with the
  mock-up's pairing fix round on 2026-10-05: a code stops a flash under way, the Devices tile with nothing paired gives
  ui the "pair at" feet to measure, Power off and Restart send `TB_FX_PAIRING_RESET`);
- Away's back-at time and note (api.md 8.1, proposed).

Where core departs from the mock-up's behavior (each marked "Firmware:" in `tb_app.c`; the product manager may want
3 and 4 in the mock-up and decisions.md):
1. **Flip during the splash** (or Powering off) only turns the layout: the IMU reports an absolute orientation.
2. **Sync now** really fetches: the tile shows "Sync…" and the toast comes with the result, "Calendar synced" or the
   new "Couldn't sync the calendar". "No Wi-Fi, can't sync" also covers a link that dropped.
3. **A message from the Remote wakes a dark screen**, as the Remote's status buttons do.
4. **Settings changed through the API each show a toast** (api.md 10.2): the mock-up's log lines ("Focus set to
   50 min"), plus new copy "Chime on/off" and "Brightness 55%". Name and time-zone changes show none.
5. **Ticking is silent while "Powering off" shows.**
6. **Open menus refresh their tiles** ("synced 2m ago" ages; a tile that no longer applies goes).
7. **Midnight** resets only today's tallies; a session that crosses midnight carries on.
8. **New copy:** the Devices tile ("3 paired", "Forget all", toast "Forgot 3 devices"), and the Calendar tile's "not
   synced yet" and "tap to sync" when there's no sync time or no clock.

## 8. Where each mock-up behavior lives

| Mock-up (`docs/mockup.html`) | Firmware |
|---|---|
| `STATES`, `s.idx`, `s.lastStatus`, `go()` | core `tb_app.c`: own status, `tb_app_remote_status`, the tap/swipe/BOOT handlers |
| `pomo`, `phaseLen`, `isReady`, `pomoActive`, `nextPhase`, `startWaiting`, `remSec`, `resetPomo` | core `tb_pomodoro.c` |
| `startPause`, `skip`, `stop`, `endPhase`, `silence`, alarm repeats in `loop()` | core `tb_app.c` (alarm state in `tb_app_t`) |
| `tomatoRow`, `buildRipenFrames`, `ripenPixel`, the pale tomato | core `tb_pomo_tomatoes` (which tomato), ui `tools/gen_tomatoes.py` (the images) |
| `autoTop`, `asideKind`, `callNow`, `meetNow`, `quiet`, `setAside`, `backToOwn`, `showAgain`, `takeover`, `releaseHeldAlarm`, `syncAuto` | core `tb_app.c` |
| `macTimers`, the 90 s time-out, per-Mac state | net `proto/net_macs.c` (+ `net_api_tick`) |
| `todays`, `currentEvent`, `nextEvent`, `leftText`, `calData`, `titleOf`, `placeOf` | core `tb_app.c` queries |
| `checkIcal` | calendar `cal_url.c` (and the same checks in the Remote and setup pages' JavaScript) |
| `saveCalendar`, `removeCalendar`, Sync now, the 10-minute sync | calendar `esp/cal_sync.c` → `TB_EV_CAL_*` → core toasts |
| `toast`, `notify`, `screenFree`, `pendingToast`, `clearUnderToast` (hide small text under a toast), `withTitle` | core (toast text, pending, timing), ui (drawing, the hide rule, title cutting by measured width) |
| `showMenu`, `calTile`, `showTimerSettings`, `showWifiMenu`, `showPowerMenu`, `armMenuTimer`, `menuAction` | core `tb_menu.c` (tiles) and `tb_app.c` (actions, 8 s close) |
| pointer handlers on `#screen` | core `tb_gesture.c` + `tb_app_pointer`; ui reports raw touches and the tile under them |
| `btnBoot`, `pwrShort`, PWR down/up, `showHold`, `powerOff`, `powerOn`, `restart` | core (logic, timing, overlays) → effects → board (`board_power_off`, `board_restart`) |
| `flipBtn` | board IMU → core `tb_app_flip` |
| `togglePower`, `wake` (dark screen) | core (`off`) → `TB_FX_BACKLIGHT` 0 or the brightness |
| Light tile (40, 70, 100%) | core → settings.display.brightness → `TB_FX_BACKLIGHT` → board (perceptual curve, inverted PWM) |
| `chime`, `tickSound`, `ticking()` | core decides (`TB_FX_CHIME`, `TB_FX_TICKING`); board synthesizes and plays |
| `flash` | core `flash_at` → ui animates the white flash for 1.7 s |
| `view`, `pomoView`, `autoView`, `wifiView`, `sysRow`, `side`, splash | ui `view/ui_view.c` (words) + `src/ui.c` (layout) |
| `fitWords` with Bold Signal's ladder | ui `src/ui.c`: largest ladder size that fits 404 px (`lv_text_get_width`), marquee for long messages |
| `qrSvg` | ui: `lv_qrcode` with `WIFI:T:nopass;S:TinyBar-Setup;;` |
| `startSetup`, `skipWifi`, `wifiTap`, the setup page's Connect | core (screens) + net (AP, DNS, page, join) via `TB_FX_WIFI_*` and `TB_EV_WIFI` |
| The Remote panel (`#remoteMain`, `updateRemote`, `updateAutoRemote`) | net `web/remote.html` over `/api/v1/` |
| The setup page (`#setupView`) | net `web/setup.html` over `/api/v1/setup/` |
| "How the Mac app talks to TinyBar" | superseded by `docs/api.md` (net) |
| Style panel, design directions, Simulate, demo speed | mock-up only; not in the firmware |

## 9. Screens and the host snapshot tool

`ui_view.h` lists the layouts (status, alarm, message, setup QR, setup text, splash) and everything they show. The
snapshot tool (`components/ui/host/`) renders scenes with the real ui code and LVGL's software renderer into
640 × 172 RGB565 frames, written as PNG. **Implemented:** the 89 scenes in `components/ui/host/ui_scenes.c` cover the
list below (with the pairing round's screens: the pairing screen for each kind and near its end, the Wi-Fi menu's
three "pair at" feet and Full, Forget all with ten devices, and the toasts pairing ends with); `tools/ref_scenes.js` renders the same scenes from the mock-up
and `tools/compare.py` diffs them (see `components/ui/README.md`). Scenes to cover (compare each with the mock-up
rendered at 640 × 172 in headless Chromium per `docs/testing.md`):

- Available (with no calendar, "Free until" a meeting, "Rest of day"), Busy, In a meeting by hand (with and without
  Next), Away (plain, and with back-at and note), Message (short, long scrolling), Clock (each info-column variant:
  Wi-Fi off, not set up, calendar off, Next up with and without titles, Nothing).
- Pomodoro: ready, running focus (with tomatoes ripening), paused, short and long break, both waiting screens; the
  pill on another status (running, paused, done) for focus and a break.
- On a call (app name, no name, long name cut; during a meeting with progress), In a meeting from the calendar
  (titles off and on, private, with location and Next).
- Wi-Fi setup: QR, Connecting, Connected, Couldn't connect.
- Overlays: quick menu (each Calendar tile state, Show again), timer menu (with and without +5, Show again),
  timer settings, Wi-Fi menu, power menu, setup menu; a toast over each layout (and flipped), the hold screen
  (Keep holding, Powering off), the alarm flash, the splash, the set-aside glyphs, the Mac and Wi-Fi-off icons, the
  pairing screen and how pairing ends.

## 10. Network

- **Wi-Fi:** station with WPA2/WPA3 Personal, or WPA2-Enterprise (PEAP/MSCHAPv2, TTLS) through `esp_eap_client`.
  Credentials in NVS namespace `wifi`. Setup mode is APSTA: the open `TinyBar-Setup` AP at 4.3.2.1/24, a DNS
  catch-all, and the setup page; the station side tries the chosen network while the AP stays up, so the phone sees
  the result (api.md 13.3 `setup/state`). The page's list comes from one scan made just before the AP opens; while a
  phone is on the AP the bar scans again only if that list is empty, because an APSTA scan takes the radio off the
  AP's channel for a second or two. The AP opens in the order of ESP-IDF's captive_portal example: the DHCP server
  changed while it's stopped (address, the bar as DNS server, no option 114), then mode, AP settings and
  `esp_wifi_start()`; the DNS catch-all starts on `WIFI_EVENT_AP_START`. If the address can't be set, the AP stays
  closed (every request would get 421) and Set up again tries again. During "Set up again" that restart drops the
  office link: the calendar is told at once (`cal_sync_set_online(false)`); core, which shows setup, is told once the
  Connected screen moves on if the link is still down then, and the station reconnects. A Skip that lands while the
  AP is opening leaves the radio off (the worker checks again under the radio lock).
- **Why 4.3.2.1 (since 1.0.1):** what made the first Android test say "Connected, no internet" at 192.168.4.1 is
  **not confirmed**. The most likely cause: Android's captive-portal check (AOSP NetworkMonitor) has a rule, "a
  private IP DNS response means no internet" (flag `dns_probe_private_ip_no_internet`), that's off in stock AOSP but
  can be turned on by Google's server-side flags or forced on by the phone's maker
  (`config_force_dns_probe_private_ip_no_internet`). With it on, a check host (`connectivitycheck.gstatic.com`) that
  resolves into 10/8, 172.16/12, 192.168/16, 169.254/16 or an IPv6 ULA gets no HTTP check at all, and the phone
  reports "Connected, no internet" with no sign-in sheet, whatever the bar would have answered. Against it: ESP-IDF's
  own captive_portal example uses 192.168.4.1 and works on Android. 1.0.0 also differed from that example in ways
  1.0.1 removes too: it scanned just after the access point opened, offered an `http://` DHCP option 114, and started
  its DNS socket bound to the access point's address before the access point was up. Two tests tell the causes apart
  (net README, bring-up item 3): `adb shell dumpsys network_stack` on the phone (its validation log says "DNS
  response to the URL is private IP", or shows the HTTP probe's `ret=`), and ESP-IDF's stock example flashed on the
  same bar with the same phone. The setup network leads nowhere, so a public address only stands in for the hosts
  phones check while they're on it; 4.3.2.1 is the one many ESP32 captive portals use (WLED's, for one). It is a real,
  routed internet address (Lumen's), so a request that leaves the phone over mobile data instead (a typed
  `http://4.3.2.1` with mobile data on) reaches that network, not the bar; decisions.md (Wi-Fi) has the trade-off and
  the never-routed alternative, 192.0.2.1. It's `NET_SETUP_IP` (net_port.h).
- **Captive portal:** during setup, the phones' check paths (`/generate_204`, `/hotspot-detect.html`,
  `/connecttest.txt` and the others in `net_setup_probe_path()`) and any non-API request with a foreign `Host` get
  `302` to `http://4.3.2.1/` with a short HTML body, so phones open the sign-in sheet; API requests keep api.md's
  `421 wrong_host`. A request is on the setup network when the socket's own address is 4.3.2.1 **and** the peer is
  in 4.3.2.0/24 (lwIP reports an IPv4 client on the dual-stack listener as `::ffff:a.b.c.d`, which is unmapped). The
  peer matters because lwIP accepts a packet for 4.3.2.1 on any interface: while the AP is up next to the office link
  (the join, the Connected screen, the 15 s linger), an office host with a route to 4.3.2.1 through the bar would
  otherwise reach the setup endpoints, which take no token; a forged source address can't finish the TCP handshake.
  If the socket's address can't be read as IPv4, a peer in the setup subnet while the AP is up counts.
- **mDNS:** host `tinybar` (or what it gets after a conflict), `_tinybar._tcp` and `_http._tcp` on port 80 with the
  TXT record of api.md section 3; instance name = `device.name`.
- **HTTP:** `max_open_sockets` 7 with `lru_purge_enable`, header limit 2048 (sdkconfig), body limit 2048 (router),
  replies ≤ 8 KB (GET calendar lists as many meetings as fit); every API response carries `Content-Type:
  application/json; charset=utf-8`, `Cache-Control: no-store`, `X-Content-Type-Options: nosniff`, no CORS. Pages are
  served gzipped from flash. Every method under `/api/` reaches the router (`HTTP_ANY`), so OPTIONS and HEAD get its
  JSON 405 with Allow. The Remote page gets the API's Host check too (a plain-text 421); the setup network keeps its
  captive-portal 302.
- **One client can't hold the server task** (it runs every handler): each socket has a receive override that gives a
  request 3 s from its first byte for its headers and body (esp_http_server's own header parsing included: a request
  that runs over gets 408 or is dropped, and its socket closes), sends give up after 3 s without progress, and a body
  the handler didn't read (over 2 KB, or on a page request) closes the socket after the reply rather than being
  drained.
- **JSON nesting:** cJSON's limit is 16 for the whole build (`CJSON_NESTING_LIMIT`, set in `CMakeLists.txt` with
  `idf_build_set_property(COMPILE_DEFINITIONS)`; the compile commands show it on IDF's `cJSON.c`), and the router
  checks the same depth before parsing (`net_json_depth_ok`), so neither the parse nor `cJSON_Delete` can recurse
  deep on the app task's 12 KB stack (or usb_rx's 4 KB, whose busy-reply path parses for the `id`).
- **USB:** the USB Serial/JTAG driver with one writer for logs and protocol lines (a mutex; tracks whether the last
  byte was a line break; short write time-outs that drop log output rather than block when nobody reads). No line of
  log output starts with the `@tb ` marker: an `@` at the start of any line in it (after a line break inside one write,
  and after the ANSI color codes the Mac app strips) gets a space first (`net_log_scan`), since a logged SSID can
  carry a line break. The setup page's `setup/wifi` refuses an SSID with control characters too. Release builds log
  at Warning (`CONFIG_TINYBAR_RELEASE`). Never log tokens, the calendar address, Wi-Fi passwords or raw protocol
  lines.
- **Pairing** (api.md section 4, approved by the user on 2026-10-04): built behind `CONFIG_TINYBAR_API_AUTH_BEARER`
  (default) so `TINYBAR_API_AUTH_NONE` can still ship it off. Since the mock-up's pairing fix round (2026-10-05):
  a code on the screen for a device that isn't paired yet holds one of the 10 places (`has_room()` in `net_pair.c`),
  so a USB pairing of another new device meanwhile gets `token_limit` and `pair` refuses an eleventh only as a
  safeguard (the code ends, "Pairing canceled", not a failed pairing); `POST /api/v1/pair/cancel` ends the asking
  device's own code as a failed pairing and shares `pair`'s one-a-second limit; only a code typed right, Power off
  and Restart clear the back-off; `pair` no longer answers `in_setup` (setup ends any code, so it's `not_pairing`);
  and `info` reports `paired`, the count the Remote's prompt needs to clear its 10-device refusal (api.md 7.1).
- **Logging and secrets:** `cal_sync_init()` silences esp_http_client's own log tag (`HTTP_CLIENT`) for the whole
  firmware, because some of its messages print the URL, and the calendar address is a secret. A later feature that
  uses esp_http_client (OTA, say) won't see its log either.
- **Work login (PEAP/TTLS)** doesn't check the RADIUS server's certificate (office servers mostly use a private CA),
  and the Wi-Fi password is kept in plain NVS until encryption is agreed. Both are open decisions (section 13).
- **Power off on USB** is a deep sleep: the USB serial port disappears, and comes back on the next PWR press.
- **Skip is remembered** (`wifi/skipped` in NVS): an offline bar starts offline after Restart or power-on, with the
  radio off and no TinyBar-Setup network, as the mock-up's `powerOn()` keeps offline mode. Set up (the QR code) and a
  join that works clear it.

### Secrets

The calendar address and the token hashes live in the `nvs_sec` partition, apart from ordinary settings; the Wi-Fi
password and a work login's username and password are in `nvs/wifi`. **All of it is plain text in flash today, and
anyone with a laptop and a USB-C cable can read it in about a minute:** esptool resets the ESP32-S3 into download
mode through the same USB Serial/JTAG port (RTS and DTR), with no button press and without opening the case, then
reads the flash. A work login is often the person's company sign-in.

- **NVS encryption** (api.md section 15, the HMAC-based scheme) **burns an HMAC key into an eFuse block on first boot
  and can't be undone** on that board. It stops a passive dump only: without secure boot, someone could flash a
  dumping firmware that uses the same HMAC key (the chip still holds it), read the secrets, and flash TinyBar back.
  Switching it on is a config change (`CONFIG_NVS_ENCRYPTION`, `CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC`, the eFuse key
  id) plus `nvs_flash_secure_init_partition("nvs_sec")` (and the default partition) in `settings_store_init()`.
- **Full protection** needs flash encryption plus secure boot (signed images only), or disabling USB download mode and
  JTAG in eFuse. All of these are irreversible, and they change how the bar is updated (the web flasher at 0x0 would
  no longer work as-is).

It's off in `sdkconfig.defaults` until the user decides; decisions.md asks the question, and it should be settled
before the bar joins an office network with work-login credentials.

In RAM, copies of the calendar address are zeroed before they're freed (the setup page's pending address in
`net_wifi.c`, the fetch's and the check's copies and the saved one in `cal_sync.c`); esp_http_client's own copy of
the URL is freed by IDF without that. The masked form the API returns (host, file, the token's last four
characters) never holds the private token, even for feeds whose file name is the token (`cal_url.c`).

**Certificate dates aren't checked** (`CONFIG_MBEDTLS_HAVE_TIME_DATE` off, the IDF default; security review,
2026-10-04). With it on, mbedTLS would refuse a calendar server's certificate whenever the bar's clock reads outside
its validity, and the bar can't guarantee a valid clock before every fetch: a PUT's check (and the setup page's
address) runs as soon as the bar is online, before SNTP has answered, and the clock may then still be the 1970 of a
cold start (the RTC forgets the time whenever the bar is unplugged, since there's no battery, and its time is used
only with the oscillator-stop flag clear). SNTP is also unauthenticated, so anyone able to intercept the TLS
connection could set the clock as well. Turning it on would trade a real failure mode (a calendar that won't check
or sync until the clock is right) for little protection (only against an expired-but-compromised certificate). The
chain, the host name and the bundle's roots are still checked. Revisit if a later fetch path waits for SNTP (or a
trusted RTC) before connecting.

## 11. Memory plan

Measured on the integrated build (`idf.py size`, 2026-10-04) where it says so; the rest is from the builders'
reports or estimated, and the real heap figures come from the health log line on the board.

| Where | What | Size |
|---|---|---|
| Internal SRAM, static (measured) | code that must run from RAM (IDF, Wi-Fi, the IRAM-safe I2C ISR), `.data`, `.bss` | **141 KB of 342 KB DIRAM, so 201 KB is left for the heap** (141,159 bytes used, 200,601 free after the security review's fixes; it was 164 KB and 178 KB before the review round), plus the 16 KB IRAM block |
| Internal heap (estimated) | app task stack | 12 KB |
| | TinyBar's internal stacks: net 4, cal_sync 10, imu 4, audio 4 | 22 KB |
| | IDF's task stacks: wifi about 6.5, sys_evt 5, mdns 4, esp_timer 3.5, tiT 4, two idle 1.5 each, two ipc 1.25 each, the timer task 2 | about 30 KB |
| | display DMA chunk buffer (172 × 64 × 2) | 22 KB |
| | I2S DMA buffers | about 4 KB |
| | Wi-Fi and lwIP (what can't go to PSRAM with `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`) | about 50 to 60 KB |
| | the bus queue (32 × about 140 B), small allocations under 4 KB (`SPIRAM_MALLOC_ALWAYSINTERNAL`: HTTP header copies, bus jobs, the calendar's small ones) | about 10 to 15 KB |
| | IDF's 48 KB reserve for DMA and internal-only allocations (`SPIRAM_MALLOC_RESERVE_INTERNAL`) comes out of the same heap | |
| | **Total estimate** | about 155 to 175 KB of 201 KB, before fragmentation: the margin is real now but not large, so the health line's lowest free and largest block are what to watch on the board |
| PSRAM (8 MB) | one full frame buffer and the rotation buffer (2 × 215 KB; the second draw buffer is gone, since the flush is synchronous), the synthesized sounds (about 130 KB) | about 560 KB (775 KB before) |
| | everything LVGL allocates (`CONFIG_LV_USE_CUSTOM_MALLOC`, board/src/board_lv_mem.c): objects, styles, label text, the hold track's 24 KB layer, the QR canvas | about 120 to 140 KB |
| | stacks of tasks that never write flash: httpd 6, usb_rx 4, dns 4 | 14 KB |
| | `g_app` (`tb_app_t`, `EXT_RAM_BSS_ATTR`), and with `SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` IDF also puts lwIP's and the Wi-Fi libraries' `.bss` here | 11 KB + 15 KB |
| | TLS buffers (`MBEDTLS_EXTERNAL_MEM_ALLOC`, dynamic, 16 KB in + 4 KB out) | about 40 KB during a fetch |
| | ICS reader (`cal_feed_t`, fixed whatever the feed's size) and the last list (8 KB) | 28.3 KB during a fetch, 8 KB always |
| | HTTP and USB replies (≤ 8 KB each), cJSON trees (net puts cJSON in PSRAM), the meetings handed over | under 30 KB at a time |
| Flash (16 MB) | each app slot | 6 MB |
| | **the app image (measured, after the security review): 2.02 MB (0x204bb0 bytes), 66% of the 6 MB slot free.** Of it: LVGL 335 KB, ui 330 KB (fonts 184 KB, tomato images 124 KB, icons 5 KB, code 19 KB), Wi-Fi, lwIP, WPA and mDNS about 470 KB, mbedTLS 185 KB plus the certificate bundle 70 KB, net 62 KB (the gzipped pages 22 KB of it), calendar 40 KB (time-zone table 12 KB), core 23 KB, board 13 KB | 2.01 MB |

**LVGL runs from flash, not IRAM.** `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` put 107 KB of LVGL's drawing code in
internal RAM, which left only about 70 KB of internal heap for Wi-Fi, TLS, the task stacks and the display's DMA
buffer: not enough. It's off; the code runs from the flash cache like everything else. If the frame time measured
on the board is too slow (the bar redraws a few times a second; the marquee and the flash are the busiest), put back
only the few hottest functions, not the whole set.

Task stacks were checked with `-fstack-usage`: the deepest frames on the app task are the router's setup handler
(about 1 KB), `ui_update` (0.9 KB) and LVGL's drawing; 12 KB leaves room. The protocol-safe logger adds a 304 B frame
on top of newlib's `vsnprintf` to every task that logs, so the review round raised the tight ones for the first runs
(imu 4 KB, dns 4 KB, sys_evt 5 KB, tcpip 4 KB). The health line reports every task's margin by name.

Large allocations should name their heap (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`) rather than rely on the
4 KB threshold. Stacks of tasks that do flash writes (NVS) must stay in internal RAM, and so must the stacks of tasks
that do I2C (the ISR is IRAM-safe and may read their buffers while the cache is off).

## 12. Persistence (NVS)

| Partition / namespace | Key | Written by | When |
|---|---|---|---|
| `nvs` / `tinybar` | `settings` (version + `tb_settings_t`) | main | 2 s after the last settings change |
| `nvs` / `tinybar` | `state` (own status, last status, message, today's tomatoes, focused time, date) | main | 2 s after a change; and before power off and restart, always (with `settings`), so the focus minutes since the session started aren't lost (NVS skips an unchanged blob, so this costs no wear) |
| `nvs` / `wifi` | ssid, security, username, password (plain until NVS encryption is agreed) | net | on a successful join |
| `nvs` / `wifi` | `skipped`: Skip was the last Wi-Fi choice | net | on Skip; erased by Set up and a working join |
| `nvs` / `cal` | `list`: the last good meetings list and its sync time, packed (`cal_store.h`; a few hundred bytes, at most 4.7 KB) | calendar | when the list changes |
| `nvs_sec` / `calsec` | `url`: the calendar address (write-only to the outside) | calendar | on a successful check; erased on remove |
| `nvs_sec` / `tokens` | `table`: token hashes and records | net | on pairing, revoke, forget all, and (rate-limited) last-used updates |

The settings and state blobs are written with their padding zeroed, so NVS (which skips a write whose bytes haven't
changed) doesn't wear the flash for nothing. A blob whose version or size doesn't match (an older firmware's) is
ignored and the defaults apply. Away's back-at time and note aren't in the state blob yet: after a restart the bar
shows plain Away (core's `tb_app_restore` would need two more arguments).

Flash wear: the busiest key is `state` (a few writes an hour at most); `last_used` of tokens is written at most once
an hour per token.

## 13. Open decisions and assumptions (for the lead and the product manager)

**Waiting for the user or the product manager:**

1. **Pairing** (api.md 4 and decisions.md "Pairing"; approved by the user on 2026-10-04, and working on the bar as
   of 2026-10-05 in its 1.0.1 form): built, default on (`TINYBAR_API_AUTH_BEARER`). It follows the mock-up's pairing
   round and, since 2026-10-05, its fix round: the held place, `pair/cancel`, the back-off cleared only by a code typed
   right, Power off or Restart, a code stopping a flash under way, the Devices tile's measured "pair at" foot, and the
   Remote's prompt as the mock-up draws it. One contract detail the mock-up left open is the firmware's: `info.paired`
   (api.md 7.1), which the Remote reads to clear its 10-device refusal and to say "TinyBar forgot this phone".
2. **Stored secrets** (section 10): readable over the USB-C port with esptool in about a minute. NVS encryption alone
   stops only a passive dump; full protection (flash encryption with secure boot, or USB download mode off) is
   irreversible. Off until the user decides, which should be before the bar joins an office network with a work login.
3. **First boot status:** the mock-up opens on a sample Pomodoro. The firmware uses **Clock** (idle), with Available
   as the status Stop returns to.
4. **Today's tomatoes** reset at local midnight and survive restarts (the mock-up never sees midnight).
5. **Clock unknown** (no RTC time, no Wi-Fi, no Mac): the ui proposes kicker "Clock not set", headline "--:--", no
   time in the status row, end times left out, Next up and Free until "Not known" / "the clock isn't set yet".
6. **Wi-Fi dropped** after setup: the crossed-out Wi-Fi icon shows while the link is down (the mock-up shows it only
   when Wi-Fi was skipped).
7. **Characters the bar draws:** printable ASCII, Latin-1, en and em dash, ellipsis (`tb_text.h`). Messages with
   anything else get `400 unsupported_chars`; calendar titles show "?" for it (so an emoji reads "Café ? sync";
   dropping such characters might look better).
8. **Work-login certificates:** the bar doesn't check the RADIUS server's certificate, so a fake office network could
   capture the login exchange. Checking it needs the office's CA certificate, which the setup page would have to take.
9. **Busy events:** Google's Focus time and Out of office events count as meetings, since they show you as busy.
10. **Skipped rounds on the Remote:** `status` has no list of skipped rounds, so the Remote's tomato row can't show a
    skipped one pale the way the bar does (api.md change).
11. **A feed that's too large** (over 16 MB) has no error code of its own in api.md; it's reported as
    `calendar_unreachable` with "That calendar is too large for TinyBar to read." (for the Mac app team).
12. **New copy and behaviors from core** (section 7, items 2 to 8), and the ui's proposals (plain Away "Not at my
    desk", a message from another day "yesterday" / "Oct 1", an undrawable app name shown as "Mic or camera on").
13. **The Away "back at" input on the Remote** (api.md 14.3) still needs design.

**Decided by the lead at integration (2026-10-04):**

14. **62 px headline baseline: 108** (the mock-up as drawn; the browser floors the half-leading). Likewise the sub
    line and foot at 151 and the 78 px headline at 112, 1 px off the spec. Clock AM/PM: 36 px with 2 px tracking.
15. **The setup page's time zone always applies;** the Mac's `hello` only fills one in (section 6, "Time").
16. **USB or battery** can't be told on the V2 board (no sense pin), and power off doesn't need to: it lets go of
    SYS_EN, and if the bar is still running a moment later it's on USB and goes into deep sleep.
17. **LVGL out of IRAM** (section 11), for internal RAM; since the review round its allocations go to PSRAM too.
18. **Restart** is a real reboot: like the mock-up's `restart()`, the Pomodoro run resets and today's tomatoes stay.
19. **Skip is remembered** across restarts and power-on (section 10); **Message with nothing set stores "Hello"**, the
    mock-up's fallback, so the API reports what the bar shows (decisions.md, Proposed).

**Only the board can answer** (each component README's bring-up checklist says how to check):

20. Whether opening or closing the USB port from macOS resets the bar (DTR/RTS), and the USB serial number.
21. The IMU's address, axis and sign; which LVGL rotation is upright (90 is the example's never-run path:
    `CONFIG_TINYBAR_LCD_TURN_180` swaps it); the touch coordinates after a flip (both ways up); the touch edges.
22. The panel's colors and byte order, the frame time (now logged at INFO), and whether the marquee needs LVGL's
    direct render mode.
23. The backlight's dark point (duty 164 of 255 is calculated from the schematic) and any flash at power-up: on a
    cold power-up BL_EN floats high through its pull-up until the expander is set, so the backlight may glow briefly
    over an uninitialized panel (not after a deep-sleep wake). Boot time to SYS_EN high (the PSRAM test and the
    bootloader's INFO log are off for it).
24. Audio: volume in an open office, pops when the amplifier switches, hiss, and clicks while NVS writes flash.
25. Deep sleep: that PWR wakes it and that a held PWR doesn't; SYS_EN through a software restart; the RTC mark.
26. Internal heap and stack headroom under Wi-Fi, TLS and HTTP load (the health line), and the watchdog under load.
27. Wi-Fi: WPA3, PEAP/TTLS, captive-portal sheets on iOS and Android, the phone staying on the setup network while
    the bar joins, mDNS renaming, SNTP behind office firewalls; TLS to Google and iCloud with the certificate bundle.

## 14. Testing

What was run on 2026-10-04, after the security review's fixes (the review round's figures are in brackets where
they changed):

- **Firmware build:** `idf.py build` from a clean configuration in a fresh `build-lead/` (its sdkconfig generated
  from `sdkconfig.defaults` alone): no warnings at all (TinyBar's code and the managed components). App 2.02 MB
  (0x204bb0 bytes; was 0x203db0), 66% of the slot free; DIRAM 141,159 bytes used statically, 200,601 free. The
  compile commands show `-DCJSON_NESTING_LIMIT=16` on IDF's `cJSON.c`. The merged image is
  `dist/tinybar-<version>.bin` (README, "Flash"), checked byte for byte against the build (bootloader, partition
  table, OTA data and app, with 0xFF in the gaps), the partition table decoded, and the app's and bootloader's
  checksums and SHA-256 valid.
- **Host tests** (`test/host/`, ctest, AddressSanitizer and UBSan, no compiler warnings): 5 runners, 402 tests (370),
  all passing. The security review's checks are `test_sec_review.c` in `net/` (24: JSON nesting over HTTP and USB and
  its exact limit, cJSON's own limit in this build, which token may report which call, revoke and Forget all by
  token with USB left alone, a full Mac table keeping calls, the cookie's unreadable or empty Origin, the 8 KB
  calendar and status replies, the setup network's SSID and password rules, log output split at every byte never
  showing the marker to the Mac app's parser, the Host check, the JSON 405 for OPTIONS, HEAD and others) and in
  `calendar/` (8: the masked form for token-named files and seven providers' addresses, the per-day budget, ticks
  from inside one `cal_feed_write()` on a hostile feed). By module: core 150 (every cell of the controls table,
  Pomodoro and alarm scenarios, call and meeting priority, ticking, menus, Remote operations, Wi-Fi and pairing
  screens, bookkeeping, and the review round's rules: the Wi-Fi skip remembered, the setup menu after setup, the
  calendar tile with the link down, "Hello", pairing with a flip, an alarm and a phase end, a touch from before the code, USB pairing over a code, Forget all, and QA's sweep of every
  control in every state and a way back from every screen), calendar 67 (time zones against glibc, RRULE, DST,
  overrides, streaming one byte at a time, garbage, a 9 MB feed; the runner also works outside ctest), net 121 (every
  endpoint and error code of api.md, USB lines, the Mac table, pairing, empty names, the pairing clock, the paired
  names' order, Forget all), ui 29 (every screen's copy, overlays, the redraw key, the tomato frames against the
  mock-up's pixel for pixel, the fonts' character sets and tabular digits), board 35 (backlight curve, debouncing, flip
  detection, sound levels, RTC registers, touch mapping).
- **ui host tools** (`components/ui/host`): the touch test (straight and flipped) and the layout test (the sub line
  after the QR screen on its baseline and inside its column; the longest real copy in each slot without "…") pass; the
  snapshot tool renders 79 scenes; `tools/compare.py` against the mock-up (`tools/ref_scenes.js`) finds every text
  line of 76 scenes on the mock-up's baseline.
- **Pages:** the Playwright checks of the Remote (67, including the message's characters named as you type and a
  token-named feed's mask) and the setup page (20) against the fake bar (`host/fakebar.c`: the real router and core
  on Linux, with simulated Wi-Fi and calendar, masking addresses with the real `cal_url_check`) pass at 390 px. The ui
  host tools weren't run again in the security round (ui didn't change).
- **The pairing alignment (2026-10-05):** a fresh `build-lead-align/` from `sdkconfig.defaults`: no warnings; app
  2,134,352 bytes (0x209150), 66% of the slot free; DIRAM 141,287 bytes used statically, 200,473 free; the merged
  image `dist/tinybar-1.0.0.bin` checked as above (it reports `fw` 1.0.1). 434 host tests (core 158, calendar 67, net 144, ui 30, board 35) under
  ASan and UBSan with no warnings: 8 new in core (`test_pairing_fix.c`: the text mapping of api.md 2.3, the Devices
  tile's feet, Power off and Restart as a reset, the cancel endings, a code stopping a flash), 16 in net
  (`test_pairing_fix.c`: the held place against USB, the safeguard, `pair/cancel` by id with its limit, validation,
  methods and setup, the back-off and what clears it, `info.paired`, Appendix A and B) and 1 in ui (the pairing
  round's scenes). The ui's layout test checks the three "pair at" feet drawn on one line each inside the tile, and
  Forget all's ten names cut after two lines; 89 scenes, 86 compared with the mock-up with every text line on its
  baseline. The Remote's Playwright suite is 149 checks (the prompt's every refusal clearing with its cause, Cancel
  with `pair/cancel`, the code gone within 2 s, the focus and error ring, the list and foot, the Mac line, the hidden
  characters, Forget all, Remove this phone, removed on another phone, no answer); the setup page's 20 pass.
- **Not yet:** anything on the board. Then an end-to-end run of the controls table on the bar (QA).
