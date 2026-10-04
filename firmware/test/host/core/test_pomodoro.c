/* tb_pomodoro: phaseLen, isReady, nextPhase, startWaiting, resetPomo, the loop's clock, tomatoRow, settings. */
#include "tb_pomodoro.h"
#include "tb_test.h"

static tb_settings_t S;
static tb_pomo_t P;

static void fresh(void)
{
    tb_settings_defaults(&S, "f412fa3f2a1c");
    tb_pomo_init(&P, &S, 20261004);
}

TB_TEST(pomo_fresh_is_ready)
{
    fresh();
    TB_EQ_INT(P.phase, TB_PH_FOCUS);
    TB_EQ_INT(P.round, 1);
    TB_EQ_INT(P.remaining_ms, 25 * 60000);
    TB_TRUE(tb_pomo_is_ready(&P, &S));
    TB_FALSE(tb_pomo_active(&P, &S));
    TB_FALSE(tb_pomo_mid_phase(&P, &S));
    TB_EQ_INT(tb_pomo_state(&P, &S), TB_POMO_ST_READY);
    TB_EQ_INT(tb_pomo_remaining_s(&P), 1500);
    TB_EQ_INT(P.just_ended, -1);
    TB_EQ_STR(tb_phase_name(TB_PH_SHORT), "Short break");
    TB_EQ_STR(tb_phase_name(TB_PH_LONG), "Long break");
    TB_EQ_STR(tb_phase_name(TB_PH_FOCUS), "Focus");
}

TB_TEST(pomo_remaining_seconds_round_up)
{
    fresh();
    P.remaining_ms = 1;
    TB_EQ_INT(tb_pomo_remaining_s(&P), 1);
    P.remaining_ms = 1000;
    TB_EQ_INT(tb_pomo_remaining_s(&P), 1);
    P.remaining_ms = 1001;
    TB_EQ_INT(tb_pomo_remaining_s(&P), 2);
    P.remaining_ms = -500;
    TB_EQ_INT(tb_pomo_remaining_s(&P), 0);
}

TB_TEST(pomo_next_phase_cycle_of_four)
{
    fresh();
    /* focus 1, short, focus 2, short, focus 3, short, focus 4, long, focus 1 */
    const tb_phase_t want[] = {TB_PH_SHORT, TB_PH_FOCUS, TB_PH_SHORT, TB_PH_FOCUS, TB_PH_SHORT, TB_PH_FOCUS, TB_PH_LONG, TB_PH_FOCUS};
    const int round[] = {1, 2, 2, 3, 3, 4, 4, 1};
    for (int i = 0; i < 8; i++) {
        tb_phase_t ended = tb_pomo_next_phase(&P, &S, false);
        TB_EQ_INT(ended, i % 2 == 0 ? TB_PH_FOCUS : (i == 7 ? TB_PH_LONG : TB_PH_SHORT));
        TB_EQ_INT(P.phase, want[i]);
        TB_EQ_INT(P.round, round[i]);
        TB_EQ_INT(P.remaining_ms, tb_pomo_phase_len(&S, P.phase));
    }
    TB_EQ_INT(P.done_today, 4);
    TB_EQ_INT(tb_pomo_phase_len(&S, TB_PH_LONG), 15 * 60000);
}

TB_TEST(pomo_long_break_after_every)
{
    fresh();
    S.pomodoro.long_every = 3;
    for (int i = 0; i < 4; i++) tb_pomo_next_phase(&P, &S, false);   /* f1 > s > f2 > s > f3 */
    TB_EQ_INT(P.round, 3);
    TB_EQ_INT(P.phase, TB_PH_FOCUS);
    tb_pomo_next_phase(&P, &S, false);
    TB_EQ_INT(P.phase, TB_PH_LONG);
}

