/*
 * tb_app.h: the whole device as a pure state machine. A port of the mock-up's state (`s`, `pomo`, `cal`, `mac`) and
 * of every handler that changes it: go(), startPause(), skip(), stop(), endPhase(), silence(), setAside(),
 * backToOwn(), showAgain(), takeover(), releaseHeldAlarm(), syncAuto(), toast()/notify(), menuAction(), the pointer,
 * BOOT, PWR and flip handlers, powerOff()/powerOn()/restart(), the Wi-Fi setup taps, and loop().
 *
 * Owner: core builder. Pure C: no ESP-IDF, no LVGL, no clocks, no allocation after tb_app_init().
 *
 * Threading: a tb_app_t belongs to the app task (main/app_task.c). Every other task reaches it through the bus
 * (bus/tb_bus.h: tb_bus_exec runs a function on the app task). Nothing here locks.
 *
 * How the pieces talk:
 *   inputs   tb_app_pointer / tb_app_button / tb_app_flip      (touch, BOOT, PWR, IMU; from ui and board)
 *            tb_app_remote_*                                   (API and Remote; from net's router)
 *            tb_app_set_call / _set_mac_link                   (from net's Mac table)
 *            tb_app_set_meetings / _calendar_event             (from calendar)
 *            tb_app_wifi_* / tb_app_pairing_*                  (from net)
 *            tb_app_tick                                       (the app task, every 50 ms or so)
 *   state    tb_app_t fields below, read by ui (to draw) and net (to answer GET /api/v1/status). Read-only outside core.
 *   outputs  tb_app_take_effects(): what the rest of the firmware must do (sound, backlight, rotation, power, Wi-Fi
 *            setup, calendar sync, saving). main/app_task.c dispatches them.
 *   rev      bumped whenever anything the screen or GET /api/v1/status shows changes (api.md 7.3 rev and ETag).
 *            ui also redraws when the displayed minute or the timer's second changes (mock-up loop() key).
 */
#pragma once

#include "tb_menu.h"
#include "tb_pomodoro.h"
#include "tb_settings.h"
#include "tb_types.h"
#include "tb_gesture.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Timings from the mock-up ---------- */
#define TB_TOAST_MS            1600    /* toast() */
#define TB_ALARM_REPEAT_MS     4000    /* chime and flash every 4 s while ringing */
#define TB_ALARM_LIMIT_MS      60000   /* ...for at most a minute */
#define TB_FLASH_MS            1700    /* the white alarm flash animation */
#define TB_PWR_SHOW_HOLD_MS    400     /* "Keep holding" appears; releasing earlier is a short press */
#define TB_PWR_OFF_MS          3000    /* holding PWR this long powers off */
#define TB_POWERING_OFF_MS     1200    /* "Powering off" shows this long before the power goes */
#define TB_BOOT_SPLASH_MS      1500    /* the splash on power-up and restart */
#define TB_CONNECTED_MS        3000    /* the Connected screen moves on by itself */
#define TB_MAC_TIMEOUT_S       90      /* api.md 5.2 (enforced by net's Mac table, reported here through set_call) */
#define TB_CAL_SYNC_EVERY_S    600     /* about every 10 minutes (calendar's task runs it) */

/* ---------- Effects: what core asks the rest of the firmware to do ---------- */
typedef enum {
    TB_FX_CHIME = 1,        /* arg 1: to a break (784, 988, 1175 Hz), 0: back to focus (1175, 988, 784 Hz) */
    TB_FX_TICKING,          /* arg 0 off, 1 soft, 2 medium: level, emitted when it changes (ticking() rules) */
    TB_FX_BACKLIGHT,        /* arg 0..100: percent; 0 = dark screen (PWR press) */
    TB_FX_ROTATE,           /* arg 1 = flipped 180 degrees */
    TB_FX_POWER_OFF,        /* after "Powering off": deep sleep on USB, SYS_EN low on battery */
    TB_FX_RESTART,          /* Restart tile */
    TB_FX_WIFI_SETUP,       /* show the QR code: start TinyBar-Setup, DNS catch-all and the setup page */
    TB_FX_WIFI_SKIP,        /* Skip: stop the setup network, stay offline */
    TB_FX_WIFI_DONE,        /* the Connected screen was dismissed: setup is over, stop the setup network */
    TB_FX_CAL_SYNC,         /* Sync now from the quick menu */
    TB_FX_SAVE_SETTINGS,    /* settings changed: persist (main debounces NVS writes) */
    TB_FX_SAVE_STATE,       /* own status, last status, message or today's tomatoes changed: persist */
    TB_FX_PAIRING_CANCELED, /* a tap, swipe, hold, BOOT or PWR canceled the pairing screen (net ends the pairing) */
    TB_FX_FORGET_DEVICES,   /* the Devices tile was confirmed: revoke every token */
} tb_effect_kind_t;

