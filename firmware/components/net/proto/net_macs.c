/*
 * net_macs.c: the Mac table (api.md 5.2). Owner: net builder. Skeleton stubs; see net_macs.h.
 */
#include <string.h>

#include "net_macs.h"

void net_macs_init(net_macs_t *t)
{
    memset(t, 0, sizeof(*t));
    t->next_call_id = 1;
}

bool net_macs_on_call(net_macs_t *t, const net_call_msg_t *msg, const tb_clock_t *now)
{
    (void)t; (void)msg; (void)now;
    return true;    /* TODO(net) */
}

void net_macs_on_hello(net_macs_t *t, const char *client, const char *name, const tb_clock_t *now)
{
    (void)t; (void)client; (void)name; (void)now;   /* TODO(net) */
}

bool net_macs_tick(net_macs_t *t, const tb_clock_t *now)
{
    (void)t; (void)now;
    return false;   /* TODO(net) */
}

void net_macs_aggregate(const net_macs_t *t, tb_call_t *out, tb_link_t *link)
{
    (void)t;
    memset(out, 0, sizeof(*out));
    if (link) *link = TB_LINK_NONE;  /* TODO(net) */
}

int net_macs_sorted(const net_macs_t *t, const net_mac_t **out, int max)
{
    (void)t; (void)out; (void)max;
    return 0;   /* TODO(net) */
}
