/*
 * jira_proto.c: see jira_proto.h. Owner: lead developer.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "jira_proto.h"
#include "tb_text.h"

/* ====================================================================================================== */
/* Messages                                                                                               */
/* ====================================================================================================== */

const char *jira_err_field(jira_err_t e)
{
    switch (e) {
    case JIRA_E_SITE: return "site";
    case JIRA_E_EMAIL: return "email";
    case JIRA_E_TOKEN: return "token";
    case JIRA_E_FILTER: return "filter_id";
    case JIRA_E_LABEL: return "label";
    case JIRA_E_ALERT: return "alert_above";
    default: return NULL;
    }
}

const char *jira_err_message(jira_err_t e)
{
    switch (e) {
    case JIRA_E_SITE:
        return "Use your site address, like https://yourteam.atlassian.net. It has to start with https:// and end in .atlassian.net.";
    case JIRA_E_EMAIL: return "Enter the email address of your Atlassian account.";
    case JIRA_E_TOKEN: return "Paste your API token.";
    case JIRA_E_FILTER: return "Enter the filter's ID: the number after filter= in its address.";
    case JIRA_E_LABEL: return "The screen label is 1 to 18 characters MiniBar can show.";
    case JIRA_E_ALERT: return "Alert above is a whole number from 0 to 9999, or leave it empty.";
    default: return "";
    }
}

/* ====================================================================================================== */
/* The fields                                                                                             */
/* ====================================================================================================== */

static bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char lower(char c)
{
    return (char)tolower((unsigned char)c);
}

static bool ieq_n(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (!a[i] || lower(a[i]) != lower(b[i])) return false;
    return true;
}

jira_err_t jira_check_site(const char *in, char *out, size_t out_cap, char *host, size_t host_cap)
{
    if (!in) return JIRA_E_SITE;
    while (is_ws(*in)) in++;
    size_t n = strnlen(in, 200);
    if (n >= 200) return JIRA_E_SITE;
    while (n && is_ws(in[n - 1])) n--;
    if (n && in[n - 1] == '/') n--;                     /* one trailing slash */
    static const char PRE[] = "https://", SUF[] = ".atlassian.net";
    const size_t pre = sizeof PRE - 1, suf = sizeof SUF - 1;
    if (n < pre + 1 + suf || !ieq_n(in, PRE, pre) || !ieq_n(in + n - suf, SUF, suf)) return JIRA_E_SITE;
    size_t nl = n - pre - suf;
    if (nl < 1 || nl > 63) return JIRA_E_SITE;
    const char *name = in + pre;
    for (size_t i = 0; i < nl; i++) {
        unsigned char c = (unsigned char)name[i];
        bool hy = c == '-';
        if (!(isalnum(c) || hy) || c > 0x7e) return JIRA_E_SITE;
        if (hy && (i == 0 || i == nl - 1)) return JIRA_E_SITE;
    }
    char site[JIRA_SITE_MAX + 1], hst[JIRA_HOST_MAX];
    int w = snprintf(hst, sizeof hst, "%.*s%s", (int)nl, name, SUF);
    if (w < 0 || (size_t)w >= sizeof hst) return JIRA_E_SITE;
    for (char *p = hst; *p; p++) *p = lower(*p);
    w = snprintf(site, sizeof site, "https://%s", hst);
    if (w < 0 || (size_t)w >= sizeof site) return JIRA_E_SITE;
    if (out && (!out_cap || strlen(site) >= out_cap)) return JIRA_E_SITE;
    if (host && (!host_cap || strlen(hst) >= host_cap)) return JIRA_E_SITE;
    if (out) memcpy(out, site, strlen(site) + 1);
    if (host) memcpy(host, hst, strlen(hst) + 1);
    return JIRA_OK;
}

