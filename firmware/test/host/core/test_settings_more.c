/* Settings: every range edge of api.md 10.1, the name counted in characters, sanitize, equal. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "tb_settings.h"
#include "tb_test.h"

static tb_err_t one(void (*set)(tb_settings_patch_t *, int), int v, bool cal, const char **field)
{
    tb_settings_patch_t p;
    memset(&p, 0, sizeof p);
    set(&p, v);
    return tb_settings_check(&p, cal, field);
}

static void f_focus(tb_settings_patch_t *p, int v) { p->has_focus_min = true; p->v.pomodoro.focus_min = (uint8_t)v; }
static void f_short(tb_settings_patch_t *p, int v) { p->has_short_min = true; p->v.pomodoro.short_min = (uint8_t)v; }
static void f_long(tb_settings_patch_t *p, int v) { p->has_long_min = true; p->v.pomodoro.long_min = (uint8_t)v; }
static void f_every(tb_settings_patch_t *p, int v) { p->has_long_every = true; p->v.pomodoro.long_every = (uint8_t)v; }
static void f_bright(tb_settings_patch_t *p, int v) { p->has_brightness = true; p->v.display.brightness = (uint8_t)v; }
static void f_vol(tb_settings_patch_t *p, int v) { p->has_tick_volume = true; p->v.pomodoro.tick_volume = (tb_tick_vol_t)v; }
static void f_cal(tb_settings_patch_t *p, int v) { p->has_calendar = true; p->v.automatic.calendar = v; }

TB_TEST(settings_range_edges)
{
    const char *f = NULL;
    struct { void (*set)(tb_settings_patch_t *, int); int lo, hi; const char *name; } r[] = {
        {f_focus, 1, 120, "pomodoro.focus_min"}, {f_short, 1, 60, "pomodoro.short_min"},
        {f_long, 1, 60, "pomodoro.long_min"},    {f_every, 2, 8, "pomodoro.long_every"},
        {f_bright, 10, 100, "display.brightness"},
    };
    for (size_t i = 0; i < sizeof r / sizeof r[0]; i++) {
        TB_EQ_INT(one(r[i].set, r[i].lo, false, &f), TB_OK);
        TB_TRUE(f == NULL);
        TB_EQ_INT(one(r[i].set, r[i].hi, false, &f), TB_OK);
        TB_EQ_INT(one(r[i].set, r[i].lo - 1, false, &f), TB_E_BAD_VALUE);
        TB_EQ_STR(f, r[i].name);
        TB_EQ_INT(one(r[i].set, r[i].hi + 1, false, &f), TB_E_BAD_VALUE);
        TB_EQ_STR(f, r[i].name);
    }
    TB_EQ_INT(one(f_vol, TB_TICK_MEDIUM, false, &f), TB_OK);
    TB_EQ_INT(one(f_vol, 7, false, &f), TB_E_BAD_VALUE);
    TB_EQ_STR(f, "pomodoro.tick_volume");
    TB_EQ_INT(one(f_cal, 1, false, &f), TB_E_NO_CALENDAR);
    TB_EQ_STR(f, "automatic.calendar");
    TB_EQ_INT(one(f_cal, 1, true, &f), TB_OK);
    TB_EQ_INT(one(f_cal, 0, false, &f), TB_OK);       /* turning it off needs no address */
}

