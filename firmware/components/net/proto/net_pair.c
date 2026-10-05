/*
 * net_pair.c: pairing codes and tokens (api.md section 4, approved 2026-10-04). Owner: net builder. Pure C; see net_pair.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_pair.h"
#include "net_port.h"
#include "tb_text.h"

#define BLOB_MAGIC0 'T'
#define BLOB_MAGIC1 'K'
#define BLOB_VERSION 1

/* ---------- storage ---------- */

static void put_u8(uint8_t **w, uint8_t v) { *(*w)++ = v; }
static void put_bytes(uint8_t **w, const void *p, size_t n) { memcpy(*w, p, n); *w += n; }
static void put_le(uint8_t **w, uint64_t v, int n)
{
    for (int i = 0; i < n; i++) *(*w)++ = (uint8_t)(v >> (8 * i));
}
static void put_str(uint8_t **w, const char *s, size_t max)
{
    size_t n = strlen(s);
    if (n > max) n = max;
    put_u8(w, (uint8_t)n);
    put_bytes(w, s, n);
}

size_t net_pair_serialize(const net_pair_t *p, uint8_t *out, size_t cap)
{
    if (cap < NET_TOKEN_BLOB_MAX) return 0;
    uint8_t *w = out;
    put_u8(&w, BLOB_MAGIC0);
    put_u8(&w, BLOB_MAGIC1);
    put_u8(&w, BLOB_VERSION);
    put_u8(&w, (uint8_t)net_pair_count(p));
    for (int i = 0; i < NET_TOKENS_MAX; i++) {
        const net_token_t *t = &p->tokens[i];
        if (!t->used) continue;
        put_bytes(&w, t->hash, 32);
        put_bytes(&w, t->token_id, 8);
        put_str(&w, t->name, TB_CLIENT_NAME_BYTES - 1);
        put_u8(&w, (uint8_t)t->kind);
        put_u8(&w, (uint8_t)t->scope);
        put_str(&w, t->client, 64);
        put_le(&w, (uint64_t)t->paired_at, 8);
        put_u8(&w, (uint8_t)t->paired_via);
        put_le(&w, (uint64_t)t->last_used, 8);
        put_le(&w, t->last_ip, 4);
    }
    return (size_t)(w - out);
}

typedef struct { const uint8_t *p, *end; bool ok; } rd_t;

static uint64_t get_le(rd_t *r, int n)
{
    if (r->end - r->p < n) { r->ok = false; return 0; }
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint64_t)*r->p++ << (8 * i);
    return v;
}
static void get_bytes(rd_t *r, void *dst, size_t n)
{
    if ((size_t)(r->end - r->p) < n) { r->ok = false; return; }
    memcpy(dst, r->p, n);
    r->p += n;
}
static void get_str(rd_t *r, char *dst, size_t cap)
{
    size_t n = (size_t)get_le(r, 1);
    if (!r->ok || n >= cap || (size_t)(r->end - r->p) < n) { r->ok = false; return; }
    memcpy(dst, r->p, n);
    dst[n] = '\0';
    r->p += n;
}

bool net_pair_deserialize(net_pair_t *p, const uint8_t *in, size_t n)
{
    rd_t r = {in, in + n, true};
    if (n < 4 || in[0] != BLOB_MAGIC0 || in[1] != BLOB_MAGIC1 || in[2] != BLOB_VERSION) return false;
    r.p += 3;
    int count = (int)get_le(&r, 1);
    if (count > NET_TOKENS_MAX) return false;
    net_token_t tmp[NET_TOKENS_MAX];
    memset(tmp, 0, sizeof tmp);
    for (int i = 0; i < count && r.ok; i++) {
        net_token_t *t = &tmp[i];
        t->used = true;
        get_bytes(&r, t->hash, 32);
        get_bytes(&r, t->token_id, 8);
        t->token_id[8] = '\0';
        get_str(&r, t->name, sizeof t->name);
        uint8_t kind = (uint8_t)get_le(&r, 1), scope = (uint8_t)get_le(&r, 1);
        t->kind = kind <= NET_KIND_OTHER ? (net_kind_t)kind : NET_KIND_OTHER;
        t->scope = scope == NET_SCOPE_FULL ? NET_SCOPE_FULL : NET_SCOPE_CALL;     /* unknown: the narrow scope */
        get_str(&r, t->client, sizeof t->client);
        t->paired_at = (tb_epoch_t)get_le(&r, 8);
        uint8_t via = (uint8_t)get_le(&r, 1);
        t->paired_via = via == TB_LINK_USB ? TB_LINK_USB : TB_LINK_WIFI;
        t->last_used = (tb_epoch_t)get_le(&r, 8);
        t->saved_used = t->last_used;
        t->last_ip = (uint32_t)get_le(&r, 4);
    }
    if (!r.ok) return false;
    memcpy(p->tokens, tmp, sizeof tmp);
    return true;
}