jira_err_t jira_check_email(const char *in)
{
    if (!in) return JIRA_E_EMAIL;
    size_t n = strnlen(in, JIRA_EMAIL_MAX);
    if (n < 3 || n >= JIRA_EMAIL_MAX) return JIRA_E_EMAIL;
    const char *at = NULL;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c <= 0x20 || c >= 0x7f || strchr(":,;\"\\<>()[]", c)) return JIRA_E_EMAIL;
        if (c == '@') {
            if (at) return JIRA_E_EMAIL;
            at = in + i;
        }
    }
    if (!at || at == in) return JIRA_E_EMAIL;
    const char *dom = at + 1;
    size_t dl = n - (size_t)(dom - in);
    if (dl < 3 || dom[0] == '.' || dom[dl - 1] == '.' || !memchr(dom, '.', dl)) return JIRA_E_EMAIL;
    for (size_t i = 0; i < dl; i++) {
        unsigned char c = (unsigned char)dom[i];
        if (!(isalnum(c) || c == '.' || c == '-')) return JIRA_E_EMAIL;
        if (c == '.' && i + 1 < dl && dom[i + 1] == '.') return JIRA_E_EMAIL;
    }
    return JIRA_OK;
}

jira_err_t jira_check_token(const char *in)
{
    if (!in) return JIRA_E_TOKEN;
    size_t n = strnlen(in, JIRA_TOKEN_MAX);
    if (n < 8 || n >= JIRA_TOKEN_MAX) return JIRA_E_TOKEN;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c <= 0x20 || c >= 0x7f) return JIRA_E_TOKEN;
    }
    return JIRA_OK;
}

