/*
 * The Jira endpoints (api.md section 19) through the real router with a faked service: the shape of every reply, every
 * refusal, scopes, and above all that the token and the email never come back out, in any reply, error or toast.
 * Test data is generic: example.atlassian.net, you@example.com, filter 10042.
 */
#include <stdlib.h>

#include "net_fixture.h"
#include "net_pair.h"
#include "tb_test.h"

static char T[NET_TOKEN_LEN + 1];

#define TOKEN "abcDEF123_token-xyz"
#define EMAIL "you@example.com"

static void setup_paired(void)
{
    nf_setup();
    snprintf(T, sizeof T, "%s", nf_pair("remote", "full", NULL));
}

static const char *FULL = "{\"site\": \"https://Example.atlassian.net/\", \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", "
                          "\"filter_id\": \"https://example.atlassian.net/issues/?filter=10042\"}";

/* No secret anywhere in the text of a reply. */
static void clean_of_secrets(const char *what, const char *raw)
{
    if (!raw) return;
    if (strstr(raw, TOKEN) || strstr(raw, "abcDEF")) TB_FAIL_AT("%s: the token is in the reply: %s", what, raw);
    if (strstr(raw, EMAIL) || strstr(raw, "you@")) TB_FAIL_AT("%s: the email is in the reply: %s", what, raw);
}

static void check_reply(const char *what, nf_resp_t *r)
{
    clean_of_secrets(what, r->r.body);
    nf_free(r);
}

TB_TEST(jira_get_when_not_set_up)
{
    setup_paired();
    nf_resp_t r = nf_http("GET", "/api/v1/jira", NULL, T);
    TB_EQ_INT(r.status, 200);
    TB_FALSE(nf_true(r.j, "jira.configured"));
    TB_TRUE(nf_null(r.j, "jira.site"));
    TB_TRUE(nf_null(r.j, "jira.email_hint"));
    TB_FALSE(nf_true(r.j, "jira.token_saved"));
    TB_TRUE(nf_null(r.j, "jira.filter_id"));
    TB_TRUE(nf_null(r.j, "jira.label"));
    TB_TRUE(nf_null(r.j, "jira.alert_above"));
    TB_TRUE(nf_null(r.j, "jira.state"));
    TB_TRUE(nf_null(r.j, "jira.count"));
    TB_TRUE(nf_null(r.j, "jira.updated_at"));
    TB_EQ_STR(nf_str(r.j, "jira.test.state"), "idle");
    nf_free(&r);
}

TB_TEST(jira_first_save_shows_what_is_saved_and_never_the_secrets)
{
    setup_paired();
    nf_resp_t r = nf_http("PUT", "/api/v1/jira", FULL, T);
    TB_EQ_INT(r.status, 200);
    TB_TRUE(nf_true(r.j, "jira.configured"));
    TB_EQ_STR(nf_str(r.j, "jira.site"), "https://example.atlassian.net");
    TB_EQ_STR(nf_str(r.j, "jira.email_hint"), "y\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2@example.com");
    TB_TRUE(nf_true(r.j, "jira.token_saved"));
    TB_EQ_STR(nf_str(r.j, "jira.filter_id"), "10042");
    TB_EQ_STR(nf_str(r.j, "jira.label"), "Filter 10042");              /* until Jira says the name */
    TB_TRUE(nf_null(r.j, "jira.alert_above"));
    TB_EQ_STR(nf_str(r.j, "jira.state"), "loading");
    TB_TRUE(nf_null(r.j, "jira.count"));
    TB_TRUE(nf_get(r.j, "jira.token") == NULL);                       /* no field for it at all */
    TB_TRUE(nf_get(r.j, "jira.email") == NULL);
    TB_TRUE(nf_get(r.j, "jira.password") == NULL);
    check_reply("PUT", &r);
    TB_EQ_INT(fake_jira.save_calls, 1);
    TB_EQ_STR(fake_jira.cfg.token, TOKEN);                            /* it reached the service, whole */
    TB_TRUE(strstr(nf_app.toast, "Jira added") || strstr(nf_app.pending_toast, "Jira added"));
    clean_of_secrets("toast", nf_app.toast);
    clean_of_secrets("pending toast", nf_app.pending_toast);
    /* and GET says the same, still without them */
    r = nf_http("GET", "/api/v1/jira", NULL, T);
    TB_TRUE(nf_true(r.j, "jira.token_saved"));
    TB_EQ_STR(nf_str(r.j, "jira.site"), "https://example.atlassian.net");
    check_reply("GET", &r);
    /* the other replies that carry settings or status don't mention it either */
    const char *paths[] = {"/api/v1/settings", "/api/v1/status", "/api/v1/calendar", "/api/v1/clients"};
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        r = nf_http("GET", paths[i], NULL, T);
        check_reply(paths[i], &r);
    }
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    check_reply("info", &r);
}

