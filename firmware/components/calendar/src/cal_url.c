/*
 * cal_url.c: the secret address's format checks and masked form: a port of the mock-up's checkIcal().
 * Owner: calendar builder. See cal_url.h.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cal_url.h"
#include "tb_text.h"

/* Characters a URL path or query can't carry as they are (a browser percent-encodes them). */
static bool needs_escape(unsigned char c)
{
    return c <= 0x20 || c >= 0x7F || c == '"' || c == '<' || c == '>' || c == '`' || c == '{' || c == '}';
}

static bool host_char(unsigned char c)
{
    return isalnum(c) || c == '.' || c == '-' || c == '_';
}

static bool ends_with_ci(const char *s, size_t n, const char *suffix)
{
    size_t k = strlen(suffix);
    return n >= k && !strncasecmp(s + n - k, suffix, k);
}

/* Case-insensitive search for needle in s[0..n). */
static const char *find_ci(const char *s, size_t n, const char *needle)
{
    size_t k = strlen(needle);
    for (size_t i = 0; i + k <= n; i++)
        if (!strncasecmp(s + i, needle, k)) return s + i;
    return NULL;
}

/* Case-sensitive, as the mock-up's /\/public\// is. */
static const char *find_cs(const char *s, size_t n, const char *needle)
{
    size_t k = strlen(needle);
    for (size_t i = 0; i + k <= n; i++)
        if (!strncmp(s + i, needle, k)) return s + i;
    return NULL;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void url_decode(const char *s, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        int h, l;
        if (s[i] == '%' && i + 2 < n && (h = hexval(s[i + 1])) >= 0 && (l = hexval(s[i + 2])) >= 0) {
            out[o++] = (char)(h * 16 + l);
            i += 2;
        } else {
            out[o++] = s[i];
        }
    }
    out[o] = '\0';
}

cal_url_err_t cal_url_check(const char *raw, cal_url_info_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!raw) return CAL_URL_EMPTY;
    size_t rawn = strlen(raw);
    if (rawn > CAL_URL_MAX) return CAL_URL_TOO_LONG;

    /* Trim, and drop tabs and line breaks inside (as the URL parser does). */
    char v[CAL_URL_MAX + 1];
    size_t n = 0;
    for (size_t i = 0; i < rawn; i++)
        if (raw[i] != '\t' && raw[i] != '\n' && raw[i] != '\r') v[n++] = raw[i];
    v[n] = '\0';
    size_t a = 0;
    while (a < n && (unsigned char)v[a] <= 0x20) a++;
    while (n > a && (unsigned char)v[n - 1] <= 0x20) n--;
    v[n] = '\0';
    const char *s = v + a;
    n -= a;
    if (!n) return CAL_URL_EMPTY;

    /* scheme:// */
    const char *colon = strchr(s, ':');
    if (!colon || colon == s || strncmp(colon, "://", 3)) return CAL_URL_NOT_A_URL;
    size_t sl = (size_t)(colon - s);
    bool https = sl == 5 && !strncasecmp(s, "https", 5);
    bool http = sl == 4 && !strncasecmp(s, "http", 4);
    bool webcal = sl == 6 && !strncasecmp(s, "webcal", 6);
    if (!https && !http && !webcal) return CAL_URL_NOT_A_URL;

    /* authority: [user[:pass]@]host[:port] */
    const char *auth = colon + 3;
    const char *auth_end = auth;
    while (*auth_end && *auth_end != '/' && *auth_end != '?' && *auth_end != '#') auth_end++;
    const char *host = auth;
    for (const char *p = auth; p < auth_end; p++)
        if (*p == '@') host = p + 1;
    const char *host_end = host;
    while (host_end < auth_end && *host_end != ':') host_end++;
    size_t hn = (size_t)(host_end - host);
    if (!hn || hn > 253) return CAL_URL_NOT_A_URL;
    bool dot = false;
    for (const char *p = host; p < host_end; p++) {
        if (!host_char((unsigned char)*p)) return CAL_URL_NOT_A_URL;
        dot |= *p == '.';
    }
    if (!dot) return CAL_URL_NOT_A_URL;
    int port = -1;
    if (host_end < auth_end) {
        const char *p = host_end + 1;
        if (p < auth_end) {
            long pv = 0;
            for (; p < auth_end; p++) {
                if (!isdigit((unsigned char)*p) || pv > 65535) return CAL_URL_NOT_A_URL;
                pv = pv * 10 + (*p - '0');
            }
            if (pv > 65535) return CAL_URL_NOT_A_URL;
            port = (int)pv;
        }
    }

    /* path and query (the #fragment is dropped) */
    const char *path = auth_end, *path_end = path;
    while (*path_end && *path_end != '?' && *path_end != '#') path_end++;
    const char *query = path_end, *query_end = query;
    if (*query == '?') {
        while (*query_end && *query_end != '#') query_end++;
    }

    if (http) return CAL_URL_HTTP;

    /* Build the normalized address; the checks below run on its (encoded) path, as the browser's do. */
    char *u = out->url;
    size_t un = 0, cap = sizeof(out->url);
