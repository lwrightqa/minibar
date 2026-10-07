/*
 * jira_service.h: what net's router (and the Remote) needs from the Jira service: read it, save it, test it, remove it.
 * Implemented by esp/jira_sync.c on the bar (it keeps the settings in protected storage and asks Jira over HTTPS in its
 * own task) and by fakes in test/host/net and net/host/fakebar.c on Linux, so the router, its replies and the Remote
 * page are host-tested. No function here ever returns the token or the email: only whether a token is saved, and the
 * email masked ("y•••@example.com").
 *
 * Owner: lead developer. Called from the app task only (the router runs there); each returns at once. The check itself
 * (a TLS request, 1 to 2 seconds) runs in the service's own task.
 */
#pragma once

#include "jira_proto.h"
#include "tb_jira.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The Test button's progress: asked of Jira once, nothing saved. */
typedef enum {
    JIRA_TEST_IDLE = 0,
    JIRA_TEST_ASKING,
    JIRA_TEST_OK,           /* name and count are filled in */
    JIRA_TEST_ERROR,        /* error is one of the codes below */
} jira_test_state_t;

#define JIRA_ERR_UNREACHABLE  "jira_unreachable"        /* DNS, TLS, time-out, anything but an answer */
#define JIRA_ERR_TOKEN        "jira_token_rejected"     /* 401 or 403 */
#define JIRA_ERR_FILTER       "jira_filter_not_found"   /* 404 or 400 */
#define JIRA_ERR_OFFLINE      "offline"                 /* the bar has no Wi-Fi or no clock yet */

/* The plain sentence for a test error code (no codes, no echo), and "" for anything else. */
const char *jira_test_message(const char *code);

typedef struct {
    bool configured;
    char site[JIRA_SITE_MAX + 1];
    char email_hint[JIRA_EMAIL_MAX + 12];   /* "y•••@example.com" */
    bool token_saved;
    char filter_id[JIRA_FILTER_MAX];
    char filter_name[TB_JIRA_LABEL_BYTES];  /* what Jira calls the filter, cut to a label's length; "" until known */
    char label[TB_JIRA_LABEL_BYTES];
    int32_t alert_above;                    /* -1: none */
    tb_jira_state_t state;
    int32_t count;                          /* -1: none */
    tb_epoch_t updated_at;                  /* the last good answer; 0 when never */
    struct {
        jira_test_state_t state;
        char name[TB_JIRA_LABEL_BYTES];
        int32_t count;
        const char *error;                  /* a JIRA_ERR_ code, or NULL */
    } test;
} jira_info_t;

void jira_get_info(jira_info_t *out);
/* PUT: resolve (blank fields keep what is saved), save, take the screen into the swipe order, and check at once. JIRA_OK, or
 * the first field that failed. */
jira_err_t jira_save(const jira_input_t *in);
/* POST test: resolve the same way, change nothing, ask Jira once (filter, then count). JIRA_OK when the check started (or
 * one is already running). The answer comes in jira_get_info().test. */
jira_err_t jira_test(const jira_input_t *in);
/* DELETE: erase the token and every Jira setting, and take the screen out of the swipe order. false when none was set up. */
bool jira_remove(void);
/* Wi-Fi and the clock: the periodic check runs only while online; coming back online checks at once. Called from the app task. */
void jira_set_online(bool online);

#ifdef __cplusplus
}
#endif
