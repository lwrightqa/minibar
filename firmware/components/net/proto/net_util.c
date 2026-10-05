/*
 * net_util.c: pure helpers of the network layer (see net_util.h). Owner: net builder.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "net_util.h"

/* ======================================================================================================== */
/* Rate limits                                                                                              */
/* ======================================================================================================== */

#define ALL_CAP_MT   20000      /* bursts up to 20 */
#define ALL_RATE     10         /* milli-requests per ms = 10 a second */
#define ANON_CAP_MT  5000
#define ANON_RATE    5          /* 5 a second */
#define COST_MT      1000

void net_rate_init(net_rate_t *r)
{
    memset(r, 0, sizeof(*r));
}

static int32_t refill(int32_t have, tb_ms_t dt, int32_t rate, int32_t cap)
{
    int64_t v = (int64_t)have + dt * rate;
    return v > cap ? cap : (int32_t)v;
}

static int wait_s(int32_t have, int32_t rate)
{
    int32_t need_ms = (COST_MT - have + rate - 1) / rate;
    return need_ms <= 0 ? 1 : (need_ms + 999) / 1000;
}

int net_rate_take(net_rate_t *r, uint32_t ip, bool anon, tb_ms_t now)
{
    net_rate_slot_t *s = NULL, *victim = NULL;
    for (int i = 0; i < NET_RATE_SLOTS && !s; i++) {
        net_rate_slot_t *x = &r->s[i];
        if (x->used && x->ip == ip) s = x;
        else if (!victim) victim = x;
        else if (victim->used && (!x->used || x->last < victim->last)) victim = x;   /* a free slot, else the LRU */
    }
    if (!s) {
        s = victim;
        s->used = true;
        s->ip = ip;
        s->last = now;
        s->all_mt = ALL_CAP_MT;
        s->anon_mt = ANON_CAP_MT;
    }
    tb_ms_t dt = now - s->last;
    if (dt < 0) dt = 0;
    s->last = now;
    s->all_mt = refill(s->all_mt, dt, ALL_RATE, ALL_CAP_MT);
    s->anon_mt = refill(s->anon_mt, dt, ANON_RATE, ANON_CAP_MT);
    if (s->all_mt < COST_MT) return wait_s(s->all_mt, ALL_RATE);
    if (anon && s->anon_mt < COST_MT) return wait_s(s->anon_mt, ANON_RATE);
    s->all_mt -= COST_MT;
    if (anon) s->anon_mt -= COST_MT;
    return 0;
}

/* ======================================================================================================== */
/* Time                                                                                                     */
/* ======================================================================================================== */

static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static bool digits(const char *s, int n, int *out)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return true;
}

