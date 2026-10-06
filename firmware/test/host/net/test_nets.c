/* test_nets.c: net_nets.h (the saved Wi-Fi networks: add, update, evict, order, the saved form, the move from one
 * network, which to try next). Owner: net builder. */
#include <string.h>

#include "net_nets.h"
#include "tb_test.h"

static void add(net_nets_t *l, const char *ssid, const char *pass)
{
    TB_TRUE(net_nets_add(l, ssid, "", pass, pass && pass[0] ? 1 : 0, NULL, NULL));
}

static void names(const net_nets_t *l, char *out, size_t cap)
{
    int o[NET_NETS_MAX];
    int n = net_nets_order(l, o);
    out[0] = '\0';
    for (int i = 0; i < n; i++) {
        if (i) strncat(out, ",", cap - strlen(out) - 1);
        strncat(out, l->n[o[i]].ssid, cap - strlen(out) - 1);
    }
}

TB_TEST(nets_add_orders_newest_first)
{
    net_nets_t l;
    net_nets_init(&l);
    char b[200];
    names(&l, b, sizeof b);
    TB_EQ_STR(b, "");
    add(&l, "home", "p1");
    add(&l, "work", "p2");
    add(&l, "cafe", "p3");
    names(&l, b, sizeof b);
    TB_EQ_STR(b, "cafe,work,home");
    TB_EQ_INT(l.count, 3);
    TB_TRUE(net_nets_replaces(&l) == NULL);
}

TB_TEST(nets_same_ssid_updates_and_is_exact)
{
    net_nets_t l;
    net_nets_init(&l);
    net_nets_res_t r;
    add(&l, "Office", "old");
    add(&l, "home", "h");
    TB_TRUE(net_nets_add(&l, "Office", "me", "new", 2, &r, NULL));
    TB_EQ_INT(r, NET_NETS_UPDATED);
    TB_EQ_INT(l.count, 2);
    int i = net_nets_find(&l, "Office");
    TB_TRUE(i >= 0);
    TB_EQ_STR(l.n[i].pass, "new");
    TB_EQ_STR(l.n[i].user, "me");
    TB_EQ_INT(l.n[i].sec, 2);
    char b[100];
    names(&l, b, sizeof b);
    TB_EQ_STR(b, "Office,home");
    /* case matters: another network */
    TB_TRUE(net_nets_add(&l, "office", "", "x", 1, &r, NULL));
    TB_EQ_INT(r, NET_NETS_ADDED);
    TB_EQ_INT(l.count, 3);
    TB_EQ_INT(net_nets_find(&l, "nope"), -1);
}

TB_TEST(nets_sixth_evicts_the_least_recently_used)
{
    net_nets_t l;
    net_nets_init(&l);
    const char *n[] = {"a", "b", "c", "d", "e"};
    for (int i = 0; i < 5; i++) add(&l, n[i], "p");
    TB_EQ_STR(net_nets_replaces(&l), "a");
    TB_TRUE(net_nets_touch(&l, net_nets_find(&l, "a")));    /* a used again: b is now the oldest */
    TB_EQ_STR(net_nets_replaces(&l), "b");
    net_nets_res_t r;
    char ev[33];
    TB_TRUE(net_nets_add(&l, "f", "", "p", 1, &r, ev));
    TB_EQ_INT(r, NET_NETS_REPLACED);
    TB_EQ_STR(ev, "b");
    TB_EQ_INT(l.count, 5);
    TB_EQ_INT(net_nets_find(&l, "b"), -1);
    char b[100];
    names(&l, b, sizeof b);
    TB_EQ_STR(b, "f,a,e,d,c");
    /* an update with five saved evicts nothing */
    TB_TRUE(net_nets_add(&l, "c", "", "q", 1, &r, ev));
    TB_EQ_INT(r, NET_NETS_UPDATED);
    TB_EQ_STR(ev, "");
    TB_EQ_INT(l.count, 5);
}

TB_TEST(nets_touch_changes_the_order_only_when_it_must)
{
    net_nets_t l;
    net_nets_init(&l);
    add(&l, "home", "p");
    add(&l, "work", "p");
    TB_FALSE(net_nets_touch(&l, 1));    /* work is the newest already */
    TB_FALSE(net_nets_touch(&l, 7));
    TB_FALSE(net_nets_touch(&l, -1));
    TB_TRUE(net_nets_touch(&l, 0));
    TB_FALSE(net_nets_touch(&l, 0));
    char b[100];
    names(&l, b, sizeof b);
    TB_EQ_STR(b, "home,work");
}

