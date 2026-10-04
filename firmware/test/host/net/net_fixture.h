/*
 * net_fixture.h: a bar on the office Wi-Fi, past its splash, with the router bound to it, plus helpers to send HTTP
 * requests and USB lines and read the JSON that comes back. Owner: net builder.
 */
#pragma once

#include "cJSON.h"
#include "fake_port.h"
#include "net_api.h"
#include "tb_app.h"

extern tb_app_t nf_app;
extern uint32_t nf_ip;          /* the peer address of the next HTTP request (network order) */
extern const char *nf_host;     /* the Host header of the next HTTP request */

typedef struct {
    int status;
    cJSON *j;                   /* the parsed body (NULL for 304 or no body) */
    net_resp_t r;               /* headers (etag, allow, set_cookie, www_authenticate, retry_after_s) */
} nf_resp_t;

/* Fresh bar and router; auth bearer; the token table empty. */
void nf_setup(void);
/* Let time pass on the bar (core and the router's timers run; effects are drained). */
void nf_advance(tb_ms_t ms);
/* HTTP: token may be NULL. body NULL = none. Content-Type is application/json. */
nf_resp_t nf_http(const char *method, const char *path, const char *body, const char *token);
/* Any request (for headers and odd cases); the caller fills it. */
nf_resp_t nf_req(net_req_t *req);
void nf_free(nf_resp_t *r);
/* A USB line ("@tb {...}"); returns the reply object (NULL if no reply), the raw reply in nf_last_line. */
cJSON *nf_usb(const char *line);
extern char nf_last_line[];

/* Pair over Wi-Fi with the code on the bar's screen; returns the token (static buffer) or NULL. */
const char *nf_pair(const char *kind, const char *scope, const char *client);

/* Dotted-path readers: "call.app", "macs.0.via". */
cJSON *nf_get(cJSON *j, const char *path);
const char *nf_str(cJSON *j, const char *path);     /* NULL if missing or not a string */
double nf_num(cJSON *j, const char *path);          /* -9999 if missing */
bool nf_true(cJSON *j, const char *path);
bool nf_null(cJSON *j, const char *path);           /* present and null */
const char *nf_err(nf_resp_t *r);                   /* the "error" code, or NULL */
