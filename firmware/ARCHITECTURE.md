# TinyBar firmware architecture

Lead developer, 2026-10-04. Read with `../docs/decisions.md` (what), `../docs/mockup.html` (how it behaves and looks)
and `../docs/api.md` (the wire contract). Where this file and those disagree, they win; tell the lead.

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
| imu | board | 0 | 3 | 3 KB | QMI8658 at about 25 Hz; posts `TB_EV_ORIENTATION` after 0.5 s of a steady new orientation |
| audio | board | 1 | 6 | 4 KB | Writes the chime and tick PCM to I2S; one tick a second while ticking is on |
| httpd (IDF) | net | 0 | 5 | 6 KB | HTTP handlers: parse, then `tb_bus_exec(router)`; serves the gzipped pages directly |
| usb_rx | net | 0 | 4 | 4 KB | Reads "@tb " lines (2 KB max), `tb_bus_exec(net_api_usb_line)`, writes the reply |
| dns | net | 0 | 3 | 3 KB | Setup mode only: answers every name with 192.168.4.1 |
| cal_sync | calendar | 0 | 2 | 10 KB, internal | HTTPS fetch streamed through the ICS reader; one at a time |
| wifi, tcpip, mdns, sntp, sys_evt | IDF | 0 | IDF defaults | IDF | |

- The app task loops every 5 to 33 ms (what `lv_timer_handler` asks for). Each loop: drain the bus, poll the hold
  gesture, `tb_app_tick`, `net_api_tick`, run effects, `ui_update`, `lv_timer_handler` (which reads the touch panel
  and flushes), `settings_store_poll`, feed the watchdog.
- **LVGL has no OS layer** (`CONFIG_LV_OS_NONE`): only the app task calls it. The flush callback blocks on the DMA
  semaphore inside the app task. `board_display_lock()` exists for emergencies; no other task should need it.
- Wi-Fi and the network stack stay on core 0, the screen on core 1, so a slow TLS handshake never stalls a frame.
- **Watchdog:** the task watchdog (10 s, panic and restart) watches the idle tasks, the app task, and should watch
  usb_rx and cal_sync between blocking calls. A crash leaves a core dump in the `coredump` partition.
- **Router latency:** `tb_bus_exec` waits up to 900 ms for the app task to start the job (api.md's 1 s reply rule);
  if it can't, the job is canceled and the client gets `503 busy`. A job that has started always finishes before
  `tb_bus_exec` returns, so the request and reply buffers can live on the handler's stack.

## 4. Start-up order (`main/app_main.c`)

1. `board_power_hold()`: system I2C bus, TCA9554, **SYS_EN high**. First, before anything slow: on battery the
   board turns itself off again unless SYS_EN is held. `board_backlight_early_off()` keeps the panel dark.
2. `tb_bus_init()`, `net_usb_init()`: from here every log line goes through the protocol-safe USB writer.
3. NVS (`nvs` and `nvs_sec`), the device id (Wi-Fi MAC), settings, and the time zone (`setenv("TZ")`).
4. `board_imu_init()` and one reading, so the first frame is drawn the right way up.
5. `lv_init()`, `board_display_init(flipped)`, `board_touch_init()`, `ui_init()`.
6. `board_rtc_init()`: the system clock from the RTC if it holds a valid time.
7. `tb_app_init()` (booting = true: the splash), `settings_store_restore()`, `tb_app_flip(initial)`.
8. `app_task_start()`. From here only the app task touches the model, LVGL and the ui. The backlight comes on with
   the first frame (the initial `TB_FX_BACKLIGHT`).
9. `board_buttons_init()`, `board_audio_init()`, `board_imu_start()`.
10. `net_init()`, `net_start()` (station or setup mode, HTTP, mDNS, SNTP, the USB reader and its `ready` line),
    `cal_sync_init()` (posts the saved meetings, then waits for Wi-Fi).
11. `esp_ota_mark_app_valid_cancel_rollback()` (later: only after a self-test passes).

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
| `TB_EV_EXEC` | `tb_bus_exec` | fn, ctx | runs fn on the app task (the router) |

The queue holds 32 events and posts never block; a full queue drops the event and counts it (`tb_bus_dropped()`).

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
Origin, If-None-Match) and the body (≤ 2 KB) into a `net_req_t` → `tb_bus_exec(net_api_handle)` on the app task →
the router checks host, rate limits, token and scope, validates, calls `tb_app_remote_*` (or the calendar and Wi-Fi
ports) and builds the JSON reply (≤ 8 KB) → `net_http.c` sends it with the headers api.md requires.

**API over USB.** `usb_rx` assembles lines → `tb_bus_exec(net_api_usb_line)` → the same router (`hello`, `call`,
`status`, `pair`, `request`) → one "@tb " reply line, written whole through the shared writer.

