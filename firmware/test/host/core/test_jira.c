/*
 * The Jira issue count in core (decisions.md "Jira issue count (2026-10-07)"): the state machine, how long a count is
 * kept, the alert, the words, the check interval and back-off, and the screen's place in the swipe order.
 */
#include <string.h>

#include "core_fixture.h"
#include "tb_jira.h"

static tb_jira_t cfg(const char *label, int alert)
{
    tb_jira_t j;
    tb_jira_init(&j);
    tb_jira_configure(&j, label, alert);
    return j;
}

TB_TEST(jira_states_and_transitions)
{
    tb_jira_t j;
    tb_jira_init(&j);
    TB_FALSE(j.configured);
    tb_jira_apply(&j, TB_JIRA_RES_OK, 12, 1000);            /* not configured: ignored */
    TB_FALSE(j.configured);
    TB_EQ_INT(j.count, -1);
    j = cfg("Open bugs", 10);
    TB_TRUE(j.configured);
    TB_EQ_INT(j.state, TB_JIRA_LOADING);
    TB_EQ_INT(j.count, -1);
    TB_EQ_STR(j.label, "Open bugs");
    TB_EQ_INT(j.alert_above, 10);
    tb_jira_apply(&j, TB_JIRA_RES_OK, 12, 1000);
    TB_EQ_INT(j.state, TB_JIRA_OK);
    TB_EQ_INT(j.count, 12);
    TB_EQ_INT(j.ok_at, 1000);
    tb_jira_apply(&j, TB_JIRA_RES_UNREACHABLE, 0, 1300);    /* can't reach: the last count and its time stay */
    TB_EQ_INT(j.state, TB_JIRA_UNREACHABLE);
    TB_EQ_INT(j.count, 12);
    TB_EQ_INT(j.ok_at, 1000);
    tb_jira_apply(&j, TB_JIRA_RES_OK, 0, 1600);             /* zero is a count */
    TB_EQ_INT(j.state, TB_JIRA_OK);
    TB_EQ_INT(j.count, 0);
    tb_jira_apply(&j, TB_JIRA_RES_TOKEN, 0, 1900);          /* rejected: no number at all */
    TB_EQ_INT(j.state, TB_JIRA_TOKEN);
    TB_EQ_INT(j.count, -1);
    TB_EQ_INT(j.ok_at, 0);
    tb_jira_apply(&j, TB_JIRA_RES_OK, 5, 2200);
    tb_jira_apply(&j, TB_JIRA_RES_NOFILTER, 0, 2500);
    TB_EQ_INT(j.state, TB_JIRA_NOFILTER);
    TB_EQ_INT(j.count, -1);
    tb_jira_apply(&j, TB_JIRA_RES_OK, -4, 2800);            /* a negative count (it can't be) shows as zero */
    TB_EQ_INT(j.count, 0);
    /* saving again starts over: loading, nothing known, the new label and limit */
    tb_jira_configure(&j, "Needs review", -1);
    TB_EQ_INT(j.state, TB_JIRA_LOADING);
    TB_EQ_INT(j.count, -1);
    TB_EQ_INT(j.alert_above, -1);
    tb_jira_configure(&j, "x", 10000);                      /* out of range: no alert */
    TB_EQ_INT(j.alert_above, -1);
    tb_jira_clear(&j);
    TB_FALSE(j.configured);
    TB_EQ_STR(j.label, "");
}

