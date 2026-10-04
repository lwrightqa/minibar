/*
 * test_view.c: every screen's copy, word for word from docs/mockup.html, through the scenes the snapshot tool renders
 * (components/ui/host/ui_scenes.c), plus the overlays, the redraw key, the flash curve and the capitals.
 * Owner: ui builder.
 */
#include <stdlib.h>
#include <string.h>

#include "tb_test.h"
#include "tb_text.h"
#include "ui_scenes.h"
#include "ui_view.h"

static tb_app_t A;
static tb_clock_t NOW;
static ui_view_t V;
static ui_overlay_t O;

static void scene(const char *name)
{
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    const ui_scene_t *s = ui_scene_find(name);
    if (!s) {
        TB_FAIL_AT("no scene %s", name);
        exit(1);
    }
    s->build(&A, &NOW);
    ui_view_build(&A, &NOW, &V);
    ui_overlay_build(&A, &NOW, &O);
}

#define MAIN(k, h, s)                       \
    do {                                    \
        TB_EQ_STR(V.kicker, k);             \
        TB_EQ_STR(V.head, h);               \
        TB_EQ_STR(V.sub, s);                \
    } while (0)
#define SIDE(l, v, ap, f)                   \
    do {                                    \
        TB_TRUE(V.side);                    \
        TB_EQ_STR(V.label, l);              \
        TB_EQ_STR(V.value, v);              \
        TB_EQ_STR(V.value_ampm, ap);        \
        TB_EQ_STR(V.foot, f);               \
    } while (0)

/* ---------- view() ---------- */

TB_TEST(view_available_without_a_calendar)
{
    scene("available");
    TB_EQ_INT(V.layout, UI_LAYOUT_STATUS);
    TB_EQ_INT(V.key, TB_KEY_AVAILABLE);
    MAIN("Status", "Available", "Happy to chat");
    TB_TRUE(V.head_caps);
    TB_EQ_INT(V.fit, UI_FIT_WORD);
    SIDE("Available for", "12m", "", "since 1:52 PM");
    TB_FALSE(V.value_small);
    TB_EQ_INT(V.bar_permille, -1);
    TB_EQ_STR(V.sys_time, "2:04 PM");
    TB_FALSE(V.pill);
}

TB_TEST(view_available_free_until)
{
    scene("available_free");
    SIDE("Free until", "3:00", "PM", "then a meeting");
    scene("available_free_titles");
    SIDE("Free until", "3:00", "PM", "then Design review");
    scene("available_rest");
    SIDE("Free", "Rest of day", "", "no meetings left");
    TB_TRUE(V.value_small);
}

TB_TEST(view_busy_meeting_away)
{
    scene("busy");
    TB_EQ_INT(V.key, TB_KEY_BUSY);
    MAIN("Status", "Busy", "Heads down, message me instead");
    SIDE("Busy for", "12m", "", "since 1:52 PM");
    scene("meeting_manual");
    TB_EQ_INT(V.key, TB_KEY_MEETING);
    MAIN("Status", "In a meeting", "Message me if it's urgent");
    SIDE("Meeting for", "12m", "", "since 1:52 PM");
    scene("meeting_manual_next");
    TB_EQ_STR(V.sub, "Next: Design review at 3:00 PM");
    scene("away_back");
    TB_EQ_INT(V.key, TB_KEY_AWAY);
    MAIN("Away", "Back at 2:30", "Grabbing lunch");
    SIDE("Gone for", "8m", "", "left at 1:56 PM");
    scene("away");      /* proposed: the plain Away screen */
    MAIN("Status", "Away", "Not at my desk");
}

