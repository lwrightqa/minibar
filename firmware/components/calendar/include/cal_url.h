/*
 * cal_url.h: the secret iCal address: format checks and the masked form (api.md 11.2, mock-up checkIcal()). Pure C.
 *
 * Owner: calendar builder. net's router calls it for PUT /api/v1/calendar and POST /api/v1/setup/wifi; the
 * Remote and setup pages run the same checks in JavaScript first.
 *
 * The checks, in the mock-up's order (the address is read the way a browser's URL parser reads it: surrounding
 * spaces trimmed, tabs and line breaks inside removed, scheme and host lowercased, the #fragment dropped):
 *   empty -> CAL_URL_EMPTY; over 1,024 bytes -> CAL_URL_TOO_LONG; not https://, http:// or webcal:// with a host that
 *   has a dot -> CAL_URL_NOT_A_URL; http:// -> CAL_URL_HTTP; a path with /public/ -> CAL_URL_PUBLIC; a path not
 *   ending in .ics -> CAL_URL_NOT_ICS.
 * The normalized address is "https://" + host[:port] + path + ?query (webcal:// becomes https://, user:password@ is
 * dropped, spaces and other characters a URL can't carry are percent-encoded).
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAL_URL_MAX 1024

typedef enum {
    CAL_URL_OK = 0,
    CAL_URL_EMPTY,          /* 400 bad_request:       "Paste your secret address first." */
    CAL_URL_NOT_A_URL,      /* 400 not_a_url:         "Copy the whole address, starting with https://." */
    CAL_URL_HTTP,           /* 400 http_not_allowed:  "It isn't encrypted. Use the https:// address Google gives you." */
    CAL_URL_PUBLIC,         /* 400 public_address:    "Copy the Secret address in iCal format instead." */
    CAL_URL_NOT_ICS,        /* 400 not_ics */
    CAL_URL_TOO_LONG,       /* 400 bad_value: over 1,024 bytes */
} cal_url_err_t;

typedef struct {
    char url[CAL_URL_MAX + 1];  /* normalized (see above) */
    char host[128];             /* "calendar.google.com" (the host name, without the port) */
    char file[64];              /* "basic.ics" (the last path segment, cut on a character boundary) */
    char ending[9];             /* last four characters of the private token ("3f2a"), or "····" (4 x U+00B7) */
    bool google;                /* the host is google.com or a subdomain */
    char self_email[128];       /* the calendar id from a Google path (/ical/<id>/private-...), URL-decoded, or "" */
} cal_url_info_t;

/* On CAL_URL_OK, out is filled in; on an error, out is cleared. */
cal_url_err_t cal_url_check(const char *raw, cal_url_info_t *out);
/* The api.md error code ("not_a_url"...) and the Remote's English sentence for it (NULL for CAL_URL_OK). */
const char *cal_url_err_code(cal_url_err_t e);
const char *cal_url_err_message(cal_url_err_t e);

#ifdef __cplusplus
}
#endif
