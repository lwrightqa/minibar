/*
 * cal_migrate.h: the move from firmware before 1.0.8, which kept one address (nvs_sec "calsec"/"url") and one saved
 * copy (nvs "cal"/"list"), to the slots of several calendars ("url0".."url2", "list0".."list2"). Pure C over a
 * key-value interface, so the order of steps and every failure are tested on the host; cal_sync.c supplies NVS.
 *
 * Order, the same as net_nets did for Wi-Fi: write the new key, read it back and compare, and only then erase the old
 * one. A cut anywhere leaves the old key, and the next start finishes the job (a new key equal to the old one just
 * lets the erase go ahead). The address is never logged here.
 *
 * Owner: calendar builder.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { CAL_KV_URL = 0, CAL_KV_COPY } cal_kv_kind_t;
#define CAL_KV_LEGACY (-1)      /* the slot number of the keys of firmware before 1.0.8 */

typedef struct {
    void *ctx;
    /* The value's length (no terminator counted), -1 if there is none or it can't be read. At most cap bytes are
     * written; a longer value returns its real length. A URL is stored as text, a copy as a blob. */
    int (*get)(void *ctx, cal_kv_kind_t kind, int slot, uint8_t *buf, size_t cap);
    bool (*put)(void *ctx, cal_kv_kind_t kind, int slot, const uint8_t *buf, size_t len);
    bool (*erase)(void *ctx, cal_kv_kind_t kind, int slot);
} cal_kv_ops_t;

typedef enum {
    CAL_MIG_NOTHING = 0,    /* no old address (any stray old copy is gone now) */
    CAL_MIG_MOVED,          /* the old address is in `slot` now and the old keys are gone */
    CAL_MIG_DROPPED,        /* the old address wasn't usable (too long, damaged, fails the format check): erased */
    CAL_MIG_NO_SPACE,       /* every slot holds a different address: the old keys stay, nothing is lost */
    CAL_MIG_FAILED,         /* a write, read-back or erase failed: the old keys stay; the next start tries again */
} cal_mig_res_t;

typedef struct {
    cal_mig_res_t res;
    int slot;               /* where it went (MOVED), else -1 */
} cal_mig_t;

#define CAL_COPY_BLOB_MAX 6000      /* the saved copy's size limit, a little over cal_store.h's CAL_STORE_BYTES_MAX */

cal_mig_t cal_migrate_legacy(const cal_kv_ops_t *kv);

#ifdef __cplusplus
}
#endif
