/*
 * test_calendars.c: several calendars over HTTP (api.md 11.5): the list, add, edit, remove, the limit of 3, name and tag
 * errors, the single-address calls while one or several are saved, and that no address ever comes back.
 * Owner: net builder.
 */
#include <stdlib.h>

#include "net_fixture.h"
#include "net_pair.h"
#include "tb_test.h"

static char T[NET_TOKEN_LEN + 1];

#define URL1 "https://calendar.example.com/ical/team/private-8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a/basic.ics"
#define URL2 "https://calendar.example.com/ical/other/private-0a1b2c3d4e5f60718293a4b5c6d7e8f9/basic.ics"

static void paired(void)
{
    nf_setup();
    snprintf(T, sizeof T, "%s", nf_pair("remote", "full", NULL));
}

/* A calendar the way the service reports it once its check passed. */
static void have(int id, const char *name, const char *tag, bool failing)
{
    cal_item_t *c = &fake_items.c[id - 1];
    memset(c, 0, sizeof *c);
    c->used = true;
    c->id = id;
    snprintf(c->name, sizeof c->name, "%s", name);
    snprintf(c->tag, sizeof c->tag, "%s", tag);
    c->last_sync = fake_now.wall - 300;
    c->failing = failing;
    if (failing) {
        c->error = "calendar_unreachable";
        snprintf(c->error_message, sizeof c->error_message, "MiniBar couldn't reach the calendar's server. Try again in a minute.");
        c->error_at = fake_now.wall - 60;
    }
    c->left_today = 2;
    fake_items.n = 0;
    for (int i = 0; i < TB_CALS_MAX; i++) fake_items.n += fake_items.c[i].used;
    fake_cal.saved = true;
    nf_app.cal_saved = true;
}

static nf_resp_t post(const char *body) { return nf_http("POST", "/api/v1/calendars", body, T); }