TB_TEST(pomo_skip_marks_the_tomato_pale_until_the_long_break)
{
    fresh();
    tb_pomo_next_phase(&P, &S, true);          /* focus 1 skipped */
    TB_EQ_INT(P.done_today, 0);
    TB_EQ_INT(P.skipped_mask, 1);
    tb_tomato_t row[TB_POMO_MAX_ROUNDS];
    TB_EQ_INT(tb_pomo_tomatoes(&P, &S, row), 4);
    TB_EQ_INT(row[0].kind, TB_TOMATO_GHOST);   /* k === round and phase !== focus, skipped: ghost */
    for (int i = 0; i < 7; i++) tb_pomo_next_phase(&P, &S, false);   /* ... through the long break */
    TB_EQ_INT(P.round, 1);
    TB_EQ_INT(P.skipped_mask, 0);
    TB_EQ_INT(P.done_today, 3);
}

TB_TEST(pomo_start_waiting_verbs)
{
    fresh();
    TB_EQ_INT(tb_pomo_start_waiting(&P, &S), TB_POMO_STARTED);      /* ready */
    TB_TRUE(P.running);
    TB_EQ_INT(tb_pomo_start_waiting(&P, &S), TB_POMO_NOTHING);      /* already running */
    tb_pomo_advance(&P, &S, 1000);
    P.running = false;
    P.auto_paused = TB_AUTO_CALL;
    TB_EQ_INT(tb_pomo_state(&P, &S), TB_POMO_ST_PAUSED);
    TB_EQ_INT(tb_pomo_start_waiting(&P, &S), TB_POMO_RESUMED);      /* paused */
    TB_EQ_INT(P.auto_paused, TB_AUTO_NONE);
    P.running = false;
    P.waiting = true;
    P.just_ended = TB_PH_FOCUS;
    TB_EQ_INT(tb_pomo_state(&P, &S), TB_POMO_ST_WAITING);
    TB_EQ_INT(tb_pomo_start_waiting(&P, &S), TB_POMO_STARTED);      /* waiting */
    TB_FALSE(P.waiting);
    TB_EQ_INT(P.just_ended, -1);
}

TB_TEST(pomo_advance_credits_focus_without_the_overshoot)
{
    fresh();
    tb_pomo_start_waiting(&P, &S);
    TB_FALSE(tb_pomo_advance(&P, &S, 60000));
    TB_EQ_INT(P.focused_ms, 60000);
    TB_EQ_INT(P.remaining_ms, 24 * 60000);
    P.remaining_ms = 30;
    TB_TRUE(tb_pomo_advance(&P, &S, 50));      /* reaches zero: 30 ms credited, not 50 */
    TB_EQ_INT(P.focused_ms, 60030);
    /* A break counts no focus. */
    tb_pomo_next_phase(&P, &S, false);
    TB_FALSE(tb_pomo_advance(&P, &S, 1000));
    TB_EQ_INT(P.focused_ms, 60030);
    /* Paused: nothing moves. */
    P.running = false;
    TB_FALSE(tb_pomo_advance(&P, &S, 1000));
    TB_EQ_INT(P.remaining_ms, 5 * 60000 - 1000);
}

TB_TEST(pomo_reset_run_keeps_tallies)
{
    fresh();
    tb_pomo_start_waiting(&P, &S);
    tb_pomo_advance(&P, &S, 120000);
    tb_pomo_next_phase(&P, &S, false);
    tb_pomo_next_phase(&P, &S, true);
    P.held_alarm = TB_AUTO_MEETING;
    tb_pomo_reset_run(&P, &S);
    TB_TRUE(tb_pomo_is_ready(&P, &S));
    TB_EQ_INT(P.round, 1);
    TB_EQ_INT(P.skipped_mask, 0);
    TB_EQ_INT(P.held_alarm, TB_AUTO_NONE);
    TB_EQ_INT(P.done_today, 1);
    TB_EQ_INT(P.focused_ms, 120000);
}