static bool all_digits(const char *s, size_t n)
{
    if (!n) return false;
    for (size_t i = 0; i < n; i++)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

/* digits at s (1 to 10 and then not another digit) into out without leading zeros; false when none, too long or zero */
static bool take_id(const char *s, char out[JIRA_FILTER_MAX])
{
    size_t n = 0;
    while (s[n] >= '0' && s[n] <= '9') n++;
    if (n < 1 || n > 10) return false;
    size_t z = 0;
    while (z < n - 1 && s[z] == '0') z++;
    if (s[z] == '0') return false;          /* all zero */
    memcpy(out, s + z, n - z);
    out[n - z] = '\0';
    return true;
}

jira_err_t jira_filter_id(const char *in, char out[JIRA_FILTER_MAX])
{
    if (!in) return JIRA_E_FILTER;
    while (is_ws(*in)) in++;
    size_t n = strnlen(in, 513);
    if (n > 512) return JIRA_E_FILTER;
    while (n && is_ws(in[n - 1])) n--;
    char buf[520];
    memcpy(buf, in, n);
    buf[n] = '\0';
    if (all_digits(buf, n)) return take_id(buf, out) ? JIRA_OK : JIRA_E_FILTER;
    for (size_t i = 0; i < n; i++) {
        if ((buf[i] == '?' || buf[i] == '&') && !strncmp(buf + i + 1, "filter=", 7) && take_id(buf + i + 8, out)) return JIRA_OK;
        if (buf[i] == '/' && !strncmp(buf + i + 1, "filters/", 8) && take_id(buf + i + 9, out)) return JIRA_OK;
    }
    return JIRA_E_FILTER;
}

jira_err_t jira_check_label(const char *in, char out[TB_JIRA_LABEL_BYTES])
{
    if (!in || strnlen(in, 400) >= 400) return JIRA_E_LABEL;
    char tmp[TB_JIRA_LABEL_BYTES * 4];
    size_t n = tb_text_clean(tmp, sizeof tmp, in);
    if (n < 1 || n > TB_JIRA_LABEL_CHARS) return JIRA_E_LABEL;
    tb_text_replace_unsupported(tmp, sizeof tmp);
    if (strlen(tmp) >= TB_JIRA_LABEL_BYTES) return JIRA_E_LABEL;
    memcpy(out, tmp, strlen(tmp) + 1);
    return JIRA_OK;
}

jira_err_t jira_check_alert(const char *in, int32_t *out)
{
    *out = -1;
    if (!in) return JIRA_OK;
    while (is_ws(*in)) in++;
    size_t n = strnlen(in, 16);
    if (n >= 16) return JIRA_E_ALERT;
    while (n && is_ws(in[n - 1])) n--;
    if (!n) return JIRA_OK;
    if (n > 4 || !all_digits(in, n)) return JIRA_E_ALERT;
    int32_t v = 0;
    for (size_t i = 0; i < n; i++) v = v * 10 + (in[i] - '0');
    if (v > TB_JIRA_ALERT_MAX) return JIRA_E_ALERT;
    *out = v;
    return JIRA_OK;
}

void jira_label_from_name(const char *name, char out[TB_JIRA_LABEL_BYTES])
{
    out[0] = '\0';
    if (!name) return;
    char tmp[TB_JIRA_LABEL_BYTES * 8];
    tb_strlcpy(tmp, name, sizeof tmp);
    char clean[sizeof tmp];
    tb_text_clean(clean, sizeof clean, tmp);
    tb_text_replace_unsupported(clean, sizeof clean);
    tb_text_ellipsize(clean, sizeof clean, TB_JIRA_LABEL_CHARS);
    tb_strlcpy(out, clean, TB_JIRA_LABEL_BYTES);
}

void jira_email_hint(const char *email, char *out, size_t cap)
{
    if (!cap) return;
    const char *at = email ? strchr(email, '@') : NULL;
    char first[2] = {email && email[0] && at && at != email ? email[0] : '\0', '\0'};
    snprintf(out, cap, "%s\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2%s", first, at ? at : "");
}

/* ====================================================================================================== */
/* The settings                                                                                           */
/* ====================================================================================================== */

static bool given(const char *s)
{
    if (!s) return false;
    while (*s) {
        if (!is_ws(*s)) return true;
        s++;
    }
    return false;
}

jira_err_t jira_resolve(const jira_cfg_t *saved, const jira_input_t *in, jira_cfg_t *out)
{
    jira_input_t none;
    memset(&none, 0, sizeof none);
    if (!in) in = &none;
    memset(out, 0, sizeof *out);
    jira_err_t e;
    if (given(in->site)) {
        if ((e = jira_check_site(in->site, out->site, sizeof out->site, out->host, sizeof out->host)) != JIRA_OK) return e;
    } else if (saved && saved->site[0]) {
        memcpy(out->site, saved->site, sizeof out->site);
        memcpy(out->host, saved->host, sizeof out->host);
    } else {
        return JIRA_E_SITE;
    }
    if (given(in->email)) {
        if ((e = jira_check_email(in->email)) != JIRA_OK) return e;
        memcpy(out->email, in->email, strlen(in->email) + 1);
    } else if (saved && saved->email[0]) {
        memcpy(out->email, saved->email, sizeof out->email);
    } else {
        return JIRA_E_EMAIL;
    }
    if (given(in->token)) {
        if ((e = jira_check_token(in->token)) != JIRA_OK) return e;
        memcpy(out->token, in->token, strlen(in->token) + 1);
    } else if (saved && saved->token[0]) {
        memcpy(out->token, saved->token, sizeof out->token);
    } else {
        return JIRA_E_TOKEN;
    }
    if (given(in->filter)) {
        if ((e = jira_filter_id(in->filter, out->filter)) != JIRA_OK) return e;
    } else if (saved && saved->filter[0]) {
        memcpy(out->filter, saved->filter, sizeof out->filter);
    } else {
        return JIRA_E_FILTER;
    }
    if (given(in->label)) {
        if ((e = jira_check_label(in->label, out->label)) != JIRA_OK) return e;
        out->label_auto = false;
    } else if (saved && saved->label[0] && (!saved->label_auto || !strcmp(saved->filter, out->filter))) {
        memcpy(out->label, saved->label, sizeof out->label);
        out->label_auto = saved->label_auto;
    } else {
        snprintf(out->label, sizeof out->label, "Filter %s", out->filter);
        out->label_auto = true;
    }
    if (in->has_alert) {
        if ((e = jira_check_alert(in->alert, &out->alert_above)) != JIRA_OK) return e;
    } else {
        out->alert_above = saved ? saved->alert_above : -1;
    }
    return JIRA_OK;
}

/* ====================================================================================================== */
/* The request                                                                                            */
/* ====================================================================================================== */

static bool host_ok(const char *host)
{
    if (!host) return false;
    size_t n = strnlen(host, JIRA_HOST_MAX);
    if (n < 4 || n >= JIRA_HOST_MAX) return false;
    for (size_t i = 0; i < n; i++)
        if (!(isalnum((unsigned char)host[i]) || host[i] == '.' || host[i] == '-')) return false;
    return true;
}

static bool id_ok(const char *id)
{
    if (!id) return false;
    size_t n = strnlen(id, JIRA_FILTER_MAX);
    return n >= 1 && n <= 10 && all_digits(id, n);
}

bool jira_count_request(const char *host, const char *filter_id, jira_request_t *out)
{
    memset(out, 0, sizeof *out);
    if (!host_ok(host) || !id_ok(filter_id)) return false;
    /* The one place that names the endpoint and the body (not verified against the live API: jira_proto.h). */
    int u = snprintf(out->url, sizeof out->url, "https://%s/rest/api/3/search/approximate-count", host);
    int b = snprintf(out->body, sizeof out->body, "{\"jql\":\"filter = %s\"}", filter_id);
    out->post = true;
    return u > 0 && (size_t)u < sizeof out->url && b > 0 && (size_t)b < sizeof out->body;
}

bool jira_filter_request(const char *host, const char *filter_id, jira_request_t *out)
{
    memset(out, 0, sizeof *out);
    if (!host_ok(host) || !id_ok(filter_id)) return false;
    int u = snprintf(out->url, sizeof out->url, "https://%s/rest/api/3/filter/%s", host, filter_id);
    return u > 0 && (size_t)u < sizeof out->url;
}

void jira_wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}

