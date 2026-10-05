/* test_util.c: net_util.h (rate limits, RFC 3339, the Mac time rule, DNS, join errors, USB lines). Owner: net builder. */
#include <stdlib.h>

#include "net_util.h"
#include "tb_test.h"

/* ---------- rate limits (api.md 2.5) ---------- */

TB_TEST(rate_allows_a_burst_of_20_then_10_a_second)
{
    net_rate_t r;
    net_rate_init(&r);
    for (int i = 0; i < 20; i++) TB_EQ_INT(net_rate_take(&r, 1, false, 1000), 0);
    TB_TRUE(net_rate_take(&r, 1, false, 1000) >= 1);
    /* 100 ms later one more request has been earned */
    TB_EQ_INT(net_rate_take(&r, 1, false, 1100), 0);
    TB_TRUE(net_rate_take(&r, 1, false, 1100) >= 1);
    /* another address has its own allowance */
    TB_EQ_INT(net_rate_take(&r, 2, false, 1100), 0);
    /* steady 10 a second goes on forever */
    tb_ms_t t = 1100;
    for (int i = 0; i < 100; i++) {
        t += 100;
        TB_EQ_INT(net_rate_take(&r, 1, false, t), 0);
    }
}

TB_TEST(rate_without_a_token_is_5_a_second)
{
    net_rate_t r;
    net_rate_init(&r);
    for (int i = 0; i < 5; i++) TB_EQ_INT(net_rate_take(&r, 7, true, 0), 0);
    int wait = net_rate_take(&r, 7, true, 0);
    TB_EQ_INT(wait, 1);
    /* the same address with a token still has room */
    TB_EQ_INT(net_rate_take(&r, 7, false, 0), 0);
    TB_EQ_INT(net_rate_take(&r, 7, true, 200), 0);
}

TB_TEST(rate_forgets_the_least_recently_used_address)
{
    net_rate_t r;
    net_rate_init(&r);
    for (int i = 0; i < 20; i++) net_rate_take(&r, 99, false, 0);     /* 99 is out of requests */
    for (uint32_t ip = 1; ip <= NET_RATE_SLOTS; ip++) net_rate_take(&r, ip, false, 10 + ip);
    /* 99 was the oldest and was dropped, so it starts over with a full allowance */
    TB_EQ_INT(net_rate_take(&r, 99, false, 100), 0);
}

/* ---------- RFC 3339 ---------- */

TB_TEST(rfc3339_parses_offsets_and_zulu)
{
    tb_epoch_t t;
    TB_TRUE(net_parse_rfc3339("2026-10-04T14:12:00-07:00", &t));
    TB_EQ_INT(t, 1791148320);
    TB_TRUE(net_parse_rfc3339("2026-10-04T21:12:00Z", &t));
    TB_EQ_INT(t, 1791148320);
    TB_TRUE(net_parse_rfc3339("2026-10-04T21:12:00.512Z", &t));
    TB_EQ_INT(t, 1791148320);
    TB_TRUE(net_parse_rfc3339("2026-10-05T02:42:00+05:30", &t));
    TB_EQ_INT(t, 1791148320);
    TB_TRUE(net_parse_rfc3339("2028-02-29T00:00:00Z", &t));
}

TB_TEST(rfc3339_refuses_what_isnt_one)
{
    tb_epoch_t t;
    TB_FALSE(net_parse_rfc3339("2026-10-04 14:12", &t));
    TB_FALSE(net_parse_rfc3339("2026-10-04T14:12:00", &t));            /* no offset */
    TB_FALSE(net_parse_rfc3339("2026-13-04T14:12:00Z", &t));
    TB_FALSE(net_parse_rfc3339("2027-02-29T00:00:00Z", &t));
    TB_FALSE(net_parse_rfc3339("2026-10-04T24:00:00Z", &t));
    TB_FALSE(net_parse_rfc3339("2026-10-04T14:12:00+0700", &t));
    TB_FALSE(net_parse_rfc3339("2026-10-04T14:12:00Zjunk", &t));
    TB_FALSE(net_parse_rfc3339("1969-12-31T23:59:59Z", &t));
    TB_FALSE(net_parse_rfc3339("", &t));
    TB_FALSE(net_parse_rfc3339(NULL, &t));
}

