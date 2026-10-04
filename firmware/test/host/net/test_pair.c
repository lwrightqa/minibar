/* test_pair.c: codes, tokens, back-off and the saved table (api.md section 4). Owner: net builder. */
#include <string.h>

#include "fake_port.h"
#include "net_pair.h"
#include "tb_test.h"

static tb_clock_t at(tb_ms_t ms)
{
    return (tb_clock_t){.mono = ms, .wall = 1791148320 + ms / 1000, .valid = true};
}

static void fresh(net_pair_t *p)
{
    fake_reset();
    fake_tokens_clear();
    net_pair_init(p);
}

/* Pair with the code on screen; returns the result and the token. */
static net_pair_err_t pair_ok(net_pair_t *p, tb_ms_t t, const char *client, net_scope_t scope, char tok[NET_TOKEN_LEN + 1],
                              const net_token_t **rec)
{
    char pid[17];
    int retry;
    tb_clock_t now = at(t);
    net_pair_err_t e = net_pair_start(p, "iPhone", NET_KIND_REMOTE, scope, client, &now, pid, &retry);
    if (e) return e;
    now = at(t + 2000);
    int left;
    char code[7];
    memcpy(code, p->code, 7);
    return net_pair_finish(p, pid, code, 0x3f04000a, &now, tok, rec, &left);
}

TB_TEST(pair_token_format_and_hash_only)
{
    net_pair_t p;
    fresh(&p);
    char tok[NET_TOKEN_LEN + 1];
    const net_token_t *rec;
    TB_EQ_INT(pair_ok(&p, 1000, NULL, NET_SCOPE_FULL, tok, &rec), NET_PAIR_OK);
    TB_EQ_INT(strlen(tok), 47);
    TB_TRUE(!strncmp(tok, "tb1_", 4));
    for (int i = 4; i < 47; i++) {
        char c = tok[i];
        TB_TRUE((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_');
    }
    TB_EQ_INT(strlen(rec->token_id), 8);
    TB_EQ_STR(rec->name, "iPhone");
    TB_EQ_INT(rec->scope, NET_SCOPE_FULL);
    TB_EQ_INT(rec->paired_via, TB_LINK_WIFI);
    TB_EQ_INT(rec->last_ip, 0x3f04000a);
    /* the table never holds the token itself */
    for (int i = 0; i < NET_TOKENS_MAX; i++) TB_TRUE(!memmem(&p.tokens[i], sizeof p.tokens[i], tok + 4, 12));
    tb_clock_t now = at(5000);
    TB_TRUE(net_pair_check(&p, tok, 0, &now) == rec);
    char wrong[NET_TOKEN_LEN + 1];
    memcpy(wrong, tok, sizeof wrong);
    wrong[20] = wrong[20] == 'A' ? 'B' : 'A';
    TB_TRUE(net_pair_check(&p, wrong, 0, &now) == NULL);
    TB_TRUE(net_pair_check(&p, "tb1_short", 0, &now) == NULL);
    TB_TRUE(net_pair_check(&p, NULL, 0, &now) == NULL);
}

TB_TEST(pair_base64url_encoding)
{
    uint8_t raw[32];
    for (int i = 0; i < 32; i++) raw[i] = (uint8_t)(i * 8 + 3);
    char out[NET_TOKEN_LEN + 1];
    net_pair_format_token(raw, out);
    /* python: 'tb1_' + base64.urlsafe_b64encode(bytes(i*8+3 for i in range(32))).rstrip(b'=') */
    TB_EQ_STR(out, "tb1_AwsTGyMrMztDS1NbY2tze4OLk5ujq7O7w8vT2-Pr8_s");
    memset(raw, 0xFF, 32);
    net_pair_format_token(raw, out);
    TB_EQ_STR(out, "tb1___________________________________________8");
}

TB_TEST(pair_one_code_at_a_time_for_two_minutes)
{
    net_pair_t p;
    fresh(&p);
    char pid[17], pid2[17];
    int retry;
    tb_clock_t now = at(1000);
    TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, NULL, &now, pid, &retry), NET_PAIR_OK);
    TB_EQ_INT(strlen(pid), 16);
    TB_EQ_INT(strlen(p.code), 6);
    TB_EQ_STR(net_pair_state(&p, &now), "showing");
    now = at(47000);
    TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, NULL, &now, pid2, &retry), NET_PAIR_BUSY);
    TB_EQ_INT(retry, 74);
    now = at(1000 + NET_PAIR_CODE_MS - 1);
    TB_FALSE(net_pair_tick(&p, &now));
    now = at(1000 + NET_PAIR_CODE_MS);
    TB_TRUE(net_pair_tick(&p, &now));
    TB_FALSE(net_pair_tick(&p, &now));
    TB_EQ_STR(net_pair_state(&p, &now), "idle");     /* one failure costs nothing */
    char tok[NET_TOKEN_LEN + 1];
    int left;
    TB_EQ_INT(net_pair_finish(&p, pid, p.code, 0, &now, tok, NULL, &left), NET_PAIR_NOT_PAIRING);
}

