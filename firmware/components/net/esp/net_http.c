/*
 * net_http.c: the HTTP server (api.md 2.2, 2.5 and section 15). Owner: net builder.
 *
 * - /api/... : every method goes to the router, net_api_handle(), run ON THE APP TASK through tb_bus_exec() (900 ms
 *   to start, inside the 1 s reply rule; otherwise 503 busy). This file only turns the request into a net_req_t
 *   (headers, body up to 2 KB, which network it came in on, the peer's address) and writes the reply with the headers
 *   api.md requires: Content-Type, Cache-Control: no-store, X-Content-Type-Options: nosniff, no CORS, plus ETag,
 *   Set-Cookie, Allow, WWW-Authenticate and Retry-After when the router asks for them.
 * - everything else (GET): the Remote page on the office Wi-Fi, the setup page on TinyBar-Setup, both gzipped in
 *   flash. On the setup network a phone's captive-portal check (a well-known check path, or any other host) gets a
 *   302 to http://NET_SETUP_IP/ with a short HTML body (iOS wants one), so the phone opens the setup page in its
 *   sign-in sheet (ARCHITECTURE.md 10). Every request on the setup network is logged (INFO, the first 40 a minute):
 *   method, Host, path, both addresses, and what answered it.
 * Which network a request came in on is the socket's own address (setup_net()); if that ever can't be read as a plain
 * IPv4 address, a peer in the setup subnet while TinyBar-Setup is up counts as the setup network.
 * esp_http_server runs every handler in one task, so handlers stay short; slow work answers 202 (the router's job).
 * Seven sockets at most, the least recently used one is closed for a new client (lru_purge_enable).
 *
 * One client mustn't hold that task: every request has REQ_DEADLINE_US from its first byte to read its headers and
 * body (a receive override on each socket, so it covers esp_http_server's own header parsing too); a request that
 * runs over gets 408 from the server, or is dropped, and its socket closes. A body that isn't read (over 2 KB, or on
 * a page request) closes the socket after the reply, rather than being drained.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

#include "net_api.h"
#include "net_internal.h"
#include "net_util.h"
#include "tb_bus.h"

static const char *TAG = "net.http";

#define REQ_DEADLINE_US (3 * 1000 * 1000LL)     /* a request's headers and body, from its first byte */

extern const uint8_t remote_html_gz_start[] asm("_binary_remote_html_gz_start");
extern const uint8_t remote_html_gz_end[] asm("_binary_remote_html_gz_end");
extern const uint8_t setup_html_gz_start[] asm("_binary_setup_html_gz_start");
extern const uint8_t setup_html_gz_end[] asm("_binary_setup_html_gz_end");

static httpd_handle_t s_server;

/* ---------- helpers ---------- */

static const char *status_line(int st)
{
    switch (st) {
    case 200: return "200 OK";
    case 202: return "202 Accepted";
    case 204: return "204 No Content";
    case 302: return "302 Found";
    case 304: return "304 Not Modified";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    case 413: return "413 Content Too Large";
    case 415: return "415 Unsupported Media Type";
    case 421: return "421 Misdirected Request";
    case 429: return "429 Too Many Requests";
    case 503: return "503 Service Unavailable";
    default: return "500 Internal Server Error";
    }
}

/* The IPv4 address of either end of the socket, or 0. esp_http_server's listening socket is IPv6 dual-stack
 * (CONFIG_LWIP_IPV6), and lwIP reports an IPv4 client's addresses on it as ::ffff:a.b.c.d (lwIP 2.1 sockets.c,
 * lwip_getaddrname: "Dual-stack: Map IPv4 addresses to IPv4 mapped IPv6"), which this unmaps. text (optional, 48
 * bytes) gets the address as lwIP gave it, for the log. */