/* ---------- the Mac's hello time (api.md 6.6) ---------- */

TB_TEST(mac_time_rule)
{
    tb_clock_t unknown = {.mono = 5000, .wall = 0, .valid = false};
    tb_clock_t known = {.mono = 5000, .wall = 1791148320, .valid = true};
    /* no network time ever: an unknown clock is set, a clock more than 2 s off is set, a close one isn't */
    TB_TRUE(net_time_mac_should_set(1791148320, &unknown, -1));
    TB_TRUE(net_time_mac_should_set(1791148323, &known, -1));
    TB_TRUE(net_time_mac_should_set(1791148317, &known, -1));
    TB_FALSE(net_time_mac_should_set(1791148322, &known, -1));
    /* network time within the last 24 hours wins */
    TB_FALSE(net_time_mac_should_set(1791148400, &known, 4000));
    tb_clock_t later = known;
    later.mono = 4000 + 24LL * 3600 * 1000;
    TB_TRUE(net_time_mac_should_set(1791148400, &later, 4000));
}

/* ---------- DNS catch-all ---------- */

static size_t query(uint8_t *q, const char *name, uint16_t type)
{
    static const uint8_t hdr[12] = {0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};
    memcpy(q, hdr, 12);
    size_t p = 12;
    const char *s = name;
    while (*s) {
        const char *dot = strchr(s, '.');
        size_t n = dot ? (size_t)(dot - s) : strlen(s);
        q[p++] = (uint8_t)n;
        memcpy(q + p, s, n);
        p += n;
        s += n + (dot ? 1 : 0);
    }
    q[p++] = 0;
    q[p++] = (uint8_t)(type >> 8);
    q[p++] = (uint8_t)type;
    q[p++] = 0;
    q[p++] = 1;
    return p;
}

TB_TEST(dns_answers_every_name_with_the_setup_address)
{
    uint8_t q[256], a[512];
    size_t n = query(q, "captive.apple.com", 1);
    uint32_t ip;
    memcpy(&ip, (uint8_t[]){192, 168, 4, 1}, 4);
    size_t m = net_dns_answer(q, n, ip, a, sizeof a);
    TB_EQ_INT(m, n + 16);
    TB_EQ_INT(a[0], 0x12);
    TB_EQ_INT(a[1], 0x34);
    TB_TRUE(a[2] & 0x80);                       /* a response */
    TB_EQ_INT(a[3] & 0x0F, 0);                  /* no error */
    TB_EQ_INT(a[7], 1);                         /* one answer */
    TB_EQ_INT(a[n + 12], 192);
    TB_EQ_INT(a[n + 15], 1);
    /* AAAA: no answer, so the phone uses IPv4 */
    n = query(q, "connectivitycheck.gstatic.com", 28);
    m = net_dns_answer(q, n, ip, a, sizeof a);
    TB_EQ_INT(m, n);
    TB_EQ_INT(a[7], 0);
}

