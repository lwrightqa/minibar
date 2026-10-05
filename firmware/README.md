# TinyBar firmware

ESP-IDF v5.4 and LVGL 9.5 firmware for TinyBar, the open-office status bar, on the **Waveshare ESP32-S3-Touch-LCD-3.49 V2**
(ESP32-S3R8, 8 MB octal PSRAM, 16 MB flash, AXS15231B 172 × 640 QSPI screen used as 640 × 172).

What it must do is decided elsewhere, and those documents win over anything here:

- `../docs/decisions.md`: every product and hardware decision, including the V2 pin map and power behavior.
- `../docs/mockup.html`: the screens, behavior, copy and timings, and the Bold Signal look.
- `../docs/api.md`: the HTTP and USB contract shared with the Mac app.

How the code is organized, and why, is in [ARCHITECTURE.md](ARCHITECTURE.md).

> **Status: integrated and reviewed; first run on the board under way.** Every module is built and wired together,
> and the 2026-10-04 review round's findings are fixed (or answered in decisions.md), as are the defensive security
> review's (ARCHITECTURE.md sections 10 and 14). On 2026-10-05 pairing was brought in line with the mock-up's pairing
> fix round (the held place, `pair/cancel`, the back-off rules, the Devices tile's "pair at" foot, and the Remote's
> prompt and Paired devices as the mock-up draws them). The firmware compiles with no warnings, 452 host tests pass,
> and every screen the mock-up can show matches it line for line. 1.0.0 was flashed once: it boots, holds power and
> draws the QR screen, and an Android phone couldn't get to the setup page. With 1.0.1 the phone sets up Wi-Fi and
> the Remote pairs with the code on the bar (which of 1.0.1's changes fixed the phone isn't confirmed; see "What's
> verified"), but its first frame came up upside down with the side buttons on top, which is how the bar stands,
> until it righted itself. 1.0.2 fixes that, confirmed on the bar (the picture is right straight away), and is
> merged here with the pairing alignment. **1.0.3** (2026-10-05) answers the review of that alignment: removing a Mac
> that is on a Wi-Fi call toasts once ("Removed Mac · back to Busy"), the Remote's prompt keeps its code across a
> reload and tells its own code from the next device's (`pairing_seq` in `info`), and the setup page's test polls
> instead of sleeping. 459 host tests and both page suites pass; nothing of it has run on the bar yet.
> Everything else that touches the hardware or the radio is unverified until it runs on the bar. See "What's
> verified" at the end, and the bring-up checklists in `components/board/README.md`, `components/net/README.md` and
> `components/calendar/README.md`.

## Layout

```text
firmware/
  CMakeLists.txt, sdkconfig.defaults, partitions.csv, dependencies.lock
  main/                 start-up order, the app task, NVS persistence               (lead)
  components/
    bus/                event queue between the tasks and the app task             (lead)
    core/               pure logic: status engine, Pomodoro, settings, menus,      (core builder)
                        gestures, formatting, the drawable character set
    ui/                 LVGL screens in Bold Signal; view/ is the pure view model;  (ui builder)
                        tools/ makes the tomato images at build time; host/ renders
                        screens to PNG on Linux; fonts/ holds the converted fonts
    net/                Wi-Fi, setup network, mDNS, HTTP API, Remote page, USB      (net builder)
                        serial protocol, SNTP; proto/ is the pure router, Mac table
                        and pairing; web/ is the Remote and setup pages
    calendar/           secret iCal address, streaming ICS reader, recurrence,     (calendar builder)
                        time zones (src/ is pure), the HTTPS sync service (esp/)
    board/              board support: power hold, display, touch, buttons, IMU,   (board builder)
                        RTC, audio, watchdog
  fonts/                Barlow TTF sources and their OFL license                   (ui builder)
  test/host/            Linux test runner (CMake + ctest); one folder per module   (lead; folders: builders)
  dist/                 the merged image to flash, tinybar-<version>.bin           (lead; not in git)
```

## Build

The toolchain lives at `/home/user/esp/esp-idf` (ESP-IDF v5.4.2 with the esp32s3 tools).
**Build in your own build directory**, never the shared default `build/`, because several people build at once:

```sh
. /home/user/esp/esp-idf/export.sh >/dev/null 2>&1
cd /home/user/tinybar/firmware
idf.py -B build-<name> -D SDKCONFIG=build-<name>/sdkconfig build
```

- The first build downloads the registry components into `managed_components/` and pins them in `dependencies.lock`
  (LVGL 9.5.0, esp_lcd_axs15231b 1.0.1, esp_codec_dev 1.5.11, mdns 1.14.0). The TCA9554 expander has its own small
  driver in board (the registry one resets every pin when it starts, which would drop the power hold).
  Don't change a component manifest without the lead: the lock file and `managed_components/` are shared.
- `sdkconfig.defaults` holds every setting we rely on. Your `build-<name>/sdkconfig` is generated from it; to pick up
  a change to the defaults, delete your `build-<name>/sdkconfig` (or the whole build directory).
- TinyBar's own options are under `idf.py menuconfig` → TinyBar (API authorization, release logging, app task stack)
  and TinyBar board (IMU axis, backlight dark point, volume, amplifier gating, QSPI clock, the bring-up self-test).
- **Release builds** (the image in `dist/`) are made by the lead from a clean configuration, in a fresh build directory
  of the lead's own (not the shared `build/`, since others may be building): `rm -rf build-lead && idf.py -B build-lead
  -D SDKCONFIG=build-lead/sdkconfig build`. A fresh `build-<name>/sdkconfig` is generated from `sdkconfig.defaults`
  alone, so nothing stale carries over.

### Host tests

Pure logic (core, calendar's `src/`, net's `proto/`, ui's `view/`) also builds on Linux with gcc:

```sh
cmake -S test/host -B build-host-<name>
cmake --build build-host-<name> -j
ctest --test-dir build-host-<name> --output-on-failure
```

The runner builds with AddressSanitizer and UBSan. Run one module's tests with `build-host-<name>/test_core`, or one
test with `build-host-<name>/test_core <name-substring>` (the calendar's runner finds its sample feeds on its own,
outside ctest too). Each module's tests live in `test/host/<module>/`.

### Screen snapshots on Linux

The ui builds against LVGL's software renderer on Linux and writes one PNG per screen, so screens can be compared
with the mock-up without hardware (needs `managed_components/` from any idf.py build):

```sh
cmake -S components/ui/host -B build-host-ui-<name>
cmake --build build-host-ui-<name> -j
build-host-ui-<name>/tinybar_snapshot <output-dir>        # 89 scenes; --list names them
ctest --test-dir build-host-ui-<name>                       # the touch test (straight and flipped) and the layout test
```

`components/ui/README.md` shows how to render the same scenes from the mock-up and compare them.

## Flash

Flash **one merged image at address 0x0, at 115200 baud**. A faster write once left the screen showing noise
(decisions.md, hardware notes).

1. Make the merged image (bootloader, partition table, OTA data and the app in one file), from your build directory:

   ```sh
   cd build-<name> && mkdir -p ../dist
   esptool.py --chip esp32s3 merge_bin -o ../dist/tinybar-1.0.3.bin @flash_args
   ```

   The version is `PROJECT_VER` in `CMakeLists.txt` (also what `GET /api/v1/info` reports as `fw`). The image in
   `dist/` today is `dist/tinybar-1.0.3.bin`, built on 2026-10-05 after the review of the pairing alignment (2.33 MB;
   the app 2.04 MB; it reports `fw` 1.0.3, this tree's `PROJECT_VER`). `dist/tinybar-1.0.0.bin` is the same image
   under the name the alignment round asked for; `dist/tinybar-1.0.2.bin` is the one the user has flashed.
2. Plug the bar into the computer with a USB-C **data** cable (a charge-only cable shows no port). If the Mac app is
   running, choose **Pause USB** in its menu first, so it lets go of the serial port.
3. Open the Espressif web flasher in Chrome or Edge (<https://espressif.github.io/esptool-js/>), set the baud rate to
   **115200**, click Connect and pick the "USB JTAG/serial debug unit" port. The flasher puts the chip into download
   mode through the port itself; if it can't, see Troubleshooting.
4. Add `dist/tinybar-1.0.3.bin` at flash address **0x0** and click Program.
5. Unplug and plug the bar back in (or press its reset), and it starts.

**Flashing the merged image starts the bar from scratch.** The file covers the whole start of the flash, and the gaps
between its parts are blank, so it also wipes the settings, the saved Wi-Fi, paired devices and the calendar address
(the NVS partitions at 0x9000 and 0x12000). The bar comes up on the Wi-Fi setup QR code, as on a first start. To
update while keeping all that, flash only the app instead: `build/tinybar.bin` at **0x30000** (the app slot), with the
same flasher and baud rate. Erase Flash isn't needed in either case (the merged image already clears the crash dump
too).

With the command line instead: `idf.py -p <port> -b 115200 flash` (keeps NVS too), then `idf.py -p <port> monitor`.

## First boot

1. The splash (a tomato and "TinyBar") shows for about 1.5 seconds. The bar holds its own power on from the first
   instructions, and draws the right way up whichever way it stands. Upright is with the side buttons (BOOT, PWR) on
   top; turned over, buttons at the bottom, the layout turns with it. Started lying flat, it draws the way it last
   stood (with nothing remembered yet, buttons on top).
2. With no Wi-Fi saved, it shows **Scan to set up** with a QR code. Scanning it joins the phone to the bar's own open
   network, **TinyBar-Setup** (it appears about 2 seconds after the QR code, once the bar has looked for networks),
   and the phone's sign-in sheet opens the setup page: the bar answers every name on that network with its own
   address and redirects the phone's check to `http://4.3.2.1/`. On Android, if the sheet doesn't open by itself
   (with mobile data on it often doesn't), pull down the notifications and tap "Sign in to Wi-Fi network". Otherwise
   open `http://4.3.2.1` in the phone's browser **with mobile data off**. *(Since 1.0.1 the setup address is 4.3.2.1,
   not 192.168.4.1: with a rule some Android phones have turned on, a private address means "Connected, no internet"
   and no sheet. That's the most likely, but unconfirmed, cause of what the first test saw; see decisions.md, Wi-Fi.
   4.3.2.1 is a real internet address, so with mobile data on, a typed `http://4.3.2.1` goes out over mobile data
   instead of to the bar.)*
3. On the page, pick the office Wi-Fi and enter its password, or a work username and password for a WPA2-Enterprise
   network. Guest networks with a sign-in page aren't supported, and the page says so. The calendar's secret iCal
   address can be pasted here too (optional).
4. The bar shows **Connecting**, then **Connected** with its address (`tinybar.local`), and moves on after 3 seconds
   or a tap. If it can't connect, it says why; a tap goes back to the QR code.
5. To use the bar without Wi-Fi, hold the screen on the QR code and choose **Skip**. Statuses and the Pomodoro work;
   the calendar and the Remote don't. **The bar remembers it:** after Restart or power-on it starts offline, with its
   radio off and no TinyBar-Setup network. Wi-Fi can be set up later from the quick menu (hold, Wi-Fi, Set up), which
   brings the QR code back.
6. On the office Wi-Fi, open `http://tinybar.local` on a phone or computer for the Remote. It asks you to pair first
   (below). The Wi-Fi menu (hold, Wi-Fi) shows the bar's name and real address, for example "TinyBar 2A1C" and
   `tinybar.local · 10.0.4.42` (`tinybar-2.local` if another bar has the name).
7. The Mac app finds the bar over USB (when the bar is powered from the Mac) or over Wi-Fi.

## Pairing the Mac app and a phone

Pairing (api.md section 4, decisions.md "Pairing"; approved by the user on 2026-10-04) is on in this build: nothing
controls the bar over Wi-Fi until it has paired once with a 6-digit code that only the bar's screen shows. The code
lasts 2 minutes from when it appears, allows 3 tries, and only one code shows at a time. After two failed pairings in
a row (timed out, canceled on the bar or by the device that asked, or out of tries) the bar refuses new codes for 30
seconds, doubling up to an hour; a code typed right, Power off or Restart clears that. A bar keeps 10 paired devices,
and a code on its screen for a new device holds one of the 10 places until it ends.

- **The Mac app, over USB:** plug the bar into the Mac once. The app pairs over the cable with no code (the cable is
  the proof), and the bar says "Paired · Mac · over USB". If another device's code is on the bar at that moment, the
  code stays up and the confirmation follows once that pairing ends; if that code holds the bar's last place, the Mac
  gets no Wi-Fi token yet (`token_limit`) but the cable keeps working.
- **The Mac app, over Wi-Fi:** in the TinyBar menu on the Mac, choose **Connect…**, then **Pair Over Wi-Fi**. The bar
  shows "PAIRING · MAC" with the code and "Type it on your Mac · tap to cancel"; type it in the app. The Mac's token
  can only report calls.
- **A phone (the Remote):** open `http://tinybar.local` and choose **Pair this phone**. The bar shows the code
  ("PAIRING · IPHONE", "Type it on your phone · tap to cancel"; the Remote names itself iPhone, iPad or Android phone
  when the browser says, otherwise the bar says Phone); type it on the phone, which pairs at the sixth digit. Cancel
  on the phone takes the code off the bar (`pair/cancel`). If the page reloads meanwhile (or the phone's browser
  discards the tab), it comes back to its field with the same code and countdown while that code is still on the
  bar. The phone gets full control, and the Remote lists every paired device, each with Remove (asked first, in
  place). While the bar is busy with another code or waiting after failed pairings, Pair this phone is dimmed and
  says how long; each such message goes as soon as its cause is over.
- **On the bar:** a tap, swipe, hold or BOOT cancels a code ("Pairing canceled"); a PWR press cancels it and darkens
  the screen; flipping the bar cancels it and does what a flip does. The info column counts down "Code expires in"
  over the bar's name, so in an office with several bars you can check it's the one you meant.
- **Forget them all:** hold, Wi-Fi, Devices ("3 paired", "Full" at 10), then **Forget all** (a second, deliberate
  tap: one in its first 600 ms is ignored; Keep goes back to the Wi-Fi menu). The bar says "Forgot 3 devices"; every
  device needs a new code, and a phone's Remote says "TinyBar forgot this phone". USB keeps working. With nothing
  paired the tile reads "None" and says where to pair: "pair at" over the bar's address (`tinybar.local`, or its IP
  address when a renamed host is too long for the tile).
