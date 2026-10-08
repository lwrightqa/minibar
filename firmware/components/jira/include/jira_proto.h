/*
 * jira_proto.h: everything about talking to Jira Cloud that doesn't need the network, so it is host-tested with hostile
 * inputs: checking what the Remote sends (site, email, token, filter, label, alert), building the request, signing it in,
 * and reading the answer. No ESP-IDF headers, no allocation, no logging.
 *
 * Owner: lead developer. See decisions.md "Jira issue count (2026-10-07)".
 *
 * NOT VERIFIED AGAINST THE LIVE JIRA API: Atlassian's reference pages were unreachable when this was written. The
 * endpoint, the request body and the answer's shape come from search results quoting them (decisions.md, Research note).
 * They are all in jira_count_request() and jira_parse_count() below, in one place, so changing one is a one-line edit:
 *     POST https://<site>/rest/api/3/search/approximate-count   body {"jql": "filter = <id>"}   answer {"count": N}
 * The parser is lenient about the answer (a number or a numeric string, the old "total" as a fallback).
 *
 * Secrets: the token and the email are passed in and out of these functions only through the buffers the caller owns.
 * Nothing here logs, and the one function that makes the Authorization value (jira_basic_auth) writes it only into the
 * caller's buffer, which the caller wipes (jira_wipe) as soon as the request has gone.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tb_jira.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JIRA_SITE_MAX     96      /* "https://" + a name of up to 63 characters + ".atlassian.net" (85) and the NUL fit */
#define JIRA_HOST_MAX     80
#define JIRA_EMAIL_MAX    128     /* an address is at most 254 characters; a longer one is refused here, plainly */
#define JIRA_TOKEN_MAX    256
#define JIRA_FILTER_MAX   12      /* up to 10 digits and the NUL */
#define JIRA_URL_MAX      160
#define JIRA_BODY_MAX     48
#define JIRA_AUTH_MAX     ((JIRA_EMAIL_MAX + JIRA_TOKEN_MAX + 1) * 4 / 3 + 16)   /* "Basic " + base64(email:token) */

/* Why a field was refused, for the Remote's plain sentences. */
typedef enum {
    JIRA_OK = 0,
    JIRA_E_SITE,        /* not https://<name>.atlassian.net */
    JIRA_E_EMAIL,
    JIRA_E_TOKEN,
    JIRA_E_FILTER,
    JIRA_E_LABEL,
    JIRA_E_ALERT,
} jira_err_t;

/* The API field that failed, for {"field": ...}: "site", "email", "token", "filter_id", "label", "alert_above". */
const char *jira_err_field(jira_err_t e);
/* The plain sentence for the Remote (no codes, never an echo of what was sent). */
const char *jira_err_message(jira_err_t e);

/*
 * Site: exactly "https://<name>.atlassian.net", with an optional single trailing "/" and any capitals (the name is
 * lower-cased). <name> is 1 to 63 letters, digits and hyphens, not starting or ending with a hyphen. No user name, port,
 * path, query, fragment, spaces or control characters: the address the Basic auth is sent to must be exactly this host.
 * Writes "https://<name>.atlassian.net" (lower case, no slash) to out and the bare host to host (either may be NULL).
 */
jira_err_t jira_check_site(const char *in, char *out, size_t out_cap, char *host, size_t host_cap);
/* Email: one "@", something before it and a dot in the domain after it, printable ASCII without spaces, no ":" (it
 * would end the user name in Basic auth), at most JIRA_EMAIL_MAX - 1 characters. */
jira_err_t jira_check_email(const char *in);
/* Token: 8 to JIRA_TOKEN_MAX - 1 printable ASCII characters, no spaces. (Atlassian's classic tokens are a few dozen.) */
jira_err_t jira_check_token(const char *in);
/* Filter: the ID as digits (1 to 10, not all zero), or an address that carries it: "...?filter=10042", "...&filter=10042"
 * or ".../filters/10042". Surrounding spaces are ignored. Writes the digits, without leading zeros. */
jira_err_t jira_filter_id(const char *in, char out[JIRA_FILTER_MAX]);
/* Label: up to 18 characters the bar can draw (the same cleaning as a message: tb_text_clean, then the characters it
 * can't draw become "?"). Empty or all blank is refused: the caller defaults it to the filter's name first. */