TB_TEST(settings_name_counts_characters)
{
    tb_settings_patch_t p;
    const char *f = NULL;
    memset(&p, 0, sizeof p);
    p.has_name = true;
    /* 24 × "é": 48 bytes but 24 characters */
    for (int i = 0; i < 24; i++) memcpy(p.v.device.name + 2 * i, "\xC3\xA9", 2);
    TB_EQ_INT(tb_settings_check(&p, false, &f), TB_OK);
    memcpy(p.v.device.name + 48, "\xC3\xA9", 3);      /* 25 characters */
    TB_EQ_INT(tb_settings_check(&p, false, &f), TB_E_BAD_VALUE);
    TB_EQ_STR(f, "device.name");
    strcpy(p.v.device.name, "Desk 4 \xE2\x80\x93 Alex's bar");   /* 19 characters with an en dash */
    TB_EQ_INT(tb_settings_check(&p, false, &f), TB_OK);
    p.v.device.name[0] = '\0';
    TB_EQ_INT(tb_settings_check(&p, false, &f), TB_E_BAD_VALUE);
    /* All or nothing: a bad field anywhere fails the patch, and the caller applies nothing. */
    memset(&p, 0, sizeof p);
    p.has_focus_min = true;
    p.v.pomodoro.focus_min = 50;
    p.has_brightness = true;
    p.v.display.brightness = 5;
    TB_EQ_INT(tb_settings_check(&p, false, &f), TB_E_BAD_VALUE);
    TB_EQ_STR(f, "display.brightness");
}

TB_TEST(settings_time_zone_length)
{
    tb_settings_patch_t p;
    const char *f = NULL;
    memset(&p, 0, sizeof p);
    p.has_time_zone = true;
    strcpy(p.v.device.time_zone, "America/Argentina/ComodRivadavia");
    TB_EQ_INT(tb_settings_check(&p, false, &f), TB_OK);
    p.v.device.time_zone[0] = '\0';
    TB_EQ_INT(tb_settings_check(&p, false, &f), TB_E_BAD_VALUE);
    TB_EQ_STR(f, "device.time_zone");
}

TB_TEST(settings_sanitize_and_equal)
{
    tb_settings_t s, d;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    d = s;
    TB_TRUE(tb_settings_equal(&s, &d));
    TB_FALSE(tb_settings_sanitize(&s));
    s.pomodoro.focus_min = 0;
    s.pomodoro.long_every = 9;
    s.display.brightness = 3;
    s.pomodoro.tick_volume = (tb_tick_vol_t)5;
    TB_FALSE(tb_settings_equal(&s, &d));
    TB_TRUE(tb_settings_sanitize(&s));
    TB_EQ_INT(s.pomodoro.focus_min, 25);
    TB_EQ_INT(s.pomodoro.long_every, 4);
    TB_EQ_INT(s.display.brightness, 70);
    TB_EQ_INT(s.pomodoro.tick_volume, TB_TICK_SOFT);
    TB_TRUE(tb_settings_equal(&s, &d));
    strcpy(d.device.time_zone, "Europe/Paris");
    TB_FALSE(tb_settings_equal(&s, &d));
    tb_settings_defaults(&s, "ab");                 /* a short id falls back to 0000 */
    TB_EQ_STR(s.device.name, "MiniBar 0000");
}

/* ---------- the saved blob, and the two settings added in 1.0.9 ---------- */

TB_TEST(settings_new_fields_defaults_and_patch)
{
    tb_settings_t s;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    TB_FALSE(s.more.time_24h);          /* 12-hour: today's behavior */
    TB_TRUE(s.more.meeting_chime);      /* on by default (decisions.md, Meeting-start sound) */
    tb_settings_patch_t p;
    memset(&p, 0, sizeof p);
    p.has_time_24h = true;
    p.v.more.time_24h = true;
    TB_EQ_INT(tb_settings_check(&p, false, NULL), TB_OK);
    tb_settings_t t = s;
    tb_settings_apply(&t, &p);
    TB_TRUE(t.more.time_24h);
    TB_TRUE(t.more.meeting_chime);      /* only its own field */
    TB_FALSE(tb_settings_equal(&s, &t));
    memset(&p, 0, sizeof p);
    p.has_meeting_chime = true;
    p.v.more.meeting_chime = false;
    tb_settings_apply(&t, &p);
    TB_FALSE(t.more.meeting_chime);
    TB_TRUE(t.more.time_24h);
}