- **Without pairing:** build with `CONFIG_TINYBAR_API_AUTH_NONE` (menuconfig → TinyBar) and the bar answers anyone
  on the office Wi-Fi, as the API's `"auth": "none"` (api.md 4.1).

## Troubleshooting

- **The flasher can't connect, or the port doesn't show up:** use a data cable, quit or pause the Mac app (Pause USB),
  and close any serial monitor. Then put the chip into download mode by hand: **hold BOOT while plugging the bar in**
  (or hold BOOT, press and release reset, then release BOOT), and connect again. After flashing, unplug and plug it
  back in to start normally.
- **The screen shows noise after flashing:** flash again at **115200** baud.
- **The bar came up on the Wi-Fi QR code and your settings are gone:** flashing the merged image at 0x0 wipes the
  settings, Wi-Fi, paired devices and the calendar address (see Flash). Flash only the app at 0x30000 to keep them.
- **The first frame is upside down, then the bar rights itself half a second later** (1.0.1 with the side buttons on
  top): the start-up reading couldn't tell which way up the bar stood. 1.0.2 waits for settled samples and calls
  buttons on top "upright"; its log line `IMU at start: ...` says what it read and how long it took
  (`components/board/README.md`, "Which way is up" and bring-up step 5).
- **The picture is upside down both ways up**, even after the bar has stood still: first check the build didn't warn
  that the IMU axis and turn aren't 1.0.2's (an sdkconfig from before 1.0.2: delete `build-<name>/sdkconfig` and
  rebuild). Otherwise change `CONFIG_TINYBAR_LCD_TURN_180` (menuconfig → TinyBar board; on by default since 1.0.2) and
  rebuild. If it's right one way up but doesn't turn after a flip, the IMU axis is wrong (bring-up step 5). The
  axis's sign and the turn go together: inverting both changes nothing once the IMU has a reading, only which pose
  counts as upright.