static uint32_t sock_ip(int fd, bool local, char *text)
{
    struct sockaddr_storage ss;
    socklen_t len = sizeof ss;
    if (text) strcpy(text, "?");
    int r = local ? getsockname(fd, (struct sockaddr *)&ss, &len) : getpeername(fd, (struct sockaddr *)&ss, &len);
    if (r) return 0;
    if (ss.ss_family == AF_INET) {
        const struct sockaddr_in *s4 = (const struct sockaddr_in *)&ss;
        if (text) inet_ntop(AF_INET, &s4->sin_addr, text, 48);
        return s4->sin_addr.s_addr;
    }
#if CONFIG_LWIP_IPV6
    if (ss.ss_family == AF_INET6) {
        const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)&ss;
        const uint8_t *b = (const uint8_t *)&s6->sin6_addr;
        static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        if (!memcmp(b, mapped, 12)) {
            uint32_t ip;
            memcpy(&ip, b + 12, 4);
            if (text) inet_ntop(AF_INET, &ip, text, 48);
            return ip;
        }
        if (text) inet_ntop(AF_INET6, &s6->sin6_addr, text, 48);
    }
#endif
    return 0;
}

static uint32_t setup_ip(void)
{
    static uint32_t ip;
    if (!ip) ip = inet_addr(NET_SETUP_IP);
    return ip;
}

static uint32_t setup_mask(void)
{
    static uint32_t mask;
    if (!mask) mask = inet_addr(NET_SETUP_NETMASK);
    return mask;
}

/* Which network a request came in on, and its two ends for the log. */
typedef struct {
    bool setup;         /* on TinyBar-Setup */
    bool by_peer;       /* decided by the peer's address: the local one wasn't a plain IPv4 address */
    uint32_t local, peer;
    char local_text[48], peer_text[48];
} net_side_t;

static void setup_net(int fd, net_side_t *s)
{
    s->local = sock_ip(fd, true, s->local_text);
    s->peer = sock_ip(fd, false, s->peer_text);
    s->by_peer = !s->local;
    if (!s->by_peer) s->setup = s->local == setup_ip();
    else s->setup = net_wifi_setup_net_up() && net_ip_same_subnet(s->peer, setup_ip(), setup_mask());
}

/* ---------- the setup network's log ---------- */

#define HTTP_LOG_PER_MIN 40
static net_log_quota_t s_quota;     /* the server's task only */

static void log_setup(httpd_req_t *r, const char *method, const char *host, const net_side_t *s, const char *answer)
{
    int dropped;
    if (!net_log_quota_take(&s_quota, esp_timer_get_time() / 1000, HTTP_LOG_PER_MIN, &dropped)) return;
    if (dropped) ESP_LOGI(TAG, "(%d more requests on the setup network in the last minute weren't logged)", dropped);
    char h[48], p[72];
    ESP_LOGI(TAG, "setup network: %s %s%s from %s to %s%s: %s", method, net_log_text(h, sizeof h, host),
             net_log_text(p, sizeof p, r->uri), s->peer_text, s->local_text,
             s->by_peer ? " (told by the peer's address)" : "", answer);
}

/* A request header as a heap string, or NULL. *unreadable (optional) tells "absent" (false) from "it came, but is
 * over max bytes or there was no memory for it" (true): for Origin, the second must never pass as the first. */
static char *header_ex(httpd_req_t *r, const char *name, size_t max, bool *unreadable)
{
    if (unreadable) *unreadable = false;
    size_t n = httpd_req_get_hdr_value_len(r, name);
    if (!n) {
        /* 0 is both "absent" and "present but empty": the value accessor tells them apart */
        char c;
        if (httpd_req_get_hdr_value_str(r, name, &c, 1) != ESP_OK) return NULL;
        char *e = calloc(1, 1);
        if (!e && unreadable) *unreadable = true;
        return e;
    }
    char *s = n <= max ? malloc(n + 1) : NULL;
    if (s && httpd_req_get_hdr_value_str(r, name, s, n + 1) != ESP_OK) {
        free(s);
        s = NULL;
    }
    if (!s && unreadable) *unreadable = true;
    return s;
}

static char *header(httpd_req_t *r, const char *name, size_t max)
{
    return header_ex(r, name, max, NULL);
}

/* ---------- the per-request deadline ---------- */

typedef struct {
    int64_t deadline;       /* esp_timer µs; 0 = no request in progress on this socket */
} sess_t;

/* Every read of a request (esp_http_server's header parsing, httpd_req_recv) goes through here: the first one starts
 * the request's deadline, and each waits at most what's left of it. */