TB_TEST(pair_wrong_codes_three_tries)
{
    net_pair_t p;
    fresh(&p);
    char pid[17], tok[NET_TOKEN_LEN + 1];
    int retry, left;
    tb_clock_t now = at(1000);
    net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, NULL, &now, pid, &retry);
    char bad[7];
    memcpy(bad, p.code, 7);
    bad[5] = bad[5] == '9' ? '0' : (char)(bad[5] + 1);
    now = at(3000);
    TB_EQ_INT(net_pair_finish(&p, pid, bad, 0, &now, tok, NULL, &left), NET_PAIR_WRONG_CODE);
    TB_EQ_INT(left, 2);
    /* more than one a second is refused without costing a try */
    TB_EQ_INT(net_pair_finish(&p, pid, bad, 0, &now, tok, NULL, &left), NET_PAIR_RATE_LIMITED);
    now = at(4500);
    TB_EQ_INT(net_pair_finish(&p, "0000000000000000", p.code, 0, &now, tok, NULL, &left), NET_PAIR_NOT_PAIRING);
    now = at(6000);
    TB_EQ_INT(net_pair_finish(&p, pid, "12", 0, &now, tok, NULL, &left), NET_PAIR_WRONG_CODE);
    TB_EQ_INT(left, 1);
    now = at(8000);
    TB_EQ_INT(net_pair_finish(&p, pid, bad, 0, &now, tok, NULL, &left), NET_PAIR_WRONG_CODE);
    TB_EQ_INT(left, 0);
    TB_FALSE(p.showing);
    now = at(10000);
    TB_EQ_INT(net_pair_finish(&p, pid, p.code, 0, &now, tok, NULL, &left), NET_PAIR_NOT_PAIRING);
}

TB_TEST(pair_code_ignores_spaces_and_dashes)
{
    net_pair_t p;
    fresh(&p);
    char pid[17], tok[NET_TOKEN_LEN + 1], spaced[16];
    int retry, left;
    tb_clock_t now = at(1000);
    net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, NULL, &now, pid, &retry);
    snprintf(spaced, sizeof spaced, "%.3s -%s", p.code, p.code + 3);
    now = at(3000);
    TB_EQ_INT(net_pair_finish(&p, pid, spaced, 0, &now, tok, NULL, &left), NET_PAIR_OK);
}

