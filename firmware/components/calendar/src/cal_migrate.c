/*
 * cal_migrate.c: the move from one address to slots. Owner: calendar builder. See cal_migrate.h.
 */
#include <stdlib.h>
#include <string.h>

#include "cal_list.h"
#include "cal_migrate.h"
#include "cal_url.h"

/* Zero what held an address before it is freed or reused. */
static void wipe(void *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n--) *v++ = 0;
}

static cal_mig_t done(cal_mig_res_t r, int slot)
{
    cal_mig_t m = {r, slot};
    return m;
}

/* Copy the old saved copy into `slot` (a cache: when it can't be moved it is dropped, the next sync brings it back),
 * then erase the old one. */
static void move_copy(const cal_kv_ops_t *kv, int slot, uint8_t *buf, uint8_t *chk)
{
    int n = kv->get(kv->ctx, CAL_KV_COPY, CAL_KV_LEGACY, buf, CAL_COPY_BLOB_MAX);
    if (n > 0 && n <= CAL_COPY_BLOB_MAX && slot >= 0 && kv->get(kv->ctx, CAL_KV_COPY, slot, chk, 1) < 0) {
        bool ok = kv->put(kv->ctx, CAL_KV_COPY, slot, buf, (size_t)n) &&
                  kv->get(kv->ctx, CAL_KV_COPY, slot, chk, CAL_COPY_BLOB_MAX) == n && !memcmp(buf, chk, (size_t)n);
        if (!ok) kv->erase(kv->ctx, CAL_KV_COPY, slot);
    }
    kv->erase(kv->ctx, CAL_KV_COPY, CAL_KV_LEGACY);
}

cal_mig_t cal_migrate_legacy(const cal_kv_ops_t *kv)
{
    uint8_t *buf = malloc(CAL_COPY_BLOB_MAX), *chk = malloc(CAL_COPY_BLOB_MAX);
    cal_url_info_t *info = malloc(sizeof *info);
    cal_mig_t out = done(CAL_MIG_FAILED, -1);
    if (!buf || !chk || !info) goto end;

    int n = kv->get(kv->ctx, CAL_KV_URL, CAL_KV_LEGACY, buf, CAL_URL_MAX + 2);
    if (n < 0) {
        /* No old address; an old copy without one (a cut between the two erases) goes too. */
        kv->erase(kv->ctx, CAL_KV_COPY, CAL_KV_LEGACY);
        out = done(CAL_MIG_NOTHING, -1);
        goto end;
    }
    /* Usable only if it is text that ends where it says, within the limit, and is a calendar address as it was saved
     * (cal_url_check normalizes; the old firmware stored the normalized form). */
    bool usable = n > 0 && n <= CAL_URL_MAX && memchr(buf, 0, (size_t)n) == NULL;
    if (usable) {
        buf[n] = '\0';
        usable = cal_url_check((const char *)buf, info) == CAL_URL_OK && !strcmp(info->url, (const char *)buf);
    }
    if (!usable) {
        bool a = kv->erase(kv->ctx, CAL_KV_URL, CAL_KV_LEGACY);
        kv->erase(kv->ctx, CAL_KV_COPY, CAL_KV_LEGACY);
        out = done(a ? CAL_MIG_DROPPED : CAL_MIG_FAILED, -1);
        goto end;
    }

    /* Where: a slot that already holds this very address (a cut after the write), else the lowest free one. */
    int target = -1, free_slot = -1;
    for (int s = 0; s < TB_CALS_MAX; s++) {
        int m = kv->get(kv->ctx, CAL_KV_URL, s, chk, CAL_URL_MAX + 2);
        if (m < 0) {
            if (free_slot < 0) free_slot = s;
        } else if (m == n && !memcmp(buf, chk, (size_t)n)) {
            target = s;
            break;
        }
    }
    if (target < 0) {
        if (free_slot < 0) {
            out = done(CAL_MIG_NO_SPACE, -1);
            goto end;
        }
        /* Write, read back, compare; only then is the old key erased. */
        bool ok = kv->put(kv->ctx, CAL_KV_URL, free_slot, buf, (size_t)n) &&
                  kv->get(kv->ctx, CAL_KV_URL, free_slot, chk, CAL_URL_MAX + 2) == n && !memcmp(buf, chk, (size_t)n);
        if (!ok) {
            kv->erase(kv->ctx, CAL_KV_URL, free_slot);
            goto end;
        }
        target = free_slot;
    }
    move_copy(kv, target, buf, chk);
    if (!kv->erase(kv->ctx, CAL_KV_URL, CAL_KV_LEGACY)) goto end;
    out = done(CAL_MIG_MOVED, target);

end:
    if (buf) wipe(buf, CAL_COPY_BLOB_MAX);
    if (chk) wipe(chk, CAL_COPY_BLOB_MAX);
    if (info) wipe(info, sizeof *info);
    free(buf);
    free(chk);
    free(info);
    return out;
}
