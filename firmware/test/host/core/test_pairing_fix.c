/*
 * The pairing fix round (2026-10-04, approved with pairing): what core does differently. Owner: core builder.
 *   - api.md 2.3: characters nobody sees are mapped (odd spaces, zero-width characters, Latin-1 composition).
 *   - The Devices tile with nothing paired says where to pair ("pair at" over the host, the IP or "its IP address").
 *   - Power off and Restart end a pairing without counting it as failed, and clear the back-off (TB_FX_PAIRING_RESET).
 * The held place and pair/cancel are net's (test/host/net/test_pairing_fix.c).
 */
#include <string.h>

#include "core_fixture.h"
#include "tb_text.h"

/* ---------- api.md 2.3: characters nobody sees ---------- */

TB_TEST(text_odd_spaces_become_a_space)
{
    char out[64];
    /* the narrow no-break space Apple's and ICU's times put before PM */
    TB_EQ_INT(tb_text_clean(out, sizeof out, "Back at 3:00\xE2\x80\xAFPM"), 15);
    TB_EQ_STR(out, "Back at 3:00 PM");
    tb_text_clean(out, sizeof out, "5\xE2\x80\x89min");                 /* thin space U+2009 */
    TB_EQ_STR(out, "5 min");
    tb_text_clean(out, sizeof out, "a\xE2\x80\x80" "b\xE2\x80\x8A" "c");  /* U+2000 and U+200A, the ends of the range */
    TB_EQ_STR(out, "a b c");
    tb_text_clean(out, sizeof out, "one\xE2\x80\xA8two\xE2\x80\xA9three");  /* line and paragraph separators */
    TB_EQ_STR(out, "one two three");
    tb_text_clean(out, sizeof out, "x\xE2\x81\x9Fy\xE3\x80\x80z");       /* U+205F, U+3000 */
    TB_EQ_STR(out, "x y z");
    tb_text_clean(out, sizeof out, "\xE3\x80\x80Hi\xE2\x80\xAF");        /* trimmed like any space */
    TB_EQ_STR(out, "Hi");
    tb_text_clean(out, sizeof out, "no\xC2\xA0" "break");               /* U+00A0 is Latin-1: kept as it is */
    TB_EQ_STR(out, "no\xC2\xA0" "break");
}

TB_TEST(text_zero_width_and_marks_are_dropped)
{
    char out[64];
    TB_EQ_INT(tb_text_clean(out, sizeof out, "Busy\xE2\x80\x8B now"), 8);   /* zero-width space */
    TB_EQ_STR(out, "Busy now");
    tb_text_clean(out, sizeof out, "\xEF\xBB\xBF" "Busy");               /* a byte-order mark */
    TB_EQ_STR(out, "Busy");
    tb_text_clean(out, sizeof out, "a\xE2\x80\x8E" "b\xE2\x80\x8F" "c\xE2\x80\xAA" "d\xE2\x80\xAE" "e");  /* LRM RLM LRE RLO */
    TB_EQ_STR(out, "abcde");
    tb_text_clean(out, sizeof out, "a\xE2\x81\xA0" "b\xE2\x81\xA4" "c\xE2\x81\xA6" "d\xE2\x81\xAF" "e");  /* WJ .. U+206F */
    TB_EQ_STR(out, "abcde");
    tb_text_clean(out, sizeof out, "a\xE2\x80\x8C" "b\xE2\x80\x8D" "c");   /* ZWNJ, ZWJ */
    TB_EQ_STR(out, "abc");
    tb_text_clean(out, sizeof out, "\xE2\x9C\x93\xEF\xB8\x8F ok\xEF\xB8\x8E");   /* the variation selectors go */
    TB_EQ_STR(out, "\xE2\x9C\x93 ok");                                   /* (the check mark itself still can't be drawn) */
    uint32_t bad[4];
    TB_EQ_INT(tb_text_unsupported(out, bad, 4), 1);
    TB_EQ_INT(bad[0], 0x2713);
    /* U+FFF9 is a format character the mapping doesn't cover: still refused (the Remote names it as hidden) */
    tb_text_clean(out, sizeof out, "Busy\xEF\xBF\xB9");
    TB_EQ_INT(tb_text_unsupported(out, bad, 4), 1);
    TB_EQ_INT(bad[0], 0xFFF9);
}

