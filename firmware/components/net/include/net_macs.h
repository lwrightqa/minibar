/*
 * net_macs.h: the Macs the bar hears from, and the bar's call as their sum (api.md 5.2). Pure C.
 *
 * Owner: net builder.
 *
 * Rules (api.md 5.2): each Mac by `client`, at most 4 (the least recently heard is dropped); stale messages
 * (same session, seq not higher) are ignored with "stale": true; a new call when a Mac goes from not active to active
 * or reports a different call_id; its start = now - elapsed_s; the same call keeps its start and updates the app name;
 * active false ends that Mac's call; leaving true ends it and marks the Mac not connected; 90 s without a call
 * message (or a USB hello) ends that Mac's call and marks it not connected ("Lost contact with your Mac · call
 * ended" when it was on screen). The bar's call is on while any connected Mac has one; its app and via are those of
 * the call that started most recently.
 *
 * The bar's call id (tb_call_t.id, which set aside is keyed by) changes only when some Mac starts a new call, so
 * "Set aside and Show again apply to the bar's call as a whole": one Mac ending its call while another's goes on
 * keeps the id, and the call stays set aside. The next new call from any Mac takes over again.
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_MACS_MAX 4
#define NET_MAC_TIMEOUT_MS 90000    /* api.md 5.2 timeout_s */
#define NET_MAC_HEARTBEAT_S 30      /* api.md 5.3 heartbeat_s */

/* inputs bits (api.md 5.1, Proposed: reported, never shown) */
#define NET_INPUT_MIC    1
#define NET_INPUT_CAMERA 2

typedef struct {
    bool used;
    char client[65];
    char name[TB_CLIENT_NAME_BYTES];    /* from hello or pairing, "" = "Mac" */
    tb_link_t via;                      /* link of its latest message */
    bool connected;
    tb_ms_t last_heard_ms;
    tb_epoch_t last_heard;              /* 0 when the clock was unknown */
    char session[17];
    uint32_t seq;
    bool has_session;                   /* session (and seq) recorded from the last accepted message */
    bool has_seq;
    bool active;                        /* this Mac reports a call */
    uint32_t call_id;                   /* the Mac's own call_id (0 = not sent) */
    uint32_t bar_call_id;               /* order of call starts: higher = started more recently */
    char app[TB_APP_NAME_BYTES];        /* "" when not sent */
    int8_t inputs;                      /* -1 not sent, else NET_INPUT_* bits */
    tb_ms_t since_ms;
    tb_epoch_t since;                   /* 0 while the clock is unknown; filled in once it's known */
} net_mac_t;

typedef struct {
    net_mac_t m[NET_MACS_MAX];
    uint32_t next_call_id;              /* the next call start's number */
    uint32_t bar_id;                    /* the bar's call id: changes when any Mac starts a new call */
} net_macs_t;

/* A call message (api.md 5.1), already validated by the router. */
typedef struct {
    const char *client;
    const char *session;        /* NULL if not sent */
    uint32_t seq;
    bool has_seq;
    bool active;
    const char *app;            /* NULL if not sent or null (already cleaned) */
    int8_t inputs;              /* -1 not sent, else bits */
    uint32_t call_id;           /* 0 if not sent */
    int32_t elapsed_s;          /* -1 if not sent */
    bool leaving;
    tb_link_t via;
    const char *name;           /* a label for a Mac not named yet (its pairing's name), or NULL */
} net_call_msg_t;

void net_macs_init(net_macs_t *t);
/* Apply a call message. Returns false if it was stale (ignored, nothing changed). */
bool net_macs_on_call(net_macs_t *t, const net_call_msg_t *msg, const tb_clock_t *now);
/* A USB hello: counts as a heartbeat and marks the Mac connected over USB, without changing its call.
 * name NULL keeps the Mac's label. */
void net_macs_on_hello(net_macs_t *t, const char *client, const char *name, const tb_clock_t *now);
/* Run the 90 s time-outs (and fill in call start times once the clock is known). Returns true if a Mac with a call
 * was dropped (the "Lost contact" case). */
bool net_macs_tick(net_macs_t *t, const tb_clock_t *now);
/* The device with this client stopped being trusted (its Wi-Fi token was revoked, api.md 12.2): a call it reported
 * over Wi-Fi ends and it's no longer connected over Wi-Fi. Returns true if anything changed. */
bool net_macs_forget_wifi(net_macs_t *t, const char *client);
/* The bar's call (out->active false when none) and the link of the most recently heard connected Mac (the icon).
 * mac (optional): the Mac whose call is shown (for status.call.inputs), NULL when none. */
void net_macs_aggregate(const net_macs_t *t, tb_call_t *out, tb_link_t *link, const net_mac_t **mac);
/* The Macs most recently heard first, for status.macs (returns the count). */
int net_macs_sorted(const net_macs_t *t, const net_mac_t **out, int max);
const net_mac_t *net_macs_find(const net_macs_t *t, const char *client);

#ifdef __cplusplus
}
#endif