TB_TEST(view_message)
{
    scene("message_short");
    TB_EQ_INT(V.layout, UI_LAYOUT_MESSAGE);
    TB_EQ_INT(V.key, TB_KEY_MESSAGE);
    MAIN("Message", "Out to lunch", "Set from the Remote");
    TB_FALSE(V.head_caps);
    TB_FALSE(V.marquee_long);
    TB_EQ_INT(V.marquee_ms, 6000);
    SIDE("Posted", "2:04", "PM", "today");
    scene("message_long");
    TB_EQ_STR(V.head, "On a deadline until 3 PM, message me instead");
    TB_TRUE(V.marquee_long);                    /* over 22 characters: always scrolls */
    TB_EQ_INT(V.marquee_ms, 44 * 320);          /* max(6 s, 0.32 s per character) */
}

TB_TEST(view_message_from_another_day)
{
    scene("message_short");
    A.message_at -= 86400;
    ui_view_build(&A, &NOW, &V);
    TB_EQ_STR(V.foot, "yesterday");
    A.message_at -= 2 * 86400;
    ui_view_build(&A, &NOW, &V);
    TB_EQ_STR(V.foot, "Oct 1");
    A.message_at = 0;
    ui_view_build(&A, &NOW, &V);
    TB_EQ_STR(V.value, "Earlier");
    TB_TRUE(V.value_small);
}

TB_TEST(view_clock_and_its_info_column)
{
    scene("clock");
    TB_EQ_INT(V.key, TB_KEY_CLOCK);
    MAIN("Sunday, October 4", "2:04", "Idle \xC2\xB7 1 Pomodoro done today");
    TB_EQ_STR(V.head_ampm, "PM");
    TB_EQ_INT(V.fit, UI_FIT_TIME);
    SIDE("Next up", "3:00", "PM", "meeting in 56m");
    scene("clock_titles");
    TB_EQ_STR(V.foot, "Design review \xC2\xB7 Room 4");
    scene("clock_nothing");
    SIDE("Next up", "Nothing", "", "rest of today is free");
    TB_TRUE(V.value_small);
    scene("clock_notsetup");
    SIDE("Calendar", "Not set up", "", "add it on the Remote");
    scene("clock_caloff");
    SIDE("Calendar", "Off", "", "turned off on the Remote");
    scene("clock_offline");
    SIDE("Calendar", "Off", "", "needs Wi-Fi");
    TB_EQ_INT(V.icons[V.n_icons - 1], UI_ICON_WIFI_OFF);
}

TB_TEST(view_clock_pomodoros_plural)
{
    scene("clock");
    A.pomo.done_today = 3;
    ui_view_build(&A, &NOW, &V);
    TB_EQ_STR(V.sub, "Idle \xC2\xB7 3 Pomodoros done today");
    A.pomo.done_today = 0;
    ui_view_build(&A, &NOW, &V);
    TB_EQ_STR(V.sub, "Idle \xC2\xB7 0 Pomodoros done today");
}

TB_TEST(view_clock_not_set)
{
    scene("clock_unset");
    MAIN("Clock not set", "--:--", "Idle \xC2\xB7 1 Pomodoro done today");
    TB_EQ_STR(V.head_ampm, "");
    TB_EQ_STR(V.sys_time, "");
}

/* ---------- pomoView() ---------- */