static void save(const net_pair_t *p)
{
    uint8_t *blob = malloc(NET_TOKEN_BLOB_MAX);     /* brief, and off the app task's stack */
    size_t n = blob ? net_pair_serialize(p, blob, NET_TOKEN_BLOB_MAX) : 0;
    if (n) net_port_tokens_save(blob, n);
    free(blob);
}

/* ---------- helpers ---------- */

void net_pair_format_token(const uint8_t raw[32], char out[NET_TOKEN_LEN + 1])
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    memcpy(out, "tb1_", 4);
    char *o = out + 4;
    int i = 0;
    for (; i + 3 <= 32; i += 3) {
        uint32_t v = (uint32_t)raw[i] << 16 | (uint32_t)raw[i + 1] << 8 | raw[i + 2];
        *o++ = b64[(v >> 18) & 63];
        *o++ = b64[(v >> 12) & 63];
        *o++ = b64[(v >> 6) & 63];
        *o++ = b64[v & 63];
    }
    /* 32 = 30 + 2: the last two bytes give three characters, no padding */
    uint32_t v = (uint32_t)raw[i] << 16 | (uint32_t)raw[i + 1] << 8;
    *o++ = b64[(v >> 18) & 63];
    *o++ = b64[(v >> 12) & 63];
    *o++ = b64[(v >> 6) & 63];
    *o = '\0';
}

static void hex(const uint8_t *b, size_t n, char *out)
{
    static const char h[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = h[b[i] >> 4];
        out[2 * i + 1] = h[b[i] & 15];
    }
    out[2 * n] = '\0';
}

static void lock_after_failure(net_pair_t *p, const tb_clock_t *now)
{
    if (p->failures_in_row < 255) p->failures_in_row++;
    if (p->failures_in_row < 2) return;     /* the first failure costs nothing, so a typo doesn't make you wait */
    tb_ms_t wait = NET_PAIR_LOCK_FIRST_MS;
    for (int i = 2; i < p->failures_in_row && wait < NET_PAIR_LOCK_MAX_MS; i++) wait *= 2;
    if (wait > NET_PAIR_LOCK_MAX_MS) wait = NET_PAIR_LOCK_MAX_MS;
    p->locked_until = now->mono + wait;
}

static int ceil_s(tb_ms_t ms)
{
    return ms <= 0 ? 0 : (int)((ms + 999) / 1000);
}

static net_token_t *by_client(net_pair_t *p, const char *client)
{
    if (!client || !client[0]) return NULL;
    for (int i = 0; i < NET_TOKENS_MAX; i++)
        if (p->tokens[i].used && !strcmp(p->tokens[i].client, client)) return &p->tokens[i];
    return NULL;
}

/* Room for one more token, counting the one this client would replace (a device that's paired already always has
 * room). A code on the screen for a device that isn't paired yet holds a place until the pairing ends, so the code it
 * shows can always work (api.md 4.3): meanwhile another new device (the Mac over USB) is refused when it would take
 * that place. The code's own device (the same client) isn't kept out by its own place. */
static bool has_room(net_pair_t *p, const char *client)
{
    if (by_client(p, client)) return true;
    bool held = p->showing && !(client && client[0] && !strcmp(client, p->client)) && !by_client(p, p->client);
    return net_pair_count(p) + (held ? 1 : 0) < NET_TOKENS_MAX;
}

/* Make a token, keep only its hash, and replace an older one with the same client. */
static const net_token_t *issue(net_pair_t *p, const char *name, net_kind_t kind, net_scope_t scope, const char *client,
                                tb_link_t via, uint32_t peer_ip, const tb_clock_t *now, char token_out[NET_TOKEN_LEN + 1])
{
    net_token_t *old = by_client(p, client);
    if (old) memset(old, 0, sizeof(*old));
    net_token_t *t = NULL;
    for (int i = 0; i < NET_TOKENS_MAX && !t; i++)
        if (!p->tokens[i].used) t = &p->tokens[i];
    if (!t) return NULL;

    uint8_t raw[32];
    net_port_random(raw, sizeof raw);
    net_pair_format_token(raw, token_out);
    memset(raw, 0, sizeof raw);

    memset(t, 0, sizeof(*t));
    t->used = true;
    net_port_sha256(token_out, NET_TOKEN_LEN, t->hash);
    for (;;) {     /* a public id no other token has */
        uint8_t id[4];
        net_port_random(id, sizeof id);
        hex(id, 4, t->token_id);
        bool dup = false;
        for (int i = 0; i < NET_TOKENS_MAX; i++)
            if (&p->tokens[i] != t && p->tokens[i].used && !strcmp(p->tokens[i].token_id, t->token_id)) dup = true;
        if (!dup) break;
    }
    tb_strlcpy(t->name, name, sizeof t->name);
    t->kind = kind;
    t->scope = scope;
    tb_strlcpy(t->client, client ? client : "", sizeof t->client);
    t->paired_at = now->valid ? now->wall : 0;
    t->paired_via = via;
    t->last_used = t->saved_used = t->paired_at;
    t->last_ip = peer_ip;
    save(p);
    return t;
}

