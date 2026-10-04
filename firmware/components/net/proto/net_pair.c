/*
 * net_pair.c: pairing codes and tokens (api.md section 4). Owner: net builder. Skeleton stubs; see net_pair.h.
 */
#include <string.h>

#include "net_pair.h"
#include "net_port.h"

void net_pair_init(net_pair_t *p)
{
    memset(p, 0, sizeof(*p));
    /* TODO(net): net_port_tokens_load() */
}

net_pair_err_t net_pair_start(net_pair_t *p, const char *name, net_kind_t kind, net_scope_t scope, const char *client,
                              const tb_clock_t *now, char pairing_id_out[17], int *retry_after_s)
{
    (void)p; (void)name; (void)kind; (void)scope; (void)client; (void)now;
    pairing_id_out[0] = '\0';
    if (retry_after_s) *retry_after_s = 0;
    return NET_PAIR_NOT_PAIRING;    /* TODO(net) */
}

net_pair_err_t net_pair_finish(net_pair_t *p, const char *pairing_id, const char *code, uint32_t peer_ip,
                               const tb_clock_t *now, char token_out[NET_TOKEN_LEN + 1], const net_token_t **rec,
                               int *attempts_left)
{
    (void)p; (void)pairing_id; (void)code; (void)peer_ip; (void)now;
    token_out[0] = '\0';
    if (rec) *rec = NULL;
    if (attempts_left) *attempts_left = 0;
    return NET_PAIR_NOT_PAIRING;    /* TODO(net) */
}

net_pair_err_t net_pair_usb(net_pair_t *p, const char *client, const tb_clock_t *now, char token_out[NET_TOKEN_LEN + 1],
                            const net_token_t **rec)
{
    (void)p; (void)client; (void)now;
    token_out[0] = '\0';
    if (rec) *rec = NULL;
    return NET_PAIR_TOKEN_LIMIT;    /* TODO(net) */
}

const net_token_t *net_pair_check(net_pair_t *p, const char *token, uint32_t peer_ip, const tb_clock_t *now)
{
    (void)p; (void)token; (void)peer_ip; (void)now;
    return NULL;    /* TODO(net) */
}

bool net_pair_revoke(net_pair_t *p, const char *token_id)
{
    (void)p; (void)token_id;
    return false;   /* TODO(net) */
}

void net_pair_forget_all(net_pair_t *p)
{
    memset(p->tokens, 0, sizeof(p->tokens));    /* TODO(net): save */
}

int net_pair_count(const net_pair_t *p)
{
    int n = 0;
    for (int i = 0; i < NET_TOKENS_MAX; i++) n += p->tokens[i].used;
    return n;
}

void net_pair_cancel(net_pair_t *p)
{
    p->showing = false; /* TODO(net): count as a failure */
}

bool net_pair_tick(net_pair_t *p, const tb_clock_t *now)
{
    (void)p; (void)now;
    return false;   /* TODO(net) */
}

const char *net_pair_state(const net_pair_t *p, const tb_clock_t *now)
{
    if (p->showing) return "showing";
    if (now->mono < p->locked_until) return "locked";
    return "idle";
}