TB_TEST(jira_count_kept_two_hours_when_unreachable_then_dropped)
{
    tb_jira_t j = cfg("Bugs", 10);
    int32_t c = -9;
    TB_FALSE(tb_jira_count_shown(&j, 5000, true, &c));      /* loading: no number */
    tb_jira_apply(&j, TB_JIRA_RES_OK, 12, 10000);
    TB_TRUE(tb_jira_count_shown(&j, 10000, true, &c));
    TB_EQ_INT(c, 12);
    TB_TRUE(tb_jira_count_shown(&j, 10000 + 40 * 3600, true, &c));   /* a good answer is never "old" */
    tb_jira_apply(&j, TB_JIRA_RES_UNREACHABLE, 0, 10300);
    TB_TRUE(tb_jira_count_shown(&j, 10000 + 7199, true, &c));        /* 1 s short of 2 hours: kept */
    TB_EQ_INT(c, 12);
    TB_FALSE(tb_jira_count_shown(&j, 10000 + 7200, true, &c));       /* 2 hours: dropped */
    TB_FALSE(tb_jira_count_shown(&j, 10000 + 3 * 3600, true, &c));
    TB_TRUE(tb_jira_count_shown(&j, 5, false, &c));                  /* clock unknown: nothing to measure by, kept */
    TB_FALSE(tb_jira_count_shown(&j, 9000, true, &c) && false);
    tb_jira_apply(&j, TB_JIRA_RES_TOKEN, 0, 10400);
    TB_FALSE(tb_jira_count_shown(&j, 10400, true, &c));              /* rejected or missing: never */
    tb_jira_apply(&j, TB_JIRA_RES_NOFILTER, 0, 10500);
    TB_FALSE(tb_jira_count_shown(&j, 10500, true, &c));
    tb_jira_t off;
    tb_jira_init(&off);
    TB_FALSE(tb_jira_count_shown(&off, 1, true, &c));
    TB_TRUE(tb_jira_count_shown(&j, 0, true, NULL) == false);        /* NULL out is fine */
}

TB_TEST(jira_over_the_alert_limit)
{
    tb_jira_t j = cfg("Bugs", 10);
    tb_jira_apply(&j, TB_JIRA_RES_OK, 10, 100);
    TB_FALSE(tb_jira_over(&j, 100, true));                  /* equal is not over */
    tb_jira_apply(&j, TB_JIRA_RES_OK, 11, 100);
    TB_TRUE(tb_jira_over(&j, 100, true));
    tb_jira_apply(&j, TB_JIRA_RES_UNREACHABLE, 0, 200);
    TB_TRUE(tb_jira_over(&j, 200, true));                   /* the kept count keeps its color */
    TB_FALSE(tb_jira_over(&j, 100 + 7200, true));           /* once dropped it is not over anything */
    tb_jira_t none = cfg("Bugs", -1);
    tb_jira_apply(&none, TB_JIRA_RES_OK, 9999, 100);
    TB_FALSE(tb_jira_over(&none, 100, true));               /* no limit: never */
    tb_jira_t zero = cfg("Bugs", 0);
    tb_jira_apply(&zero, TB_JIRA_RES_OK, 0, 100);
    TB_FALSE(tb_jira_over(&zero, 100, true));
    tb_jira_apply(&zero, TB_JIRA_RES_OK, 1, 100);
    TB_TRUE(tb_jira_over(&zero, 100, true));                /* "above 0" means any issue at all */
}

TB_TEST(jira_words)
{
    char b[24];
    TB_EQ_STR(tb_jira_count_text(b, sizeof b, 0), "0");
    TB_EQ_STR(tb_jira_count_text(b, sizeof b, 1), "1");
    TB_EQ_STR(tb_jira_count_text(b, sizeof b, 9999), "9999");
    TB_EQ_STR(tb_jira_count_text(b, sizeof b, 10000), "10k+");
    TB_EQ_STR(tb_jira_count_text(b, sizeof b, 2000000000), "10k+");
    TB_EQ_STR(tb_jira_count_text(b, sizeof b, -3), "0");
    TB_EQ_STR(tb_jira_ago_text(b, sizeof b, 0), "1 min");
    TB_EQ_STR(tb_jira_ago_text(b, sizeof b, 29), "1 min");
    TB_EQ_STR(tb_jira_ago_text(b, sizeof b, 12 * 60), "12 min");
    TB_EQ_STR(tb_jira_ago_text(b, sizeof b, 59 * 60 + 29), "59 min");
    TB_EQ_STR(tb_jira_ago_text(b, sizeof b, 60 * 60), "1 h");
    TB_EQ_STR(tb_jira_ago_text(b, sizeof b, 65 * 60), "1 h 5 min");
    TB_EQ_STR(tb_jira_ago_text(b, sizeof b, 3 * 3600), "3 h");
    TB_EQ_STR(tb_jira_ago_text(b, sizeof b, -50), "1 min");
}

