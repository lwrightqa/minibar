/*
 * net_nets.h: the saved Wi-Fi networks (up to 5), as pure logic kept apart from ESP-IDF so it's tested on Linux:
 * add or update by exact SSID, evict the least recently used, order by last use, the saved form (one NVS blob),
 * the move from the single network of firmware before 1.0.5, and which network to try next.
 *
 * Order is by `seq`, a counter, not by time: it works with no clock, and a clock set later can't reorder anything.
 *
 * Owner: net builder.
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_NETS_MAX 5

typedef struct {
    char ssid[33];
    char user[129];     /* work login only, "" otherwise */
    char pass[129];     /* password or 64-hex key, "" for open */
    uint8_t sec;        /* net_security_t: 0 open, 1 password, 2 work login */
    uint32_t seq;       /* higher = used more recently; 0 is never used */
} net_saved_t;

typedef struct {
    int count;
    uint32_t next_seq;  /* what the next join takes; always above every saved seq */
    net_saved_t n[NET_NETS_MAX];
} net_nets_t;

typedef enum { NET_NETS_ADDED = 0, NET_NETS_UPDATED, NET_NETS_REPLACED } net_nets_res_t;

void net_nets_init(net_nets_t *l);
/* Index of the network with exactly this SSID (case matters: "Office" and "office" are two networks), or -1. */
int net_nets_find(const net_nets_t *l, const char *ssid);
/* A join worked: the network becomes the most recent. The same SSID updates its login and password; a new one is
 * added, and with 5 saved the least recently used goes first (its name is copied to evicted, if given). Returns
 * false if the SSID is empty or too long. */
bool net_nets_add(net_nets_t *l, const char *ssid, const char *user, const char *pass, uint8_t sec, net_nets_res_t *res,
                  char evicted[33]);
/* A saved network joined again: it becomes the most recent. True if the order changed (and so, if NVS should be
 * written); false when it already was the newest or idx is bad. */
bool net_nets_touch(net_nets_t *l, int idx);
/* The indexes in order of last use, newest first. Returns the count. */
int net_nets_order(const net_nets_t *l, int out[NET_NETS_MAX]);
/* The network a sixth would replace (the least recently used), or NULL below 5. */
const char *net_nets_replaces(const net_nets_t *l);

/* The saved form: [1 version][1 count][4 next_seq LE], then for each network [4 seq LE][1 sec] and its ssid, user
 * and pass as NUL-terminated text. Typically 100 to 300 bytes, 1480 at most (NET_NETS_BLOB_MAX). Encode returns the
 * length (0 if cap is too small); decode returns false for anything that isn't a version 1 blob exactly (wrong
 * length, a string that doesn't end, a bad security value, an empty or duplicate SSID, a seq of 0 or at or above
 * next_seq) and leaves l empty. */
#define NET_NETS_BLOB_MAX (6 + NET_NETS_MAX * (5 + 33 + 129 + 129))
size_t net_nets_encode(const net_nets_t *l, uint8_t *out, size_t cap);
bool net_nets_decode(net_nets_t *l, const uint8_t *in, size_t len);

/* Firmware before 1.0.5 kept one network: it becomes the first entry. Returns false if it has no SSID. */
bool net_nets_from_legacy(net_nets_t *l, const char *ssid, const char *user, const char *pass, int sec_or_neg);

/* ---------- which network to try ---------- */
/* An attempt at the network at position *pos of the order failed (no signal, refused, no address): go on to the next.
 * Wraps to 0 after the last and returns true: that was the end of a round, so the caller waits its backoff before
 * the next one. With one saved network every failure ends a round, as before 1.0.5. */
bool net_nets_try_next(int count, int *pos);

#ifdef __cplusplus
}
#endif
