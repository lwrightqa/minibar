/* Seed tests for the settings model (lead). Owner from here: core builder. */
#include <stdio.h>

#include "tb_settings.h"
#include "tb_test.h"

TB_TEST(settings_defaults)
{
    tb_settings_t s;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    TB_EQ_INT(s.pomodoro.focus_min, 25);
    TB_EQ_INT(s.pomodoro.short_min, 5);
    TB_EQ_INT(s.pomodoro.long_min, 15);
    TB_EQ_INT(s.pomodoro.long_every, 4);
    TB_FALSE(s.pomodoro.auto_start);
    TB_TRUE(s.pomodoro.chime);
    TB_FALSE(s.pomodoro.ticking);
    TB_EQ_INT(s.pomodoro.tick_volume, TB_TICK_SOFT);
    TB_EQ_INT(s.display.brightness, 70);
    TB_FALSE(s.automatic.calendar);
    TB_TRUE(s.automatic.mac);
    TB_FALSE(s.automatic.meeting_titles);
    TB_EQ_STR(s.device.name, "MiniBar 2A1C");
}

/* The rename (1.0.3): only the old default name, the one the firmware wrote itself, becomes the new default. */
TB_TEST(settings_name_migrates_from_the_old_default_only)
{
    tb_settings_t s;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    /* A bar set up before the rename saved "TinyBar 2A1C". */
    snprintf(s.device.name, sizeof s.device.name, "TinyBar 2A1C");
    TB_TRUE(tb_settings_migrate_name(&s, "f412fa3f2a1c"));
    TB_EQ_STR(s.device.name, "MiniBar 2A1C");
    /* Already the new default: nothing to do, nothing to save. */
    TB_FALSE(tb_settings_migrate_name(&s, "f412fa3f2a1c"));
    TB_EQ_STR(s.device.name, "MiniBar 2A1C");
    /* A name a person typed stays, however close to the old default. */
    const char *typed[] = {"Desk by the window", "TinyBar", "TinyBar 2A1C ", "tinybar 2a1c", "TinyBar 2A1D", "TinyBar 0000"};
    for (size_t i = 0; i < sizeof typed / sizeof typed[0]; i++) {
        snprintf(s.device.name, sizeof s.device.name, "%s", typed[i]);
        TB_FALSE(tb_settings_migrate_name(&s, "f412fa3f2a1c"));
        TB_EQ_STR(s.device.name, typed[i]);
    }
    /* With no id the old default was "TinyBar 0000", and only that. */
    snprintf(s.device.name, sizeof s.device.name, "TinyBar 0000");
    TB_TRUE(tb_settings_migrate_name(&s, NULL));
    TB_EQ_STR(s.device.name, "MiniBar 0000");
    /* The id's case doesn't matter: the default upper-cases the tail. */
    snprintf(s.device.name, sizeof s.device.name, "TinyBar 2A1C");
    TB_TRUE(tb_settings_migrate_name(&s, "F412FA3F2A1C"));
    TB_EQ_STR(s.device.name, "MiniBar 2A1C");
}

TB_TEST(settings_patch_ranges)
{
    tb_settings_patch_t p = {0};
    const char *field = NULL;
    p.has_focus_min = true;
    p.v.pomodoro.focus_min = 121;
    TB_EQ_INT(tb_settings_check(&p, false, &field), TB_E_BAD_VALUE);
    TB_EQ_STR(field, "pomodoro.focus_min");
    p.v.pomodoro.focus_min = 50;
    TB_EQ_INT(tb_settings_check(&p, false, &field), TB_OK);

    tb_settings_patch_t q = {0};
    q.has_meeting_titles = true;
    q.v.automatic.meeting_titles = true;
    TB_EQ_INT(tb_settings_check(&q, false, &field), TB_E_NO_CALENDAR);
    TB_EQ_STR(field, "automatic.meeting_titles");
    TB_EQ_INT(tb_settings_check(&q, true, &field), TB_OK);
}

TB_TEST(settings_patch_applies_only_what_is_set)
{
    tb_settings_t s;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    tb_settings_patch_t p = {0};
    p.has_ticking = p.has_tick_volume = true;
    p.v.pomodoro.ticking = true;
    p.v.pomodoro.tick_volume = TB_TICK_MEDIUM;
    tb_settings_apply(&s, &p);
    TB_TRUE(s.pomodoro.ticking);
    TB_EQ_INT(s.pomodoro.tick_volume, TB_TICK_MEDIUM);
    TB_EQ_INT(s.pomodoro.focus_min, 25);
}