TB_TEST(jira_check_interval_and_back_off)
{
    TB_EQ_INT(TB_JIRA_EVERY_S, 300);                        /* every 5 minutes */
    TB_EQ_INT(tb_jira_next_s(0, 0), 300);
    TB_EQ_INT(tb_jira_next_s(1, 0), 300);                   /* after failures: 5, 10, 20, then 30 minutes */
    TB_EQ_INT(tb_jira_next_s(2, 0), 600);
    TB_EQ_INT(tb_jira_next_s(3, 0), 1200);
    TB_EQ_INT(tb_jira_next_s(4, 0), 1800);
    TB_EQ_INT(tb_jira_next_s(50, 0), 1800);
    TB_EQ_INT(tb_jira_next_s(-3, 0), 300);
    TB_EQ_INT(tb_jira_next_s(0, 90), 300);                  /* Retry-After can only lengthen it */
    TB_EQ_INT(tb_jira_next_s(0, 900), 900);
    TB_EQ_INT(tb_jira_next_s(4, 900), 1800);
    TB_EQ_INT(tb_jira_next_s(1, 999999), 86400);            /* but never past a day */
}

/* ---------- the screen in the swipe order ---------- */

static void set_jira(bench_t *b, const tb_jira_t *j)
{
    tb_app_set_jira(&b->a, j, &b->now);
    bench_drain(b);
}

TB_TEST(jira_screen_is_out_of_the_swipe_order_until_it_is_set_up)
{
    bench_t *b = bench_new();
    set_jira(b, &(tb_jira_t){0});                           /* the service reported: nothing set up */
    TB_FALSE(tb_app_jira_shown(&b->a));
    go_status(b, TB_ST_AWAY);
    tb_app_remote_message(&b->a, "Hello there", true, &b->now);
    TB_EQ_INT(b->a.idx, TB_ST_MESSAGE);
    swipe(b, -80);
    TB_EQ_INT(b->a.idx, TB_ST_CLOCK);                       /* Message, Clock: the screen doesn't exist */
    swipe(b, 80);
    TB_EQ_INT(b->a.idx, TB_ST_MESSAGE);
    tap(b);
    TB_EQ_INT(b->a.idx, TB_ST_CLOCK);
    /* set up: Message, Jira, Clock, in both directions, and for a tap and BOOT */
    tb_jira_t j = cfg("Open bugs", 10);
    set_jira(b, &j);
    TB_TRUE(tb_app_jira_shown(&b->a));
    swipe(b, 80);
    TB_EQ_INT(b->a.idx, TB_ST_JIRA);
    TB_EQ_STR(tb_status_name(TB_ST_JIRA), "Jira");
    swipe(b, 80);
    TB_EQ_INT(b->a.idx, TB_ST_MESSAGE);
    swipe(b, -80);
    TB_EQ_INT(b->a.idx, TB_ST_JIRA);
    tap(b);
    TB_EQ_INT(b->a.idx, TB_ST_CLOCK);
    tap(b);
    TB_EQ_INT(b->a.idx, TB_ST_AVAILABLE);                   /* and round again */
    go_status(b, TB_ST_MESSAGE);
    boot_btn(b);
    TB_EQ_INT(b->a.idx, TB_ST_JIRA);
    boot_btn(b);
    TB_EQ_INT(b->a.idx, TB_ST_CLOCK);
}

TB_TEST(jira_screen_is_not_a_status_you_can_set_and_does_not_become_the_last_status)
{
    bench_t *b = bench_new();
    tb_jira_t j = cfg("Open bugs", -1);
    set_jira(b, &j);
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_JIRA, NULL, NULL, true, &b->now), TB_E_BAD_VALUE);   /* no Jira button */
    go_status(b, TB_ST_AWAY);
    tb_app_remote_message(&b->a, "Hello", true, &b->now);
    swipe(b, -80);
    TB_EQ_INT(b->a.idx, TB_ST_JIRA);
    TB_EQ_INT(b->a.last_status, TB_ST_MESSAGE);             /* Jira, like Clock and Pomodoro, isn't what Stop returns to */
    TB_TRUE(b->a.since_ms == b->now.mono);
}