#define PUT(c) do { if (un + 1 >= cap) goto too_long; u[un++] = (char)(c); } while (0)
    static const char hex[] = "0123456789ABCDEF";
    const char *pre = "https://";
    for (const char *p = pre; *p; p++) PUT(*p);
    for (const char *p = host; p < host_end; p++) PUT(tolower((unsigned char)*p));
    /* The default port of https:// is left out, as the browser leaves it out. */
    if (port >= 0 && !(port == 443 && !webcal)) {
        char pb[8];
        int k = 0, pv = port;
        do pb[k++] = (char)('0' + pv % 10); while ((pv /= 10) && k < 7);
        PUT(':');
        while (k) PUT(pb[--k]);
    }
    size_t path_at = un;
    if (path == path_end) PUT('/');
    for (const char *p = path; p < path_end; p++) {
        unsigned char c = (unsigned char)*p;
        if (needs_escape(c)) {
            PUT('%');
            PUT(hex[c >> 4]);
            PUT(hex[c & 15]);
        } else {
            PUT(c);
        }
    }
    size_t path_len = un - path_at;
    if (query_end - query > 1) {
        for (const char *p = query; p < query_end; p++) {
            unsigned char c = (unsigned char)*p;
            if (needs_escape(c)) {
                PUT('%');
                PUT(hex[c >> 4]);
                PUT(hex[c & 15]);
            } else {
                PUT(c);
            }
        }
    }
#undef PUT
    u[un] = '\0';

    /* The checks run on the encoded path, as the browser's do. */
    const char *pb = u + path_at;
    if (find_cs(pb, path_len, "/public/")) {
        memset(out, 0, sizeof(*out));
        return CAL_URL_PUBLIC;
    }
    if (!ends_with_ci(pb, path_len, ".ics")) {
        memset(out, 0, sizeof(*out));
        return CAL_URL_NOT_ICS;
    }

    /* host, file, ending, google */
    size_t hcopy = hn < sizeof(out->host) - 1 ? hn : sizeof(out->host) - 1;
    for (size_t i = 0; i < hcopy; i++) out->host[i] = (char)tolower((unsigned char)host[i]);
    out->host[hcopy] = '\0';
    out->google = !strcasecmp(out->host, "google.com") || ends_with_ci(out->host, strlen(out->host), ".google.com");

    const char *last = pb + path_len;
    while (last > pb && *(last - 1) != '/') last--;
    last--;     /* at the last '/' (the path always starts with one) */
    {
        char file[sizeof(out->file)];
        size_t fn = (size_t)(pb + path_len - (last + 1));
        if (fn >= sizeof(file)) fn = sizeof(file) - 1;
        memcpy(file, last + 1, fn);
        file[fn] = '\0';
        tb_strlcpy(out->file, file, sizeof(out->file));
    }

    /* The token: what follows "private-" (letters and digits), else the second-to-last path segment. */
    const char *tok = NULL;
    size_t tn = 0;
    for (const char *p = pb; (p = find_ci(p, (size_t)(pb + path_len - p), "private-")) != NULL; p++) {
        const char *q = p + 8;
        while (q < pb + path_len && isalnum((unsigned char)*q)) q++;
        if (q > p + 8) {
            tok = p + 8;
            tn = (size_t)(q - tok);
            break;
        }
    }
    if (!tok && last > pb) {
        const char *prev = last - 1;
        while (prev > pb && *prev != '/') prev--;
        if (*prev == '/') prev++;
        tok = prev;
        tn = (size_t)(last - prev);
    }
    if (tok && tn) {
        size_t k = tn < 4 ? tn : 4;
        memcpy(out->ending, tok + tn - k, k);
        out->ending[k] = '\0';
    } else {
        strcpy(out->ending, "\xC2\xB7\xC2\xB7\xC2\xB7\xC2\xB7");
    }

    /* Google's path names the calendar: /calendar/ical/<id>/private-<token>/basic.ics. */
    if (out->google) {
        const char *ical = find_ci(pb, path_len, "/ical/");
        if (ical) {
            const char *id = ical + 6, *id_end = id;
            while (id_end < pb + path_len && *id_end != '/') id_end++;
            if (id_end < pb + path_len && id_end > id) {
                url_decode(id, (size_t)(id_end - id), out->self_email, sizeof(out->self_email));
                if (!strchr(out->self_email, '@')) out->self_email[0] = '\0';
            }
        }
    }
    return CAL_URL_OK;

too_long:
    memset(out, 0, sizeof(*out));
    return CAL_URL_TOO_LONG;
}

const char *cal_url_err_code(cal_url_err_t e)
{
    switch (e) {
    case CAL_URL_OK: return NULL;
    case CAL_URL_EMPTY: return "bad_request";
    case CAL_URL_NOT_A_URL: return "not_a_url";
    case CAL_URL_HTTP: return "http_not_allowed";
    case CAL_URL_PUBLIC: return "public_address";
    case CAL_URL_NOT_ICS: return "not_ics";
    case CAL_URL_TOO_LONG: return "bad_value";
    }
    return "bad_value";
}

const char *cal_url_err_message(cal_url_err_t e)
{
    switch (e) {
    case CAL_URL_OK: return NULL;
    case CAL_URL_EMPTY: return "Paste your secret address first.";
    case CAL_URL_NOT_A_URL: return "That isn't a web address. Copy the whole address, starting with https://.";
    case CAL_URL_HTTP:
        return "That address starts with http://, so it isn't encrypted. Use the https:// address Google gives you.";
    case CAL_URL_PUBLIC:
        return "That's your calendar's public address, which only works if the calendar is public. Copy the Secret "
               "address in iCal format instead; it's further down the same page.";
    case CAL_URL_NOT_ICS:
        return "That isn't a calendar address. The secret address ends in .ics (Google's ends in basic.ics).";
    case CAL_URL_TOO_LONG: return "That address is too long.";
    }
    return "That address can't be used.";
}
