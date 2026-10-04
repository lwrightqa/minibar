/*
 * net_macs.c: the Mac table (api.md 5.2). Owner: net builder. Pure C; see net_macs.h for the rules.
 */
#include <string.h>

#include "net_macs.h"
#include "tb_text.h"

void net_macs_init(net_macs_t *t)
{
    memset(t, 0, sizeof(*t));
    t->next_call_id = 1;
}

static net_mac_t *find(net_macs_t *t, const char *client)
{
    for (int i = 0; i < NET_MACS_MAX; i++)
        if (t->m[i].used && !strcmp(t->m[i].client, client)) return &t->m[i];
    return NULL;
}

const net_mac_t *net_macs_find(const net_macs_t *t, const char *client)
{
    return find((net_macs_t *)t, client);
}

/* Find the Mac, or make room for it: a free slot, else the least recently heard one is dropped. */
static net_mac_t *find_or_add(net_macs_t *t, const char *client)
{
    net_mac_t *m = find(t, client);
    if (m) return m;
    net_mac_t *slot = NULL;
    for (int i = 0; i < NET_MACS_MAX && !slot; i++)
        if (!t->m[i].used) slot = &t->m[i];
    if (!slot) {
        slot = &t->m[0];
        for (int i = 1; i < NET_MACS_MAX; i++)
            if (t->m[i].last_heard_ms < slot->last_heard_ms) slot = &t->m[i];
    }
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    slot->inputs = -1;
    tb_strlcpy(slot->client, client, sizeof(slot->client));
    return slot;
}

static void heard(net_mac_t *m, tb_link_t via, const tb_clock_t *now)
{
    m->via = via;
    m->connected = true;
    m->last_heard_ms = now->mono;
    m->last_heard = now->valid ? now->wall : 0;
}

static void end_call(net_mac_t *m)
{
    m->active = false;
    m->call_id = 0;
    m->app[0] = '\0';
    m->inputs = -1;
    m->since_ms = 0;
    m->since = 0;
}

/* Fill in a start time that was taken while the clock was unknown. */
static void fill_since(net_mac_t *m, const tb_clock_t *now)
{
    if (m->active && !m->since && now->valid) m->since = now->wall - (now->mono - m->since_ms) / 1000;
}

bool net_macs_on_call(net_macs_t *t, const net_call_msg_t *msg, const tb_clock_t *now)
{
    net_mac_t *m = find(t, msg->client);
    /* Out of order: the same session as the last accepted message and a seq that isn't higher (api.md 5.2). A new
     * session is always accepted; messages without session and seq (scripts) are always accepted. */
    if (m && msg->session && msg->has_seq && m->has_session && m->has_seq && !strcmp(m->session, msg->session) &&
        msg->seq <= m->seq)
        return false;
    if (!m) m = find_or_add(t, msg->client);

    if (msg->session) {
        tb_strlcpy(m->session, msg->session, sizeof(m->session));
        m->has_session = true;
        m->has_seq = msg->has_seq;
        m->seq = msg->has_seq ? msg->seq : 0;
    } else {
        m->has_session = m->has_seq = false;
        m->seq = 0;
        m->session[0] = '\0';
    }
    if (!m->name[0] && msg->name && msg->name[0]) tb_strlcpy(m->name, msg->name, sizeof(m->name));
    heard(m, msg->via, now);

    if (msg->leaving || !msg->active) {
        end_call(m);
        if (msg->leaving) m->connected = false;
        return true;
    }
    bool fresh = !m->active || (msg->call_id && msg->call_id != m->call_id);
    if (fresh) {
        /* A new call: its start is the bar's clock minus elapsed_s (api.md 5.2). */
        int32_t el = msg->elapsed_s > 0 ? msg->elapsed_s : 0;
        m->since_ms = now->mono - (tb_ms_t)el * 1000;
        m->since = now->valid ? now->wall - el : 0;
        m->bar_call_id = t->next_call_id++;
        if (!t->next_call_id) t->next_call_id = 1;
        t->bar_id = m->bar_call_id;
    }
    m->active = true;
    m->call_id = msg->call_id;
    tb_strlcpy(m->app, msg->app ? msg->app : "", sizeof(m->app));
    m->inputs = msg->inputs;
    fill_since(m, now);
    return true;
}

void net_macs_on_hello(net_macs_t *t, const char *client, const char *name, const tb_clock_t *now)
{
    net_mac_t *m = find_or_add(t, client);
    if (name && name[0]) tb_strlcpy(m->name, name, sizeof(m->name));
    heard(m, TB_LINK_USB, now);
    fill_since(m, now);
}

bool net_macs_tick(net_macs_t *t, const tb_clock_t *now)
{
    bool dropped_call = false;
    for (int i = 0; i < NET_MACS_MAX; i++) {
        net_mac_t *m = &t->m[i];
        if (!m->used) continue;
        fill_since(m, now);
        if (m->connected && now->mono - m->last_heard_ms >= NET_MAC_TIMEOUT_MS) {
            m->connected = false;
            if (m->active) {
                end_call(m);
                dropped_call = true;
            }
        }
    }
    return dropped_call;
}

bool net_macs_forget_wifi(net_macs_t *t, const char *client)
{
    net_mac_t *m = client && client[0] ? find(t, client) : NULL;
    if (!m || m->via != TB_LINK_WIFI || !m->connected) return false;
    end_call(m);
    m->connected = false;
    return true;
}

void net_macs_aggregate(const net_macs_t *t, tb_call_t *out, tb_link_t *link, const net_mac_t **mac)
{
    memset(out, 0, sizeof(*out));
    const net_mac_t *latest = NULL, *heard_last = NULL;
    for (int i = 0; i < NET_MACS_MAX; i++) {
        const net_mac_t *m = &t->m[i];
        if (!m->used || !m->connected) continue;
        if (!heard_last || m->last_heard_ms > heard_last->last_heard_ms) heard_last = m;
        if (m->active && (!latest || m->bar_call_id > latest->bar_call_id)) latest = m;
    }
    if (latest) {
        out->active = true;
        out->id = t->bar_id ? t->bar_id : 1;
        tb_strlcpy(out->app, latest->app, sizeof(out->app));
        out->via = latest->via;
        out->since_ms = latest->since_ms;
        out->since = latest->since;
    }
    if (link) *link = heard_last ? heard_last->via : TB_LINK_NONE;
    if (mac) *mac = latest;
}

int net_macs_sorted(const net_macs_t *t, const net_mac_t **out, int max)
{
    int n = 0;
    for (int i = 0; i < NET_MACS_MAX; i++)
        if (t->m[i].used && n < max) out[n++] = &t->m[i];
    /* most recently heard first (insertion sort; at most 4) */
    for (int i = 1; i < n; i++) {
        const net_mac_t *x = out[i];
        int j = i - 1;
        while (j >= 0 && out[j]->last_heard_ms < x->last_heard_ms) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = x;
    }
    return n;
}