TB_TEST(view_pomodoro_ready_running_paused)
{
    scene("pomo_ready");
    TB_EQ_INT(V.key, TB_KEY_FOCUS);
    MAIN("Pomodoro \xC2\xB7 ready", "25:00", "Flip or tap to start 25 min of focus");
    TB_EQ_INT(V.fit, UI_FIT_TIME);
    TB_EQ_INT(V.bar_permille, 0);
    TB_FALSE(V.pill);                   /* never on the Pomodoro screen itself */
    TB_TRUE(V.tomatoes);
    TB_EQ_STR(V.label, "Today");
    TB_EQ_STR(V.foot, "1 done \xC2\xB7 31m focused");
    TB_EQ_INT(V.n_tomatoes, 4);
    TB_EQ_INT(V.tomato[0].kind, TB_TOMATO_RIPENING);
    TB_EQ_INT(V.tomato[0].frame, 0);
    TB_EQ_INT(V.tomato[1].kind, TB_TOMATO_GHOST);

    scene("pomo_focus");
    MAIN("Pomodoro \xC2\xB7 focus 2 of 4", "18:42", "Please don't interrupt \xC2\xB7 break at 2:22 PM");
    TB_EQ_INT(V.bar_permille, (1500 - 1122) * 1000 / 1500);
    TB_EQ_INT(V.tomato[0].kind, TB_TOMATO_RIPE);
    TB_EQ_INT(V.tomato[1].kind, TB_TOMATO_RIPENING);
    TB_EQ_INT(V.tomato[1].frame, 6);    /* round((1 - 1122/1500) * 23) */
    TB_EQ_INT(V.tomato[2].kind, TB_TOMATO_GHOST);

    scene("pomo_paused");
    MAIN("Paused \xC2\xB7 Pomodoro \xC2\xB7 focus 2 of 4", "18:42", "Flip or tap to resume");
}

TB_TEST(view_pomodoro_breaks)
{
    scene("pomo_short");
    TB_EQ_INT(V.key, TB_KEY_SHORT);
    MAIN("Short break \xC2\xB7 then focus 3 of 4", "3:10", "Free to chat \xC2\xB7 back to focus at 2:07 PM");
    scene("pomo_long");
    TB_EQ_INT(V.key, TB_KEY_LONG);
    MAIN("Long break \xC2\xB7 then focus 1 of 4", "12:00", "Free to chat \xC2\xB7 back to focus at 2:16 PM");
}

TB_TEST(view_pomodoro_waiting_screens)
{
    scene("pomo_break_time");
    TB_EQ_INT(V.layout, UI_LAYOUT_ALARM);
    MAIN("Focus 2 of 4 done", "Break time", "Flip or tap to start a 5 min short break");
    TB_EQ_INT(V.fit, UI_FIT_62);
    TB_TRUE(V.head_caps);
    TB_EQ_INT(V.bar_permille, -1);
    TB_EQ_STR(V.foot, "2 done \xC2\xB7 31m focused");
    scene("pomo_back_to_it");
    MAIN("Break over", "Back to it", "Flip or tap to start focus 3 of 4");
}

TB_TEST(view_pomodoro_long_break_waiting)
{
    scene("pomo_break_time");
    A.pomo.round = 4;
    A.pomo.phase = TB_PH_LONG;
    ui_view_build(&A, &NOW, &V);
    MAIN("Focus 4 of 4 done", "Break time", "Flip or tap to start a 15 min long break");
}

TB_TEST(view_pomodoro_pill)
{
    scene("pill_focus");
    TB_TRUE(V.pill);
    TB_EQ_STR(V.pill_text, "18:42");
    TB_EQ_INT(V.pill_phase, TB_PH_FOCUS);
    TB_EQ_STR(V.sys_time, "2:04");     /* fmtShort next to the pill */
    scene("pill_paused");
    TB_EQ_STR(V.pill_text, "Paused");
    scene("pill_done");
    TB_EQ_STR(V.pill_text, "Done");
    scene("pill_short");
    TB_EQ_STR(V.pill_text, "3:10");
    TB_EQ_INT(V.pill_phase, TB_PH_SHORT);
}

/* ---------- autoView() ---------- */

TB_TEST(view_on_a_call)
{
    scene("call");
    TB_EQ_INT(V.key, TB_KEY_CALL);
    TB_EQ_INT(V.chip, UI_CHIP_MAC);
    MAIN("Slack", "On a call", "Please keep voices low nearby");
    SIDE("On a call for", "12m", "", "since 1:52 PM");
    TB_EQ_INT(V.bar_permille, -1);
    TB_EQ_INT(V.n_icons, 2);
    TB_EQ_INT(V.icons[0], UI_ICON_MAC);
    TB_EQ_INT(V.icons[1], UI_ICON_WIFI);
    scene("call_noapp");
    TB_EQ_STR(V.kicker, "Mic or camera on");
    scene("call_longname");
    TB_EQ_STR(V.kicker, "Microsoft Teams (work o\xE2\x80\xA6");
    scene("call_paused");
    TB_TRUE(V.pill);
    TB_EQ_STR(V.pill_text, "Paused");
}

