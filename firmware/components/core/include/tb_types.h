/*
 * tb_types.h: types shared by every MiniBar module. Pure C, no ESP-IDF headers, so it builds on Linux too.
 *
 * Owner: core builder. Other modules include it read-only; ask the core builder for changes.
 *
 * Time: MiniBar keeps two clocks.
 *   tb_ms_t     monotonic milliseconds since boot (esp_timer_get_time() / 1000 on the device, a fake clock in
 *               tests). Every duration and timer (Pomodoro, alarm repeats, menus, toasts, the Mac time-out) runs on it.
 *   tb_epoch_t  wall-clock seconds since 1970 UTC. What the screen prints ("2:04 PM") and what calendar events use.
 *               It may be unknown (no network time, clock chip never set): see tb_clock_t.valid.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t tb_ms_t;
typedef int64_t tb_epoch_t;

/* "Now", passed into every core function that needs it, so tests drive time and nothing in core reads a clock. */
typedef struct {
    tb_ms_t mono;       /* monotonic ms since boot */
    tb_epoch_t wall;    /* UTC seconds; meaningful only when valid */
    bool valid;         /* false until SNTP, the RTC or the Mac's hello set the clock (api.md 7.1 time_source) */
} tb_clock_t;

/* ---------- Limits (api.md 2.5) ---------- */
#define TB_MESSAGE_MAX_CHARS 80     /* custom message, after trimming */
#define TB_MESSAGE_BYTES     (TB_MESSAGE_MAX_CHARS * 3 + 1)   /* UTF-8, Latin-1 and the few 3-byte marks we draw */
#define TB_APP_NAME_BYTES    65     /* call "app": 1 to 64 bytes; the bar shows up to 24 characters */
#define TB_APP_SHOW_CHARS    24
#define TB_AWAY_NOTE_CHARS   40
#define TB_AWAY_NOTE_BYTES   (TB_AWAY_NOTE_CHARS * 3 + 1)
#define TB_DEVICE_NAME_CHARS 24
#define TB_DEVICE_NAME_BYTES (TB_DEVICE_NAME_CHARS * 3 + 1)
#define TB_CLIENT_NAME_BYTES (32 * 3 + 1)
#define TB_TZ_NAME_BYTES     48     /* IANA name, e.g. "America/Los_Angeles" */
#define TB_SSID_BYTES        33
#define TB_IP_BYTES          16
#define TB_TITLE_BYTES       128    /* meeting title kept on the bar (cut on a character boundary) */
#define TB_LOCATION_BYTES    96
#define TB_MEETINGS_MAX      32     /* today's and tomorrow's meetings that count */
#define TB_CALS_MAX          3      /* calendars (decisions.md, Multiple calendars) */
#define TB_CAL_NAME_BYTES    25     /* a calendar's name: up to 24 characters (ASCII and what the fonts draw) */
#define TB_CAL_TAG_BYTES     5      /* its bar tag: up to 4 capitals or digits */
#define TB_TOAST_BYTES       128

/* ---------- Your own status: the tap and swipe cycle, in the mock-up's STATES order ---------- */
typedef enum {
    TB_ST_AVAILABLE = 0,
    TB_ST_BUSY,
    TB_ST_MEETING,      /* In a meeting picked by hand */
    TB_ST_POMODORO,
    TB_ST_AWAY,
    TB_ST_MESSAGE,
    TB_ST_CLOCK,        /* idle */
    TB_ST_COUNT
} tb_status_t;

/* Pomodoro phases (mock-up PHASE_NAME). */
typedef enum { TB_PH_FOCUS = 0, TB_PH_SHORT, TB_PH_LONG } tb_phase_t;

/* The color a screen is painted in: the Bold Signal palette keys (decisions.md "Spec: Bold Signal"). */
typedef enum {
    TB_KEY_AVAILABLE = 0,
    TB_KEY_BUSY,
    TB_KEY_MEETING,
    TB_KEY_CALL,
    TB_KEY_FOCUS,
    TB_KEY_SHORT,
    TB_KEY_LONG,
    TB_KEY_AWAY,
    TB_KEY_MESSAGE,
    TB_KEY_CLOCK,       /* dark surface #0E1013, info column #1B1E23 */
    TB_KEY_SETUP,       /* Wi-Fi setup #1D3557 */
    TB_KEY_COUNT
} tb_color_key_t;

