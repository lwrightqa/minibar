/*
 * board_power.h: power hold, power off, restart, buttons. Owner: board builder.
 *
 * decisions.md: the bar runs on USB. "Power off" on USB is a deep sleep woken by a PWR press (GPIO 16, ext0 wake,
 * active low); on battery it drops SYS_EN (EXIO6) so the board switches itself off. A press turns it back on either way.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Step 1 of start-up: create the system I2C bus (I2C_NUM_0, GPIO 47/48), the TCA9554, and drive SYS_EN (EXIO6) and
 * NS_MODE (EXIO7) high. Also sets EXIO1 (BL_EN) and EXIO5 (LCD_RST) as outputs with BL_EN low. Idempotent. */
esp_err_t board_power_hold(void);

/* Keep the backlight dark until the first frame (GPIO 42 and BL_EN). */
void board_backlight_early_off(void);

/* True when USB power is present (so power off means deep sleep). How to tell on the V2 board is to be checked on the
 * hardware (SYS_OUT level at boot, battery ADC on GPIO 4); until then, assume USB (decisions.md). */
bool board_on_usb(void);

/* Power off now: the screen and backlight off, the amplifier off, then deep sleep (USB) or SYS_EN low (battery).
 * Doesn't return. */
void board_power_off(void) __attribute__((noreturn));

/* Restart (Restart tile, or after a fatal error). Doesn't return. */
void board_restart(void) __attribute__((noreturn));

/* True if this boot came from a PWR press in deep sleep (then the PWR release that follows is ignored, so the
 * press that turned the bar on doesn't also turn the screen off). */
bool board_woke_from_pwr(void);

/*
 * Buttons. Polls BOOT (GPIO 0) and PWR (GPIO 16) every 5 ms from an esp_timer, debounces them, and posts
 * TB_EV_BUTTON events: TB_BTN_BOOT on a BOOT click (press and release), TB_BTN_PWR_DOWN and TB_BTN_PWR_UP on PWR's
 * edges. core times the PWR press itself (400 ms shows "Keep holding", 3 s powers off). If PWR is down at boot,
 * nothing is posted until it has been released.
 */
esp_err_t board_buttons_init(void);

#ifdef __cplusplus
}
#endif
