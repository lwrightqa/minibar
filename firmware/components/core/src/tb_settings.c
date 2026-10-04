/*
 * tb_settings.c: settings defaults, ranges and the all-or-nothing PATCH check (api.md section 10).
 * Owner: core builder. The lead filled in the straightforward parts so the skeleton has something to test.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "tb_internal.h"
#include "tb_settings.h"
#include "tb_text.h"

void tb_settings_defaults(tb_settings_t *s, const char *device_id)
{
    memset(s, 0, sizeof(*s));
    s->pomodoro.focus_min = 25;
    s->pomodoro.short_min = 5;
    s->pomodoro.long_min = 15;
    s->pomodoro.long_every = 4;
    s->pomodoro.auto_start = false;
    s->pomodoro.chime = true;
    s->pomodoro.ticking = false;
    s->pomodoro.tick_volume = TB_TICK_SOFT;
    s->display.brightness = 70;
    s->automatic.calendar = false;      /* true once an address is saved */
    s->automatic.mac = true;
    s->automatic.meeting_titles = false;
    /* "TinyBar" plus the last four characters of the id, upper case: "TinyBar 2A1C" (api.md section 3, proposed). */
    char tail[5] = "0000";
    size_t n = device_id ? strlen(device_id) : 0;
    if (n >= 4) {
        for (int i = 0; i < 4; i++) tail[i] = (char)toupper((unsigned char)device_id[n - 4 + i]);
    }
    snprintf(s->device.name, sizeof(s->device.name), "TinyBar %s", tail);
    s->device.time_zone[0] = '\0';
}

static bool in_range(int v, int lo, int hi) { return v >= lo && v <= hi; }

tb_err_t tb_settings_check(const tb_settings_patch_t *p, bool calendar_saved, const char **field)
{
    const char *f = NULL;
    tb_err_t err = TB_OK;
#define BAD(name) do { f = name; err = TB_E_BAD_VALUE; goto out; } while (0)
    if (p->has_focus_min && !in_range(p->v.pomodoro.focus_min, 1, 120)) BAD("pomodoro.focus_min");
    if (p->has_short_min && !in_range(p->v.pomodoro.short_min, 1, 60)) BAD("pomodoro.short_min");
    if (p->has_long_min && !in_range(p->v.pomodoro.long_min, 1, 60)) BAD("pomodoro.long_min");
    if (p->has_long_every && !in_range(p->v.pomodoro.long_every, 2, 8)) BAD("pomodoro.long_every");
    if (p->has_tick_volume && p->v.pomodoro.tick_volume != TB_TICK_SOFT && p->v.pomodoro.tick_volume != TB_TICK_MEDIUM)
        BAD("pomodoro.tick_volume");
    if (p->has_brightness && !in_range(p->v.display.brightness, 10, 100)) BAD("display.brightness");
    if (p->has_name) {
        /* 1 to 24 characters (api.md 2.5), counted as characters, not bytes: "Café" is 4. The buffer holds 24
         * characters of up to 3 bytes each, so a name that fits the count always fits the bytes. */
        size_t bytes = strnlen(p->v.device.name, sizeof(p->v.device.name));
        if (bytes >= sizeof(p->v.device.name)) BAD("device.name");
        size_t n = tb_utf8_len(p->v.device.name);
        if (n < 1 || n > TB_DEVICE_NAME_CHARS) BAD("device.name");
    }
    if (p->has_time_zone) {
        size_t n = strnlen(p->v.device.time_zone, sizeof(p->v.device.time_zone));
        if (n < 1 || n > TB_TZ_NAME_BYTES - 1) BAD("device.time_zone");
    }
    if (p->has_calendar && p->v.automatic.calendar && !calendar_saved) {
        f = "automatic.calendar";
        err = TB_E_NO_CALENDAR;
        goto out;
    }
    if (p->has_meeting_titles && p->v.automatic.meeting_titles && !calendar_saved) {
        f = "automatic.meeting_titles";
        err = TB_E_NO_CALENDAR;
        goto out;
    }
#undef BAD
out:
    if (field) *field = f;
    return err;
}

void tb_settings_apply(tb_settings_t *s, const tb_settings_patch_t *p)
{
    if (p->has_focus_min) s->pomodoro.focus_min = p->v.pomodoro.focus_min;
    if (p->has_short_min) s->pomodoro.short_min = p->v.pomodoro.short_min;
    if (p->has_long_min) s->pomodoro.long_min = p->v.pomodoro.long_min;
    if (p->has_long_every) s->pomodoro.long_every = p->v.pomodoro.long_every;
    if (p->has_auto_start) s->pomodoro.auto_start = p->v.pomodoro.auto_start;
    if (p->has_chime) s->pomodoro.chime = p->v.pomodoro.chime;
    if (p->has_ticking) s->pomodoro.ticking = p->v.pomodoro.ticking;
    if (p->has_tick_volume) s->pomodoro.tick_volume = p->v.pomodoro.tick_volume;
    if (p->has_brightness) s->display.brightness = p->v.display.brightness;
    if (p->has_calendar) s->automatic.calendar = p->v.automatic.calendar;
    if (p->has_mac) s->automatic.mac = p->v.automatic.mac;
    if (p->has_meeting_titles) s->automatic.meeting_titles = p->v.automatic.meeting_titles;
    if (p->has_name) tb_strlcpy(s->device.name, p->v.device.name, sizeof(s->device.name));
    if (p->has_time_zone) tb_strlcpy(s->device.time_zone, p->v.device.time_zone, sizeof(s->device.time_zone));
}

static bool clamp_u8(uint8_t *v, int lo, int hi, int def)
{
    if (*v < lo || *v > hi) {
        *v = (uint8_t)def;
        return true;
    }
    return false;
}

bool tb_settings_sanitize(tb_settings_t *s)
{
    bool changed = false;
    changed |= clamp_u8(&s->pomodoro.focus_min, 1, 120, 25);
    changed |= clamp_u8(&s->pomodoro.short_min, 1, 60, 5);
    changed |= clamp_u8(&s->pomodoro.long_min, 1, 60, 15);
    changed |= clamp_u8(&s->pomodoro.long_every, 2, 8, 4);
    changed |= clamp_u8(&s->display.brightness, 10, 100, 70);
    if (s->pomodoro.tick_volume != TB_TICK_SOFT && s->pomodoro.tick_volume != TB_TICK_MEDIUM) {
        s->pomodoro.tick_volume = TB_TICK_SOFT;
        changed = true;
    }
    s->device.name[sizeof(s->device.name) - 1] = '\0';
    s->device.time_zone[sizeof(s->device.time_zone) - 1] = '\0';
    return changed;
}

bool tb_settings_equal(const tb_settings_t *a, const tb_settings_t *b)
{
    return a->pomodoro.focus_min == b->pomodoro.focus_min && a->pomodoro.short_min == b->pomodoro.short_min &&
           a->pomodoro.long_min == b->pomodoro.long_min && a->pomodoro.long_every == b->pomodoro.long_every &&
           a->pomodoro.auto_start == b->pomodoro.auto_start && a->pomodoro.chime == b->pomodoro.chime &&
           a->pomodoro.ticking == b->pomodoro.ticking && a->pomodoro.tick_volume == b->pomodoro.tick_volume &&
           a->display.brightness == b->display.brightness && a->automatic.calendar == b->automatic.calendar &&
           a->automatic.mac == b->automatic.mac && a->automatic.meeting_titles == b->automatic.meeting_titles &&
           !strcmp(a->device.name, b->device.name) && !strcmp(a->device.time_zone, b->device.time_zone);
}