TB_TEST(nets_rejects_bad_names)
{
    net_nets_t l;
    net_nets_init(&l);
    TB_FALSE(net_nets_add(&l, "", "", "p", 1, NULL, NULL));
    TB_FALSE(net_nets_add(&l, NULL, "", "p", 1, NULL, NULL));
    TB_FALSE(net_nets_add(&l, "012345678901234567890123456789012", "", "p", 1, NULL, NULL));
    TB_TRUE(net_nets_add(&l, "01234567890123456789012345678901", "", "p", 1, NULL, NULL));
    TB_FALSE(net_nets_add(&l, "x", "", "p", 3, NULL, NULL));
    TB_EQ_INT(l.count, 1);
}

TB_TEST(nets_blob_round_trips_and_stays_small)
{
    net_nets_t l, m;
    net_nets_init(&l);
    add(&l, "home", "p1");
    net_nets_add(&l, "work", "jo@corp", "secret", 2, NULL, NULL);
    add(&l, "open-cafe", "");
    uint8_t buf[NET_NETS_BLOB_MAX];
    size_t n = net_nets_encode(&l, buf, sizeof buf);
    TB_TRUE(n > 0 && n < 100);
    TB_TRUE(net_nets_decode(&m, buf, n));
    TB_TRUE(!memcmp(&l, &m, sizeof l));     /* padding is zeroed on both sides */
    TB_EQ_INT(net_nets_encode(&l, buf, n - 1), 0);
    /* a list written while it was full keeps its order */
    net_nets_init(&l);
    for (int i = 0; i < 7; i++) {
        char s[8];
        s[0] = (char)('a' + i);
        s[1] = 0;
        add(&l, s, "pw");
    }
    n = net_nets_encode(&l, buf, sizeof buf);
    TB_TRUE(net_nets_decode(&m, buf, n));
    char b1[100], b2[100];
    names(&l, b1, sizeof b1);
    names(&m, b2, sizeof b2);
    TB_EQ_STR(b1, b2);
    TB_EQ_STR(b1, "g,f,e,d,c");
}

TB_TEST(nets_blob_largest_fits)
{
    net_nets_t l, m;
    net_nets_init(&l);
    char ssid[33], user[129], pass[129];
    memset(user, 'u', 128);
    user[128] = 0;
    memset(pass, 'p', 128);
    pass[128] = 0;
    for (int i = 0; i < 5; i++) {
        memset(ssid, 'a' + i, 32);
        ssid[32] = 0;
        TB_TRUE(net_nets_add(&l, ssid, user, pass, 2, NULL, NULL));
    }
    uint8_t buf[NET_NETS_BLOB_MAX];
    size_t n = net_nets_encode(&l, buf, sizeof buf);
    TB_EQ_INT((int)n, NET_NETS_BLOB_MAX);
    TB_TRUE(net_nets_decode(&m, buf, n));
    TB_TRUE(!memcmp(&l, &m, sizeof l));
}

TB_TEST(nets_decode_refuses_damage_and_leaves_the_list_empty)
{
    net_nets_t l, m;
    net_nets_init(&l);
    add(&l, "home", "p1");
    add(&l, "work", "p2");
    uint8_t buf[NET_NETS_BLOB_MAX], bad[NET_NETS_BLOB_MAX + 8];
    size_t n = net_nets_encode(&l, buf, sizeof buf);
    TB_TRUE(net_nets_decode(&m, buf, n));
    /* every shorter prefix is refused, and ASan watches the reads */
    for (size_t k = 0; k < n; k++) {
        memcpy(bad, buf, k);
        TB_FALSE(net_nets_decode(&m, bad, k));
        TB_EQ_INT(m.count, 0);
    }
    /* trailing bytes, a wrong version, a count over 5, a bad security value, a seq of 0, a seq above next_seq */
    memcpy(bad, buf, n);
    bad[n] = 0;
    TB_FALSE(net_nets_decode(&m, bad, n + 1));
    memcpy(bad, buf, n);
    bad[0] = 2;
    TB_FALSE(net_nets_decode(&m, bad, n));
    memcpy(bad, buf, n);
    bad[1] = 6;
    TB_FALSE(net_nets_decode(&m, bad, n));
    memcpy(bad, buf, n);
    bad[10] = 9;
    TB_FALSE(net_nets_decode(&m, bad, n));
    memcpy(bad, buf, n);
    bad[6] = bad[7] = bad[8] = bad[9] = 0;
    TB_FALSE(net_nets_decode(&m, bad, n));
    memcpy(bad, buf, n);
    bad[2] = 1;     /* next_seq 1: below the saved seqs */
    TB_FALSE(net_nets_decode(&m, bad, n));
    /* an empty SSID, and a duplicate one */
    memcpy(bad, buf, n);
    bad[11] = 0;
    TB_FALSE(net_nets_decode(&m, bad, n));
    TB_FALSE(net_nets_decode(&m, NULL, 0));
    /* an unterminated string at the very end */
    memcpy(bad, buf, n);
    bad[n - 1] = 'x';
    TB_FALSE(net_nets_decode(&m, bad, n));
    /* an empty list is a list */
    net_nets_init(&l);
    n = net_nets_encode(&l, buf, sizeof buf);
    TB_EQ_INT((int)n, 6);
    TB_TRUE(net_nets_decode(&m, buf, n));
    TB_EQ_INT(m.count, 0);
    /* a duplicate SSID */
    add(&l, "x", "1");
    add(&l, "y", "2");
    n = net_nets_encode(&l, buf, sizeof buf);
    memcpy(bad, buf, n);
    for (size_t i = 6; i + 1 < n; i++)
        if (bad[i] == 'y') bad[i] = 'x';
    TB_FALSE(net_nets_decode(&m, bad, n));
}

