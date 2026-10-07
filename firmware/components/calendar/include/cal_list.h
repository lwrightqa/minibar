/*
 * cal_list.h: the calendars (up to TB_CALS_MAX = 3) as pure logic kept apart from ESP-IDF so it's tested on Linux:
 * add, edit (name and tag), remove, the saved form of the names and tags, the merge of several calendars' meetings
 * into the one list the bar follows, and the move from the single address of firmware before 1.0.8 (cal_migrate.h).
 *
 * A calendar lives in a slot (0 to 2) for as long as it exists: the slot is its API id (slot + 1), the number in the
 * merged meetings (tb_meeting_t.cal) and the order of the list ("the earlier calendar" is the lower slot). The
 * addresses are not here: they stay in nvs_sec (cal_sync.c), and this list never holds one.
 *
 * Owner: calendar builder. Decisions: docs/decisions.md, Multiple calendars (2026-10-07).
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAL_COPY_MAX    16                                  /* meetings kept per calendar (saved copy and in RAM) */
#define CAL_MERGE_CAP   (TB_CALS_MAX * CAL_COPY_MAX)        /* the most the merge ever sees */

typedef struct {
    tb_cal_info_t c[TB_CALS_MAX];   /* by slot; used = false for a free one (failing is filled in by the service) */
} cal_list_t;

typedef enum {
    CAL_LIST_OK = 0,
    CAL_LIST_FULL,          /* "You can add up to 3 calendars. Remove one to add another." */
    CAL_LIST_BAD_NAME,      /* not 1 to 24 characters, or characters the bar can't draw */
    CAL_LIST_BAD_TAG,       /* not 1 to 4 letters or digits */
    CAL_LIST_DUP,           /* another calendar already has that name or tag (compared without case) */
    CAL_LIST_NO_SUCH,       /* no calendar in that slot */
} cal_list_err_t;

void cal_list_init(cal_list_t *l);
int cal_list_count(const cal_list_t *l);
/* Add into the lowest free slot. name or tag NULL or empty (after trimming): the default, "Calendar N" and "CN" with
 * the lowest N that isn't taken. The tag is kept in capitals. *slot gets the slot on success. */
cal_list_err_t cal_list_add(cal_list_t *l, const char *name, const char *tag, int *slot);
/* Change the name and/or tag (NULL or empty keeps the current one). Nothing changes on an error. */
cal_list_err_t cal_list_edit(cal_list_t *l, int slot, const char *name, const char *tag);
/* Free the slot. False if it already was free (or out of range). */
bool cal_list_remove(cal_list_t *l, int slot);
/* The sentence for an error, as the Remote shows it. */
const char *cal_list_err_message(cal_list_err_t e);

/* The saved names and tags (nvs "cal"/"meta"): [1 version][1 used mask] then, per used slot, name and tag as
 * NUL-terminated text. At most CAL_LIST_BLOB_MAX bytes. */
#define CAL_LIST_BLOB_MAX (2 + TB_CALS_MAX * (TB_CAL_NAME_BYTES + TB_CAL_TAG_BYTES))
size_t cal_list_encode(const cal_list_t *l, uint8_t *out, size_t cap);
/* Read it back; never fails and never trusts it: a damaged blob gives an empty list, and a name or tag that is bad or
 * repeats another's is replaced by the default. */
void cal_list_decode(cal_list_t *l, const uint8_t *in, size_t len);
/* Make the list agree with which slots have an address (bit i of url_mask): a slot with an address and no entry gets
 * a default name and tag, an entry with no address goes. */
void cal_list_reconcile(cal_list_t *l, unsigned url_mask);

/* ---------- merging ---------- */

typedef struct {
    int slot;
    bool failing;               /* its last sync failed: left out, no stale copy */
    const tb_meeting_t *m;      /* that calendar's meetings, sorted by start */
    int n;
} cal_source_t;

/* The meetings of every source that isn't failing, soonest first (ties: the earlier source first), the same event in
 * two calendars once (same id, which is UID plus start, and same start; the earlier source's copy stays, and so its
 * tag), each tagged with its slot in .cal. Sources are in list order. out holds at least CAL_MERGE_CAP entries; at
 * most keep (<= TB_MEETINGS_MAX) are returned, trimmed as cal_today_trim() does (over ones first, then the latest).
 * Returns the count. */
int cal_merge(const cal_source_t *src, int n_src, tb_meeting_t *out, int keep, tb_epoch_t now);

#ifdef __cplusplus
}
#endif