TB_TEST(view_call_during_a_meeting)
{
    scene("call_meeting");
    SIDE("Meeting ends in", "26m", "", "at 2:30 PM \xC2\xB7 call 12m");
    TB_EQ_INT(V.bar_permille, 19 * 1000 / 45);
    TB_EQ_STR(V.sub, "Please keep voices low nearby");
    scene("call_meeting_titles");
    TB_EQ_STR(V.sub, "Design review");
}

TB_TEST(view_in_a_meeting_from_the_calendar)
{
    scene("meeting_cal");
    TB_EQ_INT(V.key, TB_KEY_MEETING);
    TB_EQ_INT(V.chip, UI_CHIP_CALENDAR);
    MAIN("1:45\xE2\x80\x93" "2:30 PM", "In a meeting", "Next meeting at 3:00 PM");
    TB_TRUE(V.head_caps);
    SIDE("Ends in", "26m", "", "at 2:30 PM");
    TB_EQ_INT(V.bar_permille, 19 * 1000 / 45);
    scene("meeting_cal_titles");
    MAIN("In a meeting \xC2\xB7 1:45\xE2\x80\x93" "2:30 PM", "Design review", "Room 4 \xC2\xB7 Next: 1:1 with Sam at 3:00 PM");
    TB_FALSE(V.head_caps);
    TB_EQ_INT(V.fit, UI_FIT_62);
    scene("meeting_cal_private");
    MAIN("1:45\xE2\x80\x93" "2:30 PM", "In a meeting", "Next: 1:1 with Sam at 3:00 PM");
}

TB_TEST(view_set_aside_glyphs)
{
    scene("aside_call");
    TB_EQ_INT(V.key, TB_KEY_BUSY);
    TB_EQ_INT(V.n_icons, 3);
    TB_EQ_INT(V.icons[0], UI_ICON_HEADSET);
    TB_EQ_INT(V.icons[1], UI_ICON_MAC);
    TB_EQ_INT(V.icons[2], UI_ICON_WIFI);
    scene("aside_meeting_pill");    /* next to the pill: the glyph and Wi-Fi, not the Mac */
    TB_TRUE(V.pill);
    TB_EQ_INT(V.n_icons, 2);
    TB_EQ_INT(V.icons[0], UI_ICON_CALENDAR);
    TB_EQ_INT(V.icons[1], UI_ICON_WIFI);
    scene("mac_icon");
    TB_EQ_INT(V.n_icons, 2);
    TB_EQ_INT(V.icons[0], UI_ICON_MAC);
}

TB_TEST(view_wifi_dropped_crosses_the_icon)
{
    scene("busy");
    tb_app_wifi_link(&A, false, NULL, NULL, &NOW);
    ui_view_build(&A, &NOW, &V);
    TB_EQ_INT(V.icons[V.n_icons - 1], UI_ICON_WIFI_OFF);
}

/* ---------- wifiView(), the splash, the pairing screen ---------- */

