/*
 * tb_internal.h: helpers shared by core's source files. Not part of the interface.
 * Owner: core builder.
 */
#pragma once

#include "tb_app.h"

/* Queue an effect (dropped with a counter if the queue is full; the app task drains it every loop). */
void tb_fx(tb_app_t *a, tb_effect_kind_t kind, int32_t arg);
/* bump(): something visible changed. */
static inline void tb_bump(tb_app_t *a) { a->rev++; }
/* Open a menu of this kind (showMenu and friends): its tiles for the current state, and the 8 s close timer.
 * TB_MENU_NONE closes it. */
void tb_menu_build(tb_app_t *a, tb_menu_kind_t kind, const tb_clock_t *now);
/* Refill the open menu's tiles from the current state, keeping its kind and timer (Forget all with nothing left to
 * forget becomes the Wi-Fi menu). */
void tb_menu_fill(tb_app_t *a, const tb_clock_t *now);
/* Copy a string into a fixed buffer, always NUL-terminated. */
void tb_strlcpy(char *dst, const char *src, size_t cap);
