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
 * low-pass on). ESP_ERR_NOT_FOUND if it doesn't answer: the bar then keeps the orientation it started with. Call
 * board_imu_read_flipped() right after it. */
esp_err_t board_imu_init(void);
/* Which way up to draw the first frame, once at boot before the display: *flipped = true when the bar stands upside
 * down (side buttons at the bottom), false when upright (buttons on top). Blocks until three settled samples are in
 * (STATUS0's data-ready bit, past the turn-on and filter settling, 0.7 to 1.3 g; typically about 85 ms after the
 * enable) or 150 ms after the enable at most. When that can't tell (lying flat, or no IMU), it uses the orientation
 * remembered in NVS from the last steady reading, and with nothing remembered, upright. Logs the averaged reading,
 * how long it took and where the answer came from. Returns false if there's no IMU. */
bool board_imu_read_flipped(bool *flipped);
/* Start watching (task "imu", core 0, about 25 Hz). Posts TB_EV_ORIENTATION {flipped, initial=true} once (after
 * 0.5 s of a steady reading, or after 2 s with the boot orientation if the bar lies flat), then
 * {flipped, initial=false} whenever the bar has stood the other way up, steadily, for 0.5 s. Gravity is read along
 * the axis set in menuconfig (TinyBar board → IMU axis), with hysteresis (votes need +-0.6 g along it) so a bar
 * lying flat, tipped a little, or being carried doesn't flip. Once the bar has stood still for 10 s in an orientation
 * other than the remembered one, it remembers it (NVS, namespace "board"). ESP_ERR_INVALID_STATE if the IMU isn't
 * there. */
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
