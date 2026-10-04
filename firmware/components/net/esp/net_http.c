/*
 * net_http.c: the HTTP server (api.md 2.2, 2.5 and section 15). Owner: net builder.
 *
 * - /api/... : every method goes to the router, net_api_handle(), run ON THE APP TASK through tb_bus_exec() (900 ms
 *   to start, inside the 1 s reply rule; otherwise 503 busy). This file only turns the request into a net_req_t
 *   (headers, body up to 2 KB, which network it came in on, the peer's address) and writes the reply with the headers
 *   api.md requires: Content-Type, Cache-Control: no-store, X-Content-Type-Options: nosniff, no CORS, plus ETag,
 *   Set-Cookie, Allow, WWW-Authenticate and Retry-After when the router asks for them.
 * - everything else (GET): the Remote page on the office Wi-Fi, the setup page on TinyBar-Setup, both gzipped in
 *   flash. On the setup network a request for any other host gets a 302 to http://192.168.4.1/, so a phone's
 *   captive-portal check opens the setup page (ARCHITECTURE.md 10).
 * esp_http_server runs every handler in one task, so handlers stay short; slow work answers 202 (the router's job).
 * Seven sockets at most, the least recently used one is closed for a new client (lru_purge_enable).
 */
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

#include "net_api.h"
#include "net_internal.h"
#include "net_util.h"
#include "tb_bus.h"

static const char *TAG = "net.http";

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

/* The IPv4 address of either end of the socket (lwIP's IPv6 sockets carry IPv4 as ::ffff:a.b.c.d). */
static uint32_t sock_ip(int fd, bool local)
{
    struct sockaddr_storage ss;
    socklen_t len = sizeof ss;
    int r = local ? getsockname(fd, (struct sockaddr *)&ss, &len) : getpeername(fd, (struct sockaddr *)&ss, &len);
    if (r) return 0;
    if (ss.ss_family == AF_INET) return ((struct sockaddr_in *)&ss)->sin_addr.s_addr;
#if CONFIG_LWIP_IPV6
    if (ss.ss_family == AF_INET6) {
        const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)&ss;
        const uint8_t *b = (const uint8_t *)&s6->sin6_addr;
        static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        if (!memcmp(b, mapped, 12)) {
            uint32_t ip;
            memcpy(&ip, b + 12, 4);
            return ip;
        }
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

/* A request header as a heap string, or NULL. */
static char *header(httpd_req_t *r, const char *name, size_t max)
{
    size_t n = httpd_req_get_hdr_value_len(r, name);
    if (!n || n > max) return NULL;
    char *s = malloc(n + 1);
    if (s && httpd_req_get_hdr_value_str(r, name, s, n + 1) != ESP_OK) {
        free(s);
        s = NULL;
    }
    return s;
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

static const char *method_name(int m)
{
    switch (m) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    default: return "OPTIONS";
    }
}

static esp_err_t api_handler(httpd_req_t *r)
{
    int fd = httpd_req_to_sockfd(r);
    net_req_t q;
    memset(&q, 0, sizeof q);
    q.via = sock_ip(fd, true) == setup_ip() ? NET_VIA_SETUP : NET_VIA_HTTP;
    q.peer_ip = sock_ip(fd, false);
    q.method = method_name(r->method);
    q.path = r->uri;
    char *host = header(r, "Host", 255);
    char *origin = header(r, "Origin", 255);
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
        q.body_too_large = true;   /* not read; the server throws the rest away */
    } else if (r->content_len > 0) {
        body = heap_caps_malloc(r->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!body) body = malloc(r->content_len + 1);
        size_t got = 0;
        int timeouts = 0;
        while (body && got < r->content_len) {
            int n = httpd_req_recv(r, body + got, r->content_len - got);
            if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
            if (n <= 0) break;
            got += (size_t)n;
        }
        if (!body || got < r->content_len) {
            free(body);
            free(host); free(origin); free(auth); free(ctype); free(inm);
            return ESP_FAIL;    /* the client went away: close the socket */
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
    esp_err_t err = httpd_resp_send(r, resp.status == 304 ? NULL : resp.body, resp.status == 304 ? 0 : (ssize_t)resp.len);
    memset(cookie, 0, sizeof cookie);
    if (auth) memset(auth, 0, strlen(auth));
    free(resp.body);
    free(body);
    free(host); free(origin); free(auth); free(ctype); free(inm);
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

static esp_err_t redirect(httpd_req_t *r, const char *to)
{
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", to);
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_sendstr(r, "<a href=\"http://" NET_SETUP_IP "/\">TinyBar setup</a>");
}

static esp_err_t not_found(httpd_req_t *r)
{
    httpd_resp_set_status(r, "404 Not Found");
    httpd_resp_set_type(r, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(r, "Not found.");
}

static esp_err_t page_handler(httpd_req_t *r)
{
    int fd = httpd_req_to_sockfd(r);
    bool setup = sock_ip(fd, true) == setup_ip();
    char *host = header(r, "Host", 255);
    const char *uri = r->uri;
    size_t plen = strcspn(uri, "?#");
    bool root = (plen == 1 && uri[0] == '/') || (plen == 11 && !strncmp(uri, "/index.html", 11));
    esp_err_t err;
    if (setup) {
        /* the captive portal: any other name is sent to the setup page */
        if (!host_is(host, NET_SETUP_IP)) err = redirect(r, "http://" NET_SETUP_IP "/");
        else if (root) err = send_page(r, setup_html_gz_start, setup_html_gz_end);
        else err = redirect(r, "/");
    } else if (root) {
        err = send_page(r, remote_html_gz_start, remote_html_gz_end);
    } else if (plen == 12 && !strncmp(uri, "/favicon.ico", 12)) {
        httpd_resp_set_status(r, "204 No Content");
        err = httpd_resp_send(r, NULL, 0);
    } else {
        err = not_found(r);
    }
    free(host);
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
    c.recv_wait_timeout = 5;
    c.send_wait_timeout = 5;
    esp_err_t err = httpd_start(&s_server, &c);
    if (err != ESP_OK) return err;
    static const httpd_method_t methods[] = {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE};
    for (size_t i = 0; i < sizeof methods / sizeof methods[0]; i++) {
        httpd_uri_t u = {.uri = "/api/*", .method = methods[i], .handler = api_handler};
        httpd_register_uri_handler(s_server, &u);
    }
    httpd_uri_t page = {.uri = "/*", .method = HTTP_GET, .handler = page_handler};
    httpd_register_uri_handler(s_server, &page);
    ESP_LOGI(TAG, "listening on port 80 (Remote %u bytes, setup page %u bytes, gzipped)",
             (unsigned)(remote_html_gz_end - remote_html_gz_start), (unsigned)(setup_html_gz_end - setup_html_gz_start));
    return ESP_OK;
}
