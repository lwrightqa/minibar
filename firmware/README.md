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
> review's (ARCHITECTURE.md sections 10 and 14). The firmware compiles with no warnings, 408 host tests pass, and every
> screen the mock-up can show matches it line for line. 1.0.0 was flashed once: it boots, holds power and draws the
> QR screen, and an Android phone couldn't get to the setup page, which 1.0.1 addresses (see "What's verified").
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
build-host-ui-<name>/tinybar_snapshot <output-dir>        # 79 scenes; --list names them
ctest --test-dir build-host-ui-<name>                       # the touch test (straight and flipped) and the layout test
```

`components/ui/README.md` shows how to render the same scenes from the mock-up and compare them.

## Flash

Flash **one merged image at address 0x0, at 115200 baud**. A faster write once left the screen showing noise
(decisions.md, hardware notes).

1. Make the merged image (bootloader, partition table, OTA data and the app in one file), from your build directory:

   ```sh
   cd build-<name> && mkdir -p ../dist
   esptool.py --chip esp32s3 merge_bin -o ../dist/tinybar-1.0.1.bin @flash_args
   ```

   The version is `PROJECT_VER` in `CMakeLists.txt` (also what `GET /api/v1/info` reports as `fw`). The image in
   `dist/` today is the 2026-10-04 security review's release build (2.31 MB; the app 2.02 MB).
2. Plug the bar into the computer with a USB-C **data** cable (a charge-only cable shows no port). If the Mac app is
   running, choose **Pause USB** in its menu first, so it lets go of the serial port.
3. Open the Espressif web flasher in Chrome or Edge (<https://espressif.github.io/esptool-js/>), set the baud rate to
   **115200**, click Connect and pick the "USB JTAG/serial debug unit" port. The flasher puts the chip into download
   mode through the port itself; if it can't, see Troubleshooting.
4. Add `dist/tinybar-1.0.1.bin` at flash address **0x0** and click Program.
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
   instructions, and draws the right way up whichever way it stands.
2. With no Wi-Fi saved, it shows **Scan to set up** with a QR code. Scanning it joins the phone to the bar's own open
   network, **TinyBar-Setup** (it appears about 2 seconds after the QR code, once the bar has looked for networks),
   and the phone's sign-in sheet opens the setup page: the bar answers every name on that network with its own
   address and redirects the phone's check to `http://4.3.2.1/`. If no sheet appears, open `http://4.3.2.1` in the
   phone's browser with mobile data off. *(Since 1.0.1 the setup address is 4.3.2.1, not 192.168.4.1: some Android
   phones treat a private address as "Connected, no internet" and never show the sheet; see decisions.md, Wi-Fi.)*
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

Pairing is **Proposed** (api.md section 4, decisions.md "Pairing") and on in this build: nothing controls the bar over
Wi-Fi until it has paired once with a 6-digit code that only the bar's screen shows. The code lasts 2 minutes from
when it appears, allows 3 tries, and only one code shows at a time. After two failed pairings in a row the bar
refuses new codes for 30 seconds, doubling up to an hour.

- **The Mac app, over USB:** plug the bar into the Mac once. The app pairs over the cable with no code (the cable is
  the proof), and the bar says "Paired · Mac · over USB". If another device's code is on the bar at that moment, the
  code stays up and the confirmation follows once that pairing ends.
- **The Mac app, over Wi-Fi:** in the TinyBar menu on the Mac, choose **Connect…**, then **Pair Over Wi-Fi**. The bar
  shows "PAIRING · MAC" with the code and "Type it on your Mac · tap to cancel"; type it in the app. The Mac's token
  can only report calls.
- **A phone (the Remote):** open `http://tinybar.local`, name the phone if you like, and choose **Show a code on
  TinyBar**. The bar shows the code ("Type it on your phone · tap to cancel"); type it on the phone. The phone gets
  full control, and the Remote lists every paired device, each with Remove.
- **On the bar:** a tap, swipe, hold or BOOT cancels a code ("Pairing canceled"); a PWR press cancels it and darkens
  the screen; flipping the bar cancels it and does what a flip does. The info column counts down "Code expires in"
  over the bar's name, so in an office with several bars you can check it's the one you meant.
- **Forget them all:** hold, Wi-Fi, Devices ("3 paired"), then **Forget all** (a second, deliberate tap; Keep goes
  back). Every device needs a new code; USB keeps working.
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
- **The picture is upside down whichever way the bar stands:** set `CONFIG_TINYBAR_LCD_TURN_180` (menuconfig →
  TinyBar board) and rebuild. If it's right one way up but turns the wrong way after a flip, pick the opposite sign of
  the IMU axis instead (`components/board/README.md`, bring-up step 5).
- **No Remote at `tinybar.local`:** the bar may have been set up offline (Skip is remembered): hold, Wi-Fi, Set up.
  Some office networks block mDNS: use the address the Wi-Fi menu shows. Guest networks with a sign-in page aren't
  supported.
- **"Another device is pairing with this TinyBar":** a code for another device is on the bar; wait for it to run out
  (2 minutes at most) or cancel it with a tap on the bar.
- **The phone joins TinyBar-Setup but says "Connected, no internet" and no sign-in sheet opens:** open
  `http://4.3.2.1` in its browser with mobile data turned off (with mobile data on, Android sends the browser over it
  on a network it judged to have no internet). The log tells which step failed: `components/net/README.md`, bring-up
  item 3.
- **The log:** `idf.py -p <port> monitor` at any time; protocol lines start with `@tb `. A health line 15 s after start
  (and every minute) shows memory, every task's stack margin and the frame time.

Controls are the mock-up's: tap for the next status (on the Pomodoro screen, start or pause), swipe for the previous
or next, hold for the quick menu; BOOT for the next status; a PWR press turns the screen off or on, holding PWR for 3 s
powers off (on USB, a deep sleep), and a press turns it back on; flip the bar over to turn the layout, silence the
alarm and start what the Pomodoro is waiting for.

## What's verified

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
  Android phone joined TinyBar-Setup but said "Connected, no internet", with no sign-in sheet. **1.0.1** (branch
  `captive-fix`) answers that: the setup network moved to 4.3.2.1 (Android's portal check gives up on a private DNS
  answer; ARCHITECTURE.md section 10), it opens after one scan and in the order of ESP-IDF's captive_portal example,
  it answers the phones' check paths, it offers no DHCP option 114, and it logs every step (net README, bring-up
  item 3). Built clean with no warnings (app 2.03 MB; static internal RAM unchanged at 141 KB); 408 host tests (net
  127) and both page suites pass. Unverified until the user flashes it.