TB_TEST(calendars_list_is_empty_then_lists_by_id_without_any_address)
{
    paired();
    nf_resp_t r = nf_http("GET", "/api/v1/calendars", NULL, T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_INT(nf_num(r.j, "max"), 3);
    TB_EQ_INT(cJSON_GetArraySize(nf_get(r.j, "calendars")), 0);
    TB_TRUE(nf_null(r.j, "check"));
    nf_free(&r);
    have(1, "Work", "WRK", false);
    have(3, "Kids", "K", true);
    r = nf_http("GET", "/api/v1/calendars", NULL, T);
    TB_EQ_INT(cJSON_GetArraySize(nf_get(r.j, "calendars")), 2);
    TB_EQ_INT(nf_num(r.j, "calendars.0.id"), 1);
    TB_EQ_STR(nf_str(r.j, "calendars.0.name"), "Work");
    TB_EQ_STR(nf_str(r.j, "calendars.0.tag"), "WRK");
    TB_TRUE(nf_true(r.j, "calendars.0.address_saved"));
    TB_EQ_STR(nf_str(r.j, "calendars.0.status"), "ok");
    TB_TRUE(nf_null(r.j, "calendars.0.error"));
    TB_EQ_INT(nf_num(r.j, "calendars.0.left_today"), 2);
    TB_EQ_INT(nf_num(r.j, "calendars.1.id"), 3);
    TB_EQ_STR(nf_str(r.j, "calendars.1.status"), "error");
    TB_EQ_STR(nf_str(r.j, "calendars.1.error.error"), "calendar_unreachable");
    TB_TRUE(strstr(r.r.body, "https://") == NULL && strstr(r.r.body, "calendar.example.com") == NULL);
    nf_free(&r);
}

TB_TEST(calendars_need_a_full_token)
{
    paired();
    nf_resp_t r = nf_http("GET", "/api/v1/calendars", NULL, NULL);
    TB_EQ_INT(r.status, 401);
    nf_free(&r);
    char call_tok[NET_TOKEN_LEN + 1];
    snprintf(call_tok, sizeof call_tok, "%s", nf_pair("mac", "call", NULL));
    r = nf_http("POST", "/api/v1/calendars", "{\"url\":\"" URL1 "\"}", call_tok);
    TB_EQ_INT(r.status, 403);
    TB_EQ_STR(nf_err(&r), "wrong_scope");
    nf_free(&r);
    r = nf_http("DELETE", "/api/v1/calendars/1", NULL, call_tok);
    TB_EQ_INT(r.status, 403);
    nf_free(&r);
    TB_EQ_INT(fake_cal_add_calls, 0);
}

TB_TEST(calendars_add_checks_the_format_name_and_tag_then_answers_202)
{
    paired();
    struct { const char *body, *code, *field; int st; } cases[] = {
        {"{}", "bad_request", "url", 400},
        {"{\"url\": 5}", "bad_request", "url", 400},
        {"{\"url\": \"nonsense\"}", "not_a_url", "url", 400},
        {"{\"url\": \"http://calendar.example.com/x.ics\"}", "http_not_allowed", "url", 400},
        {"{\"url\": \"" URL1 "\", \"name\": 5}", "bad_request", "name", 400},
        {"{\"url\": \"" URL1 "\", \"tag\": [1]}", "bad_request", "tag", 400},
        {"{\"url\": \"" URL1 "\", \"name\": \"A name that is far too long for a calendar to carry\"}", "bad_value", "name", 400},
        {"{\"url\": \"" URL1 "\", \"tag\": \"TOOLONG\"}", "bad_value", "tag", 400},
        {"{\"url\": \"" URL1 "\", \"tag\": \"A-B\"}", "bad_value", "tag", 400},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        nf_resp_t r = post(cases[i].body);
        TB_EQ_INT(r.status, cases[i].st);
        TB_EQ_STR(nf_err(&r), cases[i].code);
        TB_EQ_STR(nf_str(r.j, "field"), cases[i].field);
        nf_free(&r);
    }
    TB_EQ_INT(fake_cal_add_calls, 0);
    nf_resp_t r = post("{\"url\": \"" URL1 "\", \"name\": \"Work\", \"tag\": \"wrk\"}");
    TB_EQ_INT(r.status, 202);
    TB_EQ_STR(nf_str(r.j, "check.state"), "checking");
    TB_TRUE(nf_null(r.j, "check.id"));
    TB_TRUE(strstr(r.r.body, "8c1d5e2a") == NULL);
    nf_free(&r);
    TB_EQ_INT(fake_cal_add_calls, 1);
    TB_EQ_STR(fake_cal_last_name, "Work");
    TB_EQ_STR(fake_cal_last_url, URL1);
    /* the defaults when the name and tag are left out */
    r = post("{\"url\": \"" URL2 "\"}");
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_STR(fake_cal_last_name, "");
}

TB_TEST(calendars_limit_and_duplicates)
{
    paired();
    have(1, "Work", "WRK", false);
    have(2, "Home", "HOM", false);
    nf_resp_t r = post("{\"url\": \"" URL1 "\", \"name\": \"work\"}");
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "already_used");
    nf_free(&r);
    r = post("{\"url\": \"" URL1 "\", \"tag\": \"hom\"}");
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "already_used");
    nf_free(&r);
    have(3, "Kids", "KID", false);
    r = post("{\"url\": \"" URL1 "\"}");
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "calendar_limit");
    TB_EQ_STR(nf_str(r.j, "message"), "You can add up to 3 calendars. Remove one to add another.");
    nf_free(&r);
    TB_EQ_INT(fake_cal_add_calls, 0);
}

TB_TEST(calendars_patch_renames_at_once_and_a_new_address_is_checked)
{
    paired();
    have(1, "Work", "WRK", false);
    have(2, "Home", "HOM", false);
    nf_resp_t r = nf_http("PATCH", "/api/v1/calendars/2", "{\"name\": \"Family\", \"tag\": \"fam\"}", T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "calendars.1.name"), "Family");
    TB_EQ_STR(nf_str(r.j, "calendars.1.tag"), "FAM");
    nf_free(&r);
    r = nf_http("PATCH", "/api/v1/calendars/2", "{\"url\": \"" URL2 "\"}", T);
    TB_EQ_INT(r.status, 202);
    TB_EQ_STR(nf_str(r.j, "check.state"), "checking");
    TB_EQ_INT(nf_num(r.j, "check.id"), 2);
    TB_TRUE(strstr(r.r.body, "0a1b2c3d") == NULL);
    nf_free(&r);
    TB_EQ_STR(fake_cal_last_url, URL2);
    /* errors: nothing to change, a clash, a bad id, a bad address */
    r = nf_http("PATCH", "/api/v1/calendars/2", "{}", T);
    TB_EQ_INT(r.status, 400);
    nf_free(&r);
    r = nf_http("PATCH", "/api/v1/calendars/2", "{\"name\": \"work\"}", T);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "already_used");
    nf_free(&r);
    r = nf_http("PATCH", "/api/v1/calendars/3", "{\"name\": \"x\"}", T);
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
    r = nf_http("PATCH", "/api/v1/calendars/9", "{\"name\": \"x\"}", T);
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
    r = nf_http("PATCH", "/api/v1/calendars/0", "{\"name\": \"x\"}", T);
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
    r = nf_http("PATCH", "/api/v1/calendars/1x", "{\"name\": \"x\"}", T);
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
    r = nf_http("PATCH", "/api/v1/calendars/1", "{\"url\": \"http://calendar.example.com/x.ics\"}", T);
    TB_EQ_INT(r.status, 400);
    TB_EQ_STR(nf_err(&r), "http_not_allowed");
    nf_free(&r);
    r = nf_http("PATCH", "/api/v1/calendars/1", "[1]", T);
    TB_EQ_INT(r.status, 400);
    nf_free(&r);
}

