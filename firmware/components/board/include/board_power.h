/*
 * board_power.h: power hold, power off, restart, buttons. Owner: board builder.
 *
 * decisions.md: the bar runs on USB. "Power off" on USB is a deep sleep woken by a PWR press (GPIO 16, ext0 wake,
 * active low); on battery it drops SYS_EN (EXIO6) so the board switches itself off. A press turns it back on either way.
 * The V2 board has no pin that says whether USB is present (schematic "KEY&POWER", "Type-C"), so board_power_off()
 * finds out: it lets go of SYS_EN, waits for PWR to be released, and if the bar is still running it's on USB.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Step 1 of start-up: drive LCD_BL (GPIO 42) to its dark level, create the system I2C bus (I2C_NUM_0, GPIO 47/48)
 * and set up the TCA9554 with SYS_EN (EXIO6) high, LCD_RST (EXIO5) high, BL_EN (EXIO1) low and the amplifier (EXIO7)
 * off when it's gated (on otherwise). The expander's output register is written before its direction register, so
 * after a software restart SYS_EN never drops, even for a moment. Idempotent. */
esp_err_t board_power_hold(void);

/* Keep the backlight dark until the first frame (GPIO 42 dark, BL_EN low). board_power_hold() already does this;
 * calling it again is harmless. */
void board_backlight_early_off(void);

/* Always true: the V2 board can't tell USB from battery, and decisions.md says USB. Nothing in the power-off path
 * depends on it (see the top of this file). */
bool board_on_usb(void);

/* Power off now. Doesn't return. In order: sound off, backlight off, the panel's display-off and sleep-in commands,
 * the amplifier off, SYS_EN low (on battery the power goes as soon as PWR is let go), wait for PWR's release (it may
 * still be held from the 3 s hold) plus 300 ms; still running means USB, so deep sleep woken by the next PWR press.
 * Feeds the task watchdog while it waits. Call from the app task (it subscribes to the watchdog). */
void board_power_off(void) __attribute__((noreturn));

/* Restart (Restart tile, or after a fatal error): sound and backlight off, then esp_restart(). SYS_EN stays high.
 * Doesn't return. */
void board_restart(void) __attribute__((noreturn));

/* True if this boot came from a PWR press in deep sleep. (Either way, a PWR press that is still down at start-up is
 * ignored until released: see board_buttons_init.) */
bool board_woke_from_pwr(void);

/*
 * Buttons. Polls BOOT (GPIO 0) and PWR (GPIO 16) every 5 ms from an esp_timer, debounces them (20 ms), and posts
 * TB_EV_BUTTON events: TB_BTN_BOOT on a BOOT click (sent on release), TB_BTN_PWR_DOWN and TB_BTN_PWR_UP on PWR's
 * edges. core times the PWR press itself (400 ms shows "Keep holding", 3 s powers off). A button that is down when
 * this starts (the press that powered the bar on or woke it) posts nothing until it has been released, and its
 * release isn't posted either.
 */
esp_err_t board_buttons_init(void);

#ifdef __cplusplus
}
#endif