/* The blob is { uint32 version, tb_settings_t }. */
static size_t pack(unsigned char *out, const tb_settings_t *s, size_t n)
{
    uint32_t v = TB_SETTINGS_VERSION;
    memset(out, 0, 4 + sizeof *s);
    memcpy(out, &v, 4);
    memcpy(out + 4, s, sizeof *s);
    return n;
}

TB_TEST(settings_unpack_from_a_1_0_8_blob_keeps_everything_and_defaults_the_new_ones)
{
    tb_settings_t saved;
    tb_settings_defaults(&saved, "f412fa3f2a1c");
    saved.pomodoro.focus_min = 50;
    saved.display.brightness = 100;
    saved.automatic.meeting_titles = true;
    snprintf(saved.device.name, sizeof saved.device.name, "Desk bar");
    snprintf(saved.device.time_zone, sizeof saved.device.time_zone, "Europe/Berlin");
    saved.more.time_24h = true;         /* garbage in the bytes an old firmware never wrote must not count */
    saved.more.meeting_chime = false;
    unsigned char blob[4 + sizeof(tb_settings_t)];
    size_t legacy = tb_settings_legacy_blob_bytes();
    TB_TRUE(legacy < 4 + sizeof(tb_settings_t));     /* the two sizes differ, so a blob's size says which firmware wrote it */
    pack(blob, &saved, legacy);
    tb_settings_t out;
    tb_settings_defaults(&out, "f412fa3f2a1c");
    TB_TRUE(tb_settings_unpack(&out, blob, legacy));
    TB_EQ_INT(out.pomodoro.focus_min, 50);
    TB_EQ_INT(out.display.brightness, 100);
    TB_TRUE(out.automatic.meeting_titles);
    TB_EQ_STR(out.device.name, "Desk bar");
    TB_EQ_STR(out.device.time_zone, "Europe/Berlin");
    TB_FALSE(out.more.time_24h);        /* absent = 12-hour */
    TB_TRUE(out.more.meeting_chime);    /* absent = on */
}

TB_TEST(settings_unpack_current_blob_and_refusals)
{
    tb_settings_t saved, out;
    tb_settings_defaults(&saved, "f412fa3f2a1c");
    saved.more.time_24h = true;
    saved.more.meeting_chime = false;
    unsigned char blob[4 + sizeof saved];
    pack(blob, &saved, sizeof blob);
    tb_settings_defaults(&out, "f412fa3f2a1c");
    TB_TRUE(tb_settings_unpack(&out, blob, sizeof blob));
    TB_TRUE(out.more.time_24h);
    TB_FALSE(out.more.meeting_chime);
    TB_TRUE(tb_settings_equal(&out, &saved));
    /* a bool byte that isn't 0 or 1 reads as "not zero", never as an invalid bool */
    blob[4 + offsetof(tb_settings_t, more.time_24h)] = 0x41;
    blob[4 + offsetof(tb_settings_t, more.meeting_chime)] = 0x00;
    tb_settings_defaults(&out, "f412fa3f2a1c");
    TB_TRUE(tb_settings_unpack(&out, blob, sizeof blob));
    TB_TRUE(out.more.time_24h);
    TB_FALSE(out.more.meeting_chime);
    /* refused: wrong size, wrong version, tiny, null; *out stays as it was */
    tb_settings_defaults(&out, "f412fa3f2a1c");
    out.pomodoro.focus_min = 33;
    TB_FALSE(tb_settings_unpack(&out, blob, sizeof blob - 1));
    TB_FALSE(tb_settings_unpack(&out, blob, sizeof blob + 1));
    TB_FALSE(tb_settings_unpack(&out, blob, 3));
    TB_FALSE(tb_settings_unpack(&out, blob, 0));
    TB_FALSE(tb_settings_unpack(&out, NULL, sizeof blob));
    blob[0] = 9;
    TB_FALSE(tb_settings_unpack(&out, blob, sizeof blob));
    TB_EQ_INT(out.pomodoro.focus_min, 33);
}