TB_TEST(pair_back_off_doubles_and_resets_on_success)
{
    net_pair_t p;
    fresh(&p);
    char pid[17];
    int retry;
    tb_ms_t t = 1000;
    tb_clock_t now = at(t);
    /* first failure: free */
    net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, NULL, &now, pid, &retry);
    net_pair_cancel(&p, &now);
    TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, NULL, &now, pid, &retry), NET_PAIR_OK);
    /* second: 30 s */
    net_pair_cancel(&p, &now);
    TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, NULL, &now, pid, &retry), NET_PAIR_RATE_LIMITED);
    TB_EQ_INT(retry, 30);
    TB_EQ_STR(net_pair_state(&p, &now), "locked");
    t += 30000;
    now = at(t);
    TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, NULL, &now, pid, &retry), NET_PAIR_OK);
    /* third (it times out): 1 minute, then 2, 4 ... up to an hour */
    t += NET_PAIR_CODE_MS;
    now = at(t);
    TB_TRUE(net_pair_tick(&p, &now));
    TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, NULL, &now, pid, &retry), NET_PAIR_RATE_LIMITED);
    TB_EQ_INT(retry, 60);
    int expect = 120;
    for (int i = 0; i < 8; i++) {
        t += 3600000;
        now = at(t);
        TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, NULL, &now, pid, &retry), NET_PAIR_OK);
        net_pair_cancel(&p, &now);
        net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, NULL, &now, pid, &retry);
        TB_EQ_INT(retry, expect);
        expect = expect * 2 > 3600 ? 3600 : expect * 2;
    }
    /* success resets it */
    t += 3600000;
    char tok[NET_TOKEN_LEN + 1];
    TB_EQ_INT(pair_ok(&p, t, NULL, NET_SCOPE_CALL, tok, NULL), NET_PAIR_OK);
    now = at(t + 5000);
    net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, NULL, &now, pid, &retry);
    net_pair_cancel(&p, &now);
    TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, NULL, &now, pid, &retry), NET_PAIR_OK);
}

TB_TEST(pair_ten_tokens_at_most_and_same_client_replaces)
{
    net_pair_t p;
    fresh(&p);
    char tok[NET_TOKEN_LEN + 1], first[NET_TOKEN_LEN + 1];
    TB_EQ_INT(pair_ok(&p, 1000, "client-0001", NET_SCOPE_CALL, first, NULL), NET_PAIR_OK);
    /* the same client again: replaces, so still one token, and the old one stops working */
    TB_EQ_INT(pair_ok(&p, 10000, "client-0001", NET_SCOPE_CALL, tok, NULL), NET_PAIR_OK);
    TB_EQ_INT(net_pair_count(&p), 1);
    tb_clock_t now = at(20000);
    TB_TRUE(net_pair_check(&p, first, 0, &now) == NULL);
    TB_TRUE(net_pair_check(&p, tok, 0, &now) != NULL);
    for (int i = 2; i <= 10; i++) {
        char client[16];
        snprintf(client, sizeof client, "client-%04d", i);
        TB_EQ_INT(pair_ok(&p, 20000 + i * 10000, client, NET_SCOPE_FULL, tok, NULL), NET_PAIR_OK);
    }
    TB_EQ_INT(net_pair_count(&p), 10);
    char pid[17];
    int retry;
    now = at(500000);
    TB_EQ_INT(net_pair_start(&p, "x", NET_KIND_REMOTE, NET_SCOPE_FULL, "client-0011", &now, pid, &retry), NET_PAIR_TOKEN_LIMIT);
    TB_EQ_INT(net_pair_usb(&p, "client-0011", NULL, &now, tok, NULL), NET_PAIR_TOKEN_LIMIT);
    /* but a client that already has one may pair again */
    TB_EQ_INT(net_pair_start(&p, "x", NET_KIND_REMOTE, NET_SCOPE_FULL, "client-0005", &now, pid, &retry), NET_PAIR_OK);
}

TB_TEST(pair_usb_gives_a_call_token_without_a_code)
{
    net_pair_t p;
    fresh(&p);
    char tok[NET_TOKEN_LEN + 1];
    const net_token_t *rec;
    tb_clock_t now = at(1000);
    TB_EQ_INT(net_pair_usb(&p, "6F1C2A9E-5B7D-4E0A", NULL, &now, tok, &rec), NET_PAIR_OK);
    TB_EQ_INT(rec->scope, NET_SCOPE_CALL);
    TB_EQ_INT(rec->kind, NET_KIND_MAC);
    TB_EQ_INT(rec->paired_via, TB_LINK_USB);
    TB_EQ_STR(rec->name, "Mac");
    TB_EQ_INT(rec->last_ip, 0);
}

