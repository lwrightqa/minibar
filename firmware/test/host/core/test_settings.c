/* Seed tests for the settings model (lead). Owner from here: core builder. */
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
    TB_EQ_STR(s.device.name, "TinyBar 2A1C");
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
