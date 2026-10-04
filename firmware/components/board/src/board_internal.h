/* board_internal.h: shared between board's source files. Owner: board builder. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

/* I2C timing for the system bus devices (TCA9554, PCF85063, QMI8658) and the touch controller. Waveshare's
 * examples run them at 300 kHz. */
#define BOARD_I2C_HZ          300000
#define BOARD_I2C_TIMEOUT_MS  50
#define BOARD_ADDR_EXIO       0x20    /* TCA9554, A0..A2 grounded (schematic "I/O Extensions") */

/* The system I2C bus (I2C_NUM_0), created by board_power_hold(). NULL before. */
i2c_master_bus_handle_t board_sys_bus(void);

/* Monotonic milliseconds (esp_timer). */
int64_t board_now_ms(void);

/* ---------- TCA9554 (board_exio.c) ----------
 * A minimal driver instead of esp_io_expander_tca9554, whose constructor resets every pin to an input. After a
 * software restart the expander still holds SYS_EN high from the previous run; that reset would let go of it (and
 * of BL_EN's low) for a moment. This one writes the output register first and only then the direction register, so
 * no pin ever glitches. Every access takes a mutex: the app task (backlight), the audio task (amplifier) and power
 * off share the cached output byte. */
esp_err_t board_exio_init(i2c_master_bus_handle_t bus, uint8_t outputs_mask, uint8_t initial_levels);
esp_err_t board_exio_set(uint8_t mask, bool high);
bool board_exio_get_output(uint8_t mask);
esp_err_t board_exio_read_inputs(uint8_t *levels);
bool board_exio_ready(void);

/* ---------- Used by power off and restart ---------- */
/* Backlight off (BL_EN low, LCD_BL dark), then the panel's display off and sleep-in commands. App task only. */
void board_display_shutdown(void);
/* Backlight off only (LCD_BL dark, BL_EN low). Works before the PWM and the expander exist (power hold). */
void board_backlight_off_now(void);
/* Amplifier off and the codec muted, at once, from any task. */
void board_audio_silence_now(void);
/* PWR's raw level: true while pressed (GPIO 16 low). */
bool board_pwr_pressed_raw(void);

/* ---------- Bring-up self-test (board_bringup.c, CONFIG_TINYBAR_BOARD_BRINGUP) ---------- */
void board_backlight_raw_duty(uint8_t duty);
void board_backlight_reapply(void);
esp_err_t board_imu_read_mg(int32_t *x, int32_t *y, int32_t *z);
void board_bringup_start(void);
