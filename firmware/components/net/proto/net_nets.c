/* net_nets.c: see net_nets.h. Pure C, no ESP-IDF. Owner: net builder. */
#include "net_nets.h"

#include <string.h>

#define BLOB_VERSION 1

void net_nets_init(net_nets_t *l)
{
    memset(l, 0, sizeof *l);
    l->next_seq = 1;
}

int net_nets_find(const net_nets_t *l, const char *ssid)
{
    for (int i = 0; i < l->count; i++)
        if (!strcmp(l->n[i].ssid, ssid)) return i;
    return -1;
}

/* Copy src into a field of cap bytes, always ending in NUL. */
static void put(char *dst, size_t cap, const char *src)
{
    size_t n = src ? strnlen(src, cap - 1) : 0;
    if (n) memcpy(dst, src, n);
    memset(dst + n, 0, cap - n);
}

bool net_nets_add(net_nets_t *l, const char *ssid, const char *user, const char *pass, uint8_t sec, net_nets_res_t *res,
                  char evicted[33])
{
    if (evicted) evicted[0] = '\0';
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || sec > 2) return false;
    net_nets_res_t r = NET_NETS_UPDATED;
    int i = net_nets_find(l, ssid);
    if (i < 0) {
        if (l->count == NET_NETS_MAX) {
            int v = 0;      /* the least recently used */
            for (int k = 1; k < l->count; k++)
                if (l->n[k].seq < l->n[v].seq) v = k;
            if (evicted) put(evicted, 33, l->n[v].ssid);
            memmove(&l->n[v], &l->n[v + 1], (size_t)(l->count - 1 - v) * sizeof l->n[0]);
            l->count--;
            r = NET_NETS_REPLACED;
        } else {
            r = NET_NETS_ADDED;
        }
        i = l->count++;
    }
    net_saved_t *n = &l->n[i];
    memset(n, 0, sizeof *n);
    put(n->ssid, sizeof n->ssid, ssid);
    put(n->user, sizeof n->user, user);
    put(n->pass, sizeof n->pass, pass);
    n->sec = sec;
    n->seq = l->next_seq++;
    if (res) *res = r;
    return true;
}

bool net_nets_touch(net_nets_t *l, int idx)
{
    if (idx < 0 || idx >= l->count) return false;
    if (l->n[idx].seq + 1 == l->next_seq) return false;     /* already the newest */
    l->n[idx].seq = l->next_seq++;
    return true;
}

int net_nets_order(const net_nets_t *l, int out[NET_NETS_MAX])
{
    for (int i = 0; i < l->count; i++) {    /* insertion sort, newest first */
        int j = i;
        while (j > 0 && l->n[out[j - 1]].seq < l->n[i].seq) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = i;
    }
    return l->count;
}

const char *net_nets_replaces(const net_nets_t *l)
{
    if (l->count < NET_NETS_MAX) return NULL;
    int v = 0;
    for (int k = 1; k < l->count; k++)
        if (l->n[k].seq < l->n[v].seq) v = k;
    return l->n[v].ssid;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

size_t net_nets_encode(const net_nets_t *l, uint8_t *out, size_t cap)
{
    size_t need = 6;
    for (int i = 0; i < l->count; i++)
        need += 5 + strlen(l->n[i].ssid) + 1 + strlen(l->n[i].user) + 1 + strlen(l->n[i].pass) + 1;
    if (cap < need) return 0;
    out[0] = BLOB_VERSION;
    out[1] = (uint8_t)l->count;
    put_u32(out + 2, l->next_seq);
    size_t o = 6;
    for (int i = 0; i < l->count; i++) {
        const net_saved_t *n = &l->n[i];
        put_u32(out + o, n->seq);
        out[o + 4] = n->sec;
        o += 5;
        const char *f[3] = {n->ssid, n->user, n->pass};
        for (int k = 0; k < 3; k++) {
            size_t len = strlen(f[k]) + 1;
            memcpy(out + o, f[k], len);
            o += len;
        }
    }
    return o;
}

/* The next NUL-terminated string of at most max bytes (without its NUL) at in[*o]; false if it doesn't end in time. */
static bool take_str(const uint8_t *in, size_t len, size_t *o, size_t max, char *dst)
{
    size_t n = 0;
    while (*o + n < len && in[*o + n]) {
        if (++n > max) return false;
    }
    if (*o + n >= len) return false;
    memcpy(dst, in + *o, n);
    dst[n] = '\0';
    *o += n + 1;
    return true;
}

bool net_nets_decode(net_nets_t *l, const uint8_t *in, size_t len)
{
    net_nets_init(l);
    if (!in || len < 6 || in[0] != BLOB_VERSION || in[1] > NET_NETS_MAX) return false;
    uint32_t next = get_u32(in + 2);
    size_t o = 6;
    net_nets_t t;
    net_nets_init(&t);
    t.count = in[1];
    t.next_seq = next;
    for (int i = 0; i < t.count; i++) {
        net_saved_t *n = &t.n[i];
        if (o + 5 > len) return false;
        n->seq = get_u32(in + o);
        n->sec = in[o + 4];
        o += 5;
        if (n->sec > 2 || n->seq == 0 || n->seq >= next) return false;
        if (!take_str(in, len, &o, 32, n->ssid) || !take_str(in, len, &o, 128, n->user) ||
            !take_str(in, len, &o, 128, n->pass))
            return false;
        if (!n->ssid[0]) return false;
        for (int k = 0; k < i; k++)
            if (!strcmp(t.n[k].ssid, n->ssid) || t.n[k].seq == n->seq) return false;
    }
    if (o != len) return false;
    *l = t;
    return true;
}

bool net_nets_from_legacy(net_nets_t *l, const char *ssid, const char *user, const char *pass, int sec_or_neg)
{
    net_nets_init(l);
    /* firmware before 1.0.2 may not have saved the security: a password means a password network */
    int sec = sec_or_neg >= 0 && sec_or_neg <= 2 ? sec_or_neg : (pass && pass[0] ? 1 : 0);
    return net_nets_add(l, ssid, user, pass, (uint8_t)sec, NULL, NULL);
}

bool net_nets_try_next(int count, int *pos)
{
    if (count <= 0) {
        *pos = 0;
        return true;
    }
    if (*pos + 1 < count) {
        (*pos)++;
        return false;
    }
    *pos = 0;
    return true;
}