TB_TEST(calendars_delete_one_of_several)
{
    paired();
    have(1, "Work", "WRK", false);
    have(2, "Home", "HOM", false);
    nf_resp_t r = nf_http("DELETE", "/api/v1/calendars/1", NULL, T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_INT(cJSON_GetArraySize(nf_get(r.j, "calendars")), 1);
    TB_EQ_INT(nf_num(r.j, "calendars.0.id"), 2);          /* ids don't shift */
    nf_free(&r);
    r = nf_http("DELETE", "/api/v1/calendars/1", NULL, T);
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
    r = nf_http("DELETE", "/api/v1/calendars", NULL, T);   /* the collection itself can't be deleted */
    TB_EQ_INT(r.status, 405);
    nf_free(&r);
    r = nf_http("PUT", "/api/v1/calendars", "{}", T);
    TB_EQ_INT(r.status, 405);
    nf_free(&r);
    TB_EQ_INT(fake_cal_remove_calls, 1);
}

TB_TEST(calendars_the_single_address_calls_refuse_with_several_saved)
{
    paired();
    have(1, "Work", "WRK", false);
    nf_resp_t r = nf_http("PUT", "/api/v1/calendar", "{\"url\": \"" URL2 "\"}", T);
    TB_EQ_INT(r.status, 202);                              /* one calendar: replaces its address, as before */
    nf_free(&r);
    have(2, "Home", "HOM", false);
    r = nf_http("PUT", "/api/v1/calendar", "{\"url\": \"" URL2 "\"}", T);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "several_calendars");
    nf_free(&r);
    r = nf_http("DELETE", "/api/v1/calendar", NULL, T);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "several_calendars");
    nf_free(&r);
    /* GET and Sync now still work, over all of them */
    r = nf_http("GET", "/api/v1/calendar", NULL, T);
    TB_EQ_INT(r.status, 200);
    TB_TRUE(nf_null(r.j, "calendar.address"));
    nf_free(&r);
    r = nf_http("POST", "/api/v1/calendar/sync", "{}", T);
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
}

TB_TEST(calendars_status_summary_counts_and_meetings_say_which_calendar)
{
    paired();
    have(1, "Work", "WRK", false);
    have(2, "Home", "HOM", true);
    nf_app.n_cals = 2;
    nf_app.cals[0] = (tb_cal_info_t){.used = true};
    nf_app.cals[1] = (tb_cal_info_t){.used = true, .failing = true};
    nf_app.set.automatic.calendar = true;
    tb_meeting_t m = {.id = 4, .start = fake_now.wall + 600, .end = fake_now.wall + 2400, .cal = 1};
    tb_app_set_meetings(&nf_app, &m, 1, &fake_now);
    nf_resp_t r = nf_http("GET", "/api/v1/status", NULL, T);
    TB_EQ_INT(nf_num(r.j, "calendar.count"), 2);
    TB_EQ_INT(nf_num(r.j, "calendar.failing"), 1);
    TB_EQ_INT(nf_num(r.j, "meeting.next.calendar"), 2);
    TB_TRUE(strstr(r.r.body, "https://") == NULL);
    nf_free(&r);
    nf_app.n_cals = 1;                                      /* with one calendar the key isn't there */
    r = nf_http("GET", "/api/v1/status", NULL, T);
    TB_TRUE(nf_get(r.j, "meeting.next.calendar") == NULL);
    nf_free(&r);
}