/* ---------- API ---------- */

void net_pair_init(net_pair_t *p)
{
    memset(p, 0, sizeof(*p));
    p->last_pair_call = -1;
    uint8_t *blob = malloc(NET_TOKEN_BLOB_MAX);
    size_t n = blob ? net_port_tokens_load(blob, NET_TOKEN_BLOB_MAX) : 0;
    if (n) net_pair_deserialize(p, blob, n);
    free(blob);
}

net_pair_err_t net_pair_start(net_pair_t *p, const char *name, net_kind_t kind, net_scope_t scope, const char *client,
                              const tb_clock_t *now, char pairing_id_out[17], int *retry_after_s)
{
    pairing_id_out[0] = '\0';
    if (retry_after_s) *retry_after_s = 0;
    /* The caller runs net_pair_tick() first, so an expired code has already ended (and said so on the bar). */
    if (p->showing && now->mono < p->expires) {
        if (retry_after_s) *retry_after_s = ceil_s(p->expires - now->mono);
        return NET_PAIR_BUSY;
    }
    if (now->mono < p->locked_until) {
        if (retry_after_s) *retry_after_s = ceil_s(p->locked_until - now->mono);
        return NET_PAIR_RATE_LIMITED;
    }
    if (!has_room(p, client)) return NET_PAIR_TOKEN_LIMIT;

    uint8_t id[8];
    net_port_random(id, sizeof id);
    hex(id, sizeof id, p->pairing_id);
    uint32_t r;
    do net_port_random(&r, sizeof r);
    while (r >= 4294000000u);    /* uniform over the million codes */
    snprintf(p->code, sizeof p->code, "%06u", (unsigned)(r % 1000000u));
    p->showing = true;
    p->seq++;
    p->tries_left = NET_PAIR_TRIES;
    p->expires = now->mono + NET_PAIR_CODE_MS;
    tb_strlcpy(p->name, name ? name : "", sizeof p->name);
    p->kind = kind;
    p->scope = scope;
    tb_strlcpy(p->client, client ? client : "", sizeof p->client);
    memcpy(pairing_id_out, p->pairing_id, 17);
    return NET_PAIR_OK;
}

net_pair_err_t net_pair_finish(net_pair_t *p, const char *pairing_id, const char *code, uint32_t peer_ip,
                               const tb_clock_t *now, char token_out[NET_TOKEN_LEN + 1], const net_token_t **rec,
                               int *attempts_left)
{
    token_out[0] = '\0';
    if (rec) *rec = NULL;
    if (attempts_left) *attempts_left = 0;
    if (p->last_pair_call >= 0 && now->mono - p->last_pair_call < 1000) return NET_PAIR_RATE_LIMITED;
    p->last_pair_call = now->mono;
    if (!p->showing || now->mono >= p->expires || !pairing_id || strcmp(pairing_id, p->pairing_id))
        return NET_PAIR_NOT_PAIRING;

    /* Spaces and dashes are ignored ("482 913"); compare without an early exit. */
    char digits[16];
    size_t n = 0;
    for (const char *c = code; c && *c && n < sizeof digits - 1; c++)
        if (*c != ' ' && *c != '-') digits[n++] = *c;
    digits[n] = '\0';
    uint8_t diff = n == NET_PAIR_CODE_LEN ? 0 : 1;
    for (size_t i = 0; i < NET_PAIR_CODE_LEN; i++) diff |= (uint8_t)(digits[i < n ? i : 0] ^ p->code[i]);
    if (diff) {
        if (p->tries_left) p->tries_left--;
        if (attempts_left) *attempts_left = p->tries_left;
        if (!p->tries_left) {
            p->showing = false;
            lock_after_failure(p, now);
        }
        return NET_PAIR_WRONG_CODE;
    }
    /* The code is used up: it no longer holds a place, it takes one. No room is a safeguard only, since the code held
     * its place (api.md 4.7): the pairing ends without counting as a failed pairing. */
    p->showing = false;
    if (!has_room(p, p->client)) return NET_PAIR_TOKEN_LIMIT;
    const net_token_t *t = issue(p, p->name, p->kind, p->scope, p->client, TB_LINK_WIFI, peer_ip, now, token_out);
    if (rec) *rec = t;
    if (!t) return NET_PAIR_TOKEN_LIMIT;
    /* A code typed right resets the back-off (api.md 4.9; the mock-up's pairEnd('paired')). A pairing over USB
     * doesn't: it takes no code, so it says nothing about the codes someone may be guessing. */
    p->failures_in_row = 0;
    p->locked_until = 0;
    return NET_PAIR_OK;
}