- **A computer opens msn.com instead of the setup page:** Windows sends its sign-in check out any other connection it
  has (a network cable or a dock), so the page it opens comes from Microsoft. Type `http://4.3.2.1` in the browser
  instead; that address always goes through the bar's own Wi-Fi.
- **No Remote at `tinybar.local`:** the bar may have been set up offline (Skip is remembered): hold, Wi-Fi, Set up.
  Some office networks block mDNS: use the address the Wi-Fi menu shows. Guest networks with a sign-in page aren't
  supported.
- **"Another device is pairing with this TinyBar":** a code for another device is on the bar; wait for it to run out
  (2 minutes at most), cancel it with a tap on the bar, or Cancel on the device that asked. The phone's message goes
  by itself once the code is gone.
- **"TinyBar 2A1C already has 10 paired devices":** remove one on a paired phone, or forget them all on the bar (hold,
  Wi-Fi, Devices). The message goes by itself once a place is free.
- **The phone joins TinyBar-Setup but says "Connected, no internet" and no sign-in sheet opens:** pull down the
  notifications and tap "Sign in to Wi-Fi network" if it's there. If not, open `http://4.3.2.1` in its browser with
  mobile data turned off (with mobile data on, Android sends the browser over it on a network it judged to have no
  internet, and 4.3.2.1 is a real internet address). The log tells which step failed, and `adb shell dumpsys
  network_stack` tells why the phone decided what it did: `components/net/README.md`, bring-up item 3.
