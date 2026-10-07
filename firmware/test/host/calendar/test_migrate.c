/* test_migrate.c: the move from firmware's single address to slots, with an in-memory store and failures injected.
 * Owner: calendar builder. */
#include <stdlib.h>
#include <string.h>

#include "cal_migrate.h"
#include "tb_test.h"

#define ADDR "https://calendar.example.com/ical/team/private-0123456789abcdef/basic.ics"
#define ADDR2 "https://calendar.example.com/ical/other/private-fedcba9876543210/basic.ics"

typedef struct { bool used; uint8_t v[7000]; int len; } slot_t;
typedef struct {
    slot_t url[4], copy[4];     /* index 0 = legacy, 1..3 = slots 0..2 */
    int ops, fail_put_at, fail_erase_at;   /* 1-based operation numbers that fail; 0 = never */
    bool corrupt_readback;
} fake_t;

static slot_t *at(fake_t *f, cal_kv_kind_t k, int slot) { return &(k == CAL_KV_URL ? f->url : f->copy)[slot + 1]; }

static int f_get(void *c, cal_kv_kind_t k, int slot, uint8_t *buf, size_t cap)
{
    fake_t *f = c;
    slot_t *s = at(f, k, slot);
    if (!s->used) return -1;
    size_t n = (size_t)s->len < cap ? (size_t)s->len : cap;
    memcpy(buf, s->v, n);
    if (f->corrupt_readback && n && slot >= 0) buf[0] ^= 0x55;
    return s->len;
}

static bool f_put(void *c, cal_kv_kind_t k, int slot, const uint8_t *buf, size_t len)
{
    fake_t *f = c;
    if (++f->ops == f->fail_put_at || len > sizeof(((slot_t *)0)->v)) return false;
    slot_t *s = at(f, k, slot);
    memcpy(s->v, buf, len);
    s->len = (int)len;
    s->used = true;
    return true;
}

static bool f_erase(void *c, cal_kv_kind_t k, int slot)
{
    fake_t *f = c;
    if (++f->ops == f->fail_erase_at) return false;
    at(f, k, slot)->used = false;
    return true;
}

static cal_kv_ops_t ops(fake_t *f) { cal_kv_ops_t o = {f, f_get, f_put, f_erase}; return o; }

static void set(fake_t *f, cal_kv_kind_t k, int slot, const void *v, int len)
{
    slot_t *s = at(f, k, slot);
    memcpy(s->v, v, (size_t)len);
    s->len = len;
    s->used = true;
}

static void set_url(fake_t *f, int slot, const char *u) { set(f, CAL_KV_URL, slot, u, (int)strlen(u)); }

static fake_t *new_fake(void) { return calloc(1, sizeof(fake_t)); }

TB_TEST(migrate_moves_the_address_and_the_copy_to_calendar_1)
{
    fake_t *f = new_fake();
    set_url(f, CAL_KV_LEGACY, ADDR);
    set(f, CAL_KV_COPY, CAL_KV_LEGACY, "blob-bytes", 10);
    cal_kv_ops_t o = ops(f);
    cal_mig_t m = cal_migrate_legacy(&o);
    TB_EQ_INT(m.res, CAL_MIG_MOVED);
    TB_EQ_INT(m.slot, 0);
    TB_EQ_INT(f->url[1].len, (int)strlen(ADDR));
    TB_EQ_INT(memcmp(f->url[1].v, ADDR, strlen(ADDR)), 0);
    TB_FALSE(f->url[0].used);                 /* the old key is gone */
    TB_TRUE(f->copy[1].used && f->copy[1].len == 10);
    TB_FALSE(f->copy[0].used);
    /* idempotent: a second run finds nothing to do and changes nothing */
    cal_mig_t m2 = cal_migrate_legacy(&o);
    TB_EQ_INT(m2.res, CAL_MIG_NOTHING);
    TB_EQ_INT(f->url[1].len, (int)strlen(ADDR));
    free(f);
}