static int recv_with_deadline(httpd_handle_t hd, int fd, char *buf, size_t len, int flags)
{
    if (!buf) return HTTPD_SOCK_ERR_INVALID;
    sess_t *s = httpd_sess_get_ctx(hd, fd);
    if (s) {
        int64_t now = esp_timer_get_time();
        if (!s->deadline) s->deadline = now + REQ_DEADLINE_US;
        int64_t left_ms = (s->deadline - now + 999) / 1000;
        if (left_ms <= 0) return HTTPD_SOCK_ERR_TIMEOUT;
        /* lwIP reads a time-out of 0 as "wait forever", so at least 1 ms */
        struct timeval tv = {.tv_sec = (time_t)(left_ms / 1000), .tv_usec = (suseconds_t)(left_ms % 1000) * 1000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    int n = recv(fd, buf, len, flags);
    if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? HTTPD_SOCK_ERR_TIMEOUT : HTTPD_SOCK_ERR_FAIL;
    return n;
}

static esp_err_t on_open(httpd_handle_t hd, int fd)
{
    sess_t *s = calloc(1, sizeof *s);
    if (!s) return ESP_ERR_NO_MEM;      /* refused: the server closes it */
    httpd_sess_set_ctx(hd, fd, s, free);
    return httpd_sess_set_recv_override(hd, fd, recv_with_deadline);
}

/* The request was answered: the socket's next request gets a deadline of its own. */
static void request_done(httpd_req_t *r)
{
    sess_t *s = r->sess_ctx;
    if (s) s->deadline = 0;
}

/* After the reply to a request whose body wasn't read: ESP_FAIL makes the server close the socket instead of
 * draining what's left (a 413's gigabyte, say) on its one task. */
static esp_err_t close_after(httpd_req_t *r)
{
    request_done(r);
    return ESP_FAIL;
}

static bool is_json_type(const char *ct)
{
    if (!ct) return false;
    while (*ct == ' ') ct++;
    if (strncasecmp(ct, "application/json", 16)) return false;
    char c = ct[16];
    return !c || c == ';' || c == ' ';
}

/* ---------- /api/ ---------- */

typedef struct {
    const net_req_t *req;
    net_resp_t *resp;
} api_job_t;

static void api_job(void *ctx)
{
    api_job_t *j = ctx;
    net_api_handle(j->req, j->resp);
}

/* Every method reaches the router, which answers the ones a path doesn't take with the JSON 405 and Allow
 * (api.md Appendix A). */
static const char *method_name(int m)
{
    switch (m) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    case HTTP_HEAD: return "HEAD";
    case HTTP_OPTIONS: return "OPTIONS";
    default: return m >= 0 ? http_method_str((enum http_method)m) : "UNKNOWN";
    }
}

static esp_err_t api_handler(httpd_req_t *r)
{
    int fd = httpd_req_to_sockfd(r);
    net_side_t side;
    setup_net(fd, &side);
    net_req_t q;
    memset(&q, 0, sizeof q);
    q.via = side.setup ? NET_VIA_SETUP : NET_VIA_HTTP;
    q.peer_ip = side.peer;
    q.method = method_name(r->method);
    q.path = r->uri;
    char *host = header(r, "Host", 255);
    char *origin = header_ex(r, "Origin", 255, &q.origin_unreadable);
    char *auth = header(r, "Authorization", 255);
    char *ctype = header(r, "Content-Type", 255);
    char *inm = header(r, "If-None-Match", 255);
    char cookie[64];                /* a token is 47 characters; anything longer isn't one */
    size_t cl = sizeof cookie;
    bool have_cookie = httpd_req_get_cookie_val(r, "tb_token", cookie, &cl) == ESP_OK;
    q.host = host;
    q.origin = origin;
    if (auth && !strncasecmp(auth, "Bearer ", 7)) {
        q.bearer = auth + 7;
        while (*q.bearer == ' ') q.bearer++;
    }
    q.cookie_token = have_cookie ? cookie : NULL;
    q.content_type_json = is_json_type(ctype);
    q.if_none_match = inm;

    char *body = NULL;
    if (r->content_len > NET_BODY_MAX) {
        q.body_too_large = true;   /* not read: the reply closes the socket (close_after) */
    } else if (r->content_len > 0) {
        body = heap_caps_malloc(r->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!body) body = malloc(r->content_len + 1);
        size_t got = 0;
        while (body && got < r->content_len) {
            /* each read waits only for what's left of the request's deadline (recv_with_deadline) */
            int n = httpd_req_recv(r, body + got, r->content_len - got);
            if (n <= 0) break;
            got += (size_t)n;
        }
        if (!body || got < r->content_len) {
            free(body);
            free(host); free(origin); free(auth); free(ctype); free(inm);
            return ESP_FAIL;    /* too slow, or the client went away: close the socket */
        }
        body[got] = '\0';
        q.body = body;
        q.body_len = got;
    }

    net_resp_t resp;
    memset(&resp, 0, sizeof resp);
    api_job_t job = {&q, &resp};
    if (!tb_bus_exec(api_job, &job, 900)) {
        resp.status = 503;
        resp.retry_after_s = 1;
        resp.body = strdup("{\"ok\":false,\"error\":\"busy\",\"message\":\"TinyBar is busy. Try again in a second.\",\"field\":null,\"retry_after_s\":1}");
        resp.len = resp.body ? strlen(resp.body) : 0;
    }

    char retry[12];
    httpd_resp_set_status(r, status_line(resp.status));
    httpd_resp_set_type(r, "application/json; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    if (resp.etag[0]) httpd_resp_set_hdr(r, "ETag", resp.etag);
    if (resp.set_cookie[0]) httpd_resp_set_hdr(r, "Set-Cookie", resp.set_cookie);
    if (resp.allow[0]) httpd_resp_set_hdr(r, "Allow", resp.allow);
    if (resp.www_authenticate) httpd_resp_set_hdr(r, "WWW-Authenticate", "Bearer realm=\"TinyBar\"");
    if (resp.retry_after_s > 0) {
        snprintf(retry, sizeof retry, "%d", resp.retry_after_s);
        httpd_resp_set_hdr(r, "Retry-After", retry);
    }
    if (q.body_too_large) httpd_resp_set_hdr(r, "Connection", "close");
    bool no_body = resp.status == 304 || r->method == HTTP_HEAD;
    esp_err_t err = httpd_resp_send(r, no_body ? NULL : resp.body, no_body ? 0 : (ssize_t)resp.len);
    if (side.setup) {
        char what[24];
        snprintf(what, sizeof what, "API %d", resp.status);
        log_setup(r, q.method, host, &side, what);
    }
    memset(cookie, 0, sizeof cookie);
    if (auth) memset(auth, 0, strlen(auth));
    free(resp.body);
    free(body);
    free(host); free(origin); free(auth); free(ctype); free(inm);
    if (q.body_too_large) return close_after(r);
    request_done(r);
    return err;
}

/* ---------- pages ---------- */

static bool host_is(const char *host, const char *want)
{
    size_t n = strlen(want);
    return host && !strncasecmp(host, want, n) && (!host[n] || !strcmp(host + n, ":80"));
}

static esp_err_t send_page(httpd_req_t *r, const uint8_t *start, const uint8_t *end)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(r, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(r, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(r, "Content-Security-Policy",
                       "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; "
                       "connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'");
    return httpd_resp_send(r, (const char *)start, end - start);
}

/* The captive portal's answer: a 302 to the setup page, with a small HTML body (iOS wants content to detect a portal,
 * as ESP-IDF's captive_portal example notes). */
static esp_err_t to_setup_page(httpd_req_t *r)
{
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "http://" NET_SETUP_IP "/");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_sendstr(r, "<!doctype html><title>TinyBar setup</title>"
                                 "<a href=\"http://" NET_SETUP_IP "/\">Set up TinyBar's Wi-Fi</a>");
}

static esp_err_t not_found(httpd_req_t *r)
{
    httpd_resp_set_status(r, "404 Not Found");
    httpd_resp_set_type(r, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(r, "Not found.");
}

static esp_err_t wrong_host(httpd_req_t *r)
{
    httpd_resp_set_status(r, "421 Misdirected Request");
    httpd_resp_set_type(r, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, "Use this TinyBar's own address, such as tinybar.local.");
}

static esp_err_t page_handler(httpd_req_t *r)
{
    int fd = httpd_req_to_sockfd(r);
    net_side_t side;
    setup_net(fd, &side);
    char *host = header(r, "Host", 255);
    const char *uri = r->uri;
    size_t plen = strcspn(uri, "?#");
    bool root = (plen == 1 && uri[0] == '/') || (plen == 11 && !strncmp(uri, "/index.html", 11));
    bool favicon = plen == 12 && !strncmp(uri, "/favicon.ico", 12);
    if (r->content_len) httpd_resp_set_hdr(r, "Connection", "close");     /* see the end */
    esp_err_t err;
    if (side.setup) {
        /* The captive portal: a check path (whatever its Host) or any other name goes to the setup page */
        const char *answer;
        if (net_setup_probe_path(uri)) {
            err = to_setup_page(r);
            answer = "302 to the setup page (a captive-portal check)";
        } else if (!host_is(host, NET_SETUP_IP)) {
            err = to_setup_page(r);
            answer = "302 to the setup page (another host)";
        } else if (root) {
            err = send_page(r, setup_html_gz_start, setup_html_gz_end);
            answer = "the setup page";
        } else if (favicon) {
            httpd_resp_set_status(r, "204 No Content");
            err = httpd_resp_send(r, NULL, 0);
            answer = "204 (no icon)";
        } else {
            err = to_setup_page(r);
            answer = "302 to the setup page (another path)";
        }
        log_setup(r, "GET", host, &side, err == ESP_OK ? answer : "the reply didn't go out");
    } else if (!net_api_host_ok(host)) {
        /* api.md 2.2 for the page too: a DNS-rebinding page under another name gets nothing from the bar */
        err = wrong_host(r);
    } else if (root) {
        err = send_page(r, remote_html_gz_start, remote_html_gz_end);
    } else if (favicon) {
        httpd_resp_set_status(r, "204 No Content");
        err = httpd_resp_send(r, NULL, 0);
    } else {
        err = not_found(r);
    }
    free(host);
    /* A page request carries no body; one that does is answered, then closed rather than drained. */
    if (r->content_len) return close_after(r);
    request_done(r);
    return err;
}

/* ---------- start ---------- */

esp_err_t net_http_start(void)
{
    if (s_server) return ESP_OK;
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.max_open_sockets = 7;
    c.lru_purge_enable = true;
    c.uri_match_fn = httpd_uri_match_wildcard;
    c.max_uri_handlers = 8;
    c.max_resp_headers = 10;
    c.stack_size = 6144;
    c.core_id = 0;
    /* The server task's stack goes to PSRAM, sparing 6 KB of internal RAM for Wi-Fi, lwIP and TLS (ARCHITECTURE.md
     * section 11). It never touches flash: the router runs on the app task (tb_bus_exec), and the pages are read
     * through the cache. */
    c.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    /* Reads wait at most what's left of the request's REQ_DEADLINE_US (recv_with_deadline); a send that makes no
     * progress for 3 s ends the reply and closes the socket. */
    c.recv_wait_timeout = 3;
    c.send_wait_timeout = 3;
    c.open_fn = on_open;
    esp_err_t err = httpd_start(&s_server, &c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "the HTTP server didn't start: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }
    /* Every method under /api/ goes to the router (OPTIONS and HEAD get its JSON 405 with Allow) */
    httpd_uri_t api = {.uri = "/api/*", .method = HTTP_ANY, .handler = api_handler};
    esp_err_t e1 = httpd_register_uri_handler(s_server, &api);
    httpd_uri_t page = {.uri = "/*", .method = HTTP_GET, .handler = page_handler};
    esp_err_t e2 = httpd_register_uri_handler(s_server, &page);
    if (e1 != ESP_OK || e2 != ESP_OK)
        ESP_LOGE(TAG, "registering the handlers failed: /api/* %s, /* %s", esp_err_to_name(e1), esp_err_to_name(e2));
    ESP_LOGI(TAG, "listening on port 80, every address (Remote %u bytes, setup page %u bytes, gzipped)",
             (unsigned)(remote_html_gz_end - remote_html_gz_start), (unsigned)(setup_html_gz_end - setup_html_gz_start));
    return ESP_OK;
}