TB_TEST(jira_second_save_keeps_the_token_and_changes_only_what_is_sent)
{
    setup_paired();
    nf_resp_t r = nf_http("PUT", "/api/v1/jira", FULL, T);
    nf_free(&r);
    r = nf_http("PUT", "/api/v1/jira", "{\"label\": \"Open bugs\", \"alert_above\": 10}", T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "jira.label"), "Open bugs");
    TB_EQ_INT(nf_num(r.j, "jira.alert_above"), 10);
    TB_TRUE(nf_true(r.j, "jira.token_saved"));
    check_reply("PUT", &r);
    TB_EQ_STR(fake_jira.cfg.token, TOKEN);                            /* nothing re-sent, nothing lost */
    TB_EQ_STR(fake_jira.cfg.email, EMAIL);
    TB_TRUE(strstr(nf_app.toast, "Jira saved") || strstr(nf_app.pending_toast, "Jira saved"));
    /* a string of digits works too, and null clears it */
    r = nf_http("PUT", "/api/v1/jira", "{\"alert_above\": \"25\"}", T);
    TB_EQ_INT(nf_num(r.j, "jira.alert_above"), 25);
    nf_free(&r);
    r = nf_http("PUT", "/api/v1/jira", "{\"alert_above\": null}", T);
    TB_EQ_INT(r.status, 200);
    TB_TRUE(nf_null(r.j, "jira.alert_above"));
    nf_free(&r);
    /* a new token replaces the old one; the reply is the same shape */
    r = nf_http("PUT", "/api/v1/jira", "{\"token\": \"another-token-123\"}", T);
    TB_EQ_INT(r.status, 200);
    check_reply("PUT", &r);
    TB_EQ_STR(fake_jira.cfg.token, "another-token-123");
}

TB_TEST(jira_put_refuses_each_bad_field_and_changes_nothing)
{
    setup_paired();
    const struct { const char *body, *field; } bad[] = {
        {"{\"site\": \"http://example.atlassian.net\", \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", \"filter_id\": \"1\"}", "site"},
        {"{\"site\": \"https://evil.example.com/example.atlassian.net\", \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", \"filter_id\": \"1\"}", "site"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": \"nope\", \"token\": \"" TOKEN "\", \"filter_id\": \"1\"}", "email"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": \"" EMAIL "\", \"token\": \"short\", \"filter_id\": \"1\"}", "token"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", \"filter_id\": \"abc\"}", "filter_id"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", \"filter_id\": \"0\"}", "filter_id"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", \"filter_id\": \"1\", \"label\": \"1234567890123456789\"}", "label"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", \"filter_id\": \"1\", \"alert_above\": 10000}", "alert_above"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", \"filter_id\": \"1\", \"alert_above\": -1}", "alert_above"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", \"filter_id\": \"1\", \"alert_above\": 1.5}", "alert_above"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", \"filter_id\": \"1\", \"alert_above\": true}", "alert_above"},
        {"{\"site\": 5, \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", \"filter_id\": \"1\"}", "site"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": [1], \"token\": \"" TOKEN "\", \"filter_id\": \"1\"}", "email"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": \"" EMAIL "\", \"token\": {\"a\": 1}, \"filter_id\": \"1\"}", "token"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", \"filter_id\": 10042}", "filter_id"},
        /* nothing saved yet, so a missing field is its own error */
        {"{\"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", \"filter_id\": \"1\"}", "site"},
        {"{\"site\": \"https://example.atlassian.net\", \"token\": \"" TOKEN "\", \"filter_id\": \"1\"}", "email"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": \"" EMAIL "\", \"filter_id\": \"1\"}", "token"},
        {"{\"site\": \"https://example.atlassian.net\", \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\"}", "filter_id"},
        {"{}", "site"},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        nf_resp_t r = nf_http("PUT", "/api/v1/jira", bad[i].body, T);
        if (r.status != 400) TB_FAIL_AT("case %zu answered %d", i, r.status);
        TB_EQ_STR(nf_err(&r), "bad_value");
        TB_EQ_STR(nf_str(r.j, "field"), bad[i].field);
        TB_TRUE(nf_str(r.j, "message") != NULL);
        check_reply("error", &r);       /* the message names the field in words and never echoes what was sent */
    }
    TB_EQ_INT(fake_jira.save_calls, 0);
    TB_FALSE(fake_jira.saved);
    /* a bad field after a good save changes nothing either */
    nf_resp_t r = nf_http("PUT", "/api/v1/jira", FULL, T);
    nf_free(&r);
    r = nf_http("PUT", "/api/v1/jira", "{\"label\": \"New label\", \"filter_id\": \"nope\"}", T);
    TB_EQ_INT(r.status, 400);
    TB_EQ_STR(nf_str(r.j, "field"), "filter_id");
    nf_free(&r);
    TB_EQ_STR(fake_jira.cfg.label, "Filter 10042");
    TB_EQ_INT(fake_jira.save_calls, 1);
    /* the request itself: not JSON, not an object, too big */
    r = nf_http("PUT", "/api/v1/jira", "not json", T);
    TB_EQ_INT(r.status, 400);
    nf_free(&r);
    r = nf_http("PUT", "/api/v1/jira", "[1,2]", T);
    TB_EQ_INT(r.status, 400);
    nf_free(&r);
    char huge[2600];
    memset(huge, 'a', sizeof huge - 1);
    huge[sizeof huge - 1] = '\0';
    char body[2800];
    snprintf(body, sizeof body, "{\"token\": \"%s\"}", huge);
    r = nf_http("PUT", "/api/v1/jira", body, T);
    TB_EQ_INT(r.status, 413);
    nf_free(&r);
}