jira_err_t jira_check_label(const char *in, char out[TB_JIRA_LABEL_BYTES]);
/* Alert above: whole numbers 0 to 9999 as a decimal string. NULL or "" means none (-1). */
jira_err_t jira_check_alert(const char *in, int32_t *out);
/* A name Jira sent for the filter, made into a label: cleaned, unsupported characters replaced, cut to 18 characters. */
void jira_label_from_name(const char *name, char out[TB_JIRA_LABEL_BYTES]);
/* "y•••@example.com" for the Remote: the first character, three bullets (U+2022) and everything from the "@". */
void jira_email_hint(const char *email, char *out, size_t cap);

/* ---------- the settings ---------- */

/* Everything the bar keeps about Jira. token is the only secret (and the email goes only to the user's own site): both live
 * in protected storage on the device, are never logged, and are never returned by any API (jira_service.h). */
typedef struct {
    char site[JIRA_SITE_MAX + 1];       /* https://<name>.atlassian.net, lower case */
    char host[JIRA_HOST_MAX];           /* <name>.atlassian.net */
    char email[JIRA_EMAIL_MAX];
    char token[JIRA_TOKEN_MAX];
    char filter[JIRA_FILTER_MAX];       /* the digits */
    char label[TB_JIRA_LABEL_BYTES];    /* the screen's label */
    bool label_auto;                    /* the label is the filter's name (or "Filter <id>" until it is known): follows it */
    int32_t alert_above;                /* -1: none */
    int32_t goal_type;                  /* 0: none, 1: target count, 2: reduce by */
    int32_t goal_value;                 /* target count or reduce-by amount */
} jira_cfg_t;

/* What a Save or a Test sends: NULL (or a blank string) leaves a field as it is saved. alert: has_alert with NULL or a blank
 * string clears it, a number string sets it; without has_alert it stays. goal_type/goal_value: 0 when not provided. */
typedef struct {
    const char *site, *email, *token, *filter, *label;
    bool has_alert;
    const char *alert;
    int32_t goal_type;
    int32_t goal_value;
} jira_input_t;

/* Merge the input over what is saved (saved may be NULL for a first save) and check every field. A field that is neither
 * given nor saved is the error for that field. The label: given, it is kept as typed (label_auto false); not given, a saved
 * label stays unless it followed the filter's name and the filter changed; otherwise it is "Filter <id>" (label_auto), which
 * the service replaces with the filter's name once Jira says it. On an error out is not complete. */
jira_err_t jira_resolve(const jira_cfg_t *saved, const jira_input_t *in, jira_cfg_t *out);

/* ---------- the request ---------- */

typedef struct {
    char url[JIRA_URL_MAX];         /* https://<host><path> */
    char body[JIRA_BODY_MAX];       /* "" for a GET */
    bool post;
} jira_request_t;

/* The count of the issues in a filter: ONE place to change if Atlassian's endpoint differs. false when it won't fit
 * or an argument is bad. host is the bare host from jira_check_site. */
bool jira_count_request(const char *host, const char *filter_id, jira_request_t *out);
/* The filter's own details (its name), asked on Save and Test only: GET /rest/api/3/filter/<id>. */
bool jira_filter_request(const char *host, const char *filter_id, jira_request_t *out);
/* "Basic " + base64("email:token"), into out (JIRA_AUTH_MAX). false when it won't fit. The caller wipes out after use. */
bool jira_basic_auth(const char *email, const char *token, char *out, size_t cap);
/* Overwrite a buffer so the compiler can't drop it (secrets). */
void jira_wipe(void *p, size_t n);

/* ---------- the answer ---------- */

/* What an HTTP status means for the count or filter request: 200 to 299 is JIRA_RES_ANSWER (read the body); 401 and 403
 * the token; 404 and 400 the filter (no such filter, or this account can't see it, or it can't be counted); 429 and
 * everything else unreachable. retry_after_s is the Retry-After header (0 when none or unreadable). */
typedef enum { JIRA_HTTP_ANSWER = 0, JIRA_HTTP_TOKEN, JIRA_HTTP_NOFILTER, JIRA_HTTP_UNREACHABLE } jira_http_t;
jira_http_t jira_http_class(int status);
/* A Retry-After header value: seconds, 0..86400, 0 when it isn't a plain number (a date is ignored). */
int32_t jira_parse_retry_after(const char *value);
/* The count in an answer, leniently: the top-level "count" (a number, or a string of digits), else the top-level
 * "total"; whole, 0 to 2,000,000,000. false when there is none, it is negative or fractional, or the text isn't JSON
 * of that shape. Only looks at the top level, never recurses, and never reads past len. */
bool jira_parse_count(const char *json, size_t len, int32_t *count);
/* The filter's "name" (a top-level string), unescaped to UTF-8, up to cap-1 bytes. false when there is none. */
bool jira_parse_filter_name(const char *json, size_t len, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