**A Mac's call.** `POST /api/v1/call` or USB `call` → `net_macs_on_call()` (per-Mac state, staleness, call ids,
elapsed time) → `net_macs_aggregate()` → `tb_app_set_call()` → core's `syncAuto()`: takeover, pause the Pomodoro,
hold a ringing alarm, toasts. Every app loop `net_api_tick()` runs the 90 s time-outs ("Lost contact with your Mac").

**Calendar.** `cal_sync` task: HTTPS GET with the certificate bundle → each chunk into `cal_feed_write()` (line
unfolding, VEVENTs, RRULE expansion in the window, EXDATE, overrides) → `cal_feed_finish()` → today's and tomorrow's
meetings that count → `TB_EV_CAL_MEETINGS` → core starts and ends In a meeting at each event's exact times from that
list; syncs every 10 minutes only refresh it. The list is saved in NVS when it changes.

**Time.** RTC at boot → SNTP once online (→ RTC) → or the Mac's `hello` time when there's been no network time for
24 h. The time zone (IANA, from the setup page or the Mac) becomes a POSIX rule through calendar's table.

## 7. The core model (`components/core/include/tb_app.h`)

`tb_app_t` is the mock-up's `s`, `pomo`, `cal` and `mac` in one struct, plus the overlays (menu, toast, pending
toast, PWR hold, pairing) and the effect queue. It's transparent so ui and net can read it; only core writes it.
`rev` goes up on every visible change and is also the API's `rev` and ETag.

Firmware additions the mock-up doesn't have (each flagged in section 13):
- the wall clock may be unknown (`tb_clock_t.valid`);
- today's tomatoes and focused time reset at local midnight (`tb_pomo_roll_day`) and survive a restart (NVS);
- Wi-Fi can drop after setup (`wifi_link_up`), apart from being skipped (`TB_WIFI_OFFLINE`);
- the pairing screen and the Devices tile (api.md 4.8, proposed);
- Away's back-at time and note (api.md 8.1, proposed).

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
640 × 172 RGB565 frames, written as PNG. Scenes to cover (compare each with the mock-up rendered at 640 × 172 in
headless Chromium per `docs/testing.md`):

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
  pairing screen (once designed).

## 10. Network

- **Wi-Fi:** station with WPA2/WPA3 Personal, or WPA2-Enterprise (PEAP/MSCHAPv2, TTLS) through `esp_eap_client`.
  Credentials in NVS namespace `wifi`. Setup mode is APSTA: the open `TinyBar-Setup` AP at 192.168.4.1, a DNS
  catch-all, and the setup page; the station side scans and tries the chosen network while the AP stays up, so the
  phone sees the result (api.md 13.3 `setup/state`).
- **Captive portal:** during setup, any non-API request with a foreign `Host` gets `302` to `http://192.168.4.1/`
  so phones open the sign-in sheet; API requests keep api.md's `421 wrong_host`.
- **mDNS:** host `tinybar` (or what it gets after a conflict), `_tinybar._tcp` and `_http._tcp` on port 80 with the
  TXT record of api.md section 3; instance name = `device.name`.
- **HTTP:** `max_open_sockets` 7 with `lru_purge_enable`, header limit 2048 (sdkconfig), body limit 2048 (router),
  replies ≤ 8 KB; every API response carries `Content-Type: application/json; charset=utf-8`,
  `Cache-Control: no-store`, `X-Content-Type-Options: nosniff`, no CORS. Pages are served gzipped from flash.
- **USB:** the USB Serial/JTAG driver with one writer for logs and protocol lines (a mutex; tracks whether the last
  byte was a line break; short write time-outs that drop log output rather than block when nobody reads). Release
  builds log at Warning (`CONFIG_TINYBAR_RELEASE`). Never log tokens, the calendar address, Wi-Fi passwords or raw
  protocol lines.
- **Pairing** (api.md section 4, **Proposed**): built behind `CONFIG_TINYBAR_API_AUTH_BEARER` (default) so
  `TINYBAR_API_AUTH_NONE` can ship it off if the user says no.

### Secrets

The calendar address and the token hashes live in the `nvs_sec` partition, apart from ordinary settings. api.md
section 15 asks for NVS encryption with the HMAC-based scheme, which **burns an HMAC key into an eFuse block on first
boot and can't be undone** on that board. It is off in `sdkconfig.defaults` until the user agrees; switching it on is
a config change (`CONFIG_NVS_ENCRYPTION`, `CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC`, the eFuse key id) plus
`nvs_flash_secure_init_partition("nvs_sec")` in `settings_store_init()`.

## 11. Memory plan

