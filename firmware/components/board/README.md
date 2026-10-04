# board: Waveshare ESP32-S3-Touch-LCD-3.49 V2 support

Everything TinyBar does with the hardware: power hold and power off, the panel and backlight, touch, BOOT and PWR,
flip detection, the clock chip, and sound. The interface is `include/board.h`; the start-up order is in its header
comment and in `main/app_main.c`.

> **Status:** compiled (ESP-IDF v5.4.2, no warnings in this component) and its pure logic is tested on Linux.
> **Nothing here has run on a board yet.** The checklist below is how to find out.

## Layout

```text
include/      the public API (board.h and the headers it includes) and the pin map (board_pins.h)
logic/        pure C, no ESP-IDF headers: backlight curve, button debouncer, flip detection, chime and tick
              synthesis, the two-voice player, PCF85063 registers, touch decoding. Tested in test/host/board/.
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
| IMU | 03_I2C_QMI8658 (SensorLib) | Reset, CTRL1 0x40, registers | Accelerometer only, ±2 g, 62.5 Hz, low-pass on; 0x6A tried after 0x6B |
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
| `TINYBAR_IMU_UP_*` | +Y | Checklist step 5 |
| `TINYBAR_BL_ZERO_DUTY` | 164 | Checklist step 3 |
| `TINYBAR_AUDIO_VOLUME` | 75 (−12.5 dB) | Checklist step 8 |
| `TINYBAR_AUDIO_AMP_GATE` | on | Checklist step 8 |
| `TINYBAR_LCD_PCLK_MHZ` | 40 | Leave at 40 (Waveshare's value) unless the picture shows noise |
| `TINYBAR_BOARD_BRINGUP` | off | On for the first runs only |

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
   - No flash of garbage at power-up; the splash appears the right way up for how the bar stands, with true colors
     (white text, the status color; red and blue not swapped, no noise). Log: `first frame shown (rotate + send N ms)`;
     expect N around 20 to 30 ms.
   - The sweep logs `LCD_BL duty N of 255`: note the first duty at which the screen is fully dark. Expected about 164.
     Set `TINYBAR_BL_ZERO_DUTY` to it.
   - The quick menu's Light tile: 40, 70 and 100% look clearly different, and 40% is still comfortable to read.
   - A PWR press makes the screen dark (not just dim), and a tap brings it back.
4. **Touch.** Log on every press: `touch down: raw x=.. y=.. -> native x,y`. Tap the four corners of the layout:
   native x runs 0 to 171 across the short side and native y 0 to 639 along the long side, and the hold, swipe
   direction and menu tiles land where the finger is. **Flip the bar** and repeat: tiles and swipes must still match
   (LVGL turns the point with the layout).
5. **IMU axis.** Stand the bar the normal way up. The log `IMU x=.. y=.. z=.. mg` shows one axis near ±1000.
   - If it's X or Y with + sign, set `TINYBAR_IMU_UP_` to that axis; with − sign, the negative one.
   - Then: the splash after a restart must be the right way up both ways round; flipping the bar turns the layout
     after about half a second; lying it flat or tipping it a little does nothing; carrying it around doesn't flip it.
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
10. **Watchdog.** No `task_wdt` messages in an hour of normal use, including during calendar syncs.
11. **Turn `TINYBAR_BOARD_BRINGUP` off** again.

## Open questions only the board can answer

- The backlight's real dark point (step 3) and whether the Light levels look like the mock-up's.
- The IMU's axis and sign (step 5).
- Touch ranges at the edges (whether raw X reaches 640 and raw Y 172).
- Whether gating the amplifier pops (step 8), and the volume for an open office.
- Frame time: about 11 ms of QSPI plus the rotation and copy, for every redraw (LVGL redraws the whole screen in this
  mode). Fine for a clock that changes once a second; if the message marquee stutters, the next step is an async
  flush or `LV_DISPLAY_RENDER_MODE_DIRECT`.
