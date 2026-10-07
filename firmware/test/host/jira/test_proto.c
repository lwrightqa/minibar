/*
 * The Jira request, answer and field checks (components/jira/src/jira_proto.c), with the inputs a hostile Remote page, a
 * hostile site or a broken proxy could send. Test data is generic: example.atlassian.net, you@example.com, filter 10042.
 */
#include <stdlib.h>
#include <string.h>

#include "jira_proto.h"
#include "jira_service.h"
#include "tb_test.h"

/* ---------- site ---------- */

TB_TEST(site_accepts_only_atlassian_net_over_https)
{
    char site[JIRA_SITE_MAX + 1], host[JIRA_HOST_MAX];
    TB_EQ_INT(jira_check_site("https://example.atlassian.net", site, sizeof site, host, sizeof host), JIRA_OK);
    TB_EQ_STR(site, "https://example.atlassian.net");
    TB_EQ_STR(host, "example.atlassian.net");
    /* capitals, one trailing slash and surrounding spaces are tidied */
    TB_EQ_INT(jira_check_site("  HTTPS://Example-Team.Atlassian.NET/ \n", site, sizeof site, host, sizeof host), JIRA_OK);
    TB_EQ_STR(site, "https://example-team.atlassian.net");
    TB_EQ_STR(host, "example-team.atlassian.net");
    TB_EQ_INT(jira_check_site("https://a.atlassian.net", NULL, 0, NULL, 0), JIRA_OK);
    TB_EQ_INT(jira_check_site("https://a1-b2.atlassian.net", NULL, 0, NULL, 0), JIRA_OK);
    /* a name of 63 characters is the longest */
    char longest[160], toolong[160];
    memset(longest, 'a', sizeof longest);
    snprintf(longest, sizeof longest, "https://%.63s.atlassian.net", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    snprintf(toolong, sizeof toolong, "https://%.64s.atlassian.net", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    TB_EQ_INT(jira_check_site(longest, NULL, 0, NULL, 0), JIRA_OK);
    TB_EQ_INT(jira_check_site(toolong, NULL, 0, NULL, 0), JIRA_E_SITE);
}

TB_TEST(site_refuses_everything_that_could_send_the_token_elsewhere)
{
    const char *bad[] = {
        NULL, "", " ", "example.atlassian.net", "http://example.atlassian.net", "ftp://example.atlassian.net",
        "https://atlassian.net", "https://.atlassian.net", "https://example.atlassian.com",
        "https://example.atlassian.net.evil.com", "https://example.atlassian.net.evil.com/",
        "https://evil.com/example.atlassian.net", "https://evil.com#.atlassian.net", "https://evil.com?.atlassian.net",
        "https://user@example.atlassian.net", "https://user:pass@example.atlassian.net",
        "https://evil.com@example.atlassian.net", "https://example.atlassian.net:443", "https://example.atlassian.net:8080",
        "https://example.atlassian.net/jira", "https://example.atlassian.net//", "https://example.atlassian.net/?x=1",
        "https://example.atlassian.net#top", "https://a.b.atlassian.net", "https://-a.atlassian.net", "https://a-.atlassian.net",
        "https://exa mple.atlassian.net", "https://exa_mple.atlassian.net", "https://exämple.atlassian.net",
        "https://example.atlassian.net\x01", "https://example.atlassian.net\\", "https:/example.atlassian.net",
        "https://example.atlassian.net.", "https://example..atlassian.net", "//example.atlassian.net", "webcal://example.atlassian.net",
        "https://\xE2\x80\xAE" "example.atlassian.net", "https://example.atlassian.net\n.evil.com",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char site[JIRA_SITE_MAX + 1] = "untouched", host[JIRA_HOST_MAX] = "untouched";
        jira_err_t e = jira_check_site(bad[i], site, sizeof site, host, sizeof host);
        if (e != JIRA_E_SITE) TB_FAIL_AT("site %zu (%s) was accepted", i, bad[i] ? bad[i] : "NULL");
    }
    char tiny[8];
    TB_EQ_INT(jira_check_site("https://example.atlassian.net", tiny, sizeof tiny, NULL, 0), JIRA_E_SITE);     /* never overflows */
    char huge[400];
    memset(huge, 'a', sizeof huge - 1);
    huge[sizeof huge - 1] = '\0';
    TB_EQ_INT(jira_check_site(huge, NULL, 0, NULL, 0), JIRA_E_SITE);
}

/* ---------- email, token ---------- */

TB_TEST(email_checks)
{
    const char *good[] = {"you@example.com", "a.b+c@sub.example.co.uk", "o'brien@example.org", "x@y.io"};
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) TB_EQ_INT(jira_check_email(good[i]), JIRA_OK);
    const char *bad[] = {NULL, "", "a", "@", "@example.com", "you@", "you@example", "you@.com", "you@example.", "you@@example.com",
        "you@exa..mple.com", "yo u@example.com", "you@exa mple.com", "you:x@example.com", "you@example.com\n", "\"you\"@example.com",
        "you@example.com,other@example.com", "you@exa_mple.com", "y\xC3\xB6u@example.com", "you@example.com\x01", "<you@example.com>"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        if (jira_check_email(bad[i]) != JIRA_E_EMAIL) TB_FAIL_AT("email %zu (%s) was accepted", i, bad[i] ? bad[i] : "NULL");
    char big[300];
    memset(big, 'a', sizeof big);
    memcpy(big + 200, "@example.com", 13);
    TB_EQ_INT(jira_check_email(big), JIRA_E_EMAIL);                 /* longer than the buffer: refused, not cut */
}

TB_TEST(token_checks)
{
    TB_EQ_INT(jira_check_token("abcDEF123_token-xyz="), JIRA_OK);
    TB_EQ_INT(jira_check_token("12345678"), JIRA_OK);
    TB_EQ_INT(jira_check_token("1234567"), JIRA_E_TOKEN);           /* too short to be one */
    TB_EQ_INT(jira_check_token(NULL), JIRA_E_TOKEN);
    TB_EQ_INT(jira_check_token(""), JIRA_E_TOKEN);
    TB_EQ_INT(jira_check_token("abcdefgh ijkl"), JIRA_E_TOKEN);     /* a space or a pasted newline */
    TB_EQ_INT(jira_check_token("abcdefghijkl\n"), JIRA_E_TOKEN);
    TB_EQ_INT(jira_check_token("abcdefghijkl\xC3\xA9"), JIRA_E_TOKEN);
    char big[400];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    TB_EQ_INT(jira_check_token(big), JIRA_E_TOKEN);
    big[JIRA_TOKEN_MAX - 1] = '\0';
    TB_EQ_INT(jira_check_token(big), JIRA_OK);                      /* 255 is the most */
}

/* ---------- filter, label, alert ---------- */

TB_TEST(filter_id_from_digits_or_an_address)
{
    char id[JIRA_FILTER_MAX];
    const struct { const char *in, *out; } ok[] = {
        {"10042", "10042"}, {" 10042 ", "10042"}, {"0010042", "10042"}, {"1", "1"}, {"9999999999", "9999999999"},
        {"https://example.atlassian.net/issues/?filter=10042", "10042"},
        {"https://example.atlassian.net/issues/?jql=x&filter=10043&y=1", "10043"},
        {"https://example.atlassian.net/jira/filters/10044", "10044"},
        {"https://example.atlassian.net/jira/filters/10044?x=1", "10044"},
        {"https://example.atlassian.net/issues/?filter=00012", "12"},
    };
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        TB_EQ_INT(jira_filter_id(ok[i].in, id), JIRA_OK);
        TB_EQ_STR(id, ok[i].out);
    }
    const char *bad[] = {NULL, "", "  ", "0", "0000", "abc", "10042abc", "-5", "12.5", "99999999999", "1 2", "filter=",
        "?filter=abc", "?filter=12345678901", "/filters/", "https://example.atlassian.net/issues/", "10042;DROP", "1e3", "\xE2\x91\xA0"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        if (jira_filter_id(bad[i], id) != JIRA_E_FILTER) TB_FAIL_AT("filter %zu (%s) was accepted: %s", i, bad[i] ? bad[i] : "NULL", id);
    char huge[900];
    memset(huge, '1', sizeof huge - 1);
    huge[sizeof huge - 1] = '\0';
    TB_EQ_INT(jira_filter_id(huge, id), JIRA_E_FILTER);
}

TB_TEST(label_alert_and_names)
{
    char l[TB_JIRA_LABEL_BYTES];
    TB_EQ_INT(jira_check_label("Open bugs", l), JIRA_OK);
    TB_EQ_STR(l, "Open bugs");
    TB_EQ_INT(jira_check_label("  Needs review  ", l), JIRA_OK);
    TB_EQ_STR(l, "Needs review");
    TB_EQ_INT(jira_check_label("123456789012345678", l), JIRA_OK);       /* 18 */
    TB_EQ_INT(jira_check_label("1234567890123456789", l), JIRA_E_LABEL);  /* 19 */
    TB_EQ_INT(jira_check_label("", l), JIRA_E_LABEL);
    TB_EQ_INT(jira_check_label("   ", l), JIRA_E_LABEL);
    TB_EQ_INT(jira_check_label(NULL, l), JIRA_E_LABEL);
    TB_EQ_INT(jira_check_label("Bugs \xF0\x9F\x90\x9B", l), JIRA_OK);   /* what the bar can't draw becomes ? */
    TB_EQ_STR(l, "Bugs ?");
    TB_EQ_INT(jira_check_label("Caf\xC3\xA9", l), JIRA_OK);
    TB_EQ_STR(l, "Caf\xC3\xA9");
    int32_t a = 5;
    TB_EQ_INT(jira_check_alert(NULL, &a), JIRA_OK);
    TB_EQ_INT(a, -1);
    TB_EQ_INT(jira_check_alert("", &a), JIRA_OK);
    TB_EQ_INT(a, -1);
    TB_EQ_INT(jira_check_alert("0", &a), JIRA_OK);
    TB_EQ_INT(a, 0);
    TB_EQ_INT(jira_check_alert(" 9999 ", &a), JIRA_OK);
    TB_EQ_INT(a, 9999);
    const char *bad[] = {"10000", "-1", "1.5", "ten", "12345", "1 2", "+5", "\xE2\x91\xA0", "99999999999999999999"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) TB_EQ_INT(jira_check_alert(bad[i], &a), JIRA_E_ALERT);
    /* a name from Jira becomes a label: cleaned, cut to 18 characters with an ellipsis */
    jira_label_from_name("Open bugs", l);
    TB_EQ_STR(l, "Open bugs");
    jira_label_from_name("Open bugs and incidents in the backend", l);
    TB_EQ_STR(l, "Open bugs and inc\xE2\x80\xA6");
    jira_label_from_name("Bugs \xF0\x9F\x90\x9B\xF0\x9F\x90\x9B", l);
    TB_EQ_STR(l, "Bugs ??");
    jira_label_from_name(NULL, l);
    TB_EQ_STR(l, "");
    char hint[40];
    jira_email_hint("you@example.com", hint, sizeof hint);
    TB_EQ_STR(hint, "y\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2@example.com");
    jira_email_hint("", hint, sizeof hint);
    TB_EQ_STR(hint, "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");
    char tiny[6];
    jira_email_hint("you@example.com", tiny, sizeof tiny);            /* cut, never overrun */
    TB_TRUE(strlen(tiny) < sizeof tiny);
}

/* ---------- the request and the sign-in ---------- */

TB_TEST(count_request_is_one_post_with_the_filter_in_the_jql)
{
    jira_request_t r;
    TB_TRUE(jira_count_request("example.atlassian.net", "10042", &r));
    TB_EQ_STR(r.url, "https://example.atlassian.net/rest/api/3/search/approximate-count");
    TB_EQ_STR(r.body, "{\"jql\":\"filter = 10042\"}");
    TB_TRUE(r.post);
    TB_TRUE(jira_filter_request("example.atlassian.net", "10042", &r));
    TB_EQ_STR(r.url, "https://example.atlassian.net/rest/api/3/filter/10042");
    TB_EQ_STR(r.body, "");
    TB_FALSE(r.post);
    /* the token and the email are never part of a URL or a body: they aren't even arguments */
    /* anything that isn't a host or an id is refused, so nothing is injected into the URL or the JSON */
    const char *hosts[] = {NULL, "", "a", "evil.com/x", "ex ample.atlassian.net", "example.atlassian.net\"", "example.atlassian.net?x",
                           "example.atlassian.net@evil", "ex\xC3\xA4mple.atlassian.net", "example.atlassian.net:443"};
    for (size_t i = 0; i < sizeof hosts / sizeof hosts[0]; i++) {
        TB_FALSE(jira_count_request(hosts[i], "10042", &r));
        TB_EQ_STR(r.url, "");
        TB_FALSE(jira_filter_request(hosts[i], "10042", &r));
    }
    const char *ids[] = {NULL, "", "abc", "12\"3", "1 or 1=1", "12345678901", "-1", "1\", \"x\":\"y"};
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        TB_FALSE(jira_count_request("example.atlassian.net", ids[i], &r));
        TB_EQ_STR(r.body, "");
        TB_FALSE(jira_filter_request("example.atlassian.net", ids[i], &r));
    }
}

TB_TEST(basic_auth_is_base64_of_email_colon_token)
{
    char auth[JIRA_AUTH_MAX];
    TB_TRUE(jira_basic_auth("you@example.com", "abcDEF123_token-xyz", auth, sizeof auth));
    TB_EQ_STR(auth, "Basic eW91QGV4YW1wbGUuY29tOmFiY0RFRjEyM190b2tlbi14eXo=");
    TB_TRUE(jira_basic_auth("a@b.co", "12345678", auth, sizeof auth));
    TB_EQ_STR(auth, "Basic YUBiLmNvOjEyMzQ1Njc4");
    TB_TRUE(jira_basic_auth("ab@x.io", "1234567", auth, sizeof auth));
    TB_EQ_STR(auth, "Basic YWJAeC5pbzoxMjM0NTY3");
    /* the longest email and token still fit the buffer the header is built into */
    char e[JIRA_EMAIL_MAX], t[JIRA_TOKEN_MAX];
    memset(e, 'a', sizeof e - 1);
    e[sizeof e - 1] = '\0';
    memset(t, 'b', sizeof t - 1);
    t[sizeof t - 1] = '\0';
    TB_TRUE(jira_basic_auth(e, t, auth, sizeof auth));
    TB_TRUE(strlen(auth) < JIRA_AUTH_MAX);
    /* refused: a colon in the user name (it would end it), a buffer too small, missing parts */
    TB_FALSE(jira_basic_auth("a:b@example.com", "12345678", auth, sizeof auth));
    TB_EQ_STR(auth, "");
    char small[12];
    TB_FALSE(jira_basic_auth("you@example.com", "abcDEF123_token-xyz", small, sizeof small));
    TB_FALSE(jira_basic_auth(NULL, "x", auth, sizeof auth));
    TB_FALSE(jira_basic_auth("a@b.co", NULL, auth, sizeof auth));
    TB_FALSE(jira_basic_auth("a@b.co", "x", NULL, 0));
}

TB_TEST(wipe_clears_a_buffer)
{
    char b[16] = "secret-token";
    jira_wipe(b, sizeof b);
    for (size_t i = 0; i < sizeof b; i++) TB_EQ_INT(b[i], 0);
    jira_wipe(NULL, 0);
}

/* ---------- the answer ---------- */

TB_TEST(http_status_classes_and_retry_after)
{
    TB_EQ_INT(jira_http_class(200), JIRA_HTTP_ANSWER);
    TB_EQ_INT(jira_http_class(204), JIRA_HTTP_ANSWER);
    TB_EQ_INT(jira_http_class(401), JIRA_HTTP_TOKEN);
    TB_EQ_INT(jira_http_class(403), JIRA_HTTP_TOKEN);
    TB_EQ_INT(jira_http_class(404), JIRA_HTTP_NOFILTER);
    TB_EQ_INT(jira_http_class(400), JIRA_HTTP_NOFILTER);
    const int unreachable[] = {0, -1, 100, 301, 302, 410, 429, 500, 502, 503, 504, 999};
    for (size_t i = 0; i < sizeof unreachable / sizeof unreachable[0]; i++) TB_EQ_INT(jira_http_class(unreachable[i]), JIRA_HTTP_UNREACHABLE);
    TB_EQ_INT(jira_parse_retry_after("120"), 120);
    TB_EQ_INT(jira_parse_retry_after(" 7 "), 7);
    TB_EQ_INT(jira_parse_retry_after("86400"), 86400);
    TB_EQ_INT(jira_parse_retry_after("99999"), 86400);                 /* a day at most */
    TB_EQ_INT(jira_parse_retry_after("0"), 0);
    const char *junk[] = {NULL, "", "abc", "-5", "1.5", "12 seconds", "Wed, 21 Oct 2026 07:28:00 GMT", "123456789", "\xE2\x91\xA0"};
    for (size_t i = 0; i < sizeof junk / sizeof junk[0]; i++) TB_EQ_INT(jira_parse_retry_after(junk[i]), 0);
}

static bool count_of(const char *json, int32_t *c)
{
    return jira_parse_count(json, strlen(json), c);
}

TB_TEST(count_is_read_leniently_from_the_top_level)
{
    int32_t c = -1;
    TB_TRUE(count_of("{\"count\":12}", &c));
    TB_EQ_INT(c, 12);
    TB_TRUE(count_of("  {\n  \"count\" : 0 \n}\n", &c));
    TB_EQ_INT(c, 0);
    TB_TRUE(count_of("{\"count\":\"27\"}", &c));                       /* a numeric string */
    TB_EQ_INT(c, 27);
    TB_TRUE(count_of("{\"count\":12.0}", &c));
    TB_EQ_INT(c, 12);
    TB_TRUE(count_of("{\"other\":{\"count\":99},\"count\":3,\"list\":[1,2,{\"count\":98}]}", &c));
    TB_EQ_INT(c, 3);                                                  /* the top level's, not a nested one */
    TB_TRUE(count_of("{\"total\":41}", &c));                          /* the old search's field, as a fallback */
    TB_EQ_INT(c, 41);
    TB_TRUE(count_of("{\"count\":2000000000}", &c));
    TB_EQ_INT(c, 2000000000);
    TB_TRUE(count_of("\xEF\xBB\xBF{\"count\":7}", &c));               /* a byte-order mark */
    TB_EQ_INT(c, 7);
    TB_TRUE(count_of("{\"count\":5,\"count\":6}", &c));               /* the first one */
    TB_EQ_INT(c, 5);
    TB_TRUE(count_of("{\"a\":\"}{\\\"count\\\":1\",\"count\":8}", &c));    /* braces and keys inside a string are text */
    TB_EQ_INT(c, 8);
}

TB_TEST(count_refuses_what_is_not_a_count)
{
    int32_t c = 77;
    const char *bad[] = {
        "", " ", "{}", "[]", "[{\"count\":1}]", "null", "12", "\"count\"", "{\"count\":}", "{\"count\":null}", "{\"count\":true}",
        "{\"count\":-1}", "{\"count\":+1}", "{\"count\":1.5}", "{\"count\":1e3}", "{\"count\":\"abc\"}", "{\"count\":\"-3\"}",
        "{\"count\":\"\"}", "{\"count\":2000000001}", "{\"count\":99999999999999999999}", "{\"count\":[1]}", "{\"count\":{\"n\":1}}",
        "{\"counts\":12}", "{\"Count\":12}", "{count:12}", "{\"count\" 12}", "{\"count\":12", "{\"count", "{\"co\\u0075nt\":12}",
        "<html>Service Unavailable</html>", "{\"errorMessages\":[\"The filter does not exist\"],\"errors\":{}}",
        "{\"count\":12.}", "{\"count\":0x10}", "{\"count\":1 2}", "{\"count\":.5}",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        c = 77;
        if (count_of(bad[i], &c)) TB_FAIL_AT("%zu (%s) was read as %d", i, bad[i], (int)c);
        TB_EQ_INT(c, 77);                                             /* untouched */
    }
    TB_FALSE(jira_parse_count(NULL, 5, &c));
    TB_FALSE(jira_parse_count("{\"count\":1}", 11, NULL));
    /* never reads past len: the same text cut short, with more bytes after it that would make it valid */
    char buf[] = "{\"count\":12}\0\"count\":";
    TB_FALSE(jira_parse_count(buf, 10, &c));
    TB_TRUE(jira_parse_count(buf, 12, &c));
}

TB_TEST(count_survives_hostile_bodies)
{
    int32_t c = 5;
    /* a thousand levels of nesting, before or after the key, is refused or skipped, never recursed into */
    char *deep = malloc(4096);
    size_t n = 0;
    deep[n++] = '{';
    n += (size_t)snprintf(deep + n, 100, "\"deep\":");
    for (int i = 0; i < 1000; i++) deep[n++] = '[';
    for (int i = 0; i < 1000; i++) deep[n++] = ']';
    n += (size_t)snprintf(deep + n, 100, ",\"count\":9}");
    TB_FALSE(jira_parse_count(deep, n, &c));
    TB_EQ_INT(c, 5);
    /* a long string, a long run of commas, binary junk and NUL bytes */
    char *junk = malloc(20000);
    memset(junk, 'x', 20000);
    memcpy(junk, "{\"count\":\"", 10);
    TB_FALSE(jira_parse_count(junk, 20000, &c));
    memset(junk, ',', 20000);
    junk[0] = '{';
    TB_FALSE(jira_parse_count(junk, 20000, &c));
    for (int i = 0; i < 20000; i++) junk[i] = (char)(i * 131 + 17);
    TB_FALSE(jira_parse_count(junk, 20000, &c));
    char z[] = {'{', '"', 'c', 'o', 'u', 'n', 't', '"', ':', '1', '2', '\0', '}'};
    TB_TRUE(jira_parse_count(z, 10, &c) || true);                      /* must not crash, whatever it decides */
    /* every prefix of a valid answer: no crash, and only the whole one is a count */
    const char *ok = "{\"count\": 12, \"x\": [1, \"a\\\"b\", {\"y\": null}]}";
    for (size_t k = 0; k < strlen(ok); k++) {
        bool r = jira_parse_count(ok, k, &c);
        if (k < 13 && r) TB_FAIL_AT("a prefix of %zu bytes was read as a count", k);
    }
    free(deep);
    free(junk);
}

TB_TEST(filter_name_is_decoded_and_cut_safely)
{
    char n[40];
    const char *json = "{\"self\":\"https://example.atlassian.net/rest/api/3/filter/10042\",\"id\":\"10042\",\"name\":\"Open bugs\","
                       "\"jql\":\"project = X AND name = \\\"a\\\"\",\"owner\":{\"name\":\"nested\"}}";
    TB_TRUE(jira_parse_filter_name(json, strlen(json), n, sizeof n));
    TB_EQ_STR(n, "Open bugs");
    json = "{\"name\":\"Caf\\u00e9 \\ud83d\\udc1b \\\\ \\/ \\\"q\\\"\\n\"}";
    TB_TRUE(jira_parse_filter_name(json, strlen(json), n, sizeof n));
    TB_EQ_STR(n, "Caf\xC3\xA9 \xF0\x9F\x90\x9B \\ / \"q\"\n");
    /* cut to the buffer, never in the middle of a character */
    char tiny[6];
    json = "{\"name\":\"ab\\u00e9\\u00e9\\u00e9\"}";
    TB_TRUE(jira_parse_filter_name(json, strlen(json), tiny, sizeof tiny));
    TB_EQ_STR(tiny, "ab\xC3\xA9");
    const char *bad[] = {"", "{}", "{\"name\":5}", "{\"name\":null}", "{\"name\":\"\"}", "{\"name\":\"a", "{\"name\":\"a\\\"}",
        "{\"name\":\"\\ud800\"}", "{\"name\":\"\\udc00\"}", "{\"name\":\"\\u00zz\"}", "{\"name\":\"\\x41\"}", "{\"name\":\"\\u0000\"}",
        "{\"n\\u0061me\":\"a\"}", "[\"name\"]", "{\"owner\":{\"name\":\"nested\"}}"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        n[0] = 'x';
        if (jira_parse_filter_name(bad[i], strlen(bad[i]), n, sizeof n)) TB_FAIL_AT("name %zu (%s) was read: %s", i, bad[i], n);
    }
    TB_FALSE(jira_parse_filter_name(NULL, 4, n, sizeof n));
    TB_FALSE(jira_parse_filter_name("{\"name\":\"a\"}", 12, n, 1));
}

/* ---------- jira_resolve: what a Save or a Test sends, merged over what is saved ---------- */

static jira_input_t full_input(void)
{
    jira_input_t in = {0};
    in.site = "https://Example.atlassian.net/";
    in.email = "you@example.com";
    in.token = "abcDEF123_token-xyz";
    in.filter = "https://example.atlassian.net/issues/?filter=10042";
    return in;
}

TB_TEST(resolve_first_save_needs_every_field_and_defaults_the_label)
{
    jira_cfg_t c;
    jira_input_t in = full_input();
    TB_EQ_INT(jira_resolve(NULL, &in, &c), JIRA_OK);
    TB_EQ_STR(c.site, "https://example.atlassian.net");
    TB_EQ_STR(c.host, "example.atlassian.net");
    TB_EQ_STR(c.email, "you@example.com");
    TB_EQ_STR(c.token, "abcDEF123_token-xyz");
    TB_EQ_STR(c.filter, "10042");
    TB_EQ_STR(c.label, "Filter 10042");              /* until Jira says the filter's name */
    TB_TRUE(c.label_auto);
    TB_EQ_INT(c.alert_above, -1);
    /* each missing field names itself */
    in = full_input(); in.site = NULL;   TB_EQ_INT(jira_resolve(NULL, &in, &c), JIRA_E_SITE);
    in = full_input(); in.email = "  "; TB_EQ_INT(jira_resolve(NULL, &in, &c), JIRA_E_EMAIL);
    in = full_input(); in.token = "";    TB_EQ_INT(jira_resolve(NULL, &in, &c), JIRA_E_TOKEN);
    in = full_input(); in.filter = NULL; TB_EQ_INT(jira_resolve(NULL, &in, &c), JIRA_E_FILTER);
    TB_EQ_INT(jira_resolve(NULL, NULL, &c), JIRA_E_SITE);
    /* and a bad one names itself, even when the others are missing */
    in = full_input(); in.site = "http://example.atlassian.net";
    TB_EQ_INT(jira_resolve(NULL, &in, &c), JIRA_E_SITE);
    in = full_input(); in.token = "short";
    TB_EQ_INT(jira_resolve(NULL, &in, &c), JIRA_E_TOKEN);
    in = full_input(); in.filter = "nope";
    TB_EQ_INT(jira_resolve(NULL, &in, &c), JIRA_E_FILTER);
    in = full_input(); in.label = "123456789012345678901";
    TB_EQ_INT(jira_resolve(NULL, &in, &c), JIRA_E_LABEL);
    in = full_input(); in.has_alert = true; in.alert = "10000";
    TB_EQ_INT(jira_resolve(NULL, &in, &c), JIRA_E_ALERT);
    TB_EQ_STR(jira_err_field(JIRA_E_FILTER), "filter_id");
    TB_EQ_STR(jira_err_field(JIRA_E_ALERT), "alert_above");
    TB_TRUE(jira_err_field(JIRA_OK) == NULL);
    TB_TRUE(strlen(jira_err_message(JIRA_E_SITE)) > 20);
}

TB_TEST(resolve_blank_fields_keep_what_is_saved)
{
    jira_cfg_t saved, c;
    jira_input_t in = full_input();
    in.label = "Open bugs";
    in.has_alert = true;
    in.alert = "10";
    TB_EQ_INT(jira_resolve(NULL, &in, &saved), JIRA_OK);
    TB_FALSE(saved.label_auto);
    TB_EQ_INT(saved.alert_above, 10);
    /* nothing sent: everything stays, the token included */
    jira_input_t none = {0};
    TB_EQ_INT(jira_resolve(&saved, &none, &c), JIRA_OK);
    TB_EQ_STR(c.token, "abcDEF123_token-xyz");
    TB_EQ_STR(c.email, "you@example.com");
    TB_EQ_STR(c.label, "Open bugs");
    TB_EQ_INT(c.alert_above, 10);
    /* only the alert changes: set, then cleared with a blank (has_alert, no value) */
    none.has_alert = true;
    none.alert = "25";
    TB_EQ_INT(jira_resolve(&saved, &none, &c), JIRA_OK);
    TB_EQ_INT(c.alert_above, 25);
    none.alert = NULL;
    TB_EQ_INT(jira_resolve(&saved, &none, &c), JIRA_OK);
    TB_EQ_INT(c.alert_above, -1);
    none.alert = "  ";
    TB_EQ_INT(jira_resolve(&saved, &none, &c), JIRA_OK);
    TB_EQ_INT(c.alert_above, -1);
    /* a new token replaces the saved one, nothing else does */
    jira_input_t t = {0};
    t.token = "another-token-123";
    TB_EQ_INT(jira_resolve(&saved, &t, &c), JIRA_OK);
    TB_EQ_STR(c.token, "another-token-123");
    TB_EQ_STR(c.site, saved.site);
    /* a new filter with a typed label keeps the label; with an automatic one it follows the filter */
    jira_input_t f = {0};
    f.filter = "20001";
    TB_EQ_INT(jira_resolve(&saved, &f, &c), JIRA_OK);
    TB_EQ_STR(c.filter, "20001");
    TB_EQ_STR(c.label, "Open bugs");                 /* what the user typed stays */
    jira_cfg_t autolabel;
    in = full_input();
    TB_EQ_INT(jira_resolve(NULL, &in, &autolabel), JIRA_OK);
    TB_EQ_INT(jira_resolve(&autolabel, &f, &c), JIRA_OK);
    TB_EQ_STR(c.label, "Filter 20001");              /* it followed the filter */
    TB_TRUE(c.label_auto);
    TB_EQ_INT(jira_resolve(&autolabel, &none, &c), JIRA_OK);
    TB_EQ_STR(c.label, "Filter 10042");              /* the same filter: unchanged */
    /* a bad value is an error even when something is saved: nothing is silently kept in its place */
    jira_input_t bad = {0};
    bad.site = "https://evil.example.com";
    TB_EQ_INT(jira_resolve(&saved, &bad, &c), JIRA_E_SITE);
    bad = (jira_input_t){0};
    bad.email = "not-an-email";
    TB_EQ_INT(jira_resolve(&saved, &bad, &c), JIRA_E_EMAIL);
}

TB_TEST(test_messages_are_plain_sentences)
{
    TB_TRUE(strstr(jira_test_message(JIRA_ERR_UNREACHABLE), "Couldn't reach Jira") != NULL);
    TB_TRUE(strstr(jira_test_message(JIRA_ERR_TOKEN), "didn't accept that email and token") != NULL);
    TB_TRUE(strstr(jira_test_message(JIRA_ERR_FILTER), "no filter with that ID") != NULL);
    TB_TRUE(strstr(jira_test_message(JIRA_ERR_OFFLINE), "can't reach the internet") != NULL);
    TB_EQ_STR(jira_test_message("something_else"), "");
    TB_EQ_STR(jira_test_message(NULL), "");
}