TB_TEST(text_latin1_composition)
{
    char out[64];
    TB_EQ_INT(tb_text_clean(out, sizeof out, "Cafe\xCC\x81 at noon"), 12);   /* NFD é: one character */
    TB_EQ_STR(out, "Caf\xC3\xA9 at noon");
    uint32_t bad[4];
    TB_EQ_INT(tb_text_unsupported(out, bad, 4), 0);
    static const struct { const char *in, *want; } cases[] = {
        {"A\xCC\x80", "\xC3\x80"}, {"u\xCC\x80", "\xC3\xB9"},             /* grave */
        {"Y\xCC\x81", "\xC3\x9D"}, {"y\xCC\x81", "\xC3\xBD"},             /* acute */
        {"o\xCC\x82", "\xC3\xB4"},                                       /* circumflex */
        {"N\xCC\x83", "\xC3\x91"}, {"n\xCC\x83", "\xC3\xB1"},             /* tilde */
        {"y\xCC\x88", "\xC3\xBF"}, {"U\xCC\x88", "\xC3\x9C"},             /* diaeresis */
        {"A\xCC\x8A", "\xC3\x85"}, {"a\xCC\x8A", "\xC3\xA5"},             /* ring */
        {"C\xCC\xA7", "\xC3\x87"}, {"c\xCC\xA7", "\xC3\xA7"},             /* cedilla */
        {"e\xCD\x80", "\xC3\xA8"}, {"e\xCD\x81", "\xC3\xA9"},             /* U+0340, U+0341: the grave and acute */
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        tb_text_clean(out, sizeof out, cases[i].in);
        TB_EQ_STR(out, cases[i].want);
    }
    /* No Latin-1 letter: the accent stays, and is named as one the bar can't show */
    tb_text_clean(out, sizeof out, "Y\xCC\x88");                         /* Ÿ is U+0178 */
    TB_EQ_STR(out, "Y\xCC\x88");
    TB_EQ_INT(tb_text_unsupported(out, bad, 4), 1);
    TB_EQ_INT(bad[0], 0x308);
    tb_text_clean(out, sizeof out, "e\xCC\x81\xCC\x81");                 /* é and a second accent */
    TB_EQ_STR(out, "\xC3\xA9\xCC\x81");
    tb_text_clean(out, sizeof out, "b\xCC\x81");                         /* no b with an acute in Latin-1 */
    TB_EQ_STR(out, "b\xCC\x81");
    tb_text_clean(out, sizeof out, "\xE2\x80\x93\xCC\x81");             /* after a mapping ("-"): not composed */
    TB_EQ_STR(out, "-\xCC\x81");
    tb_text_clean(out, sizeof out, " \xCC\x81" "a");                     /* a leading space was dropped: nothing to join */
    TB_EQ_STR(out, "\xCC\x81" "a");
    tb_text_clean(out, sizeof out, "e\xE2\x80\x8B\xCC\x81");             /* a dropped zero-width space between them */
    TB_EQ_STR(out, "\xC3\xA9");
    /* "e" + accent where only the "e" fits stays cut on a character boundary ("Cafe" then no room) */
    char small[5];
    TB_EQ_INT(tb_text_clean(small, sizeof small, "Cafe\xCC\x81"), 4);
    TB_EQ_STR(small, "Cafe");
    char six[6];
    tb_text_clean(six, sizeof six, "Cafe\xCC\x81");                     /* "Café" is 5 bytes: fits with the NUL */
    TB_EQ_STR(six, "Caf\xC3\xA9");
}

/* ---------- The Devices tile with nothing paired ---------- */

TB_TEST(devices_none_says_where_to_pair)
{
    bench_t *b = bench_new();
    tb_app_wifi_link(&b->a, true, "10.0.4.42", "minibar-2.local", &b->now);   /* a renamed host after a clash */
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    const tb_tile_t *t = &b->a.menu.tiles[1];
    TB_EQ_STR(t->label, "Devices");
    TB_EQ_STR(t->value, "None");
    TB_EQ_INT(t->style, TB_TILE_INFO);
    TB_EQ_INT(t->action, TB_ACT_NONE);
    TB_EQ_STR(t->foot, "pair at\nminibar-2.local");    /* ui measures: 93 px of 86.5, so it draws the next */
    TB_EQ_INT(t->n_foot_alt, 2);
    TB_EQ_STR(t->foot_alt[0], "pair at\n10.0.4.42");
    TB_EQ_STR(t->foot_alt[1], "pair at its\nIP address");
    /* the address changes while the menu is open: the tile follows */
    tb_app_wifi_link(&b->a, true, "192.168.100.200", "minibar-2.local", &b->now);
    TB_EQ_STR(b->a.menu.tiles[1].foot_alt[0], "pair at\n192.168.100.200");
    /* read-only: a tap closes the menu, as on Network */
    tap_tile(b, 1);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    TB_EQ_STR(b->a.toast, "");
    /* the link dropped: no IP to offer, only the host and "its IP address" */
    tb_app_wifi_link(&b->a, false, NULL, NULL, &b->now);
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    t = &b->a.menu.tiles[1];
    TB_EQ_STR(t->foot, "pair at\nminibar-2.local");
    TB_EQ_INT(t->n_foot_alt, 1);
    TB_EQ_STR(t->foot_alt[0], "pair at its\nIP address");
    /* offline: how to get there, nothing to measure */
    b = bench_new_opts(false, true);
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI_SKIP);
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    TB_EQ_STR(b->a.menu.tiles[1].foot, "set up Wi-Fi\nto pair");
    TB_EQ_INT(b->a.menu.tiles[1].n_foot_alt, 0);
    /* paired: the count, and a tap opens Forget all (no alternatives either) */
    b = bench_new();
    tb_app_set_paired(&b->a, 3, "iPhone, Desk script, Mac");
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    TB_EQ_STR(tile_with(b, TB_ACT_DEVICES)->value, "3 paired");
    TB_EQ_INT(tile_with(b, TB_ACT_DEVICES)->n_foot_alt, 0);
}

