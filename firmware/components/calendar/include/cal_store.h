/*
 * cal_store.h: the saved copy of the meetings (NVS "nvs" / "cal" / "list"), packed small. Pure C, so the format and
 * its checks are tested on the host. Owner: calendar builder.
 *
 * Why packed: the nvs partition is 24 KB and also holds settings, Wi-Fi and own state, and NVS keeps the old copy of
 * a blob until the new one is written. 32 full tb_meeting_t would be 8 KB. The saved copy is only a fallback for a
 * restart without Wi-Fi, so titles keep CAL_STORE_TITLE_MAX bytes and locations CAL_STORE_LOCATION_MAX (cut on a
 * character boundary; the bar's headline shows fewer than that anyway); the next sync brings them back whole.
 * Typical size: a few hundred bytes; at most CAL_STORE_BYTES_MAX.
 *
 * Format (little-endian): 'T' 'C' version n, last_sync (8), then per meeting: id (4), start (8), duration (4),
 * flags (1, bit 0 private), title length (1), location length (1), title, location.
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAL_STORE_VERSION      1
#define CAL_STORE_TITLE_MAX    80
#define CAL_STORE_LOCATION_MAX 48
#define CAL_STORE_HEADER       12
#define CAL_STORE_ENTRY_FIXED  19
#define CAL_STORE_BYTES_MAX \
    (CAL_STORE_HEADER + TB_MEETINGS_MAX * (CAL_STORE_ENTRY_FIXED + CAL_STORE_TITLE_MAX + CAL_STORE_LOCATION_MAX))

/* Pack up to TB_MEETINGS_MAX meetings. Returns the size written, or 0 if cap is too small. */
size_t cal_store_pack(const tb_meeting_t *m, int n, tb_epoch_t last_sync, uint8_t *buf, size_t cap);
/* Unpack into out (max entries). Returns the count, or -1 if the blob is damaged or of another version (out is then
 * untouched past what was written; treat it as no saved copy). */
int cal_store_unpack(const uint8_t *buf, size_t len, tb_meeting_t *out, int max, tb_epoch_t *last_sync);

#ifdef __cplusplus
}
#endif
