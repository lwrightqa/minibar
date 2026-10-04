/* Settings: every range edge of api.md 10.1, the name counted in characters, sanitize, equal. */
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
    TB_EQ_STR(s.device.name, "TinyBar 0000");
}