TB_TEST(pair_revoke_and_forget_all)
{
    net_pair_t p;
    fresh(&p);
    char a[NET_TOKEN_LEN + 1], b[NET_TOKEN_LEN + 1];
    const net_token_t *ra;
    pair_ok(&p, 1000, "client-aaaa", NET_SCOPE_FULL, a, &ra);
    char id[9];
    memcpy(id, ra->token_id, 9);
    pair_ok(&p, 10000, "client-bbbb", NET_SCOPE_FULL, b, NULL);
    TB_TRUE(net_pair_find(&p, id) != NULL);
    TB_TRUE(net_pair_revoke(&p, id));
    TB_FALSE(net_pair_revoke(&p, id));
    tb_clock_t now = at(20000);
    TB_TRUE(net_pair_check(&p, a, 0, &now) == NULL);
    TB_TRUE(net_pair_check(&p, b, 0, &now) != NULL);
    net_pair_forget_all(&p);
    TB_EQ_INT(net_pair_count(&p), 0);
    TB_TRUE(net_pair_check(&p, b, 0, &now) == NULL);
}

TB_TEST(pair_table_survives_a_restart)
{
    net_pair_t p;
    fresh(&p);
    char a[NET_TOKEN_LEN + 1], b[NET_TOKEN_LEN + 1];
    pair_ok(&p, 1000, "client-aaaa", NET_SCOPE_FULL, a, NULL);
    tb_clock_t now = at(5000);
    net_pair_usb(&p, "client-bbbb", "Work Mac", &now, b, NULL);
    /* a restart: the table comes back from storage; the back-off and the code don't */
    net_pair_t q;
    net_pair_init(&q);
    TB_EQ_INT(net_pair_count(&q), 2);
    const net_token_t *rb = net_pair_check(&q, b, 0, &now);
    TB_TRUE(rb != NULL);
    TB_EQ_STR(rb->name, "Work Mac");
    TB_EQ_STR(rb->client, "client-bbbb");
    TB_EQ_INT(rb->paired_via, TB_LINK_USB);
    TB_TRUE(net_pair_check(&q, a, 0, &now) != NULL);
    TB_FALSE(q.showing);
    /* a damaged blob is refused and leaves the table alone */
    uint8_t blob[NET_TOKEN_BLOB_MAX];
    size_t n = net_pair_serialize(&q, blob, sizeof blob);
    TB_TRUE(n > 0);
    for (size_t cut = 0; cut < n; cut++) TB_FALSE(net_pair_deserialize(&q, blob, cut));
    blob[2] = 99;
    TB_FALSE(net_pair_deserialize(&q, blob, n));
    TB_EQ_INT(net_pair_count(&q), 2);
}

TB_TEST(pair_last_used_is_saved_at_most_hourly)
{
    net_pair_t p;
    fresh(&p);
    char a[NET_TOKEN_LEN + 1];
    pair_ok(&p, 1000, NULL, NET_SCOPE_FULL, a, NULL);
    int saves = fake_tokens_saves;
    for (int i = 1; i < 60; i++) {
        tb_clock_t now = at(3000 + i * 60000);     /* every minute for an hour */
        net_pair_check(&p, a, 0x01020304, &now);
    }
    TB_EQ_INT(fake_tokens_saves, saves);
    tb_clock_t now = at(3000 + 3600 * 1000 + 5000);
    const net_token_t *t = net_pair_check(&p, a, 0x01020304, &now);
    TB_EQ_INT(fake_tokens_saves, saves + 1);
    TB_EQ_INT(t->last_used, now.wall);
    TB_EQ_INT(t->last_ip, 0x01020304);
}