TB_TEST(jira_test_asks_once_saves_nothing_and_reports_plainly)
{
    setup_paired();
    nf_resp_t r = nf_http("POST", "/api/v1/jira/test", FULL, T);
    TB_EQ_INT(r.status, 202);                                          /* started: the answer comes in GET /jira */
    TB_EQ_STR(nf_str(r.j, "jira.test.state"), "asking");
    TB_FALSE(nf_true(r.j, "jira.configured"));                         /* nothing was saved */
    check_reply("test", &r);
    TB_FALSE(fake_jira.saved);
    TB_EQ_INT(fake_jira.test_calls, 1);
    TB_EQ_STR(fake_jira.last_test_cfg.filter, "10042");
    TB_EQ_STR(fake_jira.last_test_cfg.token, TOKEN);
    /* it works */
    fake_jira.test_state = JIRA_TEST_OK;
    snprintf(fake_jira.test_name, sizeof fake_jira.test_name, "Open bugs");
    fake_jira.test_count = 12;
    r = nf_http("GET", "/api/v1/jira", NULL, T);
    TB_EQ_STR(nf_str(r.j, "jira.test.state"), "ok");
    TB_EQ_STR(nf_str(r.j, "jira.test.filter_name"), "Open bugs");
    TB_EQ_INT(nf_num(r.j, "jira.test.count"), 12);
    TB_TRUE(nf_null(r.j, "jira.test.error"));
    check_reply("GET", &r);
    /* each failure, in its own words and code */
    const struct { const char *code, *words; } errs[] = {
        {JIRA_ERR_UNREACHABLE, "Couldn't reach Jira. Check the site address, and that this MiniBar is online."},
        {JIRA_ERR_TOKEN, "Jira didn't accept that email and token. Check them, or create a new token."},
        {JIRA_ERR_FILTER, "Jira has no filter with that ID, or this account can't see it."},
        {JIRA_ERR_OFFLINE, "MiniBar can't reach the internet right now, so it can't ask Jira. Check its Wi-Fi."},
    };
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        fake_jira.test_state = JIRA_TEST_ERROR;
        fake_jira.test_error = errs[i].code;
        r = nf_http("GET", "/api/v1/jira", NULL, T);
        TB_EQ_STR(nf_str(r.j, "jira.test.state"), "error");
        TB_EQ_STR(nf_str(r.j, "jira.test.error"), errs[i].code);
        TB_EQ_STR(nf_str(r.j, "jira.test.message"), errs[i].words);
        TB_TRUE(nf_null(r.j, "jira.test.count"));
        check_reply("GET", &r);
    }
    /* a bad field is refused before anything is asked */
    int calls = fake_jira.test_calls;
    r = nf_http("POST", "/api/v1/jira/test", "{\"site\": \"https://example.com\", \"email\": \"" EMAIL "\", \"token\": \"" TOKEN "\", \"filter_id\": \"1\"}", T);
    TB_EQ_INT(r.status, 400);
    TB_EQ_STR(nf_str(r.j, "field"), "site");
    check_reply("test", &r);
    TB_EQ_INT(fake_jira.test_calls, calls);
    /* with something saved, blank fields use it: the Test button on a saved setup needs no token typed */
    r = nf_http("PUT", "/api/v1/jira", FULL, T);
    nf_free(&r);
    r = nf_http("POST", "/api/v1/jira/test", "{\"filter_id\": \"20002\"}", T);
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_STR(fake_jira.last_test_cfg.token, TOKEN);
    TB_EQ_STR(fake_jira.last_test_cfg.filter, "20002");
    TB_EQ_STR(fake_jira.cfg.filter, "10042");                          /* the test saved nothing */
}