bool net_parse_rfc3339(const char *s, tb_epoch_t *out)
{
    int y, mo, d, h, mi, se;
    if (!s || strlen(s) < 20) return false;
    if (!digits(s, 4, &y) || s[4] != '-' || !digits(s + 5, 2, &mo) || s[7] != '-' || !digits(s + 8, 2, &d) ||
        (s[10] != 'T' && s[10] != 't' && s[10] != ' ') || !digits(s + 11, 2, &h) || s[13] != ':' ||
        !digits(s + 14, 2, &mi) || s[16] != ':' || !digits(s + 17, 2, &se))
        return false;
    static const int mdays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > mdays[mo - 1] || h > 23 || mi > 59 || se > 60) return false;
    if (mo == 2 && d == 29 && !((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return false;
    const char *p = s + 19;
    if (*p == '.') {
        p++;
        if (*p < '0' || *p > '9') return false;
        while (*p >= '0' && *p <= '9') p++;
    }
    int off = 0;
    if (*p == 'Z' || *p == 'z') {
        p++;
    } else if (*p == '+' || *p == '-') {
        int oh, om;
        if (!digits(p + 1, 2, &oh) || p[3] != ':' || !digits(p + 4, 2, &om) || oh > 23 || om > 59) return false;
        off = (oh * 60 + om) * 60 * (*p == '-' ? -1 : 1);
        p += 6;
    } else {
        return false;
    }
    if (*p) return false;
    if (se == 60) se = 59;  /* a leap second lands on the second before */
    *out = (tb_epoch_t)(days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se - off);
    return true;
}

bool net_time_mac_should_set(tb_epoch_t mac_time, const tb_clock_t *now, tb_ms_t last_ntp_ms)
{
    if (mac_time <= 0) return false;
    if (last_ntp_ms >= 0 && now->mono - last_ntp_ms < 24LL * 3600 * 1000) return false;
    if (!now->valid) return true;
    tb_epoch_t d = mac_time - now->wall;
    return d > 2 || d < -2;
}

/* ======================================================================================================== */
/* DNS catch-all                                                                                            */
/* ======================================================================================================== */

/* The first question of a standard query: where it ends (just after its class), its type and class. */
static bool dns_question(const uint8_t *q, size_t qlen, size_t *qend, uint16_t *qtype, uint16_t *qclass)
{
    if (qlen < 12 || qlen > 512) return false;
    if (q[2] & 0x80) return false;              /* a response, not a query */
    if ((q[2] >> 3) & 0x0F) return false;       /* only standard queries */
    if (((q[4] << 8) | q[5]) < 1) return false; /* no question */
    /* a name made of labels (no compression pointers in a question), then type and class */
    size_t p = 12;
    while (p < qlen && q[p]) {
        if (q[p] & 0xC0) return false;
        p += 1 + q[p];
    }
    if (p >= qlen) return false;
    p++;                                        /* the root label */
    if (p + 4 > qlen) return false;
    *qtype = (uint16_t)(q[p] << 8 | q[p + 1]);
    *qclass = (uint16_t)(q[p + 2] << 8 | q[p + 3]);
    *qend = p + 4;
    return true;
}

/* Append c to s (cap bytes with its NUL); false when it's full. */
static bool put_char(char *s, size_t cap, size_t *o, char c)
{
    if (*o + 1 >= cap) return false;
    s[(*o)++] = c;
    return true;
}

/* s is full and more was coming: its last three characters become "...". */
static void mark_cut(char *s, size_t o)
{
    for (size_t k = 0; k < 3 && k < o; k++) s[o - 1 - k] = '.';
}

bool net_dns_question(const uint8_t *q, size_t qlen, char *name, size_t cap, uint16_t *qtype)
{
    if (!cap) return false;
    name[0] = '\0';
    size_t qend;
    uint16_t type, cls;
    if (!dns_question(q, qlen, &qend, &type, &cls)) return false;
    *qtype = type;
    /* dns_question() checked the labels, so every byte read here is inside q */
    size_t o = 0;
    bool full = false;
    for (size_t p = 12; q[p] && !full; p += 1 + q[p]) {
        if (p > 12) full = !put_char(name, cap, &o, '.');
        for (size_t i = 1; i <= q[p] && !full; i++) {
            uint8_t c = q[p + i];
            full = !put_char(name, cap, &o, c < 0x20 || c > 0x7E || c == '.' ? '?' : (char)c);
        }
    }
    if (full) mark_cut(name, o);
    else if (o == 0) put_char(name, cap, &o, '.');     /* the root itself */
    name[o] = '\0';
    return true;
}

size_t net_dns_answer(const uint8_t *q, size_t qlen, uint32_t ip, uint8_t *out, size_t cap)
{
    size_t qend;
    uint16_t qtype, qclass;
    if (!dns_question(q, qlen, &qend, &qtype, &qclass)) return 0;
    bool answer = (qtype == 1 || qtype == 255) && (qclass & 0x7FFF) == 1;
    size_t need = qend + (answer ? 16 : 0);
    if (need > cap) return 0;
    memcpy(out, q, qend);
    out[2] = (uint8_t)(0x84 | (q[2] & 0x01));   /* QR, AA, keep RD */
    out[3] = 0x00;                              /* RA 0, no error */
    out[4] = 0; out[5] = 1;                     /* one question */
    out[6] = 0; out[7] = answer ? 1 : 0;
    out[8] = out[9] = out[10] = out[11] = 0;
    if (answer) {
        uint8_t *a = out + qend;
        const uint8_t *ipb = (const uint8_t *)&ip;
        a[0] = 0xC0; a[1] = 0x0C;               /* the name in the question */
        a[2] = 0; a[3] = 1;                     /* A */
        a[4] = 0; a[5] = 1;                     /* IN */
        a[6] = 0; a[7] = 0; a[8] = 0; a[9] = 10; /* TTL 10 s, so phones forget it soon after setup */
        a[10] = 0; a[11] = 4;
        memcpy(a + 12, ipb, 4);
    }
    return need;
}

/* ======================================================================================================== */
/* The setup network's captive portal                                                                       */
/* ======================================================================================================== */

bool net_setup_probe_path(const char *path)
{
    static const char *const probes[] = {
        "/generate_204", "/gen_204",                        /* Android */
        "/hotspot-detect.html", "/library/test/success.html", /* iOS, macOS */
        "/connecttest.txt", "/ncsi.txt", "/redirect",       /* Windows */
        "/canonical.html", "/success.txt",                  /* Firefox */
    };
    if (!path) return false;
    size_t n = strcspn(path, "?#");
    for (size_t i = 0; i < sizeof probes / sizeof probes[0]; i++)
        if (strlen(probes[i]) == n && !strncasecmp(path, probes[i], n)) return true;
    return false;
}

bool net_ip_same_subnet(uint32_t a, uint32_t b, uint32_t mask)
{
    return mask && b && !((a ^ b) & mask);
}

/* ======================================================================================================== */
/* Rate-limited logging                                                                                     */
/* ======================================================================================================== */

bool net_log_quota_take(net_log_quota_t *q, tb_ms_t now, int max, int *dropped_before)
{
    if (dropped_before) *dropped_before = 0;
    if (!q->started || now - q->start >= 60000 || now < q->start) {
        if (dropped_before && q->started) *dropped_before = q->dropped;
        q->started = true;
        q->start = now;
        q->n = 0;
        q->dropped = 0;
    }
    if (q->n < max) {
        q->n++;
        return true;
    }
    q->dropped++;
    return false;
}

char *net_log_text(char *out, size_t cap, const char *in)
{
    if (!cap) return out;
    if (!in) in = "-";
    size_t o = 0;
    bool full = false;
    for (const unsigned char *p = (const unsigned char *)in; *p && !full; p++)
        full = !put_char(out, cap, &o, *p < 0x20 || *p > 0x7E ? '?' : (char)*p);
    if (full) mark_cut(out, o);
    out[o] = '\0';
    return out;
}

char *net_log_path(char *out, size_t cap, const char *path)
{
    if (!cap) return out;
    if (!path) path = "-";
    size_t n = strcspn(path, "?#"), o = 0;
    bool full = false;
    for (size_t i = 0; i < n && !full; i++) {
        unsigned char c = (unsigned char)path[i];
        full = !put_char(out, cap, &o, c < 0x20 || c > 0x7E ? '?' : (char)c);
    }
    for (const char *t = path[n] ? "?..." : ""; *t && !full; t++) full = !put_char(out, cap, &o, *t);
    if (full) mark_cut(out, o);
    out[o] = '\0';
    return out;
}

/* ======================================================================================================== */
/* Wi-Fi join errors                                                                                        */
/* ======================================================================================================== */

net_join_err_t net_join_err_from_reason(int reason, bool enterprise)
{
    switch (reason) {
    case NET_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case NET_REASON_HANDSHAKE_TIMEOUT:
    case NET_REASON_AUTH_FAIL:
    case NET_REASON_MIC_FAILURE:
    case NET_REASON_GROUP_KEY_UPDATE_TIMEOUT:
    case NET_REASON_IE_IN_4WAY_DIFFERS:
    case NET_REASON_802_1X_AUTH_FAILED:
        return enterprise ? NET_JOIN_LOGIN_FAILED : NET_JOIN_WRONG_PASSWORD;
    case NET_REASON_NO_AP_FOUND:
    case NET_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case NET_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return NET_JOIN_NOT_FOUND;
    default:
        return NET_JOIN_NO_SIGNAL;  /* beacon time-outs, association failures, weak signal */
    }
}

bool net_join_err_retry(net_join_err_t e)
{
    return e == NET_JOIN_NOT_FOUND || e == NET_JOIN_NO_SIGNAL;
}

const char *net_join_err_code(net_join_err_t e)
{
    switch (e) {
    case NET_JOIN_WRONG_PASSWORD: return "wrong_password";
    case NET_JOIN_LOGIN_FAILED: return "login_failed";
    case NET_JOIN_NOT_FOUND: return "not_found";
    case NET_JOIN_NO_SIGNAL: return "no_signal";
    case NET_JOIN_NO_ADDRESS: return "no_address";
    default: return NULL;
    }
}

const char *net_join_err_message(net_join_err_t e)
{
    switch (e) {
    case NET_JOIN_WRONG_PASSWORD: return "The password didn't work.";
    case NET_JOIN_LOGIN_FAILED: return "The work username or password didn't work.";
    case NET_JOIN_NOT_FOUND: return "TinyBar can't find that network anymore.";
    case NET_JOIN_NO_SIGNAL: return "The signal is too weak, or the network didn't answer.";
    case NET_JOIN_NO_ADDRESS: return "TinyBar joined but got no address. The network may need a sign-in page.";
    default: return NULL;
    }
}

const char *net_join_err_screen(net_join_err_t e)
{
    switch (e) {
    case NET_JOIN_WRONG_PASSWORD: return "Wrong password";
    case NET_JOIN_LOGIN_FAILED: return "Login failed";
    case NET_JOIN_NOT_FOUND: return "Network not found";
    case NET_JOIN_NO_SIGNAL: return "No signal";
    case NET_JOIN_NO_ADDRESS: return "No IP address";
    default: return "";
    }
}

/* ======================================================================================================== */
/* USB lines                                                                                                */
/* ======================================================================================================== */

void net_lines_init(net_lines_t *l)
{
    l->len = 0;
    l->too_long = false;
}

void net_lines_feed(net_lines_t *l, const uint8_t *data, size_t n, net_line_cb cb, void *ctx)
{
    for (size_t i = 0; i < n; i++) {
        uint8_t c = data[i];
        if (c == '\n') {
            /* The buffer holds one byte more than a line may have, for the CR before the LF. */
            if (l->len && l->buf[l->len - 1] == '\r' && !l->too_long) l->len--;
            if (l->len > NET_LINE_MAX) {
                l->len = NET_LINE_MAX;
                l->too_long = true;
            }
            l->buf[l->len] = '\0';
            cb(l->buf, l->len, l->too_long, ctx);
            l->len = 0;
            l->too_long = false;
        } else if (l->len < NET_LINE_MAX + 1) {
            l->buf[l->len++] = (char)c;
        } else {
            l->too_long = true;
        }
    }
}

char *net_ip_str(uint32_t ip, char out[16])
{
    const uint8_t *b = (const uint8_t *)&ip;
    snprintf(out, 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return out;
}

/* ======================================================================================================== */
/* JSON nesting                                                                                             */
/* ======================================================================================================== */

bool net_json_depth_ok(const char *s, size_t n, int max)
{
    int depth = 0;
    bool in_str = false, esc = false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
        } else if (c == '"') {
            in_str = true;
        } else if (c == '{' || c == '[') {
            if (++depth > max) return false;
        } else if ((c == '}' || c == ']') && depth > 0) {
            depth--;
        }
    }
    return true;
}

/* ======================================================================================================== */
/* The port's log output                                                                                    */
/* ======================================================================================================== */

size_t net_log_scan(net_log_state_t *st, const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == '\n') {
            *st = NET_LOG_LINE_START;
            continue;
        }
        switch (*st) {
        case NET_LOG_LINE_START:
            if (c == '@') return i;
            *st = c == 0x1B ? NET_LOG_ESC : NET_LOG_MID;
            break;
        case NET_LOG_ESC:       /* the Mac app strips ESC [ ... final byte, so the line's start is after it */
            *st = c == '[' ? NET_LOG_CSI : NET_LOG_MID;
            break;
        case NET_LOG_CSI:
            if (c >= 0x40 && c <= 0x7E) *st = NET_LOG_LINE_START;
            break;
        case NET_LOG_MID:
            break;
        }
    }
    return len;
}
