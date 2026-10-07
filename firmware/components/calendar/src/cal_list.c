/*
 * cal_list.c: the calendars' names and tags, and the merge. Owner: calendar builder. See cal_list.h.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "cal_list.h"
#include "cal_today.h"
#include "tb_text.h"

#define BLOB_VERSION 1

static bool same_text(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
    return !*a && !*b;
}

/* Trim and clean a typed name into out. 1 = fine (out holds it), 0 = empty (use the default), -1 = bad. */
static int clean_name(const char *in, char out[TB_CAL_NAME_BYTES])
{
    out[0] = '\0';
    if (!in) return 0;
    char tmp[4 * TB_CAL_NAME_BYTES];
    if (strlen(in) >= sizeof tmp) return -1;
    tb_text_clean(tmp, sizeof tmp, in);
    if (!tmp[0]) return 0;
    if (strlen(tmp) >= TB_CAL_NAME_BYTES || tb_text_unsupported(tmp, NULL, 0) > 0) return -1;
    memcpy(out, tmp, strlen(tmp) + 1);
    return 1;
}

static int clean_tag(const char *in, char out[TB_CAL_TAG_BYTES])
{
    out[0] = '\0';
    if (!in) return 0;
    while (*in == ' ') in++;
    size_t n = strlen(in);
    while (n && in[n - 1] == ' ') n--;
    if (!n) return 0;
    if (n >= TB_CAL_TAG_BYTES) return -1;
    for (size_t i = 0; i < n; i++) {
        if (!isalnum((unsigned char)in[i]) || (unsigned char)in[i] > 0x7F) return -1;
        out[i] = (char)toupper((unsigned char)in[i]);
    }
    out[n] = '\0';
    return 1;
}

/* Is the name or tag taken by a calendar other than skip? */
static bool taken(const cal_list_t *l, int skip, const char *name, const char *tag)
{
    for (int i = 0; i < TB_CALS_MAX; i++) {
        if (i == skip || !l->c[i].used) continue;
        if ((name && same_text(l->c[i].name, name)) || (tag && same_text(l->c[i].tag, tag))) return true;
    }
    return false;
}

/* "Calendar N" and "CN" with the lowest N free as name and as tag (they are counted apart: renaming one doesn't
 * hand the other's number away twice). */
static void default_name(const cal_list_t *l, int skip, char out[TB_CAL_NAME_BYTES])
{
    for (int n = 1;; n++) {
        snprintf(out, TB_CAL_NAME_BYTES, "Calendar %d", n);
        if (!taken(l, skip, out, NULL)) return;
    }
}

static void default_tag(const cal_list_t *l, int skip, char out[TB_CAL_TAG_BYTES])
{
    for (int n = 1; n <= 9; n++) {      /* at most 3 others, so one of C1 to C4 is free */
        out[0] = 'C';
        out[1] = (char)('0' + n);
        out[2] = '\0';
        if (!taken(l, skip, NULL, out)) return;
    }
}

void cal_list_init(cal_list_t *l)
{
    memset(l, 0, sizeof *l);
}

int cal_list_count(const cal_list_t *l)
{
    int k = 0;
    for (int i = 0; i < TB_CALS_MAX; i++) k += l->c[i].used;
    return k;
}

cal_list_err_t cal_list_add(cal_list_t *l, const char *name, const char *tag, int *slot)
{
    int s = -1;
    for (int i = 0; i < TB_CALS_MAX && s < 0; i++)
        if (!l->c[i].used) s = i;
    if (s < 0) return CAL_LIST_FULL;
    char n[TB_CAL_NAME_BYTES], t[TB_CAL_TAG_BYTES];
    int rn = clean_name(name, n), rt = clean_tag(tag, t);
    if (rn < 0) return CAL_LIST_BAD_NAME;
    if (rt < 0) return CAL_LIST_BAD_TAG;
    if (rn == 0) default_name(l, -1, n);
    if (rt == 0) default_tag(l, -1, t);
    if (taken(l, -1, n, t)) return CAL_LIST_DUP;
    memset(&l->c[s], 0, sizeof l->c[s]);
    l->c[s].used = true;
    memcpy(l->c[s].name, n, strlen(n) + 1);
    memcpy(l->c[s].tag, t, strlen(t) + 1);
    if (slot) *slot = s;
    return CAL_LIST_OK;
}

cal_list_err_t cal_list_edit(cal_list_t *l, int slot, const char *name, const char *tag)
{
    if (slot < 0 || slot >= TB_CALS_MAX || !l->c[slot].used) return CAL_LIST_NO_SUCH;
    char n[TB_CAL_NAME_BYTES], t[TB_CAL_TAG_BYTES];
    int rn = clean_name(name, n), rt = clean_tag(tag, t);
    if (rn < 0) return CAL_LIST_BAD_NAME;
    if (rt < 0) return CAL_LIST_BAD_TAG;
    if (rn == 0) memcpy(n, l->c[slot].name, sizeof n);
    if (rt == 0) memcpy(t, l->c[slot].tag, sizeof t);
    if (taken(l, slot, n, t)) return CAL_LIST_DUP;
    memcpy(l->c[slot].name, n, sizeof n);
    memcpy(l->c[slot].tag, t, sizeof t);
    return CAL_LIST_OK;
}