- **The log:** `idf.py -p <port> monitor` at any time; protocol lines start with `@tb `. A health line 15 s after start
  (and every minute) shows memory, every task's stack margin and the frame time.

Controls are the mock-up's: tap for the next status (on the Pomodoro screen, start or pause), swipe for the previous
or next, hold for the quick menu; BOOT for the next status; a PWR press turns the screen off or on, holding PWR for 3 s
powers off (on USB, a deep sleep), and a press turns it back on; flip the bar over to turn the layout, silence the
alarm and start what the Pomodoro is waiting for.

## What's verified

**2026-10-05, 1.0.3: the review of the pairing alignment answered** (details in ARCHITECTURE.md section 14):

- **Compiled:** a fresh `build-lead-r4/` from `sdkconfig.defaults` alone (`-Y` up axis and the 180° turn set, so
  `board_imu.c`'s check stays quiet), with **no warnings**; the app reports `fw` 1.0.3. App 2,137,920 bytes
  (0x209f40, 2.04 MB; 66% of the 6 MB slot free; +912 bytes). Internal RAM: 141,319 bytes used statically (DIRAM),
  200,441 free for the heap (+8 bytes: the code counter). The Remote page is 25,135 bytes gzipped in flash (was
  24 KB). The merged image `dist/tinybar-1.0.3.bin` (2,334,528 bytes; `dist/tinybar-1.0.0.bin` is a byte-for-byte
  copy under the name the round asked for) was made with `esptool.py merge_bin @flash_args` and checked: the
  bootloader, partition table, OTA data and app byte for byte at 0x0, 0x8000, 0xF000 and 0x30000 with 0xFF in the
  gaps, the partition table decoded, and the app's and bootloader's checksums and SHA-256 valid (`image_info`:
  "App version: 1.0.3").
- **Tested on the host:** 459 tests in 5 runners under AddressSanitizer and UBSan, no warnings (core 158, calendar
  67, net 151, ui 30, board 53): the merge's 452 plus 7 in `net/test_align_fix.c` (removing a Mac on a Wi-Fi call,
  without a call, with its call set aside, and unpairing itself on a call, each toasting once; `pairing_seq` in
  `pair/start`, `info` and `hello`; `retry_after_s` 1 on `pair` and `pair/cancel` and a repeated cancel counting
  nothing; kind "device" in the list). The Remote's Playwright suite is now 163 checks (14 new: the next device's
  code within the poll noticed within about 2 s, a reload back to the field with the same countdown and Cancel
  still taking the code off, a reload under another device's code or after the code is gone starting over) and the
  setup page's 20 pass, also with the host tests running alongside. The ui host tools (snapshots, layout and touch
  tests) weren't run again: ui didn't change.
