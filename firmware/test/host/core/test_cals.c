/* test_cals.c: several calendars in core: the Calendar tile's states, the removal that ends a meeting at once, and the
 * list the screens read. Owner: lead developer. Decisions: docs/decisions.md, Multiple calendars (2026-10-07). */
#include <string.h>

#include "core_fixture.h"

static void set_cals(bench_t *b, int n, int failing_mask)
{
    tb_cal_info_t c[TB_CALS_MAX];
    memset(c, 0, sizeof c);
    for (int i = 0; i < n; i++) {
        c[i].used = true;
        c[i].failing = (failing_mask >> i) & 1;
        snprintf(c[i].name, sizeof c[i].name, "Calendar %d", i + 1);
        snprintf(c[i].tag, sizeof c[i].tag, "C%c", (char)('0' + i + 1));
    }
    tb_app_set_cal_list(&b->a, c, &b->now);
}

static const tb_tile_t *open_cal_tile(bench_t *b)
{
    bench_run(b, 1000);
    hold(b);
    return tile_with(b, TB_ACT_SYNC);
}

TB_TEST(cals_tile_with_one_calendar_is_as_before)
{
    bench_t *b = bench_new();
    cal_save(b, NULL, 0);
    set_cals(b, 1, 0);
    bench_run(b, 120000);
    hold(b);
    const tb_tile_t *t = tile_with(b, TB_ACT_SYNC);
    TB_EQ_STR(t->value, "Sync");
    TB_TRUE(strncmp(t->foot, "synced", 6) == 0);
}

TB_TEST(cals_tile_counts_calendars_and_names_the_first_that_cant_sync)
{
    bench_t *b = bench_new();
    cal_save(b, NULL, 0);
    set_cals(b, 3, 0);
    const tb_tile_t *t = open_cal_tile(b);
    TB_EQ_STR(t->value, "Sync");
    TB_EQ_STR(t->foot, "3 calendars");
    tap_tile(b, 0);                 /* close the menu */
    b->a.menu.kind = TB_MENU_NONE;
    set_cals(b, 3, 4);              /* C3 can't sync */
    hold(b);
    t = tile_with(b, TB_ACT_SYNC);
    TB_EQ_STR(t->value, "2/3");
    TB_EQ_STR(t->foot, "C3 can't\nsync");
    b->a.menu.kind = TB_MENU_NONE;
    set_cals(b, 3, 6);              /* C2 and C3: the first is named */
    hold(b);
    t = tile_with(b, TB_ACT_SYNC);
    TB_EQ_STR(t->value, "1/3");
    TB_EQ_STR(t->foot, "C2 can't\nsync");
    b->a.menu.kind = TB_MENU_NONE;
    set_cals(b, 3, 7);
    hold(b);
    t = tile_with(b, TB_ACT_SYNC);
    TB_EQ_STR(t->value, "Error");
    TB_EQ_STR(t->foot, "can't reach\nany");
    b->a.menu.kind = TB_MENU_NONE;
    set_cals(b, 1, 1);              /* a single calendar that can't sync: the same copy, in the singular */
    hold(b);
    t = tile_with(b, TB_ACT_SYNC);
    TB_EQ_STR(t->value, "Error");
    TB_EQ_STR(t->foot, "can't\nreach it");
}

TB_TEST(cals_tile_still_runs_sync_now_when_some_cant_sync)
{
    bench_t *b = bench_new();
    cal_save(b, NULL, 0);
    set_cals(b, 2, 2);
    open_cal_tile(b);
    bench_clear_log(b);
    tap_tile_named(b, TB_ACT_SYNC);
    TB_EQ_INT(fx_count(b, TB_FX_CAL_SYNC), 1);
}