| Where | What | Size |
|---|---|---|
| Internal SRAM (about 512 KB, about 300 KB free after Wi-Fi) | app task stack | 12 KB |
| | `g_app` (`tb_app_t`, mostly the 32 meetings) | about 10 KB |
| | bus queue (32 × about 140 B) | 4.5 KB |
| | display DMA bounce buffer (172 × 64 × 2) | 22 KB |
| | task stacks: httpd 6, usb_rx 4, cal_sync 10, imu 3, audio 4, dns 3 | 30 KB |
| | I2S DMA buffers | about 4 KB |
| | Wi-Fi and lwIP (static parts; dynamic buffers go to PSRAM with `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`) | about 60 KB |
| PSRAM (8 MB) | two full frame buffers (640 × 172 × 2) + the rotation buffer | 660 KB |
| | LVGL objects and label text (`LV_USE_CLIB_MALLOC`: malloc over 4 KB goes to PSRAM) | about 100 KB |
| | TLS buffers (`MBEDTLS_EXTERNAL_MEM_ALLOC`, dynamic, 16 KB in + 4 KB out) | about 40 KB during a fetch |
| | ICS reader (`cal_feed_t`: line buffer, 64 candidates, 256 override keys) | about 30 KB during a fetch |
| | HTTP and USB replies (≤ 8 KB each), cJSON trees, the meetings handed over | under 30 KB at a time |
| Flash (16 MB) | each app slot | 6 MB |
| | expected app image: IDF + Wi-Fi + mbedTLS about 1.3 MB, LVGL about 0.3 MB, fonts about 0.6 to 1.2 MB at 4 bpp (to measure), tomato images about 110 KB (27 ARGB8888 sprites; RGB565A8 would be 82 KB), pages under 60 KB gzipped, time-zone table about 30 KB | about 2.5 to 3 MB |
| | the skeleton today | 0.72 MB |

Large allocations should name their heap (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`) rather than rely on the
4 KB threshold. Stacks of tasks that do flash writes (NVS) must stay in internal RAM.

## 12. Persistence (NVS)

| Partition / namespace | Key | Written by | When |
|---|---|---|---|
| `nvs` / `tinybar` | `settings` (version + `tb_settings_t`) | main | 2 s after the last settings change |
| `nvs` / `tinybar` | `state` (own status, last status, message, today's tomatoes, focused time, date) | main | 2 s after a change; and before power off and restart |
| `nvs` / `wifi` | ssid, security, username, password | net | on a successful join |
| `nvs` / `cal` | the last good meetings list and its sync time | calendar | when the list changes |
| `nvs_sec` / `calsec` | the calendar address (write-only to the outside) | calendar | on a successful check; erased on remove |
| `nvs_sec` / `tokens` | token hashes and records | net | on pairing, revoke, forget all, and (rate-limited) last-used updates |

Flash wear: the busiest key is `state` (a few writes an hour at most); `last_used` of tokens is written at most once
an hour per token.

## 13. Open decisions and assumptions (for the lead and the product manager)

1. **Pairing** (api.md 4, Proposed): built, default on (`TINYBAR_API_AUTH_BEARER`); the pairing screen and the Devices
   tile still need the UX designer's drawings (api.md 14.3).
2. **NVS encryption** burns an eFuse key (section 10); off until the user agrees.
3. **First boot status:** the mock-up opens on a sample Pomodoro. The firmware proposes **Clock** (idle), with
   Available as the status Stop returns to.
4. **Today's tomatoes** reset at local midnight and survive restarts (the mock-up never sees midnight).
5. **Clock unknown** (no RTC time, no Wi-Fi, no Mac): times and the Clock screen need a "not set" look; ui to propose.
6. **Wi-Fi dropped** after setup: proposed to show the crossed-out Wi-Fi icon while the link is down (the mock-up
   shows it only when Wi-Fi was skipped).
7. **Characters the bar draws:** printable ASCII, Latin-1, en and em dash, ellipsis (`tb_text.h`). Messages with
   anything else get `400 unsupported_chars`; calendar titles show "?" for it.
8. **62 px headline baseline:** the spec says 107, the mock-up's CSS gives 108. ui to pick one on the board.
9. **Unverified on hardware:** whether opening or closing the USB port from macOS resets the bar (DTR/RTS), the
   USB serial number, the IMU's axis and sign, the touch coordinates after rotation, the backlight curve, audio levels
   in an open office, how to tell USB from battery power.
10. **Restart** is a real reboot: like the mock-up's `restart()`, the Pomodoro run resets and today's tomatoes stay.

## 14. Testing

- **Host (every module with logic):** `test/host/` builds core, calendar, net's `proto/` and ui's `view/` with
  AddressSanitizer and UBSan; each builder adds tests in their folder. Target: every mock-up handler and every row of
  the mock-up's controls table as a core test; RRULE, time-zone and override cases (with real Google exports as
  fixtures) for calendar; every endpoint and error code of api.md for net, including USB lines; the copy of every
  screen for ui's view.
- **Screens:** the snapshot tool's PNGs against the mock-up.
- **Device:** a bring-up checklist per board feature (board builder), then an end-to-end run of the controls table
  on the bar (QA).