- **Unverified until it runs on the bar:** the toast after Remove on a real call from the Mac app, the Remote's
  reload on a real phone's browser (sessionStorage after a discarded tab on iOS Safari and Android Chrome), and
  everything in the lists below.

**2026-10-05, `orient-fix` merged into the main branch: 1.0.2 together with the pairing alignment** (details in
ARCHITECTURE.md section 14):

- **Compiled:** a fresh `build-merge/` from `sdkconfig.defaults` alone, after deleting every stale `sdkconfig`
  (`firmware/sdkconfig` and each `build-<name>/sdkconfig`, all from before 1.0.2), with **no warnings**: the
  generated sdkconfig has `CONFIG_TINYBAR_IMU_UP_Y_NEG=y` and `CONFIG_TINYBAR_LCD_TURN_180=y`, so `board_imu.c`'s
  stale-sdkconfig warning stays quiet, and the app reports `fw` 1.0.2. App 2,137,008 bytes (0x209bb0, 2.04 MB; 66% of
  the 6 MB slot free). Internal RAM: 141,311 bytes used statically (DIRAM), 200,449 free for the heap (was 141,287
  and 200,473). The merged image `dist/tinybar-1.0.2.bin` (2,333,616 bytes) was made with `esptool.py merge_bin
  @flash_args` and checked the same way as before: the bootloader, partition table, OTA data and app byte for byte at
  0x0, 0x8000, 0xF000 and 0x30000 with 0xFF in the gaps, the partition table decoded, and the app's and bootloader's
  checksums and SHA-256 valid.