net_pair_err_t net_pair_usb(net_pair_t *p, const char *client, const char *name, const tb_clock_t *now,
                            char token_out[NET_TOKEN_LEN + 1], const net_token_t **rec)
{
    token_out[0] = '\0';
    if (rec) *rec = NULL;
    if (!has_room(p, client)) return NET_PAIR_TOKEN_LIMIT;
    const net_token_t *t = issue(p, name && name[0] ? name : "Mac", NET_KIND_MAC, NET_SCOPE_CALL, client, TB_LINK_USB,
                                 0, now, token_out);
    if (rec) *rec = t;
    return t ? NET_PAIR_OK : NET_PAIR_TOKEN_LIMIT;
}

const net_token_t *net_pair_check(net_pair_t *p, const char *token, uint32_t peer_ip, const tb_clock_t *now)
{
    if (!token || strlen(token) != NET_TOKEN_LEN || strncmp(token, "tb1_", 4)) return NULL;
    uint8_t h[32];
    net_port_sha256(token, NET_TOKEN_LEN, h);
    net_token_t *hit = NULL;
    for (int i = 0; i < NET_TOKENS_MAX; i++) {
        uint8_t diff = 0;
        for (int k = 0; k < 32; k++) diff |= (uint8_t)(h[k] ^ p->tokens[i].hash[k]);
        if (!diff && p->tokens[i].used) hit = &p->tokens[i];
    }
    if (!hit) return NULL;
    if (peer_ip) hit->last_ip = peer_ip;
    if (now->valid) {
        hit->last_used = now->wall;
        if (hit->last_used - hit->saved_used >= 3600) {
            hit->saved_used = hit->last_used;
            save(p);
        }
    }
    return hit;
}

const net_token_t *net_pair_find(const net_pair_t *p, const char *token_id)
{
    if (!token_id) return NULL;
    for (int i = 0; i < NET_TOKENS_MAX; i++)
        if (p->tokens[i].used && !strcmp(p->tokens[i].token_id, token_id)) return &p->tokens[i];
    return NULL;
}

const net_token_t *net_pair_find_client(const net_pair_t *p, const char *client)
{
    if (!client || !client[0]) return NULL;
    for (int i = 0; i < NET_TOKENS_MAX; i++)
        if (p->tokens[i].used && !strcmp(p->tokens[i].client, client)) return &p->tokens[i];
    return NULL;
}

bool net_pair_revoke(net_pair_t *p, const char *token_id)
{
    net_token_t *t = (net_token_t *)net_pair_find(p, token_id);
    if (!t) return false;
    memset(t, 0, sizeof(*t));
    save(p);
    return true;
}

void net_pair_forget_all(net_pair_t *p)
{
    memset(p->tokens, 0, sizeof(p->tokens));
    save(p);
}

int net_pair_count(const net_pair_t *p)
{
    int n = 0;
    for (int i = 0; i < NET_TOKENS_MAX; i++) n += p->tokens[i].used;
    return n;
}

void net_pair_cancel(net_pair_t *p, const tb_clock_t *now)
{
    if (!p->showing) return;
    p->showing = false;
    lock_after_failure(p, now);
}

net_pair_err_t net_pair_cancel_id(net_pair_t *p, const char *pairing_id, const tb_clock_t *now)
{
    if (p->last_pair_call >= 0 && now->mono - p->last_pair_call < 1000) return NET_PAIR_RATE_LIMITED;
    p->last_pair_call = now->mono;
    if (!p->showing || now->mono >= p->expires || !pairing_id || strcmp(pairing_id, p->pairing_id))
        return NET_PAIR_NOT_PAIRING;
    net_pair_cancel(p, now);
    return NET_PAIR_OK;
}

void net_pair_reset(net_pair_t *p)
{
    p->showing = false;
    p->failures_in_row = 0;
    p->locked_until = 0;
}

bool net_pair_tick(net_pair_t *p, const tb_clock_t *now)
{
    if (!p->showing || now->mono < p->expires) return false;
    p->showing = false;
    lock_after_failure(p, now);
    return true;
}

const char *net_pair_state(const net_pair_t *p, const tb_clock_t *now)
{
    if (p->showing && now->mono < p->expires) return "showing";
    if (now->mono < p->locked_until) return "locked";
    return "idle";
}

int net_pair_locked_s(const net_pair_t *p, const tb_clock_t *now)
{
    return ceil_s(p->locked_until - now->mono);
}
