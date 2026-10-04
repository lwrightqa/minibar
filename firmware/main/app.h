/*
 * app.h: main's internal interface (start-up, the app task, persistence). Owner: lead developer.
 */
#pragma once

#include "tb_app.h"

/* The one model. Only the app task touches it after app_task_start(). */
extern tb_app_t g_app;

/* Now, from esp_timer and the system clock. valid once the clock has been set (RTC, SNTP or the Mac's hello). */
tb_clock_t app_clock_now(void);
void app_clock_set_valid(bool valid);

/* Start the app task (core 1): it owns g_app, LVGL and the ui. */
void app_task_start(void);

/* Apply settings.device.time_zone to the C library (setenv TZ + tzset) and the calendar. */
void app_apply_time_zone(const char *iana);