- **Tested on the host:** 452 tests in 5 runners under AddressSanitizer and UBSan, no warnings (core 158, calendar 67,
  net 144, ui 30, board 53): the alignment round's 434 plus the orientation fix's 18 in `board/test_orient_start.c`.
  The merge needed no code beyond what the two branches carried: only this README conflicted (the status above and
  the troubleshooting entries on an upside-down picture), and `brd_logic.h`'s tick gains (0.55 Soft, 1.10 Medium)
  were already the same on both sides. The ui host tools and the Playwright page suites weren't run again; the
  orientation fix doesn't touch ui or the pages.
- **Verified on the bar (2026-10-05):** the user flashed the 1.0.2 app from `orient-fix` (commit 901c8c4, the
  app alone at 0x30000) and the first frame is the right way up with the buttons on top. That is the orientation
  side of this merge; the pairing alignment's list below is still unverified on the bar, and this merged image
  itself hasn't been flashed.

**2026-10-05, pairing aligned with the mock-up's pairing fix round** (details in ARCHITECTURE.md sections 10 and 14):

- **Compiled:** a fresh `build-lead-align/` from `sdkconfig.defaults` alone, with **no warnings**. App 2,134,352
  bytes (0x209150, 2.04 MB; 66% of the 6 MB slot free). Internal RAM: 141,287 bytes used statically (DIRAM), 200,473
  free for the heap (was 141,159 and 200,601). The Remote page is 24 KB gzipped in flash (was 17 KB). The merged
  image `dist/tinybar-1.0.0.bin` (2,330,960 bytes; the round asked for this file name, and it reports `fw` "1.0.1",
  the `PROJECT_VER` of this tree) was made with `esptool.py merge_bin @flash_args` and checked: the bootloader,
  partition table, OTA data and app byte for byte at 0x0, 0x8000, 0xF000 and 0x30000 with 0xFF in the gaps, the
  partition table decoded, and the app's and bootloader's checksums and SHA-256 valid. It doesn't have the 1.0.2
  orientation change, which was on the `orient-fix` branch until the merge above.
- **Tested on the host:** 434 tests in 5 runners under AddressSanitizer and UBSan, no warnings (core 158, calendar 67,
  net 144, ui 30, board 35; 25 new). The ui's layout test (the three "pair at" feet each on one line inside the tile,
  Forget all's ten names cut after two lines) and touch test; 89 screen snapshots, 86 compared with the mock-up with
  every text line on its baseline (the worst scene 7.9% of pixels, all glyph edges and the QR pattern). The Remote's
  Playwright suite (149 checks) and the setup page's (20) against the fake bar.
