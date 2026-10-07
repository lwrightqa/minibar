/*
 * tb_settings.c: settings defaults, ranges and the all-or-nothing PATCH check (api.md section 10).
 * Owner: core builder. The lead filled in the straightforward parts so the skeleton has something to test.
 */
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "tb_internal.h"
#include "tb_settings.h"
#include "tb_text.h"

/* "<prefix> " plus the last four characters of the id, upper case: "MiniBar 2A1C" (api.md section 3). */
static void default_name(char *out, size_t cap, const char *prefix, const char *device_id)
{
    char tail[5] = "0000";
    size_t n = device_id ? strlen(device_id) : 0;
    if (n >= 4) {
        for (int i = 0; i < 4; i++) tail[i] = (char)toupper((unsigned char)device_id[n - 4 + i]);
    }
    snprintf(out, cap, "%s %s", prefix, tail);
}

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
    default_name(s->device.name, sizeof(s->device.name), "MiniBar", device_id);
    s->device.time_zone[0] = '\0';
    s->more.time_24h = false;           /* 12-hour, today's behavior */
    s->more.meeting_chime = true;
}

bool tb_settings_migrate_name(tb_settings_t *s, const char *device_id)
{
    /* "TinyBar 2A1C" is the default from before the rename, the one name the firmware wrote itself. */
    char old_default[TB_DEVICE_NAME_BYTES];
    default_name(old_default, sizeof old_default, "TinyBar", device_id);
    if (strcmp(s->device.name, old_default) != 0) return false;
    default_name(s->device.name, sizeof(s->device.name), "MiniBar", device_id);
    return true;
}

size_t tb_settings_legacy_blob_bytes(void)
{
    /* Before 1.0.9 tb_settings_t ended where `more` starts, padded to its own alignment (its widest member, an enum). */
    size_t al = _Alignof(tb_tick_vol_t), end = offsetof(tb_settings_t, more);
    return TB_SETTINGS_BLOB_HEADER + ((end + al - 1) / al) * al;
}

bool tb_settings_unpack(tb_settings_t *out, const void *blob, size_t n)
{
    _Static_assert(_Alignof(tb_settings_t) <= TB_SETTINGS_BLOB_HEADER, "the settings follow the 4-byte version directly");
    const unsigned char *b = blob;
    uint32_t version;
    if (!blob || n < sizeof version) return false;
    memcpy(&version, b, sizeof version);
    if (version != TB_SETTINGS_VERSION) return false;
    size_t full = TB_SETTINGS_BLOB_HEADER + sizeof(tb_settings_t);
    if (n == full) {
        unsigned char t24 = b[TB_SETTINGS_BLOB_HEADER + offsetof(tb_settings_t, more.time_24h)];
        unsigned char chime = b[TB_SETTINGS_BLOB_HEADER + offsetof(tb_settings_t, more.meeting_chime)];
        memcpy(out, b + TB_SETTINGS_BLOB_HEADER, sizeof *out);
        out->more.time_24h = t24 != 0;
        out->more.meeting_chime = chime != 0;
        return true;
    }
    if (n == tb_settings_legacy_blob_bytes()) {
        /* Copy only what 1.0.8 wrote: `more` keeps the defaults already in *out. */
        memcpy(out, b + TB_SETTINGS_BLOB_HEADER, offsetof(tb_settings_t, more));
        return true;
    }
    return false;
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
    if (p->has_time_24h) s->more.time_24h = p->v.more.time_24h;
    if (p->has_meeting_chime) s->more.meeting_chime = p->v.more.meeting_chime;
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
           !strcmp(a->device.name, b->device.name) && !strcmp(a->device.time_zone, b->device.time_zone) &&
           a->more.time_24h == b->more.time_24h && a->more.meeting_chime == b->more.meeting_chime;
}