/* An automatic source, or which one is on screen / set aside / paused the timer. */
typedef enum { TB_AUTO_NONE = 0, TB_AUTO_CALL, TB_AUTO_MEETING } tb_auto_t;

/* What the bar shows, as api.md 7.3 "showing" reports it. Overlays (menus, toasts, pairing, power) don't change it. */
typedef enum { TB_SHOWING_OWN = 0, TB_SHOWING_CALL, TB_SHOWING_MEETING, TB_SHOWING_SETUP } tb_showing_t;

/* How a Mac reached the bar. */
typedef enum { TB_LINK_NONE = 0, TB_LINK_USB, TB_LINK_WIFI } tb_link_t;

/* The Wi-Fi screen mode, as the mock-up's s.wifi.mode. OK = joined the office Wi-Fi (or joining it in the
 * background after a restart); OFFLINE = skipped. The four setup modes are full-screen (onWifiScreen()). */
typedef enum {
    TB_WIFI_OK = 0,
    TB_WIFI_OFFLINE,
    TB_WIFI_SETUP,          /* QR code */
    TB_WIFI_CONNECTING,
    TB_WIFI_CONNECTED,      /* "minibar.local" for 3 s or until a tap */
    TB_WIFI_FAILED,
} tb_wifi_mode_t;

typedef enum { TB_TICK_SOFT = 0, TB_TICK_MEDIUM } tb_tick_vol_t;

/* The bar's call, as the status engine sees it: the aggregate of every connected Mac (net/net_macs.h builds it). */
typedef struct {
    bool active;
    uint32_t id;                    /* bar-level call id: changes for each new call (aside is keyed by it) */
    char app[TB_APP_NAME_BYTES];    /* "" when no name was sent */
    tb_link_t via;
    tb_ms_t since_ms;               /* monotonic start, for "on a call for 12m" */
    tb_epoch_t since;               /* wall start, for "since 2:04 PM" (0 when the clock is unknown) */
} tb_call_t;

/* One calendar meeting that counts (timed, busy, not declined, not cancelled), as calendar/cal_today.h outputs it. */
typedef struct {
    uint32_t id;                    /* stable per instance: hash of UID and the instance's start (aside is keyed by it) */
    tb_epoch_t start, end;          /* UTC */
    char title[TB_TITLE_BYTES];     /* "" when the event has no title */
    char location[TB_LOCATION_BYTES];
    bool priv;                      /* CLASS:PRIVATE or CONFIDENTIAL: never show the title or location */
    uint8_t cal;                    /* the calendar slot (0 to TB_CALS_MAX - 1) the merged list took it from (its tag) */
} tb_meeting_t;

/* One calendar as the screens see it, by slot. The address never gets here. */
typedef struct {
    bool used;
    bool failing;                   /* its last sync failed: its meetings are left out until it works */
    char name[TB_CAL_NAME_BYTES];
    char tag[TB_CAL_TAG_BYTES];
} tb_cal_info_t;

/* Result codes for core operations; net maps them to api.md Appendix B error codes. */
typedef enum {
    TB_OK = 0,
    TB_E_BAD_VALUE,             /* 400 bad_value */
    TB_E_IN_SETUP,              /* 409 in_setup */
    TB_E_NO_MESSAGE,            /* 409 no_message */
    TB_E_NOTHING_TO_SET_ASIDE,  /* 409 nothing_to_set_aside */
    TB_E_NOTHING_SET_ASIDE,     /* 409 nothing_set_aside */
    TB_E_NOT_RUNNING,           /* 409 not_running */
    TB_E_NOTHING_TO_EXTEND,     /* 409 nothing_to_extend */
    TB_E_NO_CALENDAR,           /* 409 no_calendar */
    TB_E_POWERED_OFF,           /* the bar is powering off or starting up; net answers 503 busy */
} tb_err_t;

#ifdef __cplusplus
}
#endif