static size_t b64(const unsigned char *in, size_t n, char *out)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)in[i] << 16 | (i + 1 < n ? (unsigned)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0u);
        out[o++] = T[v >> 18 & 63];
        out[o++] = T[v >> 12 & 63];
        out[o++] = i + 1 < n ? T[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o] = '\0';
    return o;
}

bool jira_basic_auth(const char *email, const char *token, char *out, size_t cap)
{
    if (!email || !token || !out || !cap) return false;
    out[0] = '\0';
    size_t e = strnlen(email, JIRA_EMAIL_MAX), t = strnlen(token, JIRA_TOKEN_MAX);
    if (e >= JIRA_EMAIL_MAX || t >= JIRA_TOKEN_MAX || memchr(email, ':', e)) return false;
    unsigned char raw[JIRA_EMAIL_MAX + JIRA_TOKEN_MAX + 2];
    memcpy(raw, email, e);
    raw[e] = ':';
    memcpy(raw + e + 1, token, t);
    size_t rn = e + 1 + t;
    size_t need = 6 + (rn + 2) / 3 * 4 + 1;
    bool ok = need <= cap;
    if (ok) {
        memcpy(out, "Basic ", 6);
        b64(raw, rn, out + 6);
    }
    jira_wipe(raw, sizeof raw);
    return ok;
}

/* ====================================================================================================== */
/* The answer                                                                                             */
/* ====================================================================================================== */

jira_http_t jira_http_class(int status)
{
    if (status >= 200 && status <= 299) return JIRA_HTTP_ANSWER;
    if (status == 401 || status == 403) return JIRA_HTTP_TOKEN;
    if (status == 404 || status == 400) return JIRA_HTTP_NOFILTER;
    return JIRA_HTTP_UNREACHABLE;
}

