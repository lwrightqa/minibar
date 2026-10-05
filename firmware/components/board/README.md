# board: Waveshare ESP32-S3-Touch-LCD-3.49 V2 support

Everything TinyBar does with the hardware: power hold and power off, the panel and backlight, touch, BOOT and PWR,
flip detection, the clock chip, and sound. The interface is `include/board.h`; the start-up order is in its header
comment and in `main/app_main.c`.

> **Status:** compiled (ESP-IDF v5.4.2, no warnings in this component) and its pure logic is tested on Linux.
> Firmware 1.0.1 runs on the user's V2 board (2026-10-05): the picture, the flip and Wi-Fi setup were seen working,
> and the first frame came up upside down until the IMU task righted it, which 1.0.2 fixes (below, "Which way is
> up"). No serial log has been taken yet, so the checklist below hasn't been run in full.

## Layout

```text
include/      the public API (board.h and the headers it includes) and the pin map (board_pins.h)
logic/        pure C, no ESP-IDF headers: backlight curve, button debouncer, flip detection (and the start-up
              reading, the remembered pose, the LVGL rotation), chime and tick synthesis, the two-voice player,
              PCF85063 registers, touch decoding. Tested in test/host/board/.
src/          the drivers: board_exio (TCA9554), board_power, board_display (panel, LVGL, backlight),
              board_touch, board_buttons, board_imu, board_rtc, board_audio, board_bringup (self-test)
Kconfig       menuconfig → "TinyBar board": IMU axis, backlight dark point, volume, amplifier gating, QSPI clock,
              bring-up self-test
```

## Where it comes from

Waveshare's V2 examples (`../waveshareteam/esp32-s3-touch-lcd-3.49-v2/ESP-IDF/`) and the V2 schematic in the same
repository (`schematic/ESP32-S3-Touch-LCD-3.49 V2.pdf`):

| Part | Source | Kept | Changed, and why |
|---|---|---|---|
| Panel | 10_LVGL_V9_Test | QSPI on SPI3 at 40 MHz, mode 3, 32-bit commands; init 0x11, 0x29; reset on EXIO5 (30/250/30 ms); full frames in 64-line DMA chunks; `lv_draw_sw_rotate` | Byte swap done in the DMA buffer (LVGL's buffer untouched); waits time out instead of hanging; no LVGL task (the app task runs LVGL); panel failure leaves the rest of the bar running |
| Touch | 10_LVGL_V9_Test (unrotated mapping), 11_FactoryProgram | Command `b5 ab a5 5a 00 00 00 0e 00 00 00`, 32-byte reply, native point = (raw Y, 639 − raw X) | LVGL turns it by the display rotation. The example's own "rotated" mapping assumes the opposite axes and is not used. Brief read errors don't end a press. |
| Expander | 11_FactoryProgram `tca9554_init`, `example_lcd_exio_init` | Same pins and levels | Own 40-line driver: `esp_io_expander_tca9554` resets every pin to an input when created, which would drop SYS_EN for a moment on each software restart. Output register written before the direction register; a mutex around every access. |
| Power | 07_BATT_PWR_Test, 11_FactoryProgram | SYS_EN high at start; drop it to power off | Power off works on USB too (deep sleep, PWR wakes it); the example's "battery flag" is really "PWR released since start", which is the debouncer's suppression here |
| Buttons | 11_FactoryProgram `button_bsp` | 5 ms polling from an esp_timer, active low with pull-ups | Own debouncer (20 ms) and edge events; core does the timing |
| Backlight | 10_LVGL_V9_Test `lcd_bl_pwm_bsp` | LEDC, 8-bit, 50 kHz, inverted | A curve from the mock-up's Light levels, and the dark point from the schematic (below) |
| IMU | 03_I2C_QMI8658 (SensorLib) | Reset, CTRL1 0x40, registers, STATUS0 data-ready (bit 0) | Accelerometer only, ±2 g, 62.5 Hz, low-pass on; 0x6A tried after 0x6B; before the first frame, waits for settled samples (below) |
| RTC | 02_I2C_PCF85063 (SensorLib) | Registers, BCD, 24-hour mode, OS flag | TinyBar's mark in the RAM byte, so the factory program's fixed local time isn't taken as UTC |
| Audio | 08_Audio_Test `codec_board` S3_LCD_3_49 | ES8311 via esp_codec_dev, MCLK 7, BCLK 15, WS 46, DOUT 45, 24 kHz | Standard I2S output only (no TDM, the microphones are unused); amplifier gated |

What the schematic adds (none of it is in Waveshare's code):

- **Backlight.** BL_EN (EXIO1) is the AP3032 driver's enable and has a **100k pull-up**: the backlight is *on* from
  power-up until the expander drives it low, so `board_power_hold()` sets LCD_BL dark first and BL_EN low next.
  LCD_BL (GPIO 42) feeds the driver's feedback pin through 10k + 39k: the higher its average, the lower the LED
  current, reaching zero at about **64% high** (with the AP3032's 0.2 V feedback: 3.3 V × d = 0.2 + 0.2 × 49k/5.1k).
  So brightness is spread over duties 0 to 164 of 255, not 0 to 255 (`TINYBAR_BL_ZERO_DUTY`).
- **Amplifier.** EXIO7 ("NS_MODE") is the NS4150B's CTRL pin with a **10k pull-down**: an on/off switch, off by
  default. With `TINYBAR_AUDIO_AMP_GATE` (default) it's on only around sounds, so the speaker can't hiss.
- **Power.** PWR powers the board directly while held (on battery) and pulls SYS_OUT (GPIO 16, 10k pull-up) low.
  SYS_EN only gates the battery path. **No pin senses USB**, so power off tries SYS_EN low first and falls back to
  deep sleep if the board is still running.
- **IMU address.** SA0 is grounded; Waveshare's code uses 0x6B for it. Both are probed.
- **RTC power.** 3V3 or the battery through diodes: with no battery, unplugging USB loses the time (OS set).

## Configuration (menuconfig → TinyBar board)

| Option | Default | Set it from |
|---|---|---|
| `TINYBAR_IMU_UP_*` | −Y (buttons on top) | Checklist step 5 |
| `TINYBAR_BL_ZERO_DUTY` | 164 | Checklist step 3 |
| `TINYBAR_AUDIO_VOLUME` | 75 (−12.5 dB) | Checklist step 8 |
| `TINYBAR_AUDIO_AMP_GATE` | on | Checklist step 8 |
| `TINYBAR_LCD_TURN_180` | on | Checklist step 3 (swaps LVGL rotation 90 and 270; upright, buttons on top, is 270) |
| `TINYBAR_LCD_PCLK_MHZ` | 40 | Leave at 40 (Waveshare's value) unless the picture shows noise |
| `TINYBAR_BOARD_BRINGUP` | off | On for the first runs only |

## Which way is up

**Upright is the way the user stands the bar: side buttons (BOOT, PWR) on top** (verified on the bar, 2026-10-05).
Flipped is the other way up, buttons at the bottom. On the V2 board, standing upright, the QMI8658's Y axis reads
about −1000 mg and the picture needs LVGL rotation 270, hence the defaults `TINYBAR_IMU_UP_Y_NEG` and
`TINYBAR_LCD_TURN_180`. Those two are inferred from 1.0.1 (its steady pictures were right with +Y and no turn), not
yet read in a log: step 5 confirms them.

- **The two settings go together.** Once the IMU has a reading, inverting both draws every pose exactly as before
  (each inversion turns the picture 180 degrees, and they cancel; `test/host/board/test_orient_start.c` checks every
  sample and random sequences). What they change is which pose counts as upright, and so what the bar draws when it
  can't tell. 1.0.1 had +Y and no turn: the same steady pictures, but "upright" was buttons at the bottom, so a start
  where the first reading couldn't tell came up upside down until the IMU task righted it half a second later.
- **An sdkconfig from before 1.0.2 keeps +Y** (an existing value always wins over `sdkconfig.defaults`), and one
  from before `TINYBAR_LCD_TURN_180` existed takes the turn on beside it: +Y with the turn draws every pose upside
  down. `board_imu.c` warns at build time unless the pair is −Y with the turn; delete `build-<name>/sdkconfig` and
  rebuild. If step 5 shows another pair, change it in Kconfig, `sdkconfig.defaults` and that check together.
- **The first frame** waits for the IMU (`board_imu_read_flipped`, at most 150 ms after the accelerometer is turned
  on): it polls STATUS0's data-ready bit, drops the samples from turn-on and filter settling (3 ms + 3/ODR, the
  QMI8658A datasheet's accelerometer turn-on time) and any that aren't 0.8 to 1.2 g (the same range the flip detection
  uses), and averages three good ones in a row. 1.0.1 waited a fixed 40 ms and averaged four samples whatever they
  held.
- **When that can't tell** (lying flat, or no IMU), the bar uses the orientation it **remembers** from the last steady
  reading: NVS `nvs`, namespace `board`, key `pose`, the IMU axis that pointed up (so a later change of
  `TINYBAR_IMU_UP` can't invert its meaning; a pose on another axis is ignored). It's written once the bar has stood
  still for 10 s in an orientation other than the stored one, so a jiggle or a quick turn costs no flash. With nothing
  remembered, upright.

## Bring-up checklist

Build once with `TINYBAR_BOARD_BRINGUP=y` (it logs the I2C bus, the raw accelerometer, a backlight sweep, every button
and touch press, and plays the sounds), flash at 115200 (firmware/README.md), and watch the log over USB
(`idf.py -p <port> monitor`). Tick each step, and write what you measured into the Kconfig defaults (ask the lead to
put them in `sdkconfig.defaults`).

1. **Power hold.** Log: `power held (SYS_EN high)`. On USB that's all there is to see (SYS_EN only gates the battery).
   *With a battery (not used today):* press PWR to start, release it after the splash, and the bar must stay on.
2. **I2C bus.** Log: `system I2C bus answers at: 0x18 0x20 0x40 0x51 0x6b` (ES8311, TCA9554, ES7210, PCF85063,
   QMI8658). A missing 0x20 means nothing else will work.
3. **Panel and backlight.**
   - No flash of garbage at power-up; the splash appears the right way up for how the bar stands (buttons on top or
     at the bottom), from the very first frame, with true colors (white text, the status color; red and blue not
     swapped, no noise). Log: `display 640x172, rotation 270 for upright, buttons on top
     (CONFIG_TINYBAR_LCD_TURN_180)`, then `first frame shown (rotate + send N ms)`; expect N around 20 to 30 ms.
   - **If the picture is upside down both ways up**, half a second after the bar has stood still: change
     `TINYBAR_LCD_TURN_180`. Set `TINYBAR_IMU_UP_` from the log first (step 5): a wrong sign and a wrong turn look
     the same once the IMU has a reading, but the sign also decides which pose is "upright" at a start where the IMU
     can't tell. If the first frame is wrong and the picture rights itself after half a second, the start-up reading
     failed: the `IMU at start:` line says why (step 5). Both settings turn the layout and the touch together.
   - Read the boot time: the timestamp of `power held (SYS_EN high)` (the PSRAM test and the bootloader's INFO log are
     off to keep it short; on a battery, a PWR press shorter than that wouldn't keep the bar on).
   - The sweep logs `LCD_BL duty N of 255`: note the first duty at which the screen is fully dark. Expected about 164.
     Set `TINYBAR_BL_ZERO_DUTY` to it.
   - The quick menu's Light tile: 40, 70 and 100% look clearly different, and 40% is still comfortable to read.
   - A PWR press makes the screen dark (not just dim), and a tap brings it back.
4. **Touch.** Log on every press: `touch down: raw x=.. y=.. -> native x,y`. Tap the four corners of the layout:
   native x runs 0 to 171 across the short side and native y 0 to 639 along the long side, and the hold, swipe
   direction and menu tiles land where the finger is. **Flip the bar** and repeat: tiles and swipes must still match
   (LVGL turns the point with the layout).
5. **IMU axis and the start-up reading.** Stand the bar upright, side buttons on top, and restart it. The log:
   `IMU at start: x=.. y=.. z=.. mg, mean of 3 samples, N ms after enabling it (.. read, 2 while settling) -> upright,
   buttons on top`, with N about 85 (150 at most) and one axis near ±1000 (Y near −1000 on the V2 board).
   - If the axis near ±1000 is X or Y with + sign, set `TINYBAR_IMU_UP_` to that axis; with − sign, the negative one.
   - A `no settled sample of 0.8 to 1.2 g` warning, or `STATUS0 never showed data ready`, means the start-up reading
     didn't work: note the counts it gives. The bar then starts as remembered (`starting ..., as remembered from the
     last steady reading`) or upright.
   - About 0.5 s later: `steady: upright, buttons on top, as drawn at start`. A warning `steady: ..., so the start
     was drawn the wrong way up` means the start-up reading was wrong.
   - Then: the splash after a restart must be the right way up both ways round, from the first frame; flipping the
     bar turns the layout after about half a second (`turned over: upside down, buttons at the bottom`), and 10 s
     later `remembered for a start lying flat: upside down, buttons at the bottom`; lying it flat or tipping it a
     little does nothing; carrying it around doesn't flip it. Restart it lying flat: it starts the way it last stood
     (`starting ..., as remembered from the last steady reading`).
6. **Buttons.** Log `button: BOOT click`, `PWR down`, `PWR up`. BOOT goes to the next status; a PWR press darkens the
   screen; holding PWR shows "Keep holding" at 0.4 s and powers off at 3 s.
7. **Power off and wake (USB).** After "Powering off": log `still powered, so on USB: deep sleep until PWR is pressed`,
   screen dark. It must **not** wake while PWR is still held from the hold. A PWR press starts it again with the
   splash; that press must not also darken the screen (log: `PWR is down since start-up: ignored until it's
   released`). The Restart tile restarts without a flash of the old frame.
8. **Sound.** The self-test plays the rising chime, the falling chime, 5 s of Soft ticks and 5 s of Medium ticks.
   - The chime is clearly audible a couple of desks away but not startling; Soft ticks are only audible up close;
     Medium a little louder. Tune `TINYBAR_AUDIO_VOLUME` (the levels between them are fixed: 18 and 10 dB).
   - No pop when the amplifier switches on, and no hiss after the sound (1.5 s later it switches off). If switching
     it pops, turn `TINYBAR_AUDIO_AMP_GATE` off and listen for hiss instead.
   - A Pomodoro alarm repeats the chime every 4 s for up to a minute (core's timing).
9. **RTC.** First boot after flashing: `RTC time not trusted (never set by TinyBar)`. Once Wi-Fi time (SNTP) or the
   Mac has set the clock: `RTC set to ... UTC`. Restart (Restart tile): `clock set from the RTC`, and the clock is
   right before Wi-Fi connects. Unplug USB for a minute (no battery): `oscillator stopped (power lost)`.
10. **Watchdog and headroom.** No `task_wdt` messages in an hour of normal use, including during calendar syncs. The
    health lines (15 s after start, right after the first calendar fetch, then every minute) give the internal heap
    (free, lowest, largest block), every task's unused stack (`stack left (bytes): app ... tiT ... sys_evt ...`) and
    the frame time (`frame time (render + rotate + send)`). Note the lowest internal free and the smallest stack
    margins with the Remote open, a calendar sync running and the setup network up at once.
11. **Turn `TINYBAR_BOARD_BRINGUP` off** again.

## Open questions only the board can answer

- The backlight's real dark point (step 3) and whether the Light levels look like the mock-up's.
- The IMU's axis and sign (step 5): −Y with the buttons on top, inferred from 1.0.1 on the bar (its steady pictures
  were right with +Y and no turn); the `IMU at start:` line confirms it.
- What the QMI8658's first samples really hold after the enable, and how long its data-ready bit takes (the start-up
  log line gives the time and counts): the cause of 1.0.1's upside-down first frame isn't confirmed.
- Touch ranges at the edges (whether raw X reaches 640 and raw Y 172).
- Whether gating the amplifier pops (step 8), and the volume for an open office.
- Frame time: about 11 ms of QSPI plus the rotation and copy, for every redraw (LVGL redraws the whole screen in this
  mode). Fine for a clock that changes once a second; if the message marquee stutters, the next step is an async
  flush or `LV_DISPLAY_RENDER_MODE_DIRECT`. The health log reports it at INFO. There's one draw buffer (the flush is
  synchronous, so a second never overlapped any work), and nothing is sent while the backlight is off.
