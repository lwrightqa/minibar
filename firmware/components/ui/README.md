# ui: TinyBar's screens in Bold Signal (owner: ui builder)

The ui draws the core model (`tb_app_t`) with LVGL 9 and reports touches. It never changes the model. It runs on the
app task (`main/app_task.c`): `ui_init()` once, `ui_update()` every loop, touches go to the callback set with
`ui_set_pointer_cb()`.

| Path | What | Builds on Linux |
|---|---|---|
| `include/ui_view.h`, `view/ui_view.c` | What every screen says, as data: a port of the mock-up's `view()`, `pomoView()`, `autoView()`, `wifiView()`, `sysRow()`, `side()`, the splash, hold and pairing screens, the overlays and the redraw key | yes (no LVGL) |
| `include/ui_theme.h`, `view/ui_theme.c` | Bold Signal as numbers: palette, positions, baselines, font roles | yes |
| `include/ui.h`, `src/ui.c` | The LVGL object tree and its updates: the look | with LVGL |
| `src/ui_fonts.c`, `fonts/` | The 15 converted Barlow fonts (see `fonts/README.md`) | with LVGL |
| `src/ui_icons.c` | 16 px Wi-Fi, Wi-Fi off, Mac, headset and calendar icons, rasterized from the mock-up's SVGs | with LVGL |
| `ui_tomatoes.c` (build dir) | The tomato images, generated at build time by `tools/gen_tomatoes.py` | with LVGL |
| `host/` | The snapshot tool, the scenes, the touch test | Linux only |
| `tools/` | Generators and the comparison with the mock-up | Linux only |

## How a frame is made

`ui_update()` compares `ui_view_key()` (rev, the shown minute, the time since boot in minutes, the timer's second, the
pairing countdown) with the last one. When it changed, `ui_view_build()` produces the words and `ui.c` lays them out;
every setter compares before it writes, so LVGL only redraws what changed. The animations run on every call from the
monotonic clock: the message marquee (one pass of `max(6 s, 0.32 s per character)`), the alarm flash (three .55 s
ease-out pulses from 70% white, over the screen but under the progress bar), the Connecting dots (every 300 ms) and the
hold track (it fills from 400 ms to 3 s).

Every line sits on a fixed baseline (`ui_theme.h`); the headline takes the first of 112, 100, 78 and 62 px that fits
404 px, measured with the real fonts, and a 62 px headline too wide is cut with "…". Overflowing small text is cut
with "…" too. The toast centers on the status field (x 224, at most 432 px) while the info column shows and no menu is
open, else on the screen; small text it comes within 8 px of hides while it's up (`clearUnderToast()`), and a meeting
title in it is shortened further when the toast would be too wide (`withTitle()`).

The 180-degree flip is the display's rotation (`board_display_set_flipped`), which LVGL also applies to the touch
points, so the ui always works in logical 640 x 172 coordinates. Touches are forwarded as pressed, pressing, released
and press-lost samples with the menu tile under the point (`TB_TILE_NONE` off the tiles, `TB_TILE_LOST` for a lost
press).

Palette colors are rounded to the nearest RGB565 value before LVGL truncates them, so the dark surfaces stay neutral
on the panel.

## Checking the screens without the board

```sh
# the firmware's screens, one PNG per scene (host/ui_scenes.c lists them; --list prints them)
cmake -S firmware/components/ui/host -B firmware/build-host-ui && cmake --build firmware/build-host-ui -j
firmware/build-host-ui/tinybar_snapshot out/snaps
# the same scenes from docs/mockup.html (Playwright's Chromium, the real Barlow fonts, a fixed clock)
NODE_PATH=$(npm root -g) node firmware/components/ui/tools/ref_scenes.js out/ref
# side by side, with a difference map; prints the share of pixels that differ, worst first, and every text line
# 1 px or more off the mock-up's baseline (exit status 1 if any)
python3 firmware/components/ui/tools/compare.py out/ref out/snaps out/cmp
# the touch path through LVGL's input device (straight and flipped), and the layout test: screens drawn one after
# another as on the device (the sub line after the QR screen), and the longest real copy in each slot without "…"
ctest --test-dir firmware/build-host-ui
```