TB_TEST(dns_answers_with_a_public_setup_address_and_keeps_rd)
{
    /* The setup address isn't private (net_port.h): Android's portal check refuses a private answer */
    uint8_t q[256], a[512];
    size_t n = query(q, "connectivitycheck.gstatic.com", 1);
    q[2] = 0x01;                                /* RD */
    q[3] = 0x20;                                /* AD, as some resolvers send */
    uint32_t ip;
    memcpy(&ip, (uint8_t[]){4, 3, 2, 1}, 4);
    size_t m = net_dns_answer(q, n, ip, a, sizeof a);
    TB_EQ_INT(m, n + 16);
    TB_EQ_INT(a[2], 0x85);                      /* QR, AA, RD kept */
    TB_EQ_INT(a[3], 0x00);                      /* no error */
    TB_EQ_INT(a[5], 1);                         /* one question */
    TB_EQ_INT(a[7], 1);                         /* one answer */
    TB_EQ_INT(a[9] | a[11], 0);                 /* nothing else */
    TB_TRUE(!memcmp(a + 12, q + 12, n - 12));   /* the question as asked */
    TB_EQ_INT(a[n], 0xC0);                      /* the answer names the question */
    TB_EQ_INT(a[n + 1], 0x0C);
    TB_EQ_INT(a[n + 3], 1);                     /* A */
    TB_EQ_INT(a[n + 5], 1);                     /* IN */
    TB_EQ_INT(a[n + 9], 10);                    /* TTL 10 s */
    TB_EQ_INT(a[n + 11], 4);
    TB_TRUE(!memcmp(a + n + 12, (uint8_t[]){4, 3, 2, 1}, 4));
    /* ANY gets the address too; HTTPS (65) and other types get NODATA, and a class other than IN no address */
    n = query(q, "captive.apple.com", 255);
    TB_EQ_INT(net_dns_answer(q, n, ip, a, sizeof a), n + 16);
    n = query(q, "www.google.com", 65);
    TB_EQ_INT(net_dns_answer(q, n, ip, a, sizeof a), n);
    TB_EQ_INT(a[7], 0);
    TB_EQ_INT(a[3] & 0x0F, 0);                  /* NODATA is "no error, no answer", not NXDOMAIN */
    n = query(q, "example.com", 1);
    q[n - 1] = 3;                               /* class CH */
    TB_EQ_INT(net_dns_answer(q, n, ip, a, sizeof a), n);
    TB_EQ_INT(a[7], 0);
    /* An EDNS query (an OPT record after the question) is answered without it */
    n = query(q, "connectivitycheck.gstatic.com", 1);
    q[11] = 1;
    static const uint8_t opt[11] = {0, 0, 41, 0x04, 0xD0, 0, 0, 0, 0, 0, 0};
    memcpy(q + n, opt, sizeof opt);
    m = net_dns_answer(q, n + sizeof opt, ip, a, sizeof a);
    TB_EQ_INT(m, n + 16);
    TB_EQ_INT(a[11], 0);
}