/* The host isn't known yet (mDNS hasn't answered): the default name. */
TB_TEST(devices_none_without_a_host_yet)
{
    bench_t *b = bench_new();
    b->a.wifi_host[0] = '\0';
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    TB_EQ_STR(b->a.menu.tiles[1].foot, "pair at\nminibar.local");
}

/* ---------- Power off and Restart: not a failed pairing, and the back-off goes ---------- */

static bench_t *code_up(void)
{
    bench_t *b = bench_new();
    tb_app_pairing_show(&b->a, "482913", "iPhone", TB_PAIR_KIND_PHONE, &b->now);
    bench_drain(b);
    TB_TRUE(tb_app_pairing_visible(&b->a));
    return b;
}

TB_TEST(power_off_and_restart_reset_pairing)
{
    /* Power off from the menu with no code up: net still hears it (the back-off is cleared) */
    bench_t *b = bench_new();
    hold(b);
    tap_tile_named(b, TB_ACT_POWER);
    tap_tile_named(b, TB_ACT_POWER_OFF);
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_RESET), 1);
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_CANCELED), 0);
    /* PWR held all the way with a code up (a menu can't be open under a code, so this is how a code meets Power off):
     * it ends silently, as a reset, not a failure */
    b = code_up();
    pwr_hold(b, 3100);
    TB_FALSE(b->a.pairing.active);
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_CANCELED), 0);
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_RESET), 1);
    TB_EQ_STR(b->a.toast, "");                          /* no "Pairing canceled": Powering off is the confirmation */
    TB_EQ_INT(b->a.hold, TB_HOLD_POWERING_OFF);
    /* a code asked for while "Powering off" shows waits, and the power goes with it */
    tb_app_pairing_show(&b->a, "111222", "Mac", TB_PAIR_KIND_MAC, &b->now);
    TB_FALSE(tb_app_pairing_visible(&b->a));
    bench_run(b, TB_POWERING_OFF_MS + 100);
    TB_EQ_INT(fx_count(b, TB_FX_POWER_OFF), 1);
    TB_FALSE(tb_app_pairing_visible(&b->a));
    /* Restart from the menu: the reset comes before the restart, so net hears it before the reboot */
    b = bench_new();
    hold(b);
    tap_tile_named(b, TB_ACT_POWER);
    tap_tile_named(b, TB_ACT_RESTART);
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_RESET), 1);
    TB_EQ_INT(fx_count(b, TB_FX_RESTART), 1);
    int reset_at = -1, restart_at = -1;
    for (int i = 0; i < b->nlog; i++) {
        if (b->log[i].kind == TB_FX_PAIRING_RESET && reset_at < 0) reset_at = i;
        if (b->log[i].kind == TB_FX_RESTART && restart_at < 0) restart_at = i;
    }
    TB_TRUE(reset_at >= 0 && reset_at < restart_at);
    /* letting PWR go early is no reset, and the code comes back */
    b = code_up();
    pwr_hold(b, 1000);
    TB_TRUE(tb_app_pairing_visible(&b->a));
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_RESET), 0);
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_CANCELED), 0);
}

/* The other endings still count as failed pairings for net (the back-off), each once. */
TB_TEST(pairing_cancel_endings_still_tell_net)
{
    bench_t *b = code_up();
    flip(b);
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_CANCELED), 1);
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_RESET), 0);
    b = code_up();
    tb_app_pairing_end(&b->a, TB_PAIR_END_CANCELED, NULL, &b->now);   /* net's own ending (pair/cancel): no echo */
    TB_EQ_STR(b->a.toast, "Pairing canceled");
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_CANCELED), 0);
}

/* ---------- A code stops a flash already under way (the mock-up's alarm-flash fix, QA's C21) ---------- */

/* The alarm rang and its flash is mid-pulse when a code arrives: the flash stops at once, so the code never shows in
 * the alarm's white, and the alarm is held (no repeats). When pairing ends it chimes and flashes once. */
TB_TEST(pairing_stops_a_flash_under_way)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, POMO_FOCUS_MS + 100);
    TB_TRUE(b->a.ringing);
    TB_TRUE(b->a.flash_at != 0);
    TB_TRUE(b->now.mono - b->a.flash_at < TB_FLASH_MS);     /* the flash is still running */
    tb_app_pairing_show(&b->a, "482913", NULL, TB_PAIR_KIND_MAC, &b->now);
    bench_drain(b);
    TB_TRUE(tb_app_pairing_visible(&b->a));
    TB_EQ_INT(b->a.flash_at, 0);
    TB_FALSE(b->a.ringing);
    TB_TRUE(b->a.alarm_held_by_pairing);
    bench_clear_log(b);
    bench_run(b, 12000);                                     /* no repeats while the code shows */
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 0);
    TB_EQ_INT(b->a.flash_at, 0);
    tap(b);                                                  /* cancel: one chime and one flash */
    TB_EQ_STR(b->a.toast, "Pairing canceled");
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 1);
    TB_EQ_INT(b->a.flash_at, b->now.mono);
}