int32_t jira_parse_retry_after(const char *value)
{
    if (!value) return 0;
    while (is_ws(*value)) value++;
    size_t n = 0;
    while (n < 8 && value[n] >= '0' && value[n] <= '9') n++;
    if (n < 1 || n > 5) return 0;
    const char *e = value + n;
    while (is_ws(*e)) e++;
    if (*e) return 0;
    long v = 0;
    for (size_t i = 0; i < n; i++) v = v * 10 + (value[i] - '0');
    return v > 86400 ? 86400 : (int32_t)v;
}

/* A minimal reader of one JSON object's TOP level: finds `key` and returns where its value starts and ends, without
 * building anything or recursing. Strings are skipped with their escapes; nested values are skipped by counting
 * brackets (at most 32 deep); it never reads past len. */
typedef struct {
    const char *p, *end;
} cur_t;

static void skip_ws(cur_t *c)
{
    while (c->p < c->end && is_ws(*c->p)) c->p++;
}

static bool skip_string(cur_t *c)       /* c->p at the opening quote */
{
    c->p++;
    while (c->p < c->end) {
        if (*c->p == '\\') {
            c->p += 2;
            continue;
        }
        if (*c->p == '"') {
            c->p++;
            return true;
        }
        c->p++;
    }
    return false;
}

static bool skip_value(cur_t *c)
{
    skip_ws(c);
    if (c->p >= c->end) return false;
    if (*c->p == '"') return skip_string(c);
    if (*c->p == '{' || *c->p == '[') {
        int depth = 0;
        while (c->p < c->end) {
            char ch = *c->p;
            if (ch == '"') {
                if (!skip_string(c)) return false;
                continue;
            }
            if (ch == '{' || ch == '[') {
                if (++depth > 32) return false;
            } else if (ch == '}' || ch == ']') {
                if (--depth == 0) {
                    c->p++;
                    return true;
                }
            }
            c->p++;
        }
        return false;
    }
    while (c->p < c->end && *c->p != ',' && *c->p != '}' && !is_ws(*c->p)) c->p++;     /* a number, true, false, null */
    return c->p < c->end;       /* a number that runs to the end of the text may have been cut short: not a value */
}

static bool top_value(const char *json, size_t len, const char *key, const char **vs, const char **ve)
{
    cur_t c = {json, json + len};
    if (len >= 3 && !memcmp(json, "\xEF\xBB\xBF", 3)) c.p += 3;
    skip_ws(&c);
    if (c.p >= c.end || *c.p != '{') return false;
    c.p++;
    size_t kl = strlen(key);
    for (;;) {
        skip_ws(&c);
        if (c.p >= c.end || *c.p == '}') return false;
        if (*c.p != '"') return false;
        const char *ks = c.p + 1;
        if (!skip_string(&c)) return false;
        const char *ke = c.p - 1;
        skip_ws(&c);
        if (c.p >= c.end || *c.p != ':') return false;
        c.p++;
        skip_ws(&c);
        const char *start = c.p;
        if (!skip_value(&c)) return false;
        if ((size_t)(ke - ks) == kl && !memcmp(ks, key, kl)) {
            *vs = start;
            *ve = c.p;
            skip_ws(&c);
            return c.p < c.end && (*c.p == ',' || *c.p == '}');     /* a value is followed by the next key or the end */
        }
        skip_ws(&c);
        if (c.p < c.end && *c.p == ',') c.p++;
        else return false;      /* the object ended (or isn't one) without the key */
    }
}

/* A whole number from a JSON number or a string of digits; false for anything else. */
static bool whole_number(const char *s, const char *e, int32_t *out)
{
    if (s < e && *s == '"') {
        s++;
        if (e - s < 1 || e[-1] != '"') return false;
        e--;
    }
    if (s >= e || *s == '-' || *s == '+') return false;
    size_t i = 0, n = (size_t)(e - s);
    int64_t v = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        if (v > 2000000000) return false;
        i++;
    }
    if (i == 0) return false;
    if (i < n && s[i] == '.') {                         /* "12.0" is fine, "12.5" isn't a count */
        i++;
        size_t f = i;
        while (i < n && s[i] == '0') i++;
        if (i == f) return false;
    }
    if (i != n) return false;                           /* an exponent, a sign, leftovers */
    *out = (int32_t)v;
    return true;
}