TB_TEST(migrate_with_nothing_saved_does_nothing)
{
    fake_t *f = new_fake();
    cal_kv_ops_t o = ops(f);
    TB_EQ_INT(cal_migrate_legacy(&o).res, CAL_MIG_NOTHING);
    TB_EQ_INT(f->ops, 1);                     /* only the stray-copy erase */
    set(f, CAL_KV_COPY, CAL_KV_LEGACY, "x", 1);       /* a copy without an address: cleaned up */
    TB_EQ_INT(cal_migrate_legacy(&o).res, CAL_MIG_NOTHING);
    TB_FALSE(f->copy[0].used);
    free(f);
}

TB_TEST(migrate_a_failed_write_keeps_the_old_address)
{
    for (int n = 1; n <= 3; n++) {
        fake_t *f = new_fake();
        set_url(f, CAL_KV_LEGACY, ADDR);
        f->fail_put_at = n;                    /* the URL's write, then the copy's */
        cal_kv_ops_t o = ops(f);
        cal_mig_t m = cal_migrate_legacy(&o);
        if (n == 1) {
            TB_EQ_INT(m.res, CAL_MIG_FAILED);
            TB_TRUE(f->url[0].used);           /* still there for the next start */
            TB_FALSE(f->url[1].used);
            f->fail_put_at = 0;
            TB_EQ_INT(cal_migrate_legacy(&o).res, CAL_MIG_MOVED);       /* and it works then */
        } else {
            TB_EQ_INT(m.res, CAL_MIG_MOVED);   /* no copy to move: the failing put never happens */
        }
        free(f);
    }
}

TB_TEST(migrate_a_failed_copy_write_still_moves_the_address)
{
    fake_t *f = new_fake();
    set_url(f, CAL_KV_LEGACY, ADDR);
    set(f, CAL_KV_COPY, CAL_KV_LEGACY, "blob", 4);
    f->fail_put_at = 2;                        /* put 1 is the URL, put 2 the copy */
    cal_kv_ops_t o = ops(f);
    TB_EQ_INT(cal_migrate_legacy(&o).res, CAL_MIG_MOVED);
    TB_TRUE(f->url[1].used);
    TB_FALSE(f->copy[1].used);                 /* the copy is only a cache: the next sync brings it back */
    TB_FALSE(f->copy[0].used);
    free(f);
}

TB_TEST(migrate_a_wrong_read_back_keeps_the_old_address)
{
    fake_t *f = new_fake();
    set_url(f, CAL_KV_LEGACY, ADDR);
    f->corrupt_readback = true;
    cal_kv_ops_t o = ops(f);
    TB_EQ_INT(cal_migrate_legacy(&o).res, CAL_MIG_FAILED);
    TB_TRUE(f->url[0].used);
    TB_FALSE(f->url[1].used);                  /* the bad copy is removed again */
    free(f);
}

TB_TEST(migrate_a_cut_before_the_erase_finishes_on_the_next_start)
{
    fake_t *f = new_fake();
    set_url(f, CAL_KV_LEGACY, ADDR);
    f->fail_erase_at = 2;                      /* op 1 is the put; erases follow: copy (erase 2 fails)... */
    cal_kv_ops_t o = ops(f);
    cal_mig_t m = cal_migrate_legacy(&o);
    /* whichever erase failed, nothing was lost and the retry ends clean */
    f->fail_erase_at = 0;
    if (m.res != CAL_MIG_MOVED) TB_EQ_INT(m.res, CAL_MIG_FAILED);
    cal_mig_t m2 = cal_migrate_legacy(&o);
    TB_TRUE(m2.res == CAL_MIG_MOVED || m2.res == CAL_MIG_NOTHING);
    TB_TRUE(f->url[1].used);
    TB_FALSE(f->url[0].used);
    TB_EQ_INT(f->url[1].len, (int)strlen(ADDR));
    TB_FALSE(f->url[2].used);                  /* no second copy of the address anywhere */
    free(f);
}

TB_TEST(migrate_when_an_erase_of_the_old_address_fails_it_retries)
{
    fake_t *f = new_fake();
    set_url(f, CAL_KV_LEGACY, ADDR);
    f->fail_erase_at = 2;                      /* no copy: op 1 put, op 2 is the copy's erase, op 3 the URL's */
    cal_kv_ops_t o = ops(f);
    cal_migrate_legacy(&o);
    f->fail_erase_at = 3;
    f->ops = 0;
    set_url(f, CAL_KV_LEGACY, ADDR);
    memset(&f->url[1], 0, sizeof f->url[1]);
    f->fail_erase_at = 3;
    cal_mig_t m = cal_migrate_legacy(&o);
    TB_EQ_INT(m.res, CAL_MIG_FAILED);
    TB_TRUE(f->url[0].used && f->url[1].used);   /* both: safe, nothing lost */
    f->fail_erase_at = 0;
    TB_EQ_INT(cal_migrate_legacy(&o).res, CAL_MIG_MOVED);   /* the slot already holds it: only the erase is left */
    TB_EQ_INT(m.slot, -1);
    TB_FALSE(f->url[0].used);
    TB_FALSE(f->url[2].used);
    free(f);
}

