/*
 * tb_settings.h: the settings model, exactly api.md section 10 (GET/PATCH /api/v1/settings).
 *
 * Owner: core builder. Pure C.
 * Persistence is main's job (main/settings_store.c stores tb_settings_t as a versioned NVS blob); this file only
 * defines the values, their defaults and ranges, and the all-or-nothing PATCH check.
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TB_SETTINGS_VERSION 1

typedef struct {
    struct {
        uint8_t focus_min;      /* 1..120, default 25 (the Remote offers 15, 25, 50) */
        uint8_t short_min;      /* 1..60,  default 5 */
        uint8_t long_min;       /* 1..60,  default 15 */
        uint8_t long_every;     /* 2..8,   default 4: "Long break after" */
        bool auto_start;        /* default false */
        bool chime;             /* default true */
        bool ticking;           /* default false (decided 2026-10-04) */
        tb_tick_vol_t tick_volume;  /* default soft; kept while ticking is off */
    } pomodoro;
    struct {
        uint8_t brightness;     /* 10..100 percent, default 70; the Light tile steps 40, 70, 100 */
    } display;
    struct {
        bool calendar;          /* Calendar meetings: true once an address is saved, false before */
        bool mac;               /* Calls from your Mac: default true */
        bool meeting_titles;    /* Show meeting titles: default false; needs a saved address */
    } automatic;
    struct {
        char name[TB_DEVICE_NAME_BYTES];    /* default "TinyBar " + last 4 of device_id, upper case ("TinyBar 2A1C") */
        char time_zone[TB_TZ_NAME_BYTES];   /* IANA name, "" until the setup page or the Mac's hello sends one */
    } device;
} tb_settings_t;

/* A PATCH: only the fields with has_* set change (api.md 10.2). net fills it from the JSON body. */
typedef struct {
    bool has_focus_min, has_short_min, has_long_min, has_long_every;
    bool has_auto_start, has_chime, has_ticking, has_tick_volume;
    bool has_brightness;
    bool has_calendar, has_mac, has_meeting_titles;
    bool has_name, has_time_zone;
    tb_settings_t v;    /* the new values, read only where has_* is set */
} tb_settings_patch_t;

/* Defaults. device_id is the 12-hex-digit id (board_device_id()); it names the bar "TinyBar 2A1C". */
void tb_settings_defaults(tb_settings_t *s, const char *device_id);

/*
 * Check a patch against the ranges in api.md 10.1 without changing anything. Returns TB_OK, TB_E_BAD_VALUE or
 * TB_E_NO_CALENDAR (automatic.calendar or automatic.meeting_titles set true with no address saved). On error,
 * *field is set to the dotted API name, e.g. "pomodoro.focus_min". time_zone is checked by the caller (net), which
 * knows the zone table; here it only has to be 1..47 bytes.
 */
tb_err_t tb_settings_check(const tb_settings_patch_t *p, bool calendar_saved, const char **field);

/* Copy the patched fields into s. Call only after tb_settings_check() returned TB_OK. */
void tb_settings_apply(tb_settings_t *s, const tb_settings_patch_t *p);

/* Field by field (no memcmp: the struct has padding). */
bool tb_settings_equal(const tb_settings_t *a, const tb_settings_t *b);

/* Clamp every field into range (used after loading an older NVS blob). Returns true if anything changed. */
bool tb_settings_sanitize(tb_settings_t *s);

#ifdef __cplusplus
}
#endif