bool jira_parse_count(const char *json, size_t len, int32_t *count)
{
    if (!json || !count) return false;
    const char *s, *e;
    int32_t v;
    if (top_value(json, len, "count", &s, &e) && whole_number(s, e, &v)) {
        *count = v;
        return true;
    }
    if (top_value(json, len, "total", &s, &e) && whole_number(s, e, &v)) {      /* the old /search's field, as a fallback */
        *count = v;
        return true;
    }
    return false;
}

static int hex4(const char *p, const char *end)
{
    if (end - p < 4) return -1;
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return -1;
        v = v * 16 + d;
    }
    return v;
}

static size_t put_utf8(char *o, uint32_t cp)
{
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | cp >> 6); o[1] = (char)(0x80 | (cp & 63)); return 2; }
    if (cp < 0x10000) { o[0] = (char)(0xE0 | cp >> 12); o[1] = (char)(0x80 | (cp >> 6 & 63)); o[2] = (char)(0x80 | (cp & 63)); return 3; }
    o[0] = (char)(0xF0 | cp >> 18); o[1] = (char)(0x80 | (cp >> 12 & 63)); o[2] = (char)(0x80 | (cp >> 6 & 63)); o[3] = (char)(0x80 | (cp & 63));
    return 4;
}

bool jira_parse_filter_name(const char *json, size_t len, char *out, size_t cap)
{
    if (!json || !out || cap < 2) return false;
    out[0] = '\0';
    const char *s, *e;
    if (!top_value(json, len, "name", &s, &e) || e - s < 2 || *s != '"' || e[-1] != '"') return false;
    s++;
    e--;
    size_t o = 0;
    while (s < e) {
        char tmp[4];
        size_t k;
        if (*s != '\\') {
            tmp[0] = *s++;
            k = 1;
        } else {
            if (++s >= e) return false;
            char c = *s++;
            switch (c) {
            case '"': case '\\': case '/': tmp[0] = c; k = 1; break;
            case 'b': tmp[0] = '\b'; k = 1; break;
            case 'f': tmp[0] = '\f'; k = 1; break;
            case 'n': tmp[0] = '\n'; k = 1; break;
            case 'r': tmp[0] = '\r'; k = 1; break;
            case 't': tmp[0] = '\t'; k = 1; break;
            case 'u': {
                int u = hex4(s, e);
                if (u < 0) return false;
                s += 4;
                uint32_t cp = (uint32_t)u;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    int lo;
                    if (e - s < 6 || s[0] != '\\' || s[1] != 'u' || (lo = hex4(s + 2, e)) < 0xDC00 || lo > 0xDFFF) return false;
                    s += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + ((uint32_t)lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return false;
                }
                if (cp == 0) return false;
                k = put_utf8(tmp, cp);
                break;
            }
            default: return false;
            }
        }
        if (o + k >= cap) break;            /* cut here: what fits, never half a character */
        memcpy(out + o, tmp, k);
        o += k;
    }
    out[o] = '\0';
    return o > 0;
}

/* The Remote's plain test errors (decisions.md, "Test and error copy"). */
#include "jira_service.h"

const char *jira_test_message(const char *code)
{
    if (!code) return "";
    if (!strcmp(code, JIRA_ERR_UNREACHABLE)) return "Couldn't reach Jira. Check the site address, and that this MiniBar is online.";
    if (!strcmp(code, JIRA_ERR_TOKEN)) return "Jira didn't accept that email and token. Check them, or create a new token.";
    if (!strcmp(code, JIRA_ERR_FILTER)) return "Jira has no filter with that ID, or this account can't see it.";
    if (!strcmp(code, JIRA_ERR_OFFLINE)) return "MiniBar can't reach the internet right now, so it can't ask Jira. Check its Wi-Fi.";
    return "";
}