TB_TEST(migrate_hostile_old_addresses_are_dropped)
{
    const char *bad[] = {"http://calendar.example.com/x.ics", "not a url", "https://calendar.example.com/x.txt",
                         "https://calendar.example.com/public/x.ics", " https://calendar.example.com/x.ics",
                         "webcal://calendar.example.com/x.ics"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        fake_t *f = new_fake();
        set_url(f, CAL_KV_LEGACY, bad[i]);
        cal_kv_ops_t o = ops(f);
        cal_mig_t m = cal_migrate_legacy(&o);
        TB_EQ_INT(m.res, CAL_MIG_DROPPED);
        TB_FALSE(f->url[0].used);
        TB_FALSE(f->url[1].used);              /* nothing unusable is copied into a slot */
        free(f);
    }
    /* longer than the limit, an embedded NUL, empty, and a value with no end inside the read limit */
    char big[3000];
    memset(big, 'a', sizeof big);
    memcpy(big, "https://calendar.example.com/", 29);
    fake_t *f = new_fake();
    set(f, CAL_KV_URL, CAL_KV_LEGACY, big, 2000);
    cal_kv_ops_t o = ops(f);
    TB_EQ_INT(cal_migrate_legacy(&o).res, CAL_MIG_DROPPED);
    set(f, CAL_KV_URL, CAL_KV_LEGACY, "https://calendar.example.com/a\0b.ics", 36);
    TB_EQ_INT(cal_migrate_legacy(&o).res, CAL_MIG_DROPPED);
    set(f, CAL_KV_URL, CAL_KV_LEGACY, "", 0);
    TB_EQ_INT(cal_migrate_legacy(&o).res, CAL_MIG_DROPPED);
    TB_FALSE(f->url[0].used);
    free(f);
}

TB_TEST(migrate_uses_a_free_slot_or_keeps_the_old_address_when_all_are_taken)
{
    fake_t *f = new_fake();
    set_url(f, 0, ADDR2);                       /* the new firmware already has Calendar 1 */
    set_url(f, CAL_KV_LEGACY, ADDR);            /* and a different old address (a downgrade and back) */
    cal_kv_ops_t o = ops(f);
    cal_mig_t m = cal_migrate_legacy(&o);
    TB_EQ_INT(m.res, CAL_MIG_MOVED);
    TB_EQ_INT(m.slot, 1);                       /* the lowest free slot: nothing is overwritten */
    TB_EQ_INT(memcmp(f->url[1].v, ADDR2, strlen(ADDR2)), 0);
    free(f);

    f = new_fake();
    set_url(f, 0, ADDR2);
    set_url(f, 1, "https://calendar.example.com/ical/b/private-1/basic.ics");
    set_url(f, 2, "https://calendar.example.com/ical/c/private-2/basic.ics");
    set_url(f, CAL_KV_LEGACY, ADDR);
    o = ops(f);
    m = cal_migrate_legacy(&o);
    TB_EQ_INT(m.res, CAL_MIG_NO_SPACE);
    TB_TRUE(f->url[0].used);                    /* kept, not lost */
    TB_EQ_INT(f->url[1].len, (int)strlen(ADDR2));
    free(f);

    /* the same address already in slot 2 (a cut after the write): just erase the old key */
    f = new_fake();
    set_url(f, 0, ADDR2);
    set_url(f, 2, ADDR);
    set_url(f, CAL_KV_LEGACY, ADDR);
    o = ops(f);
    m = cal_migrate_legacy(&o);
    TB_EQ_INT(m.res, CAL_MIG_MOVED);
    TB_EQ_INT(m.slot, 2);
    TB_FALSE(f->url[0].used);
    free(f);
}