TB_TEST(dns_question_names_the_query_for_the_log)
{
    uint8_t q[600];
    char name[72];
    uint16_t type = 0;
    size_t n = query(q, "connectivitycheck.gstatic.com", 1);
    TB_TRUE(net_dns_question(q, n, name, sizeof name, &type));
    TB_EQ_STR(name, "connectivitycheck.gstatic.com");
    TB_EQ_INT(type, 1);
    n = query(q, "captive.apple.com", 28);
    TB_TRUE(net_dns_question(q, n, name, sizeof name, &type));
    TB_EQ_STR(name, "captive.apple.com");
    TB_EQ_INT(type, 28);
    /* control characters, bytes over 0x7E and a dot inside a label show as '?' */
    n = query(q, "ab.cd", 1);
    q[14] = 0x0A;
    q[16] = 0xC3;
    TB_TRUE(net_dns_question(q, n, name, sizeof name, &type));
    TB_EQ_STR(name, "a?.?d");
    n = query(q, "x.y", 1);
    q[13] = '.';
    TB_TRUE(net_dns_question(q, n, name, sizeof name, &type));
    TB_EQ_STR(name, "?.y");
    /* the root */
    n = query(q, "", 2);
    TB_TRUE(net_dns_question(q, n, name, sizeof name, &type));
    TB_EQ_STR(name, ".");
    TB_EQ_INT(type, 2);
    /* a long name is cut with "..." */
    n = query(q, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.bbbbbbbbbbbbbbbbbbbb.example", 1);
    char small[16];
    TB_TRUE(net_dns_question(q, n, small, sizeof small, &type));
    TB_EQ_INT(strlen(small), 15);
    TB_EQ_STR(small, "aaaaaaaaaaaa...");
    /* exactly full isn't cut */
    n = query(q, "abc", 1);
    char four[4];
    TB_TRUE(net_dns_question(q, n, four, sizeof four, &type));
    TB_EQ_STR(four, "abc");
    /* what net_dns_answer() won't answer, this won't name */
    q[2] |= 0x80;
    TB_FALSE(net_dns_question(q, n, name, sizeof name, &type));
    TB_EQ_STR(name, "");
    TB_FALSE(net_dns_question(q, 5, name, sizeof name, &type));
    TB_FALSE(net_dns_question(q, n, name, 0, &type));
    for (int seed = 0; seed < 2000; seed++) {   /* random bytes never crash it, and the name always ends */
        uint8_t g[80];
        srand((unsigned)seed + 7);
        for (size_t i = 0; i < sizeof g; i++) g[i] = (uint8_t)rand();
        g[2] &= 0x07;                           /* mostly queries, so the label walk runs */
        g[4] = 0;
        g[5] = 1;
        size_t len = 12 + (size_t)(rand() % 68);
        if (net_dns_question(g, len, small, sizeof small, &type)) TB_TRUE(strlen(small) < sizeof small);
    }
}

TB_TEST(setup_probe_paths_are_the_captive_portal_checks)
{
    const char *yes[] = {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                         "/connecttest.txt", "/ncsi.txt", "/redirect", "/canonical.html", "/success.txt",
                         "/generate_204?x=1", "/Hotspot-Detect.html", "/success.txt#top"};
    for (size_t i = 0; i < sizeof yes / sizeof yes[0]; i++) TB_TRUE(net_setup_probe_path(yes[i]));
    const char *no[] = {"/", "/index.html", "/favicon.ico", "/generate_2044", "/generate", "generate_204",
                        "/api/v1/setup/state", "/redirect/x", "", "/gen_204/"};
    for (size_t i = 0; i < sizeof no / sizeof no[0]; i++) TB_FALSE(net_setup_probe_path(no[i]));
    TB_FALSE(net_setup_probe_path(NULL));
}

TB_TEST(same_subnet_compares_network_order_addresses)
{
    uint32_t bar, phone, office, mask, near;
    memcpy(&bar, (uint8_t[]){4, 3, 2, 1}, 4);
    memcpy(&phone, (uint8_t[]){4, 3, 2, 2}, 4);
    memcpy(&office, (uint8_t[]){10, 0, 4, 42}, 4);
    memcpy(&near, (uint8_t[]){4, 3, 3, 2}, 4);
    memcpy(&mask, (uint8_t[]){255, 255, 255, 0}, 4);
    TB_TRUE(net_ip_same_subnet(phone, bar, mask));
    TB_TRUE(net_ip_same_subnet(bar, bar, mask));
    TB_FALSE(net_ip_same_subnet(office, bar, mask));
    TB_FALSE(net_ip_same_subnet(near, bar, mask));
    TB_FALSE(net_ip_same_subnet(phone, bar, 0));    /* no subnet known: nothing matches */
    TB_FALSE(net_ip_same_subnet(0, 0, mask));
}

TB_TEST(log_quota_lets_40_lines_a_minute_through_and_counts_the_rest)
{
    net_log_quota_t q = {0};
    int dropped = -1;
    for (int i = 0; i < 40; i++) TB_TRUE(net_log_quota_take(&q, 1000 + i, 40, &dropped));
    TB_EQ_INT(dropped, 0);
    for (int i = 0; i < 5; i++) TB_FALSE(net_log_quota_take(&q, 2000, 40, &dropped));
    TB_FALSE(net_log_quota_take(&q, 60999, 40, NULL));     /* still the same minute */
    /* the next minute says how many were held back, once */
    TB_TRUE(net_log_quota_take(&q, 61000, 40, &dropped));
    TB_EQ_INT(dropped, 6);
    TB_TRUE(net_log_quota_take(&q, 61001, 40, &dropped));
    TB_EQ_INT(dropped, 0);
    /* a quiet minute starts clean */
    TB_TRUE(net_log_quota_take(&q, 500000, 40, &dropped));
    TB_EQ_INT(dropped, 0);
    /* a clock that goes back starts a new minute rather than going silent */
    for (int i = 0; i < 40; i++) net_log_quota_take(&q, 500001, 40, NULL);
    TB_TRUE(net_log_quota_take(&q, 1000, 40, &dropped));
}

TB_TEST(log_text_keeps_a_line_a_line)
{
    char out[16];
    TB_EQ_STR(net_log_text(out, sizeof out, "4.3.2.1"), "4.3.2.1");
    TB_EQ_STR(net_log_text(out, sizeof out, NULL), "-");
    TB_EQ_STR(net_log_text(out, sizeof out, "a\r\nb\x1b[2J\x7f\xc3\xa9"), "a??b?[2J???");
    TB_EQ_STR(net_log_text(out, sizeof out, "connectivitycheck.gstatic.com"), "connectivity...");
    TB_EQ_INT(strlen(net_log_text(out, sizeof out, "0123456789abcde")), 15);   /* exactly full isn't cut */
    TB_EQ_STR(out, "0123456789abcde");
    char one[1] = {'x'};
    TB_EQ_STR(net_log_text(one, sizeof one, "abc"), "");
}

TB_TEST(log_path_leaves_the_query_out)
{
    char out[16];
    TB_EQ_STR(net_log_path(out, sizeof out, "/generate_204"), "/generate_204");
    TB_EQ_STR(net_log_path(out, sizeof out, "/a?token=s3cr3t"), "/a?...");
    TB_TRUE(strstr(out, "s3cr3t") == NULL);
    TB_EQ_STR(net_log_path(out, sizeof out, "/a#frag"), "/a?...");
    TB_EQ_STR(net_log_path(out, sizeof out, "?k=v"), "?...");
    TB_EQ_STR(net_log_path(out, sizeof out, "/a\r\nb"), "/a??b");
    TB_EQ_STR(net_log_path(out, sizeof out, NULL), "-");
    /* long: cut with "...", and never a byte of the query */
    TB_EQ_STR(net_log_path(out, sizeof out, "/0123456789abcdef?k=v"), "/0123456789a...");
    TB_EQ_STR(net_log_path(out, sizeof out, "/0123456789a?kv"), "/0123456789a...");
    TB_EQ_STR(net_log_path(out, sizeof out, "/0123456789?k"), "/0123456789?...");     /* exactly full */
    char one[1] = {'x'};
    TB_EQ_STR(net_log_path(one, sizeof one, "/a?b"), "");
}

TB_TEST(dns_ignores_garbage)
{
    uint8_t q[256], a[512];
    size_t n = query(q, "minibar.local", 1);
    TB_EQ_INT(net_dns_answer(q, 5, 0, a, sizeof a), 0);         /* short */
    q[2] |= 0x80;
    TB_EQ_INT(net_dns_answer(q, n, 0, a, sizeof a), 0);         /* a response */
    q[2] &= 0x7F;
    q[12] = 0xC0;
    TB_EQ_INT(net_dns_answer(q, n, 0, a, sizeof a), 0);         /* a pointer in the question */
    n = query(q, "minibar.local", 1);
    TB_EQ_INT(net_dns_answer(q, n - 3, 0, a, sizeof a), 0);     /* cut off */
    TB_EQ_INT(net_dns_answer(q, n, 0, a, 10), 0);               /* no room */
    for (int seed = 0; seed < 2000; seed++) {                   /* random bytes never crash it */
        uint8_t g[64];
        srand((unsigned)seed);
        for (size_t i = 0; i < sizeof g; i++) g[i] = (uint8_t)rand();
        net_dns_answer(g, (size_t)(rand() % 64), 0, a, sizeof a);
    }
}

/* ---------- Wi-Fi join errors (api.md 13.3) ---------- */

TB_TEST(join_errors_name_what_went_wrong)
{
    TB_EQ_INT(net_join_err_from_reason(NET_REASON_4WAY_HANDSHAKE_TIMEOUT, false), NET_JOIN_WRONG_PASSWORD);
    TB_EQ_INT(net_join_err_from_reason(NET_REASON_HANDSHAKE_TIMEOUT, false), NET_JOIN_WRONG_PASSWORD);
    TB_EQ_INT(net_join_err_from_reason(NET_REASON_AUTH_FAIL, false), NET_JOIN_WRONG_PASSWORD);
    TB_EQ_INT(net_join_err_from_reason(NET_REASON_802_1X_AUTH_FAILED, true), NET_JOIN_LOGIN_FAILED);
    TB_EQ_INT(net_join_err_from_reason(NET_REASON_4WAY_HANDSHAKE_TIMEOUT, true), NET_JOIN_LOGIN_FAILED);
    TB_EQ_INT(net_join_err_from_reason(NET_REASON_NO_AP_FOUND, false), NET_JOIN_NOT_FOUND);
    TB_EQ_INT(net_join_err_from_reason(NET_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY, false), NET_JOIN_NOT_FOUND);
    TB_EQ_INT(net_join_err_from_reason(NET_REASON_BEACON_TIMEOUT, false), NET_JOIN_NO_SIGNAL);
    TB_EQ_INT(net_join_err_from_reason(NET_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD, false), NET_JOIN_NO_SIGNAL);
    TB_TRUE(net_join_err_retry(NET_JOIN_NOT_FOUND));
    TB_FALSE(net_join_err_retry(NET_JOIN_WRONG_PASSWORD));
    TB_EQ_STR(net_join_err_code(NET_JOIN_WRONG_PASSWORD), "wrong_password");
    TB_EQ_STR(net_join_err_code(NET_JOIN_LOGIN_FAILED), "login_failed");
    TB_EQ_STR(net_join_err_code(NET_JOIN_NOT_FOUND), "not_found");
    TB_EQ_STR(net_join_err_code(NET_JOIN_NO_SIGNAL), "no_signal");
    TB_EQ_STR(net_join_err_code(NET_JOIN_NO_ADDRESS), "no_address");
    TB_EQ_STR(net_join_err_message(NET_JOIN_WRONG_PASSWORD), "The password didn't work.");
    TB_EQ_STR(net_join_err_screen(NET_JOIN_WRONG_PASSWORD), "Wrong password");
    TB_EQ_STR(net_join_err_screen(NET_JOIN_NO_SIGNAL), "No signal");
    TB_TRUE(net_join_err_code(NET_JOIN_OK) == NULL);
}

/* ---------- USB lines (api.md 6.4) ---------- */

typedef struct { int n; char last[NET_LINE_MAX + 1]; size_t len; bool too_long; } got_t;

static void on_line(const char *line, size_t len, bool too_long, void *ctx)
{
    got_t *g = ctx;
    g->n++;
    memcpy(g->last, line, len + 1);
    g->len = len;
    g->too_long = too_long;
}

static void feed(net_lines_t *l, const char *s, got_t *g)
{
    net_lines_feed(l, (const uint8_t *)s, strlen(s), on_line, g);
}

TB_TEST(lines_split_at_lf_and_drop_cr)
{
    net_lines_t l;
    net_lines_init(&l);
    got_t g = {0};
    feed(&l, "@tb {\"cmd\": \"st", &g);
    TB_EQ_INT(g.n, 0);
    feed(&l, "atus\"}\r\nhello\n", &g);
    TB_EQ_INT(g.n, 2);
    TB_EQ_STR(g.last, "hello");
    feed(&l, "@tb {}\r\n", &g);
    TB_EQ_STR(g.last, "@tb {}");
    TB_FALSE(g.too_long);
    feed(&l, "\n", &g);
    TB_EQ_INT(g.len, 0);
}

TB_TEST(lines_over_2048_bytes_are_cut_and_flagged)
{
    net_lines_t l;
    net_lines_init(&l);
    got_t g = {0};
    char *big = malloc(3001);
    memcpy(big, "@tb ", 4);
    memset(big + 4, 'x', 2996);
    big[3000] = '\0';
    feed(&l, big, &g);
    feed(&l, "\n", &g);
    TB_EQ_INT(g.n, 1);
    TB_TRUE(g.too_long);
    TB_EQ_INT(g.len, NET_LINE_MAX);
    TB_TRUE(!strncmp(g.last, "@tb ", 4));
    /* exactly 2048 bytes is fine, with or without a CR */
    big[2048] = '\0';
    feed(&l, big, &g);
    feed(&l, "\r\n", &g);
    TB_FALSE(g.too_long);
    TB_EQ_INT(g.len, 2048);
    feed(&l, big, &g);
    feed(&l, "\n", &g);
    TB_FALSE(g.too_long);
    /* 2049 isn't */
    big[2048] = 'x';
    big[2049] = '\0';
    feed(&l, big, &g);
    feed(&l, "\r\n", &g);
    TB_TRUE(g.too_long);
    /* the next line is normal again */
    feed(&l, "@tb {}\n", &g);
    TB_FALSE(g.too_long);
    free(big);
}

TB_TEST(ip_text)
{
    char b[16];
    TB_EQ_STR(net_ip_str(0x2a04000a, b), "10.0.4.42");
}
