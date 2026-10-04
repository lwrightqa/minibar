/*
 * board.h: TinyBar's board support for the Waveshare ESP32-S3-Touch-LCD-3.49 V2. Include this one header.
 *
 * Owner: board builder. Bring-up checklist: ../README.md.
 *
 * Built from Waveshare's proven V2 sequences (../waveshareteam/esp32-s3-touch-lcd-3.49-v2/ESP-IDF/) and the V2
 * schematic (schematic/ESP32-S3-Touch-LCD-3.49 V2.pdf):
 *   10_LVGL_V9_Test   LVGL 9 display (QSPI, full-frame flush in 64-line DMA chunks, software rotation) and touch
 *   11_FactoryProgram TCA9554 setup, SYS_EN power hold, BOOT/PWR buttons, backlight PWM, the LCD reset sequence
 *   08_Audio_Test     ES8311 through esp_codec_dev
 *   02, 03            PCF85063 RTC and QMI8658 IMU (register sequences from their SensorLib)
 * The pure decisions (backlight curve, debouncing, flip detection, sound synthesis and mixing, RTC registers, touch
 * decoding) live in logic/ and are tested on Linux in test/host/board/.
 *
 * Start-up order (main/app_main.c calls these in this order; see ARCHITECTURE.md):
 *   1. board_power_hold()        LCD_BL dark, I2C bus 0, TCA9554, SYS_EN high. FIRST, before anything slow: on
 *                                battery the bar switches itself off again if SYS_EN isn't held.
 *   2. board_backlight_early_off() keep the panel dark until the first frame is drawn (already is; harmless)
 *   3. board_imu_init()          so the first frame is drawn the right way up (board_imu_read_flipped)
 *   4. board_display_init()      QSPI, panel reset and init, LVGL display, buffers, backlight PWM
 *   5. board_touch_init()
 *   6. board_rtc_init()          sets the system clock if the RTC holds a time TinyBar wrote
 *   7. board_buttons_init()      posts TB_EV_BUTTON
 *   8. board_audio_init()
 *   9. board_imu_start()         posts TB_EV_ORIENTATION
 *
 * Tasks board starts: "imu" (core 0, priority 3, 3 KB) and "audio" (core 1, priority 6, 4 KB), both subscribed to the
 * task watchdog; the button poll is an esp_timer callback (5 ms). Everything else runs in the caller's task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "board_pins.h"
#include "board_audio.h"
#include "board_display.h"
#include "board_power.h"
#include "board_sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The bar's id: its Wi-Fi station MAC address as 12 lowercase hex digits ("f412fa3f2a1c"; api.md 7.1). */
void board_device_id(char out[13]);

/* Task watchdog (CONFIG_ESP_TASK_WDT_*: 10 s, panic and restart, idle tasks watched). Subscribe the calling task,
 * feed it, unsubscribe. The app task subscribes itself and feeds once a loop; board's own tasks do the same. */
esp_err_t board_wdt_add_self(void);
void board_wdt_feed(void);
void board_wdt_remove_self(void);

#ifdef __cplusplus
}
#endif
