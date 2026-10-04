# TinyBar firmware

ESP-IDF v5.4 and LVGL 9.5 firmware for TinyBar, the open-office status bar, on the **Waveshare ESP32-S3-Touch-LCD-3.49 V2**
(ESP32-S3R8, 8 MB octal PSRAM, 16 MB flash, AXS15231B 172 × 640 QSPI screen used as 640 × 172).

What it must do is decided elsewhere, and those documents win over anything here:

- `../docs/decisions.md`: every product and hardware decision, including the V2 pin map and power behavior.
- `../docs/mockup.html`: the screens, behavior, copy and timings, and the Bold Signal look.
- `../docs/api.md`: the HTTP and USB contract shared with the Mac app.

How the code is organized, and why, is in [ARCHITECTURE.md](ARCHITECTURE.md).

> **Status: skeleton.** The project builds and the host tests run, but most module bodies are stubs marked
> `TODO(<module>)`. Nothing has run on a board yet. See "What's verified" at the end.

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
  (LVGL 9.5.0, esp_lcd_axs15231b 1.0.1, esp_io_expander_tca9554 2.0.3, esp_codec_dev 1.5.11, mdns 1.14.0).
  Don't change a component manifest without the lead: the lock file and `managed_components/` are shared.
- `sdkconfig.defaults` holds every setting we rely on. Your `build-<name>/sdkconfig` is generated from it; to pick up
  a change to the defaults, delete your `build-<name>/sdkconfig` (or the whole build directory).
- TinyBar's own options are under `idf.py menuconfig` → TinyBar (API authorization, release logging, app task stack).

### Host tests

Pure logic (core, calendar's `src/`, net's `proto/`, ui's `view/`) also builds on Linux with gcc:

```sh
cmake -S test/host -B build-host-<name>
cmake --build build-host-<name> -j
ctest --test-dir build-host-<name> --output-on-failure
```

The runner builds with AddressSanitizer and UBSan. Run one module's tests with `build-host-<name>/test_core`, or one
test with `build-host-<name>/test_core <name-substring>`. Each module's tests live in `test/host/<module>/`.

### Screen snapshots on Linux

The ui builds against LVGL's software renderer on Linux and writes one PNG per screen, so screens can be compared
with the mock-up without hardware (needs `managed_components/` from any idf.py build):

```sh
cmake -S components/ui/host -B build-host-ui-<name>
cmake --build build-host-ui-<name> -j
build-host-ui-<name>/tinybar_snapshot <output-dir>
```

## Flash

Flash **one merged image at address 0x0, at 115200 baud**. A faster write once left the screen showing noise
(decisions.md, hardware notes).

1. Make the merged image (bootloader, partition table, OTA data and the app in one file):

   ```sh
   idf.py -B build-<name> -D SDKCONFIG=build-<name>/sdkconfig merge-bin -o tinybar-merged.bin
   ```

   It lands in `build-<name>/tinybar-merged.bin`.
2. Plug the bar into the computer with a USB-C data cable. If the Mac app is running, choose **Pause USB** in its
   menu first, so it lets go of the serial port.
3. Open the Espressif web flasher in Chrome or Edge (<https://espressif.github.io/esptool-js/>), set the baud rate to
   **115200**, click Connect and pick the "USB JTAG/serial debug unit" port.
4. Add `tinybar-merged.bin` at flash address **0x0** and click Program. For a first install, or to wipe settings,
   Wi-Fi and pairings, click Erase Flash first.
5. Unplug and plug the bar back in (or press its reset), and it starts.

With the command line instead: `idf.py -B build-<name> -p <port> -b 115200 flash`, then `idf.py -p <port> monitor`.
If the board doesn't enter download mode by itself, hold BOOT while plugging it in.

## First boot

1. The splash (a tomato and "TinyBar") shows for about 1.5 seconds. The bar holds its own power on from the first
   instructions, and draws the right way up whichever way it stands.
2. With no Wi-Fi saved, it shows **Scan to set up** with a QR code. Scanning it joins the phone to the bar's own open
   network, **TinyBar-Setup**, and the phone's sign-in sheet opens the setup page (the bar answers every name on that
   network and redirects it to `http://192.168.4.1/`).
3. On the page, pick the office Wi-Fi and enter its password, or a work username and password for a WPA2-Enterprise
   network. Guest networks with a sign-in page aren't supported, and the page says so. The calendar's secret iCal
   address can be pasted here too (optional).
4. The bar shows **Connecting**, then **Connected** with its address (`tinybar.local`), and moves on after 3 seconds
   or a tap. If it can't connect, it says why; a tap goes back to the QR code.
5. To use the bar without Wi-Fi, hold the screen on the QR code and choose **Skip**. Statuses and the Pomodoro work;
   the calendar and the Remote don't. Wi-Fi can be set up later from the quick menu (hold, Wi-Fi, Set up).
6. On the office Wi-Fi, open `http://tinybar.local` on a phone or computer for the Remote. (Pairing, proposed in
   api.md section 4, asks you to type a code the bar shows.)
7. The Mac app finds the bar over USB (when the bar is powered from the Mac) or over Wi-Fi.

Controls are the mock-up's: tap for the next status (on the Pomodoro screen, start or pause), swipe for the previous
or next, hold for the quick menu; BOOT for the next status; a PWR press turns the screen off or on, holding PWR for 3 s
powers off (on USB, a deep sleep), and a press turns it back on; flip the bar over to turn the layout, silence the
alarm and start what the Pomodoro is waiting for.

## What's verified

- **Compiled:** the whole firmware with `idf.py build` (ESP-IDF v5.4.2, esp32s3), with no warnings in TinyBar's code.
  The merged image builds.
- **Tested on the host:** the seed tests in `test/host/` (settings defaults and PATCH checks, the time formatters,
  civil-date arithmetic, the router's `GET /api/v1/info`, the view model's color), and the snapshot tool renders.
- **Unverified until it runs on the board:** everything that touches hardware or radio: power hold, the panel and
  its rotation, touch coordinates after a flip, buttons, the IMU's axis, the RTC, audio levels, Wi-Fi and
  WPA2-Enterprise, mDNS names, whether opening the USB port resets the bar, and timing under load.
