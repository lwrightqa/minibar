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

TB_TEST(dns_ignores_garbage)
{
    uint8_t q[256], a[512];
    size_t n = query(q, "tinybar.local", 1);
    TB_EQ_INT(net_dns_answer(q, 5, 0, a, sizeof a), 0);         /* short */
    q[2] |= 0x80;
    TB_EQ_INT(net_dns_answer(q, n, 0, a, sizeof a), 0);         /* a response */
    q[2] &= 0x7F;
    q[12] = 0xC0;
    TB_EQ_INT(net_dns_answer(q, n, 0, a, sizeof a), 0);         /* a pointer in the question */
    n = query(q, "tinybar.local", 1);
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
