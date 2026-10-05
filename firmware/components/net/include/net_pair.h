/*
 * net_pair.h: pairing codes, tokens, scopes and back-off (api.md section 4, approved 2026-10-04). Pure C; randomness, SHA-256
 * and storage come through net_port.h.
 *
 * Owner: net builder.
 *
 * Tokens: "tb1_" + 43 base64url characters (32 random bytes). Only SHA-256 hashes are kept, with a public 8-hex-digit
 * token_id; compared in constant time. At most 10. A new pairing with the same `client` replaces the old token.
 * Codes: 6 digits, shown on the bar for 2 minutes, one at a time, 3 tries. A code for a device that isn't paired yet
 * holds one of the 10 places until it ends (api.md 4.3), so a USB pairing of another new device meanwhile gets
 * token_limit. Back-off: after two failed pairings in a row (timed out, canceled on the bar or with pair/cancel, out of
 * tries), pair/start is refused for 30 s, then 1, 2, 4 minutes... up to 1 hour; a code typed right, Power off and
 * Restart reset it (a USB pairing takes no code and doesn't); kept in RAM only. POST /api/v1/pair and /pair/cancel are limited to one request a second in total.
 * Storage: the table is saved through net_port_tokens_save() as a versioned blob on every pairing, revoke and forget,
 * and when a token's last_used moves on by an hour or more (flash wear: at most once an hour per token).
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_TOKENS_MAX      10
#define NET_TOKEN_LEN       47      /* "tb1_" + 43 */
#define NET_PAIR_CODE_MS    120000
#define NET_PAIR_TRIES      3
#define NET_PAIR_CODE_LEN   6
#define NET_PAIR_LOCK_FIRST_MS  30000
#define NET_PAIR_LOCK_MAX_MS    3600000
#define NET_TOKEN_BLOB_MAX  2600    /* the saved table's largest size */

typedef enum { NET_SCOPE_CALL = 0, NET_SCOPE_FULL } net_scope_t;
typedef enum { NET_KIND_MAC = 0, NET_KIND_REMOTE, NET_KIND_AUTOMATION, NET_KIND_OTHER } net_kind_t;

typedef struct {
    bool used;
    uint8_t hash[32];
    char token_id[9];
    char name[TB_CLIENT_NAME_BYTES];
    net_kind_t kind;
    net_scope_t scope;
    char client[65];
    tb_epoch_t paired_at;           /* 0 when the clock was unknown */
    tb_link_t paired_via;
    tb_epoch_t last_used;           /* 0 = unknown */
    uint32_t last_ip;               /* network order; 0 = only used over USB */
    tb_epoch_t saved_used;          /* RAM: last_used as last saved (flash wear) */
} net_token_t;

typedef enum {
    NET_PAIR_OK = 0,
    NET_PAIR_BUSY,          /* 409 pairing_busy (retry_after_s) */
    NET_PAIR_TOKEN_LIMIT,   /* 409 token_limit */
    NET_PAIR_RATE_LIMITED,  /* 429 rate_limited (retry_after_s) */
    NET_PAIR_WRONG_CODE,    /* 403 wrong_code (attempts_left) */
    NET_PAIR_NOT_PAIRING,   /* 409 not_pairing */
} net_pair_err_t;

typedef struct {
    net_token_t tokens[NET_TOKENS_MAX];
    /* the code on screen */
    bool showing;
    char pairing_id[17];
    char code[NET_PAIR_CODE_LEN + 1];
    uint8_t tries_left;
    tb_ms_t expires;
    char name[TB_CLIENT_NAME_BYTES];
    net_kind_t kind;
    net_scope_t scope;
    char client[65];
    /* back-off (RAM only) */
    uint8_t failures_in_row;
    tb_ms_t locked_until;
    tb_ms_t last_pair_call;         /* the last POST /api/v1/pair that was let through; -1 = none */
} net_pair_t;

/* Empty state, then the saved table (net_port_tokens_load). */
void net_pair_init(net_pair_t *p);
/* pair/start: show a code. *retry_after_s is set for BUSY and RATE_LIMITED. name: the label for the screen and the
 * list (already defaulted from kind by the caller). client may be NULL or "". */
net_pair_err_t net_pair_start(net_pair_t *p, const char *name, net_kind_t kind, net_scope_t scope, const char *client,
                              const tb_clock_t *now, char pairing_id_out[17], int *retry_after_s);
/* pair: check the code (spaces and dashes ignored); on success write the token and its record. A wrong code costs a
 * try; the last one ends the pairing (attempts_left 0) and counts as a failed pairing. RATE_LIMITED: more than one
 * call a second (retry_after_s 1). */
net_pair_err_t net_pair_finish(net_pair_t *p, const char *pairing_id, const char *code, uint32_t peer_ip,
                               const tb_clock_t *now, char token_out[NET_TOKEN_LEN + 1], const net_token_t **rec,
                               int *attempts_left);
/* USB pair: a call-scope token without a code (api.md 6.6). name: the Mac's label ("Mac" if NULL or ""). */
net_pair_err_t net_pair_usb(net_pair_t *p, const char *client, const char *name, const tb_clock_t *now,
                            char token_out[NET_TOKEN_LEN + 1], const net_token_t **rec);
/* Look a token up (constant-time hash compare); updates last_used and last_ip. NULL if unknown or revoked. */
const net_token_t *net_pair_check(net_pair_t *p, const char *token, uint32_t peer_ip, const tb_clock_t *now);
const net_token_t *net_pair_find(const net_pair_t *p, const char *token_id);
/* The token paired with this client ID (pair/start's or the USB pair's `client`), or NULL. */
const net_token_t *net_pair_find_client(const net_pair_t *p, const char *client);
bool net_pair_revoke(net_pair_t *p, const char *token_id);
void net_pair_forget_all(net_pair_t *p);
int net_pair_count(const net_pair_t *p);
/* Cancel the code on screen (a tap on the pairing screen). Counts as a failed pairing. */
void net_pair_cancel(net_pair_t *p, const tb_clock_t *now);
/* POST /api/v1/pair/cancel (api.md 4.7): the device that asked takes its code off the bar. Only its pairing_id works.
 * Counts as a failed pairing, like a tap, so canceling and asking again can't be used to get more guesses. Shares
 * pair's limit of one request a second (RATE_LIMITED); NOT_PAIRING when no code is up for that pairing_id. */
net_pair_err_t net_pair_cancel_id(net_pair_t *p, const char *pairing_id, const tb_clock_t *now);
/* Power off or Restart (api.md 4.9): any code ends without counting as a failed pairing, and the back-off is cleared,
 * as a restart clears the bar's RAM. Tokens stay. */
void net_pair_reset(net_pair_t *p);
/* Expire the code after 2 minutes. Returns true if it just expired (the "Pairing timed out" toast). */
bool net_pair_tick(net_pair_t *p, const tb_clock_t *now);
/* "idle", "showing" or "locked" (api.md 7.1 pairing). */
const char *net_pair_state(const net_pair_t *p, const tb_clock_t *now);
/* Seconds until a code shows again is possible (0 when not locked). */
int net_pair_locked_s(const net_pair_t *p, const tb_clock_t *now);

/* The saved table (exposed for tests). Returns the blob size, 0 on error. */
size_t net_pair_serialize(const net_pair_t *p, uint8_t *out, size_t cap);
bool net_pair_deserialize(net_pair_t *p, const uint8_t *in, size_t n);
/* "tb1_" + base64url of 32 bytes (exposed for tests). */
void net_pair_format_token(const uint8_t raw[32], char out[NET_TOKEN_LEN + 1]);

#ifdef __cplusplus
}
#endif
