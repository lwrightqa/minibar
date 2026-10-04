/* test_macs.c: the Mac table (api.md 5.2). Owner: net builder. */
#include "net_macs.h"
#include "tb_test.h"

static tb_clock_t at(tb_ms_t ms)
{
    return (tb_clock_t){.mono = ms, .wall = 1791148320 + ms / 1000, .valid = true};
}

static net_call_msg_t msg(const char *client, const char *session, uint32_t seq, bool active)
{
    return (net_call_msg_t){.client = client, .session = session, .seq = seq, .has_seq = seq != 0, .active = active,
                            .inputs = -1, .elapsed_s = -1, .via = TB_LINK_WIFI};
}

#define A "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"
#define B "11111111-2222-3333-4444-555555555555"

TB_TEST(macs_a_call_starts_with_elapsed_time_and_keeps_its_start)
{
    net_macs_t t;
    net_macs_init(&t);
    tb_clock_t now = at(100000);
    net_call_msg_t m = msg(A, "q8Zr2Lx0", 1, true);
    m.app = "Slack";
    m.call_id = 7;
    m.elapsed_s = 30;
    TB_TRUE(net_macs_on_call(&t, &m, &now));
    tb_call_t c;
    tb_link_t link;
    const net_mac_t *who;
    net_macs_aggregate(&t, &c, &link, &who);
    TB_TRUE(c.active);
    TB_EQ_STR(c.app, "Slack");
    TB_EQ_INT(c.since_ms, 70000);
    TB_EQ_INT(c.since, now.wall - 30);
    TB_EQ_INT(c.via, TB_LINK_WIFI);
    TB_EQ_INT(link, TB_LINK_WIFI);
    TB_TRUE(who != NULL);
    uint32_t id = c.id;
    TB_TRUE(id != 0);
    /* the heartbeat 30 s later: same call, same start, a new app name is taken */
    now = at(130000);
    m = msg(A, "q8Zr2Lx0", 2, true);
    m.app = "Zoom";
    m.call_id = 7;
    m.elapsed_s = 60;
    TB_TRUE(net_macs_on_call(&t, &m, &now));
    net_macs_aggregate(&t, &c, &link, NULL);
    TB_EQ_INT(c.id, id);
    TB_EQ_INT(c.since_ms, 70000);
    TB_EQ_STR(c.app, "Zoom");
    /* a different call_id is a new call */
    m.seq = 3;
    m.call_id = 8;
    TB_TRUE(net_macs_on_call(&t, &m, &now));
    net_macs_aggregate(&t, &c, &link, NULL);
    TB_TRUE(c.id != id);
}

TB_TEST(macs_stale_messages_are_ignored)
{
    net_macs_t t;
    net_macs_init(&t);
    tb_clock_t now = at(1000);
    net_call_msg_t m = msg(A, "s1", 5, true);
    TB_TRUE(net_macs_on_call(&t, &m, &now));
    /* a late Wi-Fi message from the same session with an older seq */
    m = msg(A, "s1", 4, false);
    TB_FALSE(net_macs_on_call(&t, &m, &now));
    m.seq = 5;
    TB_FALSE(net_macs_on_call(&t, &m, &now));
    tb_call_t c;
    net_macs_aggregate(&t, &c, NULL, NULL);
    TB_TRUE(c.active);
    /* a new session is always accepted, even with a low seq */
    m = msg(A, "s2", 1, false);
    TB_TRUE(net_macs_on_call(&t, &m, &now));
    net_macs_aggregate(&t, &c, NULL, NULL);
    TB_FALSE(c.active);
    /* scripts send neither: always accepted */
    m = msg(A, NULL, 0, true);
    TB_TRUE(net_macs_on_call(&t, &m, &now));
    TB_TRUE(net_macs_on_call(&t, &m, &now));
}

TB_TEST(macs_time_out_after_90_seconds)
{
    net_macs_t t;
    net_macs_init(&t);
    tb_clock_t now = at(1000);
    net_call_msg_t m = msg(A, "s", 1, true);
    net_macs_on_call(&t, &m, &now);
    now = at(1000 + 89999);
    TB_FALSE(net_macs_tick(&t, &now));
    now = at(1000 + 90000);
    TB_TRUE(net_macs_tick(&t, &now));
    tb_call_t c;
    tb_link_t link;
    net_macs_aggregate(&t, &c, &link, NULL);
    TB_FALSE(c.active);
    TB_EQ_INT(link, TB_LINK_NONE);
    const net_mac_t *ms[4];
    TB_EQ_INT(net_macs_sorted(&t, ms, 4), 1);
    TB_FALSE(ms[0]->connected);
    /* a Mac without a call times out quietly */
    m = msg(B, "s", 1, false);
    net_macs_on_call(&t, &m, &now);
    now = at(1000 + 90000 + 90000);
    TB_FALSE(net_macs_tick(&t, &now));
}

TB_TEST(macs_leaving_ends_the_call_and_disconnects)
{
    net_macs_t t;
    net_macs_init(&t);
    tb_clock_t now = at(1000);
    net_call_msg_t m = msg(A, "s", 1, true);
    net_macs_on_call(&t, &m, &now);
    m = msg(A, "s", 2, false);
    m.leaving = true;
    TB_TRUE(net_macs_on_call(&t, &m, &now));
    tb_call_t c;
    tb_link_t link;
    net_macs_aggregate(&t, &c, &link, NULL);
    TB_FALSE(c.active);
    TB_EQ_INT(link, TB_LINK_NONE);
    now = at(200000);
    TB_FALSE(net_macs_tick(&t, &now));      /* no "Lost contact" afterwards */
}