TB_TEST(view_wifi_setup_screens)
{
    scene("setup_qr");
    TB_EQ_INT(V.layout, UI_LAYOUT_SETUP_QR);
    TB_EQ_INT(V.key, TB_KEY_SETUP);
    TB_FALSE(V.side);
    TB_EQ_STR(V.kicker, "Wi-Fi setup");
    TB_EQ_STR(V.head, "Scan to set up");
    TB_FALSE(V.head_caps);
    TB_EQ_STR(V.qr_payload, "WIFI:T:nopass;S:TinyBar-Setup;;");
    TB_EQ_STR(V.step1_pre, "Join ");
    TB_EQ_STR(V.step1_bold, "TinyBar-Setup");
    TB_EQ_STR(V.step1_post, " with your phone");
    TB_EQ_STR(V.step2, "Pick your office Wi-Fi on the page that opens");
    TB_EQ_STR(V.sub, "Hold to skip and use without Wi-Fi");
    scene("setup_connecting");
    TB_EQ_INT(V.layout, UI_LAYOUT_SETUP_TEXT);
    MAIN("Wi-Fi setup", "Connecting", "to Office-WiFi");
    TB_TRUE(V.head_dots);
    scene("setup_connected");
    MAIN("Connected to Office-WiFi", "tinybar.local", "or 10.0.4.42 \xC2\xB7 open it on your phone for the Remote");
    scene("setup_failed");
    MAIN("Couldn't connect to Office-WiFi", "Wrong password", "Tap to try again \xC2\xB7 Hold to skip");
}

TB_TEST(view_splash_and_pairing)
{
    scene("splash");
    TB_EQ_INT(V.layout, UI_LAYOUT_SPLASH);
    TB_EQ_INT(V.key, TB_KEY_CLOCK);
    TB_EQ_STR(V.head, "TinyBar");
    scene("pairing");
    TB_EQ_INT(V.key, TB_KEY_CLOCK);
    MAIN("Pairing \xC2\xB7 Mac", "482 913", "Type this code on that device \xC2\xB7 tap to cancel");
    TB_EQ_INT(V.fit, UI_FIT_TIME);
    SIDE("Code expires", "1:42", "", "");
}

/* ---------- overlays ---------- */

TB_TEST(overlay_toasts_and_where_they_sit)
{
    scene("toast_status");
    TB_TRUE(O.toast);
    TB_EQ_STR(O.toast_text, "Meeting set aside \xC2\xB7 hold to show it again");
    TB_TRUE(O.toast_on_field);
    scene("toast_setup");
    TB_EQ_STR(O.toast_text, "Scan the code with your phone");
    TB_FALSE(O.toast_on_field);         /* no info column: centered on the screen */
    scene("toast_menu");
    TB_TRUE(O.menu);
    TB_EQ_STR(O.toast_text, "Auto-start on");
    TB_FALSE(O.toast_on_field);         /* over a menu: centered on the screen */
    scene("toast_title");
    /* withTitle(): core cuts the title to 24 characters; ui.c cuts it further only if the toast is too wide */
    TB_EQ_STR(O.toast_text, "Calendar: Quarterly planning with\xE2\x80\xA6 started");
    TB_EQ_INT(O.toast_title_off, 10);
    TB_EQ_INT(O.toast_title_len, strlen("Quarterly planning with\xE2\x80\xA6"));
    scene("busy");
    TB_FALSE(O.toast);
}

TB_TEST(overlay_hold_screens)
{
    scene("hold_keep");
    TB_TRUE(O.hold);
    TB_EQ_STR(O.hold_title, "Keep holding");
    TB_EQ_STR(O.hold_sub, "to power off");
    TB_TRUE(O.hold_permille >= 480 && O.hold_permille <= 520);   /* 1.3 s of the 2.6 s fill */
    scene("hold_off");
    TB_EQ_STR(O.hold_title, "Powering off");
    TB_EQ_STR(O.hold_sub, "Press PWR to turn it back on");
    TB_EQ_INT(O.hold_permille, 1000);
}

TB_TEST(overlay_flash_dark_menu)
{
    scene("flash");
    TB_TRUE(O.flash);
    TB_EQ_INT(O.flash_opa, ui_flash_opa(120));
    NOW.mono += TB_FLASH_MS;
    ui_overlay_build(&A, &NOW, &O);
    TB_FALSE(O.flash);
    scene("dark");
    TB_TRUE(O.dark);
    TB_FALSE(O.toast);
    scene("menu_quick");
    TB_TRUE(O.menu);
    TB_FALSE(O.dark);
}

