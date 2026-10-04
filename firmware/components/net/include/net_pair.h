/*
 * net_pair.h: pairing codes, tokens, scopes and back-off (api.md section 4, Proposed). Pure C; randomness, SHA-256
 * and storage come through net_port.h.
 *
 * Owner: net builder.
 *
 * Tokens: "tb1_" + 43 base64url characters (32 random bytes). Only SHA-256 hashes are kept, with a public 8-hex-digit
 * token_id; compared in constant time. At most 10. A new pairing with the same `client` replaces the old token.
 * Codes: 6 digits, shown on the bar for 2 minutes, one at a time, 3 tries. Back-off: after two failed pairings in a
 * row, pair/start is refused for 30 s, then 1, 2, 4 minutes... up to 1 hour; success resets it; kept in RAM only.
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
    tb_epoch_t paired_at;
    tb_link_t paired_via;
    tb_epoch_t last_used;
    uint32_t last_ip;
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
    char code[7];
    uint8_t tries_left;
    tb_ms_t expires;
    char name[TB_CLIENT_NAME_BYTES];
    net_kind_t kind;
    net_scope_t scope;
    char client[65];
    /* back-off (RAM only) */
    uint8_t failures_in_row;
    tb_ms_t locked_until;
    tb_ms_t last_pair_call;
} net_pair_t;

void net_pair_init(net_pair_t *p);
/* pair/start: show a code. *retry_after_s is set for BUSY and RATE_LIMITED. */
net_pair_err_t net_pair_start(net_pair_t *p, const char *name, net_kind_t kind, net_scope_t scope, const char *client,
                              const tb_clock_t *now, char pairing_id_out[17], int *retry_after_s);
/* pair: check the code (spaces and dashes ignored); on success write the token and its record. */
net_pair_err_t net_pair_finish(net_pair_t *p, const char *pairing_id, const char *code, uint32_t peer_ip,
                               const tb_clock_t *now, char token_out[NET_TOKEN_LEN + 1], const net_token_t **rec,
                               int *attempts_left);
/* USB pair: a call-scope token without a code. */
net_pair_err_t net_pair_usb(net_pair_t *p, const char *client, const tb_clock_t *now, char token_out[NET_TOKEN_LEN + 1],
                            const net_token_t **rec);
/* Look a token up (constant-time hash compare); updates last_used and last_ip. NULL if unknown or revoked. */
const net_token_t *net_pair_check(net_pair_t *p, const char *token, uint32_t peer_ip, const tb_clock_t *now);
bool net_pair_revoke(net_pair_t *p, const char *token_id);
void net_pair_forget_all(net_pair_t *p);
int net_pair_count(const net_pair_t *p);
/* Cancel the code on screen (a tap on the pairing screen). Counts as a failed pairing. */
void net_pair_cancel(net_pair_t *p);
/* Expire the code after 2 minutes. Returns true if it just expired (the "Pairing timed out" toast). */
bool net_pair_tick(net_pair_t *p, const tb_clock_t *now);
/* "idle", "showing" or "locked" (api.md 7.1 pairing). */
const char *net_pair_state(const net_pair_t *p, const tb_clock_t *now);

#ifdef __cplusplus
}
#endif
