/*
 * cal_url.h: the secret iCal address: format checks and the masked form (api.md 11.2, mock-up checkIcal()). Pure C.
 *
 * Owner: calendar builder. net's router calls it for PUT /api/v1/calendar and POST /api/v1/setup/wifi; the
 * Remote and setup pages run the same checks in JavaScript first.
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
    char url[CAL_URL_MAX + 1];  /* normalized: webcal:// becomes https://, surrounding spaces trimmed */
    char host[128];             /* "calendar.google.com" */
    char file[64];              /* "basic.ics" */
    char ending[5];             /* last four characters of the private token ("3f2a"), or "····" */
    bool google;                /* the host is google.com or a subdomain */
    char self_email[128];       /* the calendar id from a Google path (/ical/<id>/private-...), URL-decoded, or "" */
} cal_url_info_t;

cal_url_err_t cal_url_check(const char *raw, cal_url_info_t *out);
/* The api.md error code ("not_a_url"...) and the Remote's English sentence for it. */
const char *cal_url_err_code(cal_url_err_t e);
const char *cal_url_err_message(cal_url_err_t e);

#ifdef __cplusplus
}
#endif