89 scenes: every status and its info-column variants, the Pomodoro (ready, running, paused, both breaks, both waiting
screens, the pill on other screens, a long day's foot and the longest paused kickers), On a call (with and without an
app name, a long name, during a meeting), In a meeting from the calendar (titles off and on, private, a long title),
the set-aside glyphs and the Mac icon, the four Wi-Fi setup screens, every menu (the Wi-Fi menu with 3 devices
paired, 10 ("Full"), none (the "pair at" foot with the host, the IP address after a name clash, and "pair at its IP
address" when both are too long), and offline, and Forget all's confirmation with 3 and with 10 devices), toasts over
a status, the clock, the setup screen and a menu, a cut title, the hold and Powering off screens, the flash, the
splash, the dark screen, flipped, the pairing screen (for a Mac, a phone, a named Mac, a script, and with 0:05 left),
how pairing ends ("Pairing canceled", "Paired · iPhone", "Pairing canceled · Focus started", "Forgot 3 devices"),
and the firmware's own screens (plain Away, the clock before it's set, the quick menu with the Wi-Fi link down).

The snapshot tool draws every scene in one process, one after another, as the device does; tools/compare.py misses
a line drawn a few pixels off when it only counts pixels (a 4 px drop of the sub line was under 2%), so it also
measures each text line's baseline in both. As of the 2026-10-05 firmware alignment with the mock-up's pairing round,
86 scenes are compared and every text line sits on the mock-up's baseline; the remaining differences are anti-aliasing at glyph
edges, whole-pixel glyph advances (LVGL) against the browser's fractional ones, RGB565 color depth, and the QR code's
pattern (below). Under 8% of pixels differ in any scene, nearly all at glyph edges.

- **Small text runs up to 7 px wider than in the mock-up.** LVGL rounds every glyph's advance to a whole pixel, about
  +0.16 px a character for Barlow: "PAUSED · SHORT BREAK · THEN FOCUS 3 OF 4" is 6 px wider, as are "turned off on the
  Remote" and "Idle · 1 Pomodoro done today". Nothing that fits the mock-up is cut today (the layout test checks the
  longest real copy in each slot), but copy within about 2% of a slot's width would get "…" on the bar and not in the
  mock-up. If copy grows, bake 1.25 px instead of 1.5 into the kicker font's tracking.