TB_TEST(cals_removing_the_calendar_behind_the_meeting_ends_it_at_once)
{
    bench_t *b = bench_new();
    mt_t m[] = {{-5, 30, "Review", "", false}};
    cal_save(b, m, 1);
    tb_cal_info_t c[TB_CALS_MAX];
    memset(c, 0, sizeof c);
    for (int i = 0; i < 2; i++) {
        c[i].used = true;
        snprintf(c[i].name, sizeof c[i].name, "Calendar %d", i + 1);
        snprintf(c[i].tag, sizeof c[i].tag, "C%c", (char)('0' + i + 1));
    }
    tb_app_set_cal_list(&b->a, c, &b->now);
    bench_run(b, 1000);
    TB_EQ_INT(b->a.shown_kind, TB_AUTO_MEETING);
    /* Calendar 2 goes; its list no longer has the meeting */
    c[1].used = false;
    tb_app_calendar_removed(&b->a, "Calendar 2", c, NULL, 0, &b->now);
    TB_EQ_INT(b->a.shown_kind, TB_AUTO_NONE);
    TB_TRUE(strstr(b->a.toast, "Calendar 2 removed") != NULL);
    TB_EQ_INT(b->a.n_cals, 1);
    TB_TRUE(b->a.set.automatic.calendar);          /* one is left: the switch stays */
}

TB_TEST(cals_removing_one_whose_meeting_another_calendar_also_has_carries_on)
{
    bench_t *b = bench_new();
    mt_t m[] = {{-5, 30, "Review", "", false}};
    cal_save(b, m, 1);
    tb_cal_info_t c[TB_CALS_MAX];
    memset(c, 0, sizeof c);
    c[0].used = true;
    snprintf(c[0].name, sizeof c[0].name, "Calendar 1");
    snprintf(c[0].tag, sizeof c[0].tag, "C1");
    bench_run(b, 1000);
    uint32_t id = b->a.meetings[0].id;
    /* the merged list handed over with the removal still has it (from the other calendar's copy) */
    tb_meeting_t keep = b->a.meetings[0];
    keep.cal = 0;
    tb_app_calendar_removed(&b->a, "Calendar 2", c, &keep, 1, &b->now);
    TB_EQ_INT(b->a.shown_kind, TB_AUTO_MEETING);
    TB_EQ_INT(b->a.shown_id, id);
    TB_FALSE(strstr(b->a.toast, "Calendar 2 removed") != NULL && b->a.shown_kind == TB_AUTO_NONE);
}

TB_TEST(cals_removing_the_last_one_turns_the_switches_off)
{
    bench_t *b = bench_new();
    mt_t m[] = {{-5, 30, "Review", "", false}};
    cal_save(b, m, 1);
    set_cals(b, 1, 0);
    b->a.set.automatic.meeting_titles = true;
    tb_cal_info_t none[TB_CALS_MAX];
    memset(none, 0, sizeof none);
    tb_app_calendar_removed(&b->a, "Calendar 1", none, NULL, 0, &b->now);
    TB_FALSE(b->a.cal_saved);
    TB_FALSE(b->a.set.automatic.calendar);
    TB_FALSE(b->a.set.automatic.meeting_titles);
    TB_EQ_INT(b->a.n_meetings, 0);
    TB_EQ_INT(b->a.shown_kind, TB_AUTO_NONE);
    TB_TRUE(strstr(b->a.toast, "Calendar 1 removed") != NULL);
}

TB_TEST(cals_hostile_list_input_is_cut_and_cleaned)
{
    bench_t *b = bench_new();
    tb_cal_info_t c[TB_CALS_MAX];
    memset(c, 'A', sizeof c);            /* no terminators anywhere */
    for (int i = 0; i < TB_CALS_MAX; i++) c[i].used = true;
    tb_app_set_cal_list(&b->a, c, &b->now);
    TB_EQ_INT(b->a.n_cals, TB_CALS_MAX);
    for (int i = 0; i < TB_CALS_MAX; i++) {
        TB_TRUE(strlen(b->a.cals[i].name) < TB_CAL_NAME_BYTES);
        TB_TRUE(strlen(b->a.cals[i].tag) < TB_CAL_TAG_BYTES);
    }
    /* a meeting whose slot is out of range is read as the first */
    tb_meeting_t m = {.id = 9, .start = b->now.wall + 600, .end = b->now.wall + 1200, .cal = 200};
    tb_app_set_meetings(&b->a, &m, 1, &b->now);
    TB_EQ_INT(b->a.meetings[0].cal, 0);
}
