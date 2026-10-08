/*
 * tb_jira.h: the Jira issue count as the bar's screens see it, and the pure rules around it (decisions.md "Jira issue
 * count (2026-10-07)"): the state machine, how long a count is kept when Jira can't be reached, the alert color, the
 * words, and how often to ask. No ESP-IDF, no clocks: the Jira service (components/jira/esp) fills a tb_jira_t and hands
 * it to the app task; core and ui only read it.
 *
 * Owner: lead developer. Pure C.
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TB_JIRA_LABEL_CHARS  18
#define TB_JIRA_LABEL_BYTES  (TB_JIRA_LABEL_CHARS * 3 + 1)
#define TB_JIRA_EVERY_S      300        /* JIRA_EVERY: a check every 5 minutes */
#define TB_JIRA_KEEP_S       7200       /* a count is kept for 2 hours when Jira can't be reached, then dropped */
#define TB_JIRA_BIG          10000      /* from here on the screen says "10k+" */
#define TB_JIRA_ALERT_MAX    9999       /* "Alert above": a whole number, up to 4 digits */

/* What the last check said. LOADING until the first answer after setting up (or after a restart: the count isn't saved). */
typedef enum {
    TB_JIRA_LOADING = 0,
    TB_JIRA_OK,             /* a count */
    TB_JIRA_UNREACHABLE,    /* no answer (offline, DNS, TLS, time-out, 429, 5xx, an answer that isn't a count) */
    TB_JIRA_TOKEN,          /* 401 or 403: the email or token isn't accepted */
    TB_JIRA_NOFILTER,       /* 404 or 400: no such filter, or this account can't see it */
} tb_jira_state_t;

typedef struct {
    bool configured;                /* false: the screen doesn't exist (out of the swipe order, no status button) */
    tb_jira_state_t state;
    int32_t count;                  /* the last good count, -1 when there is none */
    tb_epoch_t ok_at;               /* when it came (UTC); 0 when never */
    int32_t alert_above;            /* -1: no alert */
    char label[TB_JIRA_LABEL_BYTES];    /* the screen's label, "Jira · <label>" on the bar; drawable characters only */
    int32_t goal_type;              /* 0: none, 1: target count, 2: reduce by */
    int32_t goal_value;             /* target count or reduce-by amount */
} tb_jira_t;

/* What a check came back with. */
typedef enum { TB_JIRA_RES_OK = 0, TB_JIRA_RES_UNREACHABLE, TB_JIRA_RES_TOKEN, TB_JIRA_RES_NOFILTER } tb_jira_result_t;

/* Not configured, nothing known (alert off). */
void tb_jira_init(tb_jira_t *j);
/* Settings were saved (or changed): a new check starts, and what was known is dropped. */
void tb_jira_configure(tb_jira_t *j, const char *label, int32_t alert_above, int32_t goal_type, int32_t goal_value);
/* Everything about Jira was removed. */
void tb_jira_clear(tb_jira_t *j);
/* A check came back at wall time now (0 when the clock is unknown). OK keeps the count; UNREACHABLE keeps the last count
 * and its time (so the screen can say "as of 12 min ago"); TOKEN and NOFILTER drop the count: it would be wrong or out
 * of date for a reason the owner has to fix. Ignored while not configured. */
void tb_jira_apply(tb_jira_t *j, tb_jira_result_t res, int32_t count, tb_epoch_t now);

/* The count the screen shows: a good one, or (can't reach Jira) the last one while it is under 2 hours old. false: show
 * a dash, or the message of the state. now_valid false (the clock isn't known) keeps a last count rather than drop it. */
bool tb_jira_count_shown(const tb_jira_t *j, tb_epoch_t now, bool now_valid, int32_t *count);
/* Over the alert limit (needs a shown count and an alert set; equal is not over). */
bool tb_jira_over(const tb_jira_t *j, tb_epoch_t now, bool now_valid);
/* "12", or "10k+" from 10,000 on. */
char *tb_jira_count_text(char *buf, size_t cap, int32_t count);
/* "12 min", "1 h 5 min", "3 h": a wait, rounded to the minute and at least a minute (the mock-up's jiraAgo). */
char *tb_jira_ago_text(char *buf, size_t cap, int64_t seconds);

/* Seconds until the next check after `fails` checks in a row that didn't work: 5 minutes, then 5, 10, 20 and 30 minutes
 * (a rejected token is retried that slowly too). A 429's Retry-After (seconds, 0 for none) can only make it longer. */
int32_t tb_jira_next_s(int fails, int32_t retry_after_s);

#ifdef __cplusplus
}
#endif