- **Unverified until it runs on the bar:** this round's pairing on the device (the held place against the Mac's USB
  pairing, `pair/cancel` from the phone, the back-off after Power off and Restart, a code arriving during the alarm's
  flash), the "pair at" foot as the panel draws it, the Remote's prompt on a real phone's browser (iPhone and Android
  naming, Safari's focus rings), and the Mac app's Back calling `pair/cancel` (the Mac app's own round).

As of 2026-10-04, after the review round and the security review (details in ARCHITECTURE.md section 14):

- **Compiled:** the whole firmware with `idf.py build` from a clean configuration (a fresh `build-lead/` generated
  from `sdkconfig.defaults`; ESP-IDF v5.4.2, esp32s3), with **no warnings**, in TinyBar's code or the managed
  components. The app is 2.02 MB in a 6 MB slot (66% free). 141 KB of internal RAM is used statically, leaving 201 KB
  for the heap (178 KB before the review round: the core model, LVGL's allocations and three task stacks moved to
  PSRAM; the security review's fixes changed neither).
  The merged image `dist/tinybar-1.0.0.bin` was made with `esptool.py merge_bin @flash_args` and checked: the
  bootloader and the app byte for byte where they belong, the partition table's entries, and the app's checksum and
  SHA-256 valid.
- **Tested on the host:** 402 tests in 5 runners under AddressSanitizer and UBSan (core 150, calendar 67, net 121,
  ui 29, board 35), with no compiler warnings; the ui's touch test through LVGL's input path (straight and flipped)
  and its layout test (screens drawn one after another; the longest real copy in each slot); the Remote (67 checks)
  and setup page (20) in Playwright against a fake bar running the real router and core; and 79 screen snapshots,
  76 of them compared with the mock-up rendered in Chromium: every text line on the mock-up's baseline, under 8% of
  pixels different in any scene (glyph edges, RGB565, and the QR code's pattern).
- **Unverified until it runs on the board:** everything that touches hardware or radio: power hold and power off
  (and the boot time to SYS_EN), the panel (colors, byte order, which rotation is upright, frame time), touch after a
  flip, buttons, the IMU's axis, the RTC, the backlight's dark point and skipping frames while it's off, audio levels
  and pops, I2C during flash writes, Wi-Fi (WPA2/3, work login, the setup network and captive-portal sheets, a skipped
  Wi-Fi staying off after a restart), mDNS, SNTP behind an office firewall, TLS to the calendar, whether opening the
  USB port resets the bar, heap and stack headroom (now logged per task), task stacks in PSRAM, and timing under load.
  From the security review: the HTTP server's 3 s request deadline and closing after a 413 (esp_http_server's receive
  override), the page's 421, the time a hostile calendar feed takes on the S3, and log escaping on the real port.
  The bring-up checklists say what to look for.
- **First run on the board (1.0.0, 2026-10-04):** it boots, holds power and draws the "Scan to set up" QR screen. An
  Android phone joined TinyBar-Setup but said "Connected, no internet", with no sign-in sheet. The cause isn't
  confirmed. **1.0.1** (branch `captive-fix`) addresses the most likely one, Android's private-IP rule, by moving the
  setup network to 4.3.2.1 (ARCHITECTURE.md section 10), and removes 1.0.0's other differences from ESP-IDF's
  captive_portal example: it opens after one scan and in the example's order, answers the phones' check paths,
  offers no DHCP option 114, and binds DNS to every address. It logs every step (net README, bring-up item 3, which
  also has the two tests that settle the cause if it still fails). After the review of 1.0.1: "Set up again" tells
  the calendar the office link is gone, a failed address change keeps the setup network closed, a Skip while it's
  opening leaves the radio off, the setup endpoints need a peer on the setup network as well as its address, the
  HTTP server starts before Wi-Fi, and the log never delays a DNS answer or shows a query string. Built clean with no
  warnings (app 2.03 MB; static internal RAM unchanged at 141 KB); 409 host tests (net 128) and both page suites
  pass. **Verified on the bar (2026-10-05):** the phone opens the setup page and the bar joins Wi-Fi.
- **1.0.2 (2026-10-05, branch `orient-fix`): the first frame the right way up.** On 1.0.1 the user saw the picture
  upside down at start-up, then right without turning the bar over. The user stands the bar with its side buttons on
  top, and that's upright now: the IMU's up axis is −Y and the picture is turned 180 degrees
  (`CONFIG_TINYBAR_IMU_UP_Y_NEG`, `CONFIG_TINYBAR_LCD_TURN_180`). The two inversions cancel once the IMU has a reading,
  so the steady pictures and flips are exactly as in 1.0.1; what changed is that a start where the IMU can't tell
  draws buttons on top. The start-up reading waits for real data (STATUS0's data-ready bit, past the datasheet's
  turn-on and filter settling, three 0.8 to 1.2 g samples in a row, 150 ms at most) instead of a fixed 40 ms, and
  logs the averaged x, y, z and the time it took. The last steady orientation is remembered in NVS (`board`/`pose`,
  written after 10 s standing still in a new one) for a start lying flat. Built clean with no warnings; 427 host tests
  pass (board 53, 18 of them new). Unverified until the user flashes it: the start-up log line will say what the
  first samples held, which the 1.0.1 report couldn't (no serial log). The −Y axis and rotation 270 are inferred
  from 1.0.1's steady pictures; that log line confirms them.
  **Delete your `build-<name>/sdkconfig` (and `firmware/sdkconfig`) before building 1.0.2.** An existing sdkconfig
  keeps 1.0.1's +Y, and one older than `CONFIG_TINYBAR_LCD_TURN_180` also takes the turn on: +Y with the turn draws
  every pose upside down. The build warns (`board_imu.c`) unless the pair is −Y with the turn.