TB_TEST(macs_two_macs_one_call_id_until_a_new_call)
{
    net_macs_t t;
    net_macs_init(&t);
    tb_clock_t now = at(1000);
    net_call_msg_t a = msg(A, "s", 1, true);
    a.app = "Slack";
    net_macs_on_call(&t, &a, &now);
    tb_call_t c;
    net_macs_aggregate(&t, &c, NULL, NULL);
    uint32_t first = c.id;
    /* an idle second Mac's heartbeat doesn't end the first Mac's call */
    now = at(2000);
    net_call_msg_t b = msg(B, "t", 1, false);
    b.via = TB_LINK_USB;
    net_macs_on_call(&t, &b, &now);
    tb_link_t link;
    net_macs_aggregate(&t, &c, &link, NULL);
    TB_TRUE(c.active);
    TB_EQ_INT(c.id, first);
    TB_EQ_INT(link, TB_LINK_USB);           /* the icon follows the Mac heard last */
    /* the second Mac starts a call: a new call, shown with its app and link */
    now = at(3000);
    b = msg(B, "t", 2, true);
    b.via = TB_LINK_USB;
    b.app = "Zoom";
    net_macs_on_call(&t, &b, &now);
    net_macs_aggregate(&t, &c, NULL, NULL);
    TB_TRUE(c.id != first);
    uint32_t second = c.id;
    TB_EQ_STR(c.app, "Zoom");
    TB_EQ_INT(c.via, TB_LINK_USB);
    /* it ends; the first Mac's call goes on under the same bar call id (it stays set aside if it was) */
    b = msg(B, "t", 3, false);
    b.via = TB_LINK_USB;
    net_macs_on_call(&t, &b, &now);
    net_macs_aggregate(&t, &c, NULL, NULL);
    TB_TRUE(c.active);
    TB_EQ_INT(c.id, second);
    TB_EQ_STR(c.app, "Slack");
    TB_EQ_INT(c.via, TB_LINK_WIFI);
}

TB_TEST(macs_keeps_four_and_drops_the_least_recently_heard)
{
    net_macs_t t;
    net_macs_init(&t);
    const char *ids[5] = {"aaaaaaaa", "bbbbbbbb", "cccccccc", "dddddddd", "eeeeeeee"};
    for (int i = 0; i < 5; i++) {
        tb_clock_t now = at(1000 * (i + 1));
        net_call_msg_t m = msg(ids[i], NULL, 0, false);
        net_macs_on_call(&t, &m, &now);
    }
    const net_mac_t *ms[4];
    TB_EQ_INT(net_macs_sorted(&t, ms, 4), 4);
    TB_EQ_STR(ms[0]->client, "eeeeeeee");   /* most recently heard first */
    TB_EQ_STR(ms[3]->client, "bbbbbbbb");
    TB_TRUE(net_macs_find(&t, "aaaaaaaa") == NULL);
}

TB_TEST(macs_hello_is_a_heartbeat_over_usb_and_names_the_mac)
{
    net_macs_t t;
    net_macs_init(&t);
    tb_clock_t now = at(1000);
    net_call_msg_t m = msg(A, "s", 1, true);
    net_macs_on_call(&t, &m, &now);
    now = at(80000);
    net_macs_on_hello(&t, A, "Work Mac", &now);
    const net_mac_t *x = net_macs_find(&t, A);
    TB_EQ_STR(x->name, "Work Mac");
    TB_EQ_INT(x->via, TB_LINK_USB);
    TB_TRUE(x->active);                     /* the call is unchanged */
    now = at(80000 + 89000);
    TB_FALSE(net_macs_tick(&t, &now));      /* the hello reset the time-out */
    net_macs_on_hello(&t, A, NULL, &now);
    TB_EQ_STR(x->name, "Work Mac");         /* no name keeps the label */
}

TB_TEST(macs_a_start_taken_without_a_clock_is_filled_in_later)
{
    net_macs_t t;
    net_macs_init(&t);
    tb_clock_t now = {.mono = 50000, .wall = 0, .valid = false};
    net_call_msg_t m = msg(A, "s", 1, true);
    m.elapsed_s = 10;
    net_macs_on_call(&t, &m, &now);
    tb_call_t c;
    net_macs_aggregate(&t, &c, NULL, NULL);
    TB_EQ_INT(c.since, 0);
    now = (tb_clock_t){.mono = 60000, .wall = 1791148320, .valid = true};
    net_macs_tick(&t, &now);
    net_macs_aggregate(&t, &c, NULL, NULL);
    TB_EQ_INT(c.since, 1791148320 - 20);
}

TB_TEST(macs_forget_wifi_ends_only_a_wifi_call)
{
    net_macs_t t;
    net_macs_init(&t);
    tb_clock_t now = at(1000);
    net_call_msg_t m = msg(A, "s", 1, true);
    m.via = TB_LINK_USB;
    net_macs_on_call(&t, &m, &now);
    TB_FALSE(net_macs_forget_wifi(&t, A));      /* over USB: the cable still counts */
    m = msg(B, "s", 1, true);
    net_macs_on_call(&t, &m, &now);
    TB_TRUE(net_macs_forget_wifi(&t, B));
    TB_FALSE(net_macs_find(&t, B)->active);
    TB_FALSE(net_macs_forget_wifi(&t, "nobody00"));
}