typedef struct {
    tb_effect_kind_t kind;
    int32_t arg;
} tb_effect_t;

#define TB_EFFECTS_MAX 16

/* ---------- Overlays ---------- */
typedef enum { TB_HOLD_NONE = 0, TB_HOLD_KEEP_HOLDING, TB_HOLD_POWERING_OFF } tb_hold_t;

typedef enum {
    TB_PAIR_END_PAIRED = 0,     /* "Paired · Mac" */
    TB_PAIR_END_PAIRED_USB,     /* "Paired · Mac · over USB" */
    TB_PAIR_END_TIMEOUT,        /* "Pairing timed out" */
    TB_PAIR_END_WRONG_CODE,     /* "Pairing canceled · wrong code" */
    TB_PAIR_END_CANCELED,       /* "Pairing canceled" */
} tb_pair_end_t;

/* Calendar events that show a toast (mock-up saveCalendar, removeCalendar, the Sync buttons). */
typedef enum {
    TB_CALEV_SAVED = 1,         /* an address passed its check: "Calendar synced · 3 meetings left today" */
    TB_CALEV_SETUP_FAILED,      /* the setup page's address failed: "Calendar address didn't work · add it on the Remote" */
    TB_CALEV_REMOVED,           /* "Calendar removed" */
    TB_CALEV_SYNCED,            /* Sync now finished: "Calendar synced" */
} tb_cal_event_t;

/* Pomodoro actions from the API (api.md 9.1). */
typedef enum { TB_POMO_START = 0, TB_POMO_PAUSE, TB_POMO_TOGGLE, TB_POMO_SKIP, TB_POMO_STOP, TB_POMO_EXTEND } tb_pomo_action_t;

/* Buttons (board posts debounced edges; core does the PWR timing). */
typedef enum { TB_BTN_BOOT = 0, TB_BTN_PWR_DOWN, TB_BTN_PWR_UP } tb_button_t;

/* ---------- The state ---------- */
typedef struct {
    /* settings (persisted by main) */
    tb_settings_t set;

    /* your own status */
    tb_status_t idx;                /* s.idx */
    tb_status_t last_status;        /* s.lastStatus: what Stop returns to (never Pomodoro or Clock) */
    tb_ms_t since_ms;               /* s.since: when you picked it */
    tb_epoch_t since;               /*   ...as wall time, for "since 2:04 PM" (0 = unknown) */
    char message[TB_MESSAGE_BYTES]; /* s.message, "" if never set */
    tb_epoch_t message_at;          /* s.messageAt */
    char away_back_at[6];           /* api.md 8.1 (proposed): "13:30" or "" */
    char away_note[TB_AWAY_NOTE_BYTES];

    /* screen and power */
    bool flipped;                   /* s.flipped: layout turned 180 degrees */
    bool off;                       /* s.off: dark screen after a PWR press */
    bool booting;                   /* s.booting: splash */
    tb_ms_t boot_until;
    bool powering_off;              /* "Powering off" is up; the power goes at hold_since + TB_POWERING_OFF_MS */
    tb_hold_t hold;                 /* the PWR hold overlay */
    tb_ms_t pwr_down_at;            /* 0 when PWR is up */
    tb_ms_t hold_since;

    /* alarm */
    bool ringing;
    tb_ms_t ring_until, next_ring;
    tb_ms_t flash_at;               /* last flash start (ui animates TB_FLASH_MS from here), 0 = none */

    /* Wi-Fi as the screen sees it */
    tb_wifi_mode_t wifi_mode;
    bool wifi_link_up;              /* joined and has an address; false when it dropped (mode stays OK) */
    char wifi_ssid[TB_SSID_BYTES];
    char wifi_ip[TB_IP_BYTES];
    char wifi_host[64];             /* mDNS name actually held: "tinybar.local" or "tinybar-2.local" */
    char wifi_error[48];            /* "Wrong password", "No signal"... (the failed screen's headline) */
    tb_ms_t connected_until;

    /* automatic sources */
    tb_call_t call;                 /* mac.call (aggregate; active=false when none) */
    tb_link_t mac_link;             /* mac.link: a Mac is connected (the Mac icon), NONE otherwise */
    tb_meeting_t meetings[TB_MEETINGS_MAX];
    uint8_t n_meetings;             /* cal.events: today's and tomorrow's that count, sorted by start */
    bool cal_saved;                 /* cal.state === 'ok' */
    bool cal_checking;              /* a check or sync is running ("Sync…" tile) */
    tb_epoch_t cal_last_sync;       /* 0 = never */
    uint32_t aside_call;            /* s.aside.call: id of the call you set aside, 0 = none */
    uint32_t aside_meeting;         /* s.aside.meeting */
    tb_auto_t shown_kind;           /* autoShown: the call or meeting on screen, so changes are announced once */
    uint32_t shown_id;

    /* the Pomodoro */
    tb_pomo_t pomo;
    int8_t ticking_level;           /* last TB_FX_TICKING sent: 0, 1, 2 */

    /* overlays */
    tb_menu_t menu;
    char toast[TB_TOAST_BYTES];     /* "" = none */
    tb_ms_t toast_until;
    char pending_toast[TB_TOAST_BYTES]; /* an announcement that waits for the screen to be free */
    struct {
        bool active;
        char code[8];               /* "482913"; the UI shows it as "482 913" */
        char who[TB_CLIENT_NAME_BYTES];   /* "Mac", "iPhone" */
        tb_ms_t expires;
    } pairing;
    uint8_t paired_count;           /* for the Devices tile ("3 paired · tap to forget all") */

    /* gesture recognizer state */
    tb_gesture_state_t gesture;

    uint32_t rev;

    /* effects not yet taken */
    tb_effect_t fx[TB_EFFECTS_MAX];
    uint8_t n_fx;
} tb_app_t;

