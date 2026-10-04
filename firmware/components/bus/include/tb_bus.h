/*
 * tb_bus.h: the event bus between TinyBar's tasks and the app task.
 *
 * Owner: lead developer.
 *
 * The app task (main/app_task.c) owns the core model (tb_app_t), LVGL and the ui. Nobody else touches them.
 * Other tasks talk to it in two ways:
 *   tb_bus_post()   fire and forget: buttons, IMU, Wi-Fi and calendar news. Safe from any task;
 *                   tb_bus_post_isr() from an interrupt.
 *   tb_bus_exec()   run a function ON the app task and wait for it: the HTTP and USB router uses it to read and change
 *                   the model and build its JSON reply in one consistent step (api.md section 15, "One router").
 *
 * The queue holds TB_BUS_DEPTH events. A post to a full queue fails (returns false) rather than block, and is
 * counted; a full queue means the app task is stuck, which the task watchdog will catch.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TB_BUS_DEPTH 32

typedef enum {
    TB_EV_NONE = 0,

    /* board */
    TB_EV_BUTTON,           /* u.button: tb_button_t (BOOT click, PWR down, PWR up) */
    TB_EV_ORIENTATION,      /* u.orient: the IMU's flipped state; initial = true for the first reading */

    /* net */
    TB_EV_WIFI,             /* u.wifi: Wi-Fi progress for the screens and the status row */
    TB_EV_TIME_SET,         /* u.time: the wall clock was set (SNTP, RTC at boot, the Mac's hello) */
    TB_EV_USB_LINK,         /* u.flag: a program opened (true) or the bar lost (false) the USB protocol link */

    /* calendar */
    TB_EV_CAL_MEETINGS,     /* u.ptr: a heap-allocated tb_cal_meetings_t; the app task frees it with free() */
    TB_EV_CAL_STATUS,       /* u.cal: saved, checking, last sync */
    TB_EV_CAL_EVENT,        /* u.i32: tb_cal_event_t (saved, setup failed, removed, synced) */

    /* anyone */
    TB_EV_NOTIFY,           /* u.text: a confirmation toast (copied into the event) */
    TB_EV_EXEC,             /* internal to tb_bus_exec() */
} tb_ev_kind_t;

typedef enum {
    TB_WIFI_EV_CONNECTING = 0,  /* setup page sent credentials (ssid) */
    TB_WIFI_EV_CONNECTED,       /* joined during setup (ssid, ip, host) */
    TB_WIFI_EV_FAILED,          /* joining during setup failed (ssid, error text: "Wrong password", "No signal"...) */
    TB_WIFI_EV_LINK_UP,         /* outside setup: joined (ip, host) */
    TB_WIFI_EV_LINK_DOWN,       /* outside setup: dropped */
} tb_wifi_ev_t;

/* Meetings from one sync, as handed to the app task. */
typedef struct {
    uint8_t n;
    tb_meeting_t m[TB_MEETINGS_MAX];
} tb_cal_meetings_t;

typedef void (*tb_exec_fn)(void *ctx);

typedef struct {
    tb_ev_kind_t kind;
    union {
        int32_t i32;
        bool flag;
        void *ptr;
        struct { bool flipped; bool initial; } orient;
        int32_t button;                 /* tb_button_t */
        struct {
            tb_wifi_ev_t ev;
            char ssid[TB_SSID_BYTES];
            char ip[TB_IP_BYTES];
            char host[40];
            char error[40];
        } wifi;
        struct { int32_t source; } time;    /* 1 ntp, 2 rtc, 3 mac (api.md 7.1 time_source) */
        struct { bool saved; bool checking; tb_epoch_t last_sync; } cal;
        char text[96];
        struct { tb_exec_fn fn; void *ctx; void *done; } exec;
    } u;
} tb_event_t;

/* Create the queue. Called once by main before any task starts. */
void tb_bus_init(void);

/* Post an event (copied). Never blocks. Returns false if the queue is full. */
bool tb_bus_post(const tb_event_t *ev);
bool tb_bus_post_isr(const tb_event_t *ev);

/* Convenience wrappers. */
bool tb_bus_post_kind(tb_ev_kind_t kind, int32_t arg);
bool tb_bus_notify(const char *text);

/*
 * Run fn(ctx) on the app task and wait for it. Returns true if it ran. If the app task hasn't started fn within
 * timeout_ms, fn is canceled and never runs (returns false; the router answers 503 busy). If it has started, this
 * waits for it to finish. So ctx only has to live until tb_bus_exec returns, and may sit on the caller's stack.
 * The router uses 900 ms, inside api.md's 1 s reply rule. Calling it from the app task itself runs fn directly.
 */
bool tb_bus_exec(tb_exec_fn fn, void *ctx, uint32_t timeout_ms);

/* App task side: wait up to wait_ms for the next event. Returns false on time-out. EXEC events are run inside
 * (the caller never sees them). */
bool tb_bus_receive(tb_event_t *ev, uint32_t wait_ms);

/* Mark the calling task as the app task (for tb_bus_exec's direct-call shortcut). */
void tb_bus_set_app_task(void);

/* Events dropped because the queue was full, since boot. */
uint32_t tb_bus_dropped(void);

#ifdef __cplusplus
}
#endif