bool cal_list_remove(cal_list_t *l, int slot)
{
    if (slot < 0 || slot >= TB_CALS_MAX || !l->c[slot].used) return false;
    memset(&l->c[slot], 0, sizeof l->c[slot]);
    return true;
}

const char *cal_list_err_message(cal_list_err_t e)
{
    switch (e) {
    case CAL_LIST_OK: return NULL;
    case CAL_LIST_FULL: return "You can add up to 3 calendars. Remove one to add another.";
    case CAL_LIST_BAD_NAME: return "Give the calendar a name of up to 24 characters.";
    case CAL_LIST_BAD_TAG: return "Give the calendar a short tag, up to 4 letters or digits.";
    case CAL_LIST_DUP: return "Another calendar already uses that name or tag. Pick a different one.";
    case CAL_LIST_NO_SUCH: return "There's no calendar with that id.";
    }
    return "That calendar can't be changed.";
}

/* ---------- the saved form ---------- */

size_t cal_list_encode(const cal_list_t *l, uint8_t *out, size_t cap)
{
    if (cap < CAL_LIST_BLOB_MAX) return 0;
    size_t o = 2;
    out[0] = BLOB_VERSION;
    out[1] = 0;
    for (int i = 0; i < TB_CALS_MAX; i++) {
        if (!l->c[i].used) continue;
        out[1] |= (uint8_t)(1u << i);
        size_t a = strlen(l->c[i].name) + 1, b = strlen(l->c[i].tag) + 1;
        memcpy(out + o, l->c[i].name, a);
        o += a;
        memcpy(out + o, l->c[i].tag, b);
        o += b;
    }
    return o;
}

/* The next NUL-terminated string of the blob, or NULL when it doesn't end inside it. */
static const char *next_str(const uint8_t *in, size_t len, size_t *o)
{
    const uint8_t *z = memchr(in + *o, 0, len - *o);
    if (!z) return NULL;
    const char *s = (const char *)in + *o;
    *o = (size_t)(z - in) + 1;
    return s;
}

void cal_list_decode(cal_list_t *l, const uint8_t *in, size_t len)
{
    cal_list_init(l);
    if (!in || len < 2 || in[0] != BLOB_VERSION || (in[1] & ~((1u << TB_CALS_MAX) - 1))) return;
    size_t o = 2;
    for (int i = 0; i < TB_CALS_MAX; i++) {
        if (!(in[1] & (1u << i))) continue;
        const char *nm = next_str(in, len, &o), *tg = nm ? next_str(in, len, &o) : NULL;
        if (!tg) {
            cal_list_init(l);   /* cut short: trust none of it */
            return;
        }
        char n[TB_CAL_NAME_BYTES], t[TB_CAL_TAG_BYTES];
        if (clean_name(nm, n) <= 0 || taken(l, -1, n, NULL)) n[0] = '\0';
        if (clean_tag(tg, t) <= 0 || taken(l, -1, NULL, t)) t[0] = '\0';
        l->c[i].used = true;
        if (!n[0]) default_name(l, i, n);
        if (!t[0]) default_tag(l, i, t);
        memcpy(l->c[i].name, n, sizeof n);
        memcpy(l->c[i].tag, t, sizeof t);
    }
}

void cal_list_reconcile(cal_list_t *l, unsigned url_mask)
{
    for (int i = 0; i < TB_CALS_MAX; i++) {
        bool has = (url_mask >> i) & 1u;
        if (!has) {
            cal_list_remove(l, i);
        } else if (!l->c[i].used) {
            l->c[i].used = true;
            default_name(l, i, l->c[i].name);
            default_tag(l, i, l->c[i].tag);
        }
    }
}

/* ---------- merging ---------- */

int cal_merge(const cal_source_t *src, int n_src, tb_meeting_t *out, int keep, tb_epoch_t now)
{
    int n = 0;
    for (int s = 0; s < n_src; s++) {
        if (src[s].failing || !src[s].m) continue;
        for (int i = 0; i < src[s].n; i++) {
            const tb_meeting_t *m = &src[s].m[i];
            bool dup = false;
            for (int k = 0; k < n && !dup; k++) dup = out[k].id == m->id && out[k].start == m->start;
            if (dup || n >= CAL_MERGE_CAP) continue;
            out[n] = *m;
            out[n].cal = (uint8_t)(src[s].slot < 0 ? 0 : src[s].slot >= TB_CALS_MAX ? TB_CALS_MAX - 1 : src[s].slot);
            n++;
        }
    }
    /* stable insertion sort by start: 48 entries at most, and equal starts keep the earlier calendar first */
    for (int i = 1; i < n; i++) {
        tb_meeting_t t = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].start > t.start) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = t;
    }
    if (keep > TB_MEETINGS_MAX) keep = TB_MEETINGS_MAX;
    return cal_today_trim(out, n, keep, now);
}