/* ---------- Life cycle ---------- */

/* Power-up: settings loaded by main, own status restored from NVS (or defaults), booting = true (splash for 1.5 s,
 * then "Ready"). wifi_configured: credentials exist (else the bar opens on the QR code, as on first start). */
void tb_app_init(tb_app_t *a, const tb_settings_t *s, bool wifi_configured, const tb_clock_t *now);

/* Restore what main saved (TB_FX_SAVE_STATE). Call between init and the first tick. */
void tb_app_restore(tb_app_t *a, tb_status_t idx, tb_status_t last_status, const char *message, tb_epoch_t message_at,
                    uint16_t done_today, tb_ms_t focused_ms, int32_t tallies_day);

/* loop(): timers, the alarm, the Pomodoro clock, automatic-status changes, toasts, menus, PWR hold. ~every 50 ms. */
void tb_app_tick(tb_app_t *a, const tb_clock_t *now);

/* Take the effects queued since the last call (at most max). */
int tb_app_take_effects(tb_app_t *a, tb_effect_t *out, int max);

/* ---------- Inputs from the bar ---------- */

/* A touch sample in logical screen coordinates. tile: the menu tile under (x, y) at this sample, -1 if none
 * (ui hit-tests its tile rectangles). Runs the gesture recognizer and acts on taps, swipes and holds. */
void tb_app_pointer(tb_app_t *a, bool pressed, int16_t x, int16_t y, int8_t tile, const tb_clock_t *now);
/* Let a hold fire while the finger rests (no new samples). The app task calls it every loop. */
void tb_app_pointer_poll(tb_app_t *a, const tb_clock_t *now);
void tb_app_button(tb_app_t *a, tb_button_t b, const tb_clock_t *now);
/* The IMU's orientation. initial = true at boot: just set the layout, no flip semantics. Otherwise a change of
 * orientation is a flip: turn the layout, wake, silence, set aside, start what the Pomodoro waits for. */
void tb_app_flip(tb_app_t *a, bool flipped, bool initial, const tb_clock_t *now);

/* ---------- Inputs from the Remote and the API (net's router; api.md sections 8 to 10) ---------- */
/* Each returns TB_OK or an error; on TB_OK the bar shows the same confirmation the mock-up's Remote does. */
tb_err_t tb_app_remote_status(tb_app_t *a, tb_status_t st, const char *back_at, const char *note, bool set_aside,
                              const tb_clock_t *now);
tb_err_t tb_app_remote_message(tb_app_t *a, const char *text, bool set_aside, const tb_clock_t *now);
tb_err_t tb_app_remote_aside(tb_app_t *a, bool aside, const tb_clock_t *now);
tb_err_t tb_app_remote_pomodoro(tb_app_t *a, tb_pomo_action_t act, int minutes, bool set_aside, const tb_clock_t *now);
tb_err_t tb_app_remote_settings(tb_app_t *a, const tb_settings_patch_t *p, const char **field, const tb_clock_t *now);
/* The time zone from the setup page or the Mac's hello (api.md 6.6: only if none is set yet). No toast; queues
 * TB_FX_SAVE_SETTINGS (main then applies TZ). */
void tb_app_set_time_zone(tb_app_t *a, const char *iana, const tb_clock_t *now);

