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

/* Initialize the QMI8658 (accelerometer only, low rate) on the system I2C bus. */
esp_err_t board_imu_init(void);
/* One blocking reading of which way up the bar stands: true = upside down (layout must turn 180 degrees). Used once
 * at boot, before the display, so the splash is drawn the right way up. Returns false if the IMU didn't answer. */
bool board_imu_read_flipped(bool *flipped);
/* Start watching (a small task, about 25 Hz): post TB_EV_ORIENTATION {flipped, initial=true} once, then
 * {flipped, initial=false} whenever the bar has stood the other way up, steadily, for about 0.5 s. Uses gravity's
 * direction along the screen's short axis, with hysteresis so a bar lying flat or being carried doesn't flip.
 * The axis and sign are to be checked on the board. */
esp_err_t board_imu_start(void);

/* ---------- RTC ---------- */

/* Initialize the PCF85063 (0x51). If it holds a valid time (oscillator-stop flag clear), set the system clock from it
 * and return true in *valid. The RTC keeps UTC. */
esp_err_t board_rtc_init(bool *valid);
/* Write the system clock's current UTC time into the RTC (after SNTP, or after the Mac's hello set the clock). */
esp_err_t board_rtc_save_now(void);

#ifdef __cplusplus
}
#endif
