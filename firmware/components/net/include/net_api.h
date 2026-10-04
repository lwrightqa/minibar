/*
 * net_api.h: the one router for HTTP and USB (api.md section 15, "One router"). Pure C on top of core, cJSON and
 * net_port.h, so it builds and is tested on Linux.
 *
 * Owner: net builder.
 *
 * The HTTP handlers (esp/net_http.c) and the USB line reader (net_usb.h) turn a request into a net_req_t and call
 * net_api_handle() ON THE APP TASK through tb_bus_exec(), because it reads and changes the core model. It must stay
 * quick (well under the 1 s reply rule): anything slow (calendar checks, Wi-Fi joins) starts elsewhere and answers 202.
 *
 * It implements every endpoint in api.md Appendix A, with:
 *   - the Host check (2.2) and 421 wrong_host; Content-Type check and 415; body size 413; bad JSON 400
 *   - tokens and scopes (section 4, Proposed): 401 with WWW-Authenticate, 403 wrong_scope, cookie + Origin check
 *     (403 bad_origin), auth "none" mode (CONFIG_TINYBAR_API_AUTH_NONE) where tokens are ignored
 *   - rate limits (2.5): 10/s average and bursts of 20 per IP, 5/s per IP without a valid token, 1/s for pair
 *   - the ETag / If-None-Match 304 on GET /api/v1/status (rev)
 *   - 405 with Allow, 404 not_found, the setup-only endpoints (13), 409 in_setup while the setup screens show
 *   - error bodies {"ok": false, "error", "message", "field"} plus retry_after_s, attempts_left, chars
 *   - text cleaning (2.3) through tb_text_clean() and refusal of undrawable message characters (unsupported_chars)
 */
#pragma once

#include "tb_app.h"
#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_BODY_MAX   2048    /* request body and USB line */
#define NET_REPLY_MAX  8192    /* response body and USB reply line */

typedef enum { NET_VIA_HTTP = 0, NET_VIA_USB, NET_VIA_SETUP } net_via_t;

typedef struct {
    net_via_t via;
    const char *method;             /* "GET", "POST", "PUT", "PATCH", "DELETE" */
    const char *path;               /* "/api/v1/status", may carry a query */
    const char *body;               /* NULL or the body (not necessarily NUL-terminated) */
    size_t body_len;
    bool content_type_json;         /* HTTP: Content-Type was application/json (USB: always true) */
    const char *host;               /* HTTP Host header, or NULL (USB) */
    const char *origin;             /* HTTP Origin header, or NULL */
    const char *bearer;             /* the token from "Authorization: Bearer", or NULL */
    const char *cookie_token;       /* the tb_token cookie, or NULL */
    const char *if_none_match;      /* or NULL */
    uint32_t peer_ip;               /* IPv4 in network order, 0 for USB */
} net_req_t;

typedef struct {
    int status;                     /* HTTP status: 200, 202, 304, 400... */
    char *body;                     /* malloc'ed JSON, NUL-terminated, at most NET_REPLY_MAX bytes; NULL for 304 */
    size_t len;
    char etag[16];                  /* "\"r1842\"" for GET /api/v1/status, else "" */
    char set_cookie[160];           /* "" or a whole Set-Cookie value */
    char allow[40];                 /* for 405 */
    bool www_authenticate;          /* add WWW-Authenticate: Bearer realm="TinyBar" */
    int retry_after_s;              /* > 0: add Retry-After */
} net_resp_t;

/* Give the router the app model it serves (main, once, before any request). */
void net_api_bind(tb_app_t *app);
/* Load the token table (pairing) through net_port_tokens_load(). */
void net_api_init(void);
/* Handle one request. App task only. The caller frees resp->body. */
void net_api_handle(const net_req_t *req, net_resp_t *resp);
/* Timers that belong to the protocol: the Macs' 90 s time-out, the pairing code's 2 minutes. App task, every loop. */
void net_api_tick(const tb_clock_t *now);
/* core's effects that concern the protocol (main forwards them). */
void net_api_pairing_canceled(const tb_clock_t *now);   /* TB_FX_PAIRING_CANCELED */
void net_api_forget_devices(const tb_clock_t *now);     /* TB_FX_FORGET_DEVICES */

/*
 * USB: handle one complete line from the Mac (without its line ending). Lines without the "@tb " marker are ignored
 * (returns false, nothing to send). Otherwise writes exactly one reply line into out (starting with "@tb ", without
 * the line ending) and returns true: hello, call, status, pair, request, and the errors bad_json, unknown_cmd,
 * unsupported_api (api.md 6.5, 6.6). too_long = the line was over 2,048 bytes and was cut (reply too_large).
 * App task only (it goes through net_api_handle()).
 */
bool net_api_usb_line(const char *line, size_t len, bool too_long, char *out, size_t cap);
/* The "ready" event line (api.md 6.7), without the line ending. */
void net_api_usb_ready_line(char *out, size_t cap);

#ifdef __cplusplus
}
#endif