TB_TEST(flash_curve_is_three_ease_out_pulses)
{
    TB_EQ_INT(ui_flash_opa(0), 179);        /* .7 */
    TB_EQ_INT(ui_flash_opa(550), 179);      /* the second pulse */
    TB_EQ_INT(ui_flash_opa(1100), 179);     /* the third */
    TB_TRUE(ui_flash_opa(275) < 60);        /* ease-out: most of the fade is early */
    TB_TRUE(ui_flash_opa(100) > ui_flash_opa(200));
    TB_TRUE(ui_flash_opa(549) <= 1);
    TB_EQ_INT(ui_flash_opa(1650), 0);       /* rests clear after three */
    TB_EQ_INT(ui_flash_opa(-1), 0);
}

/* ---------- the redraw key ---------- */

TB_TEST(key_changes_with_what_the_screen_shows)
{
    scene("pomo_focus");
    uint64_t k = ui_view_key(&A, &NOW);
    TB_TRUE(ui_view_key(&A, &NOW) == k);
    A.rev++;
    TB_TRUE(ui_view_key(&A, &NOW) != k);
    k = ui_view_key(&A, &NOW);
    A.pomo.remaining_ms -= 1000;            /* the timer's second */
    TB_TRUE(ui_view_key(&A, &NOW) != k);
    k = ui_view_key(&A, &NOW);
    NOW.wall += 60;                         /* the shown minute */
    TB_TRUE(ui_view_key(&A, &NOW) != k);
    k = ui_view_key(&A, &NOW);
    NOW.mono += 10;                         /* nothing visible */
    TB_TRUE(ui_view_key(&A, &NOW) == k);
}

/* ---------- capitals ---------- */

TB_TEST(capitals_like_css_uppercase)
{
    char b[64];
    ui_text_upper(b, sizeof b, "Pomodoro \xC2\xB7 focus 2 of 4");
    TB_EQ_STR(b, "POMODORO \xC2\xB7 FOCUS 2 OF 4");
    ui_text_upper(b, sizeof b, "caf\xC3\xA9 r\xC3\xB6sti");
    TB_EQ_STR(b, "CAF\xC3\x89 R\xC3\x96STI");
    ui_text_upper(b, sizeof b, "Stra\xC3\x9F" "e");
    TB_EQ_STR(b, "STRASSE");
    ui_text_upper(b, sizeof b, "\xC3\xB7\xC3\xBF \xE2\x80\x93 \xE2\x80\xA6");   /* ÷ ÿ – … unchanged */
    TB_EQ_STR(b, "\xC3\xB7\xC3\xBF \xE2\x80\x93 \xE2\x80\xA6");
    ui_text_upper(b, 5, "abcdef");
    TB_EQ_STR(b, "ABCD");
    ui_text_upper(b, 3, "\xC3\xA9\xC3\xA9");   /* never half a character */
    TB_EQ_STR(b, "\xC3\x89");
}

/* Every scene builds, and every string it puts on the screen is text the fonts can draw. */
TB_TEST(every_scene_draws_only_drawable_text)
{
    for (int i = 0; i < ui_scene_count; i++) {
        scene(ui_scenes[i].name);
        const char *strs[] = {V.kicker, V.head, V.sub, V.label, V.value, V.foot, V.sys_time, V.pill_text,
                              O.toast_text, O.hold_title, O.hold_sub};
        for (size_t k = 0; k < sizeof strs / sizeof strs[0]; k++) {
            uint32_t bad[4];
            if (tb_text_unsupported(strs[k], bad, 4)) TB_FAIL_AT("%s: \"%s\" has U+%04X", ui_scenes[i].name, strs[k], bad[0]);
        }
    }
}
