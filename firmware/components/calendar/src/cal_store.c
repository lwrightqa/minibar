/*
 * cal_store.c: the saved copy's packed format. Owner: calendar builder. See cal_store.h.
 */
#include <string.h>

#include "cal_store.h"
#include "tb_text.h"

static void put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void put_u64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get_u32(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)p[i] << (8 * i);
    return v;
}

static uint64_t get_u64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

size_t cal_store_pack(const tb_meeting_t *m, int n, tb_epoch_t last_sync, uint8_t *buf, size_t cap)
{
    if (n < 0) n = 0;
    if (n > TB_MEETINGS_MAX) n = TB_MEETINGS_MAX;
    if (cap < CAL_STORE_HEADER) return 0;
    buf[0] = 'T';
    buf[1] = 'C';
    buf[2] = CAL_STORE_VERSION;
    buf[3] = (uint8_t)n;
    put_u64(buf + 4, (uint64_t)last_sync);
    size_t o = CAL_STORE_HEADER;
    for (int i = 0; i < n; i++) {
        char title[CAL_STORE_TITLE_MAX + 1], loc[CAL_STORE_LOCATION_MAX + 1];
        tb_strlcpy(title, m[i].title, sizeof(title));
        tb_strlcpy(loc, m[i].location, sizeof(loc));
        size_t tl = strlen(title), ll = strlen(loc);
        if (o + CAL_STORE_ENTRY_FIXED + tl + ll > cap) return 0;
        tb_epoch_t dur = m[i].end - m[i].start;
        if (dur < 0) dur = 0;
        if (dur > 0x7FFFFFFF) dur = 0x7FFFFFFF;
        put_u32(buf + o, m[i].id);
        put_u64(buf + o + 4, (uint64_t)m[i].start);
        put_u32(buf + o + 12, (uint32_t)dur);
        buf[o + 16] = m[i].priv ? 1 : 0;
        buf[o + 17] = (uint8_t)tl;
        buf[o + 18] = (uint8_t)ll;
        o += CAL_STORE_ENTRY_FIXED;
        memcpy(buf + o, title, tl);
        o += tl;
        memcpy(buf + o, loc, ll);
        o += ll;
    }
    return o;
}

int cal_store_unpack(const uint8_t *buf, size_t len, tb_meeting_t *out, int max, tb_epoch_t *last_sync)
{
    if (!buf || len < CAL_STORE_HEADER || buf[0] != 'T' || buf[1] != 'C' || buf[2] != CAL_STORE_VERSION) return -1;
    int n = buf[3];
    if (n > TB_MEETINGS_MAX) return -1;
    size_t o = CAL_STORE_HEADER;
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (o + CAL_STORE_ENTRY_FIXED > len) return -1;
        size_t tl = buf[o + 17], ll = buf[o + 18];
        if (tl > CAL_STORE_TITLE_MAX || ll > CAL_STORE_LOCATION_MAX || o + CAL_STORE_ENTRY_FIXED + tl + ll > len)
            return -1;
        if (k < max) {
            tb_meeting_t *m = &out[k++];
            memset(m, 0, sizeof(*m));
            m->id = get_u32(buf + o);
            m->start = (tb_epoch_t)get_u64(buf + o + 4);
            m->end = m->start + get_u32(buf + o + 12);
            m->priv = buf[o + 16] & 1;
            /* tb_strlcpy-style: never half a character, even from a damaged blob */
            char tmp[CAL_STORE_TITLE_MAX + 1];
            memcpy(tmp, buf + o + CAL_STORE_ENTRY_FIXED, tl);
            tmp[tl] = '\0';
            tb_strlcpy(m->title, tmp, sizeof(m->title));
            memcpy(tmp, buf + o + CAL_STORE_ENTRY_FIXED + tl, ll);
            tmp[ll] = '\0';
            tb_strlcpy(m->location, tmp, sizeof(m->location));
            if (!m->id) m->id = 1;
        }
        o += CAL_STORE_ENTRY_FIXED + tl + ll;
    }
    if (o != len) return -1;
    if (last_sync) *last_sync = (tb_epoch_t)get_u64(buf + 4);
    return k;
}