/* ---------- Automatic sources ---------- */
/* The bar's call changed (net's Mac table). call == NULL or !call->active: no call. lead names why it ended when
 * the caller knows ("Lost contact with your Mac"), else NULL ("Call ended"). */
void tb_app_set_call(tb_app_t *a, const tb_call_t *call, const char *lead, const tb_clock_t *now);
/* Whether any Mac is connected (the status row's Mac icon), and over which link. */
void tb_app_set_mac_link(tb_app_t *a, tb_link_t link, const tb_clock_t *now);
/* Today's and tomorrow's meetings that count, sorted by start (calendar; at most TB_MEETINGS_MAX). */
void tb_app_set_meetings(tb_app_t *a, const tb_meeting_t *m, int n, const tb_clock_t *now);
/* Calendar status for the menus and screens. */
void tb_app_set_calendar(tb_app_t *a, bool saved, bool checking, tb_epoch_t last_sync, const tb_clock_t *now);
void tb_app_calendar_event(tb_app_t *a, tb_cal_event_t ev, const tb_clock_t *now);

/* ---------- Wi-Fi (net) ---------- */
/* The setup page sent credentials: the Connecting screen. */
void tb_app_wifi_connecting(tb_app_t *a, const char *ssid, const tb_clock_t *now);
/* Joined during setup: the Connected screen (host and ip shown); 3 s later, or on a tap, your status. */
void tb_app_wifi_connected(tb_app_t *a, const char *ssid, const char *ip, const char *host, const tb_clock_t *now);
/* Joining during setup failed: "Couldn't connect to X" with error_text as the headline ("Wrong password"). */
void tb_app_wifi_failed(tb_app_t *a, const char *ssid, const char *error_text, const tb_clock_t *now);
/* Outside setup: the link came up or dropped (the bar keeps following its last calendar copy). */
void tb_app_wifi_link(tb_app_t *a, bool up, const char *ip, const char *host, const tb_clock_t *now);

/* ---------- Pairing screen (net; api.md 4.8, proposed) ---------- */
void tb_app_pairing_show(tb_app_t *a, const char *code, const char *who, tb_ms_t expires, const tb_clock_t *now);
void tb_app_pairing_end(tb_app_t *a, tb_pair_end_t why, const char *who, const tb_clock_t *now);
void tb_app_set_paired_count(tb_app_t *a, uint8_t n);

/* A confirmation from elsewhere ("Removed Mac", "Ticking on · Medium"...): shown now, or once the screen is free. */
void tb_app_notify(tb_app_t *a, const char *text, const tb_clock_t *now);

/* ---------- Queries (ui, net) ---------- */
/* autoTop(): the call or meeting that should show now (and its id), or TB_AUTO_NONE. */
tb_auto_t tb_app_auto_top(const tb_app_t *a, const tb_clock_t *now, uint32_t *id);
/* asideKind(): the call or meeting you set aside that's still on, or TB_AUTO_NONE. */
tb_auto_t tb_app_aside_kind(const tb_app_t *a, const tb_clock_t *now);
tb_showing_t tb_app_showing(const tb_app_t *a, const tb_clock_t *now);
/* calData(): meeting data shows (address saved, Calendar meetings on, Wi-Fi not skipped). */
bool tb_app_cal_data(const tb_app_t *a);
/* currentEvent() / nextEvent() / todays().length, by the mock-up's rules. NULL when none. */
const tb_meeting_t *tb_app_current_meeting(const tb_app_t *a, const tb_clock_t *now);
const tb_meeting_t *tb_app_next_meeting(const tb_app_t *a, const tb_clock_t *now);
int tb_app_meetings_left(const tb_app_t *a, const tb_clock_t *now);
/* titleOf() / placeOf(): "" unless Show meeting titles is on and the event isn't private (and a location that's a
 * web address is left out). */
const char *tb_app_title_of(const tb_app_t *a, const tb_meeting_t *m);
const char *tb_app_place_of(const tb_app_t *a, const tb_meeting_t *m);
/* quiet(): a call is on or a calendar meeting is in progress (no sound, even if set aside). */
bool tb_app_quiet(const tb_app_t *a, const tb_clock_t *now);
/* screenFree(): not dark, not booting, no menu, no setup or power screen, no pairing screen. */
bool tb_app_screen_free(const tb_app_t *a);
/* onWifiScreen(). */
bool tb_app_on_wifi_screen(const tb_app_t *a);
/* The color key the screen is painted in right now (status, phase, call, meeting, clock or setup). */
tb_color_key_t tb_app_color_key(const tb_app_t *a, const tb_clock_t *now);

#ifdef __cplusplus
}
#endif