- **Fixed in the review round:** the sub line was laid out with the font the screen before left on it (4 px low and
  not cut at the column's edge right after the QR screen); the Wi-Fi menu is the mock-up's five columns with Network
  two wide; Forget all is its own three tiles; the pairing screen's label, foot, sub line and progress bar; the
  Connected kicker names the bar; four palette colors that RGB565 rounding tinted; the Focus progress track; word
  values at 129.
- **The pairing round (2026-10-05):** the Devices tile with nothing paired says where to pair. core gives the feet to
  try in order (`tb_tile_t.foot` then `foot_alt`), and `put_menu()` draws the first whose every line fits the tile's
  content width on one line, measured with `lv_text_get_size` (no wrapping) against 86.53 px with the mock-up's
  half-pixel allowance: "tinybar.local" is 75 px, "10.0.4.42" 54 px, "tinybar-2.local" 90 px (too wide). Forget all's
  device names stop at two lines the way Chrome's line clamp does (what the wrap puts on two lines, then "…"), not
  with LVGL's DOTS mode, which ends in three periods. `tools/ref_scenes.js` resets the mock-up's held toast between
  scenes (the page clock is fixed, so a pairing toast would otherwise hold back every later one).

The host tests (`test/host/ui`, in the lead's ctest) check every screen's copy word for word through the same scenes,
the overlays, the redraw key, the flash curve and the capitals, that the tomato frames match the mock-up's pixel for
pixel, and that the fonts carry exactly the characters `tb_text_drawable()` accepts.

## Decisions made here

- **Baselines follow the mock-up as drawn**, which is 1 px from the spec in four places: the sub line and the foot at
  151 (spec 152), the 78 px headline at 112 (113), the 62 px headline at 108 (107). The browser floors a fractional
  half-leading. ARCHITECTURE.md 13.8 asked for a pick for the 62 px baseline: 108. Word values (28 px) sit at 129, as
  the mock-up draws .ctx-value.small. decisions.md's spec now states these (Proposed, 2026-10-04).
- **Colors are rounded to the nearest RGB565 value channel by channel, except four** where that moves the hue
  (CIEDE2000 2.7 to 4.8): tiles and toasts #1C1F24, muted text #DFE5EA, the Away field #545C65 and the hold track
  #2A2E36 use the perceptually nearest 565 neighbor instead (each within 1.6). The progress track is darkened from the
  24-bit spec color before rounding (lv_color_darken on the rounded color landed Focus one red step low).
- **The clock's AM/PM is 36 px** (the mock-up's .32 em of 112 px, 35.84), with 2 px letter-spacing.
- **The QR code** is `lv_qrcode` (`WIFI:T:nopass;S:TinyBar-Setup;;`, 116 px, 4 px modules on the white 132 px square).
  LVGL raises the error correction to Q when it fits the same size (29 modules), so the pattern differs from the
  mock-up's level M code; it's the same size and position, and more forgiving of a phone held at an angle.
- **The pill has no tomato** (Bold Signal's CSS hides it), so no 16 px sprite was needed.

## Proposed (not in the mock-up; for the product manager and the UX designer)

- **Clock not set** (ARCHITECTURE.md 13.5): the kicker says "Clock not set", the headline is "--:--" (no AM/PM), the
  status row shows no time, Pomodoro end times are left out of the sub line ("Please don't interrupt"), and Next up and
  Free until say "Not known" with "the clock isn't set yet".
- **Away without a time** (api.md 8.1): kicker "Status", headline AWAY, sub line the note or "Not at my desk". With a
  time it's the mock-up's "Away" / "Back at 2:30" / note.
- **A message from another day**: the Posted foot says "yesterday" or the date ("Oct 1") instead of "today"; posted
  while the clock was unknown, the value is "Earlier".
- **The pairing screen** (api.md 4.8; decisions.md "Pairing", proposed; now in the mock-up): the dark surface, kicker
  "Pairing · Mac" (the device's name, or Mac, Phone, Script, Device), the code at 112 px ("482 913"), the sub line
  naming the device ("Type it on your Mac · tap to cancel", "Type it on your phone · tap to cancel", otherwise "Type
  this code on that device · tap to cancel"), "Code expires in" with a m:ss countdown over the bar's name, and the
  progress bar filling as the 2 minutes run out.
- **Wi-Fi dropped** (ARCHITECTURE.md 13.6): the crossed-out icon also shows while the link is down after setup, as
  the lead proposed.
- **An app name the fonts can't draw** shows "Mic or camera on" after the MAC chip, as when none was sent.

## Unverified until it runs on the board

Everything here was compiled for the ESP32-S3 and rendered and tested on Linux with the same LVGL; nothing has been on
the panel. To check on the bar: legibility of the 12 and 14 px text and the 4 bpp anti-aliasing on the real panel;
the colors after RGB565 and the panel's gamma; the marquee and flash frame rate (partial redraws of about 404 x 75 and
the full screen) and that `ui_update()` stays well under the loop period; the touch path with the real AXS15231B
(that LVGL's rotation gives logical coordinates after a flip, which the host test checks with a virtual pointer); the
memory LVGL takes for the hold track's clipped corners (a 24 KB layer) and the QR canvas; the 64 px tomato and the
sprites at 1:1 on the panel.