TB_TEST(jira_delete_erases_everything_and_is_idempotent)
{
    setup_paired();
    nf_resp_t r = nf_http("DELETE", "/api/v1/jira", NULL, T);
    TB_EQ_INT(r.status, 200);
    TB_FALSE(nf_true(r.j, "jira.configured"));
    nf_free(&r);
    r = nf_http("PUT", "/api/v1/jira", FULL, T);
    nf_free(&r);
    r = nf_http("DELETE", "/api/v1/jira", NULL, T);
    TB_EQ_INT(r.status, 200);
    TB_FALSE(nf_true(r.j, "jira.configured"));
    TB_FALSE(nf_true(r.j, "jira.token_saved"));
    TB_TRUE(nf_null(r.j, "jira.site"));
    nf_free(&r);
    TB_FALSE(fake_jira.saved);
    TB_EQ_STR(fake_jira.cfg.token, "");                                /* the token is gone from the service */
    TB_TRUE(strstr(nf_app.toast, "Jira removed") || strstr(nf_app.pending_toast, "Jira removed"));
    /* a save after a removal needs every field again */
    r = nf_http("PUT", "/api/v1/jira", "{\"label\": \"x\"}", T);
    TB_EQ_INT(r.status, 400);
    nf_free(&r);
}

TB_TEST(jira_endpoints_need_a_full_scope_token_and_the_right_methods)
{
    setup_paired();
    nf_resp_t r = nf_http("GET", "/api/v1/jira", NULL, NULL);
    TB_EQ_INT(r.status, 401);
    nf_free(&r);
    r = nf_http("PUT", "/api/v1/jira", FULL, NULL);
    TB_EQ_INT(r.status, 401);
    nf_free(&r);
    TB_FALSE(fake_jira.saved);
    char call_tok[NET_TOKEN_LEN + 1];
    snprintf(call_tok, sizeof call_tok, "%s", nf_pair("mac", "call", "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"));
    const struct { const char *m, *p; } all[] = {{"GET", "/api/v1/jira"}, {"PUT", "/api/v1/jira"}, {"DELETE", "/api/v1/jira"},
                                                  {"POST", "/api/v1/jira/test"}};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        r = nf_http(all[i].m, all[i].p, strcmp(all[i].m, "GET") && strcmp(all[i].m, "DELETE") ? FULL : NULL, call_tok);
        TB_EQ_INT(r.status, 403);                                      /* a Mac's call-only token can't touch it */
        TB_EQ_STR(nf_err(&r), "wrong_scope");
        nf_free(&r);
    }
    TB_FALSE(fake_jira.saved);
    r = nf_http("POST", "/api/v1/jira", FULL, T);
    TB_EQ_INT(r.status, 405);
    nf_free(&r);
    r = nf_http("PATCH", "/api/v1/jira", FULL, T);
    TB_EQ_INT(r.status, 405);
    nf_free(&r);
    r = nf_http("GET", "/api/v1/jira/test", NULL, T);
    TB_EQ_INT(r.status, 405);
    nf_free(&r);
    TB_FALSE(fake_jira.saved);
}

TB_TEST(jira_over_usb_is_the_same_router_and_the_reply_line_has_no_secrets)
{
    setup_paired();
    char line[2400];
    snprintf(line, sizeof line, "@tb {\"cmd\": \"request\", \"id\": 7, \"method\": \"PUT\", \"path\": \"/api/v1/jira\", \"body\": %s}", FULL);
    cJSON *j = nf_usb(line);
    TB_TRUE(j != NULL);
    TB_EQ_INT(nf_num(j, "http_status"), 200);
    TB_TRUE(nf_true(j, "jira.configured"));
    clean_of_secrets("USB reply", nf_last_line);
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 8, \"method\": \"GET\", \"path\": \"/api/v1/jira\"}");
    TB_TRUE(nf_true(j, "jira.token_saved"));
    clean_of_secrets("USB reply", nf_last_line);
    cJSON_Delete(j);
}

TB_TEST(jira_status_is_never_a_post_status_and_shows_in_own_status)
{
    setup_paired();
    nf_resp_t r = nf_http("POST", "/api/v1/status", "{\"status\": \"jira\"}", T);
    TB_EQ_INT(r.status, 400);
    TB_EQ_STR(nf_err(&r), "bad_value");
    TB_EQ_STR(nf_str(r.j, "field"), "status");
    nf_free(&r);
    /* once set up and swiped to, GET /status says "jira" and what Stop returns to is unchanged */
    tb_jira_t j;
    tb_jira_init(&j);
    tb_jira_configure(&j, "Open bugs", -1, 0, 0);
    tb_app_set_jira(&nf_app, &j, &fake_now);
    nf_app.idx = TB_ST_JIRA;
    r = nf_http("GET", "/api/v1/status", NULL, T);
    TB_EQ_STR(nf_str(r.j, "own.status"), "jira");
    nf_free(&r);
}
