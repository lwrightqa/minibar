/*
 * app.h: main's internal interface (start-up, the app task, persistence). Owner: lead developer.
 */
#pragma once

#include <stdbool.h>

#include "tb_app.h"

/* The one model. Only the app task touches it after app_task_start(). */
extern tb_app_t g_app;

/* Now, from esp_timer and the system clock. valid once the clock has been set (RTC, SNTP or the Mac's hello). */
tb_clock_t app_clock_now(void);
void app_clock_set_valid(bool valid);

/* Start the app task (core 1): it owns g_app, LVGL and the ui. */
void app_task_start(void);

/* net_init() and net_start() have returned: the Wi-Fi effects (setup, skip, done) and the protocol's timers can reach
 * net from here on. Effects core asked for before this are carried out then, in order. Any task. */
void app_net_ready(void);

/* Apply an IANA time zone to the C library (setenv TZ + tzset, for localtime_r in core and ui) and to the calendar.
 * An unknown or empty name means UTC. Called at boot from the main task and afterwards only by the app task. */
void app_apply_time_zone(const char *iana);
