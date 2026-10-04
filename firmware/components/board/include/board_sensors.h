/*
 * board_sensors.h: the QMI8658 IMU (flip detection) and the PCF85063 RTC. Owner: board builder.
 */
#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- IMU: flip detection ---------- */

/* Find the QMI8658 (0x6B, or 0x6A) on the system I2C bus and start its accelerometer only (+-2 g, 62.5 Hz,
 * low-pass on). ESP_ERR_NOT_FOUND if it doesn't answer: the bar then keeps the orientation it started with. */
esp_err_t board_imu_init(void);
/* One blocking reading (four samples, about 70 ms) of which way up the bar stands: *flipped = true when upside down
 * (layout must turn 180 degrees), false when upright or when it can't tell (lying flat). Used once at boot, before
 * the display, so the splash is drawn the right way up. Logs the raw reading for bring-up. Returns false if the IMU
 * didn't answer. */
bool board_imu_read_flipped(bool *flipped);
/* Start watching (task "imu", core 0, about 25 Hz). Posts TB_EV_ORIENTATION {flipped, initial=true} once (after
 * 0.5 s of a steady reading, or after 2 s with the boot orientation if the bar lies flat), then
 * {flipped, initial=false} whenever the bar has stood the other way up, steadily, for 0.5 s. Gravity is read along
 * the axis set in menuconfig (TinyBar board → IMU axis), with hysteresis (votes need +-0.6 g along it) so a bar
 * lying flat, tipped a little, or being carried doesn't flip. ESP_ERR_INVALID_STATE if the IMU isn't there. */
esp_err_t board_imu_start(void);

/* ---------- RTC ---------- */

/* Initialize the PCF85063 (0x51): make sure it runs in 24-hour mode. If it holds a time TinyBar wrote (its RAM byte
 * carries TinyBar's mark), the oscillator never stopped since, and the date is sane, set the system clock from it
 * and return true in *valid. The RTC keeps UTC. Without a battery it forgets the time whenever USB is unplugged. */
esp_err_t board_rtc_init(bool *valid);
/* Write the system clock's current UTC time (rounded to the nearest second) and TinyBar's mark into the RTC, after
 * SNTP or the Mac's hello set the clock. Refuses (ESP_ERR_INVALID_STATE) if the system clock is before 2024. Takes
 * three short I2C writes; call from the app task. */
esp_err_t board_rtc_save_now(void);

#ifdef __cplusplus
}
#endif