TB_TEST(nets_decode_keeps_text_from_overflowing)
{
    net_nets_t m;
    uint8_t b[6 + 5 + 40 + 2] = {1, 1, 2, 0, 0, 0, 1, 0, 0, 0, 1};
    memset(b + 11, 'a', 40);    /* a 40-byte SSID */
    TB_FALSE(net_nets_decode(&m, b, sizeof b));
}

TB_TEST(nets_the_single_network_of_before_moves_in_as_the_first)
{
    net_nets_t l;
    TB_TRUE(net_nets_from_legacy(&l, "office", "", "pw", 1));
    TB_EQ_INT(l.count, 1);
    TB_EQ_STR(l.n[0].ssid, "office");
    TB_EQ_INT(l.n[0].sec, 1);
    /* no saved security (firmware before 1.0.2): a password means a password network */
    TB_TRUE(net_nets_from_legacy(&l, "office", "", "pw", -1));
    TB_EQ_INT(l.n[0].sec, 1);
    TB_TRUE(net_nets_from_legacy(&l, "cafe", "", "", -1));
    TB_EQ_INT(l.n[0].sec, 0);
    TB_TRUE(net_nets_from_legacy(&l, "corp", "jo", "pw", 2));
    TB_EQ_STR(l.n[0].user, "jo");
    TB_FALSE(net_nets_from_legacy(&l, "", "", "pw", 1));
    TB_EQ_INT(l.count, 0);
    /* done twice (a power cut after the list was written): the same list, so the move is idempotent */
    net_nets_t a, b;
    uint8_t x[NET_NETS_BLOB_MAX], y[NET_NETS_BLOB_MAX];
    net_nets_from_legacy(&a, "office", "", "pw", 1);
    net_nets_from_legacy(&b, "office", "", "pw", 1);
    size_t nx = net_nets_encode(&a, x, sizeof x), ny = net_nets_encode(&b, y, sizeof y);
    TB_TRUE(nx == ny && !memcmp(x, y, nx));
    /* the next join then sits ahead of it */
    add(&a, "work", "p");
    char s[40];
    names(&a, s, sizeof s);
    TB_EQ_STR(s, "work,office");
}

TB_TEST(nets_try_next_goes_round_and_backs_off_only_after_a_round)
{
    int pos = 0;
    TB_FALSE(net_nets_try_next(3, &pos));
    TB_EQ_INT(pos, 1);
    TB_FALSE(net_nets_try_next(3, &pos));
    TB_EQ_INT(pos, 2);
    TB_TRUE(net_nets_try_next(3, &pos));    /* the round is over: wrap, and wait */
    TB_EQ_INT(pos, 0);
    /* one network: every failure ends a round, as before */
    TB_TRUE(net_nets_try_next(1, &pos));
    TB_EQ_INT(pos, 0);
    TB_TRUE(net_nets_try_next(0, &pos));
    /* the full simulation: 2 networks, none joins: waits come after every second attempt */
    pos = 0;
    int waits = 0, attempts = 0;
    for (int i = 0; i < 6; i++) {
        attempts++;
        if (net_nets_try_next(2, &pos)) waits++;
    }
    TB_EQ_INT(attempts, 6);
    TB_EQ_INT(waits, 3);
}