TB_TEST(jira_removed_on_screen_goes_back_to_the_last_status)
{
    bench_t *b = bench_new();
    tb_jira_t j = cfg("Open bugs", -1);
    set_jira(b, &j);
    go_status(b, TB_ST_AWAY);
    tb_app_remote_message(&b->a, "Hello", true, &b->now);
    swipe(b, -80);
    TB_EQ_INT(b->a.idx, TB_ST_JIRA);
    tb_jira_clear(&j);
    set_jira(b, &j);                                        /* Remove on the Remote */
    TB_EQ_INT(b->a.idx, TB_ST_MESSAGE);                     /* your last status */
    TB_FALSE(tb_app_jira_shown(&b->a));
    swipe(b, -80);
    TB_EQ_INT(b->a.idx, TB_ST_CLOCK);                       /* and the screen is gone from the order */
    /* removed while another screen shows: nothing moves */
    set_jira(b, &(tb_jira_t){0});
    TB_EQ_INT(b->a.idx, TB_ST_CLOCK);
}

TB_TEST(jira_saved_status_survives_a_restart_until_the_service_reports)
{
    bench_t *b = bench_new();
    tb_app_restore(&b->a, TB_ST_JIRA, TB_ST_BUSY, "", 0, 0, 0, tb_local_yyyymmdd(T0_WALL));
    bench_run(b, 3000);
    TB_EQ_INT(b->a.idx, TB_ST_JIRA);                        /* the service hasn't spoken: don't drop it */
    tb_jira_t j = cfg("Open bugs", -1);
    set_jira(b, &j);
    TB_EQ_INT(b->a.idx, TB_ST_JIRA);                        /* set up: it stays */
    b = bench_new();
    tb_app_restore(&b->a, TB_ST_JIRA, TB_ST_BUSY, "", 0, 0, 0, tb_local_yyyymmdd(T0_WALL));
    set_jira(b, &(tb_jira_t){0});
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);                        /* removed while the bar was off: back to the last status */
    /* a status saved by firmware before 1.0.9 means what it did: Clock was 6 and still is */
    TB_EQ_INT(TB_ST_CLOCK, 6);
    TB_EQ_INT(TB_ST_JIRA, 7);
}

TB_TEST(jira_color_blue_normally_orange_over_the_limit)
{
    bench_t *b = bench_new();
    tb_jira_t j = cfg("Open bugs", 10);
    set_jira(b, &j);
    go_status(b, TB_ST_AWAY);
    tb_app_remote_message(&b->a, "Hi", true, &b->now);
    swipe(b, -80);
    TB_EQ_INT(b->a.idx, TB_ST_JIRA);
    TB_EQ_INT(tb_app_color_key(&b->a, &b->now), TB_KEY_JIRA);
    tb_jira_apply(&j, TB_JIRA_RES_OK, 12, b->now.wall);
    uint32_t rev = b->a.rev;
    set_jira(b, &j);
    TB_EQ_INT(tb_app_color_key(&b->a, &b->now), TB_KEY_FOCUS);   /* Focus's orange: the words say Over your limit */
    TB_TRUE(b->a.rev != rev);                                    /* a new count redraws and moves the API's rev */
    tb_jira_apply(&j, TB_JIRA_RES_OK, 10, b->now.wall);
    set_jira(b, &j);
    TB_EQ_INT(tb_app_color_key(&b->a, &b->now), TB_KEY_JIRA);
}

TB_TEST(jira_a_call_covers_it_and_carries_on_underneath)
{
    bench_t *b = bench_new();
    tb_jira_t j = cfg("Open bugs", -1);
    set_jira(b, &j);
    go_status(b, TB_ST_AWAY);
    tb_app_remote_message(&b->a, "Hi", true, &b->now);
    swipe(b, -80);
    TB_EQ_INT(b->a.idx, TB_ST_JIRA);
    call_start(b, "Slack");
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);
    TB_EQ_INT(b->a.idx, TB_ST_JIRA);                        /* still there underneath */
    tap(b);                                                 /* the call is set aside: back to your own status */
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    TB_EQ_INT(b->a.idx, TB_ST_JIRA);
    tap(b);                                                 /* and a tap goes on to the next screen */
    TB_EQ_INT(b->a.idx, TB_ST_CLOCK);
}