TB_TEST(pomo_tomato_row_and_ripening)
{
    fresh();
    tb_pomo_next_phase(&P, &S, false);         /* focus 1 done */
    tb_pomo_next_phase(&P, &S, false);         /* focus 2 */
    tb_pomo_start_waiting(&P, &S);
    P.remaining_ms = 25 * 60000 / 2;          /* halfway */
    tb_tomato_t row[TB_POMO_MAX_ROUNDS];
    TB_EQ_INT(tb_pomo_tomatoes(&P, &S, row), 4);
    TB_EQ_INT(row[0].kind, TB_TOMATO_RIPE);
    TB_EQ_INT(row[1].kind, TB_TOMATO_RIPENING);
    TB_EQ_INT(row[1].frame, 12);              /* Math.round(.5 * 23) = 12 */
    TB_EQ_INT(row[2].kind, TB_TOMATO_GHOST);
    TB_EQ_INT(row[3].kind, TB_TOMATO_GHOST);
    TB_EQ_INT(tb_pomo_ripen_frame(&P, &S), 12);
    P.remaining_ms = 1;
    tb_pomo_tomatoes(&P, &S, row);
    TB_EQ_INT(row[1].frame, 23);
    /* +5 past the focus length: progress below 0 stays at the first frame */
    P.remaining_ms = 30 * 60000;
    TB_EQ_INT(tb_pomo_ripen_frame(&P, &S), 0);
    /* waiting at "Back to it": the round's tomato starts unripe */
    P.remaining_ms = 25 * 60000 / 2;
    P.running = false;
    P.waiting = true;
    tb_pomo_tomatoes(&P, &S, row);
    TB_EQ_INT(row[1].frame, 0);
    /* in the break after focus 2, its tomato is ripe */
    fresh();
    for (int i = 0; i < 3; i++) tb_pomo_next_phase(&P, &S, false);
    tb_pomo_tomatoes(&P, &S, row);
    TB_EQ_INT(row[1].kind, TB_TOMATO_RIPE);
    TB_EQ_INT(row[2].kind, TB_TOMATO_GHOST);
    S.pomodoro.long_every = 8;
    TB_EQ_INT(tb_pomo_tomatoes(&P, &S, row), 8);
}

TB_TEST(pomo_settings_changed)
{
    fresh();
    tb_settings_t old = S;
    tb_pomo_start_waiting(&P, &S);
    tb_pomo_advance(&P, &S, 60000);
    /* Changing the focus length during focus restarts it at the new length. */
    S.pomodoro.focus_min = 50;
    tb_pomo_settings_changed(&P, &old, &S);
    TB_EQ_INT(P.remaining_ms, 50 * 60000);
    /* The same value again (a repeated PATCH) doesn't restart it. */
    tb_pomo_advance(&P, &S, 60000);
    tb_pomo_settings_changed(&P, &S, &S);
    TB_EQ_INT(P.remaining_ms, 49 * 60000);
    /* A break length doesn't touch a focus in progress. */
    old = S;
    S.pomodoro.short_min = 10;
    tb_pomo_settings_changed(&P, &old, &S);
    TB_EQ_INT(P.remaining_ms, 49 * 60000);
    /* Lowering long_every below the round moves the round down and forgets skipped rounds above it. */
    P.round = 4;
    P.skipped_mask = 0x0B;                    /* rounds 1, 2, 4 */
    old = S;
    S.pomodoro.long_every = 3;
    tb_pomo_settings_changed(&P, &old, &S);
    TB_EQ_INT(P.round, 3);
    TB_EQ_INT(P.skipped_mask, 0x03);
}

TB_TEST(pomo_roll_day)
{
    fresh();
    P.done_today = 3;
    P.focused_ms = 4500000;
    TB_FALSE(tb_pomo_roll_day(&P, 20261004));     /* same day */
    TB_FALSE(tb_pomo_roll_day(&P, 0));            /* clock unknown */
    TB_EQ_INT(P.done_today, 3);
    P.round = 2;
    P.running = true;
    TB_TRUE(tb_pomo_roll_day(&P, 20261005));
    TB_EQ_INT(P.done_today, 0);
    TB_EQ_INT(P.focused_ms, 0);
    TB_EQ_INT(P.day, 20261005);
    TB_TRUE(P.running);                           /* the run carries on across midnight */
    TB_EQ_INT(P.round, 2);
    /* Tallies kept while the date was unknown are adopted by the first known day. */
    P.day = 0;
    P.done_today = 2;
    TB_TRUE(tb_pomo_roll_day(&P, 20261006));
    TB_EQ_INT(P.done_today, 2);
}
