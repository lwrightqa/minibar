/*
 * cal_ics.c: the streaming iCalendar reader. Owner: calendar builder. See cal_ics.h.
 *
 * Bytes go through a small state machine that unfolds lines and keeps only the properties that matter; each kept
 * logical line is split into name, parameters and value and handled in its component's context. At END:VEVENT the
 * event is turned into candidate instances inside the window (expanding its RRULE); cal_feed_finish() then removes
 * the instances that overrides replace, sorts and copies them out.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cal_ics.h"
#include "tb_text.h"

#define NAME_MAX_LEN 24                 /* longer property names are never ones we keep */
#define TZID_MAX 64
#define NOT_CAL_AFTER 4096              /* bytes before BEGIN:VCALENDAR must have shown */
#define NEAR_BEFORE ((tb_epoch_t)31 * 86400)    /* overrides and EXDATEs this long before the window still matter */
#define MAX_DURATION ((int32_t)31 * 86400)

typedef enum { M_NAME = 0, M_KEEP, M_SKIP } line_mode_t;
typedef enum { DT_NONE = 0, DT_UTC, DT_ZONED, DT_FLOATING, DT_DATE } dt_kind_t;

typedef struct {
    dt_kind_t kind;
    cal_civil_t c;      /* as written (local civil for ZONED and FLOATING, UTC civil for UTC, the date for DATE) */
    tb_epoch_t utc;     /* not for DATE */
    cal_tz_t tz;        /* the zone the value is in (UTC for UTC) */
} dt_t;

typedef struct {
    bool is_date;
    tb_epoch_t utc;     /* an instant */
    int32_t day;        /* a date: days since 1970 */
} exdate_t;

typedef struct {
    uint32_t uid_hash;  /* FNV-1a state after the UID */
    bool has_uid;
    char title[TB_TITLE_BYTES];
    char location[TB_LOCATION_BYTES];
    dt_t start, end, rid;
    bool has_duration;
    int32_t duration;
    bool has_rrule, rrule_ok;
    cal_rrule_t rrule;
    exdate_t ex[CAL_EXDATES_MAX];
    uint8_t n_ex;
    bool cancelled, transparent, priv, declined;
} event_t;

typedef struct {
    uint32_t id;
    tb_epoch_t start, end;
    bool override;
    bool priv;
    char title[TB_TITLE_BYTES];
    char location[TB_LOCATION_BYTES];
} cand_t;

/* One STANDARD or DAYLIGHT observance of a VTIMEZONE. */
typedef struct {
    bool valid;
    cal_civil_t start;
    int32_t to;
    bool has_rule;
    cal_tz_rule_t rule;
    bool ongoing;       /* a yearly rule that hasn't ended before the window */
} obs_t;

typedef struct {
    char tzid[TZID_MAX];
    obs_t std, dst;     /* the best of each kind so far */
    obs_t cur;
    int cur_kind;       /* 0 none, 1 STANDARD, 2 DAYLIGHT */
} vtz_build_t;

typedef struct {
    uint32_t hash;
    cal_tz_t tz;
} vtz_t;

typedef struct {
    char tzid[TZID_MAX];
    cal_tz_t tz;
} tz_cache_t;

struct cal_feed {
    tb_epoch_t ws, we;
    cal_tz_t device_tz;
    cal_tz_t cal_tz;            /* X-WR-TIMEZONE, else the device zone */
    char self_email[128];
    cal_stats_t stats;

    /* line assembly */
    char line[CAL_LINE_MAX + 1];
    size_t len;
    line_mode_t mode;
    bool cut, pending_nl;
    bool seen_vcalendar, not_cal, finished;

    /* structure */
    bool in_event, in_vtz;
    int ev_sub, vtz_sub, other_depth;
    event_t ev;
    vtz_build_t vb;
    vtz_t vtz[CAL_VTIMEZONES_MAX];
    int n_vtz;
    tz_cache_t tzc[4];
    int tzc_n, tzc_next;
    uint32_t steps_left;

    /* results */
    cand_t cand[CAL_CANDIDATES_MAX];
    int n_cand;
    uint32_t ovr[CAL_OVERRIDES_MAX];
    int n_ovr;

    char text[CAL_LINE_MAX + 1];    /* unescaping scratch */
};

/* ---------- ids ---------- */

#define FNV_OFFSET 2166136261u
#define FNV_PRIME 16777619u

static uint32_t fnv_str(uint32_t h, const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) h = (h ^ *p) * FNV_PRIME;
    return h;
}

static uint32_t fnv_time(uint32_t h, tb_epoch_t t)
{
    for (int i = 0; i < 8; i++) h = (h ^ (uint8_t)((uint64_t)t >> (8 * i))) * FNV_PRIME;
    return h ? h : 1;
}

uint32_t cal_instance_id(const char *uid, tb_epoch_t start)
{
    return fnv_time(fnv_str(FNV_OFFSET, uid), start);
}

/* ---------- small parsers ---------- */

typedef struct {
    const char *name;
    size_t name_len;
    const char *params;     /* from the first ';' to the value's ':' */
    size_t params_len;
    char *value;            /* NUL-terminated, trailing spaces trimmed */
} prop_t;

static bool split_prop(char *line, prop_t *p)
{
    char *s = line;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
    char *q = s;
    while (*q && *q != ';' && *q != ':') q++;
    if (!*q || q == s) return false;
    p->name = s;
    p->name_len = (size_t)(q - s);
    p->params = q;
    bool quoted = false;
    for (; *q; q++) {
        if (*q == '"') quoted = !quoted;
        else if (*q == ':' && !quoted) break;
    }
    if (!*q) return false;
    p->params_len = (size_t)(q - p->params);
    p->value = q + 1;
    size_t n = strlen(p->value);
    while (n && (p->value[n - 1] == ' ' || p->value[n - 1] == '\t')) p->value[--n] = '\0';
    return true;
}

static bool name_is(const prop_t *p, const char *name)
{
    return strlen(name) == p->name_len && !strncasecmp(p->name, name, p->name_len);
}

static bool value_is(const char *v, const char *word)
{
    return !strcasecmp(v, word);
}

/* The first value of parameter key (quotes removed). */
static bool get_param(const prop_t *p, const char *key, char *out, size_t cap)
{
    const char *s = p->params, *end = p->params + p->params_len;
    size_t kl = strlen(key);
    while (s < end) {
        if (*s != ';') {
            s++;
            continue;
        }
        s++;
        const char *eq = s;
        while (eq < end && *eq != '=' && *eq != ';') eq++;
        if (eq >= end || *eq != '=') {
            s = eq;
            continue;
        }
        bool match = (size_t)(eq - s) == kl && !strncasecmp(s, key, kl);
        const char *v = eq + 1, *ve;
        bool quoted = v < end && *v == '"';
        if (quoted) {
            v++;
            ve = v;
            while (ve < end && *ve != '"') ve++;
        } else {
            ve = v;
            while (ve < end && *ve != ';' && *ve != ',') ve++;
        }
        if (match) {
            size_t n = (size_t)(ve - v);
            if (n >= cap) n = cap - 1;
            memcpy(out, v, n);
            out[n] = '\0';
            return true;
        }
        /* skip the rest of this parameter (more values, quoted or not) */
        s = quoted && ve < end ? ve + 1 : ve;
        bool q = false;
        while (s < end && (q || *s != ';')) {
            if (*s == '"') q = !q;
            s++;
        }
    }
    return false;
}

/* YYYYMMDD, YYYYMMDDTHHMMSS or YYYYMMDDTHHMMSSZ, exactly n bytes. */
static bool parse_dt_token(const char *v, size_t n, cal_civil_t *c, bool *is_date, bool *is_utc)
{
    if (n != 8 && n != 15 && n != 16) return false;
    for (size_t i = 0; i < n; i++) {
        if (i == 8) {
            if (v[i] != 'T' && v[i] != 't') return false;
        } else if (i == 15) {
            if (v[i] != 'Z' && v[i] != 'z') return false;
        } else if (!isdigit((unsigned char)v[i])) {
            return false;
        }
    }
#define D2(o) ((v[o] - '0') * 10 + (v[(o) + 1] - '0'))
    memset(c, 0, sizeof(*c));
    c->year = (int16_t)(D2(0) * 100 + D2(2));
    c->month = (int8_t)D2(4);
    c->day = (int8_t)D2(6);
    if (n > 8) {
        c->hour = (int8_t)D2(9);
        c->min = (int8_t)D2(11);
        c->sec = (int8_t)D2(13);
        if (c->sec == 60) c->sec = 59;  /* a leap second */
    }
#undef D2
    *is_date = n == 8;
    *is_utc = n == 16;
    return cal_civil_valid(c);
}

/* +HHMM[SS] or -HHMM[SS] */
static bool parse_utc_offset(const char *v, int32_t *out)
{
    size_t n = strlen(v);
    if ((n != 5 && n != 7) || (v[0] != '+' && v[0] != '-')) return false;
    for (size_t i = 1; i < n; i++)
        if (!isdigit((unsigned char)v[i])) return false;
    int h = (v[1] - '0') * 10 + (v[2] - '0'), m = (v[3] - '0') * 10 + (v[4] - '0');
    int s = n == 7 ? (v[5] - '0') * 10 + (v[6] - '0') : 0;
    if (h > 23 || m > 59 || s > 59) return false;
    *out = (v[0] == '-' ? -1 : 1) * (h * 3600 + m * 60 + s);
    return true;
}

/* [+-]P[nW][nD][T[nH][nM][nS]] */
static bool parse_duration(const char *v, int32_t *out)
{
    int sign = 1;
    if (*v == '+' || *v == '-') sign = *v++ == '-' ? -1 : 1;
    if (*v != 'P' && *v != 'p') return false;
    v++;
    int64_t total = 0;
    bool in_time = false, any = false;
    while (*v) {
        if (*v == 'T' || *v == 't') {
            in_time = true;
            v++;
            continue;
        }
        if (!isdigit((unsigned char)*v)) return false;
        int64_t n = 0;
        while (isdigit((unsigned char)*v)) {
            n = n * 10 + (*v++ - '0');
            if (n > 100000000) return false;
        }
        switch (toupper((unsigned char)*v++)) {
        case 'W': total += n * 604800; break;
        case 'D': total += n * 86400; break;
        case 'H': if (!in_time) return false; total += n * 3600; break;
        case 'M': if (!in_time) return false; total += n * 60; break;
        case 'S': if (!in_time) return false; total += n; break;
        default: return false;
        }
        any = true;
    }
    if (!any) return false;
    if (total > MAX_DURATION) total = MAX_DURATION;
    *out = (int32_t)(sign * total);
    return true;
}

/* RFC 5545 TEXT: \n \N \, \; \\ (and Outlook's \:). */
static void unescape(const char *s, char *out, size_t cap)
{
    size_t o = 0;
    while (*s && o + 1 < cap) {
        if (*s == '\\' && s[1]) {
            char c = s[1];
            out[o++] = (c == 'n' || c == 'N') ? '\n' : c;
            s += 2;
        } else {
            out[o++] = *s++;
        }
    }
    out[o] = '\0';
}

static void set_text(cal_feed_t *f, const char *value, char *dst, size_t cap)
{
    unescape(value, f->text, sizeof(f->text));
    tb_text_clean(dst, cap, f->text);
    tb_text_replace_unsupported(dst, cap);
}

/* ---------- time zones ---------- */

static bool lookup_iana(const char *name, cal_tz_t *out)
{
    const char *posix = cal_tz_posix_for(name);
    return posix && cal_tz_parse(posix, out);
}

static bool resolve_zone(cal_feed_t *f, const char *tzid, cal_tz_t *out)
{
    for (int i = 0; i < f->tzc_n; i++) {
        if (!strcmp(f->tzc[i].tzid, tzid)) {
            *out = f->tzc[i].tz;
            return true;
        }
    }
    bool found = lookup_iana(tzid, out);
    if (!found) {
        uint32_t h = fnv_str(FNV_OFFSET, tzid);
        for (int i = 0; i < f->n_vtz && !found; i++) {
            if (f->vtz[i].hash == h) {
                *out = f->vtz[i].tz;
                found = true;
            }
        }
    }
    for (const char *p = tzid; !found && (p = strchr(p, '/')) != NULL; p++) found = lookup_iana(p + 1, out);
    if (!found) {
        f->stats.unknown_tzid++;
        *out = f->cal_tz;
    }
    tz_cache_t *c = &f->tzc[f->tzc_next];
    f->tzc_next = (f->tzc_next + 1) % 4;
    if (f->tzc_n < 4) f->tzc_n++;
    strncpy(c->tzid, tzid, sizeof(c->tzid) - 1);
    c->tzid[sizeof(c->tzid) - 1] = '\0';
    c->tz = *out;
    return found;
}

/* A DATE or DATE-TIME token of property p (its TZID and VALUE parameters apply). */
static bool parse_dt(cal_feed_t *f, const prop_t *p, const char *v, size_t n, const cal_tz_t *floating, dt_t *out)
{
    bool is_date, is_utc;
    memset(out, 0, sizeof(*out));
    if (!parse_dt_token(v, n, &out->c, &is_date, &is_utc)) return false;
    if (is_date) {
        out->kind = DT_DATE;
        return true;
    }
    char tzid[TZID_MAX];
    if (is_utc) {
        out->kind = DT_UTC;
        cal_tz_utc(&out->tz);
    } else if (get_param(p, "TZID", tzid, sizeof(tzid)) && tzid[0]) {
        out->kind = DT_ZONED;
        resolve_zone(f, tzid, &out->tz);
    } else {
        out->kind = DT_FLOATING;
        out->tz = *floating;
    }
    out->utc = cal_tz_to_utc(&out->tz, &out->c);
    return true;
}

static bool parse_dt_prop(cal_feed_t *f, const prop_t *p, dt_t *out)
{
    return parse_dt(f, p, p->value, strlen(p->value), &f->cal_tz, out);
}

/* ---------- VTIMEZONE ---------- */

/* A yearly STANDARD or DAYLIGHT RRULE as a POSIX Mm.w.d rule at DTSTART's time of day. */
static bool tz_rule_from(const cal_rrule_t *r, const cal_civil_t *start, cal_tz_rule_t *out)
{
    if (r->freq != CAL_FREQ_YEARLY || r->n_byday != 1 || r->n_bysetpos) return false;
    int month = 0;
    for (int m = 1; m <= 12; m++) {
        if (r->bymonth_mask & (1u << m)) {
            if (month) return false;
            month = m;
        }
    }
    if (!month) month = start->month;
    int ord = r->byday[0].ord, w;
    if (ord == 0) {
        /* Older style: BYDAY=SU;BYMONTHDAY=8,9,10,11,12,13,14 is the second Sunday. */
        if (r->n_bymonthday != 7) return false;
        int lo = 99;
        for (int i = 0; i < 7; i++) {
            if (r->bymonthday[i] < 0) return false;
            if (r->bymonthday[i] < lo) lo = r->bymonthday[i];
        }
        if (lo == 1 || lo == 8 || lo == 15 || lo == 22) w = (lo + 6) / 7;
        else if (lo >= 23) w = 5;
        else return false;
    } else if (ord >= 1 && ord <= 4) {
        w = ord;
    } else if (ord == -1 || ord == 5) {
        w = 5;
    } else {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->kind = CAL_TZ_RULE_M;
    out->m = (uint8_t)month;
    out->w = (uint8_t)w;
    out->d = r->byday[0].wd;
    out->secs = start->hour * 3600 + start->min * 60 + start->sec;
    return true;
}

static void vtz_commit_observance(cal_feed_t *f)
{
    vtz_build_t *b = &f->vb;
    obs_t *o = &b->cur;
    if (!o->valid) return;
    obs_t *best = b->cur_kind == 2 ? &b->dst : &b->std;
    bool better = !best->valid || (o->ongoing && !best->ongoing) ||
                  (o->ongoing == best->ongoing && cal_civil_cmp(&o->start, &best->start) > 0);
    if (better) *best = *o;
}

static void vtz_finish(cal_feed_t *f)
{
    vtz_build_t *b = &f->vb;
    if (!b->tzid[0]) return;
    cal_tz_t tz;
    cal_tz_utc(&tz);
    if (b->std.valid && b->dst.valid && b->std.ongoing && b->dst.ongoing) {
        tz.std_offset = b->std.to;
        tz.dst_offset = b->dst.to;
        tz.has_dst = tz.std_offset != tz.dst_offset;
        tz.start = b->dst.rule;
        tz.end = b->std.rule;
    } else {
        /* No yearly change any more: the offset of the observance that started last (ongoing ones first). */
        const obs_t *o = NULL;
        const obs_t *c[2] = {&b->std, &b->dst};
        for (int i = 0; i < 2; i++) {
            if (!c[i]->valid) continue;
            if (!o || (c[i]->ongoing && !o->ongoing) ||
                (c[i]->ongoing == o->ongoing && cal_civil_cmp(&c[i]->start, &o->start) > 0))
                o = c[i];
        }
        if (!o) return;
        tz.std_offset = tz.dst_offset = o->to;
    }
    if (f->n_vtz < CAL_VTIMEZONES_MAX) {
        f->vtz[f->n_vtz].hash = fnv_str(FNV_OFFSET, b->tzid);
        f->vtz[f->n_vtz].tz = tz;
        f->n_vtz++;
        f->stats.vtimezones++;
    }
}

static void vtz_prop(cal_feed_t *f, const prop_t *p)
{
    vtz_build_t *b = &f->vb;
    if (!b->cur_kind) {
        if (name_is(p, "TZID")) {
            strncpy(b->tzid, p->value, sizeof(b->tzid) - 1);
            b->tzid[sizeof(b->tzid) - 1] = '\0';
        }
        return;
    }
    obs_t *o = &b->cur;
    if (name_is(p, "DTSTART")) {
        bool d, u;
        if (parse_dt_token(p->value, strlen(p->value), &o->start, &d, &u)) o->valid = true;
    } else if (name_is(p, "TZOFFSETTO")) {
        parse_utc_offset(p->value, &o->to);
    } else if (name_is(p, "RRULE")) {
        cal_rrule_t r;
        if (cal_rrule_parse(p->value, &r) && tz_rule_from(&r, &o->start, &o->rule)) {
            o->has_rule = true;
            o->ongoing = !r.has_until || (r.until_is_utc ? r.until_utc : cal_civil_to_secs(&r.until)) >= f->ws;
        }
    }
}

/* ---------- events ---------- */

static bool overlaps(const cal_feed_t *f, tb_epoch_t start, tb_epoch_t end)
{
    return start < f->we && end > f->ws;
}

static void add_cand(cal_feed_t *f, uint32_t id, tb_epoch_t start, tb_epoch_t end, bool override)
{
    if (!overlaps(f, start, end)) return;
    cand_t *c;
    if (f->n_cand < CAL_CANDIDATES_MAX) {
        c = &f->cand[f->n_cand++];
    } else {
        /* Full: keep the earliest; the window's latest instance makes way (or the new one is dropped). */
        f->stats.dropped_candidates++;
        int last = 0;
        for (int i = 1; i < f->n_cand; i++)
            if (f->cand[i].start > f->cand[last].start) last = i;
        if (start >= f->cand[last].start) return;
        c = &f->cand[last];
    }
    const event_t *e = &f->ev;
    c->id = id ? id : 1;
    c->start = start;
    c->end = end;
    c->override = override;
    c->priv = e->priv;
    if (e->priv) {
        c->title[0] = c->location[0] = '\0';   /* never stored, never shown */
    } else {
        memcpy(c->title, e->title, sizeof(c->title));
        memcpy(c->location, e->location, sizeof(c->location));
    }
}

static void add_override(cal_feed_t *f, uint32_t key)
{
    for (int i = 0; i < f->n_ovr; i++)
        if (f->ovr[i] == key) return;
    if (f->n_ovr < CAL_OVERRIDES_MAX) f->ovr[f->n_ovr++] = key;
    else f->stats.dropped_overrides++;
}

static bool excluded(const event_t *e, tb_epoch_t utc, const cal_civil_t *local)
{
    int32_t day = -1;
    for (int i = 0; i < e->n_ex; i++) {
        if (e->ex[i].is_date) {
            if (day < 0) day = cal_days_from_civil(local->year, local->month, local->day);
            if (e->ex[i].day == day) return true;
        } else if (e->ex[i].utc == utc) {
            return true;
        }
    }
    return false;
}

typedef struct {
    cal_feed_t *f;
    int32_t dur;
} occ_ctx_t;

static bool on_occurrence(void *ctx, const cal_civil_t *local)
{
    occ_ctx_t *o = ctx;
    const event_t *e = &o->f->ev;
    tb_epoch_t utc = cal_tz_to_utc(&e->start.tz, local);
    if (!excluded(e, utc, local)) add_cand(o->f, fnv_time(e->uid_hash, utc), utc, utc + o->dur, false);
    return true;
}

static void event_prop(cal_feed_t *f, const prop_t *p)
{
    event_t *e = &f->ev;
    const char *v = p->value;
    if (name_is(p, "UID")) {
        e->uid_hash = fnv_str(FNV_OFFSET, v);
        e->has_uid = true;
    } else if (name_is(p, "SUMMARY")) {
        set_text(f, v, e->title, sizeof(e->title));
    } else if (name_is(p, "LOCATION")) {
        set_text(f, v, e->location, sizeof(e->location));
    } else if (name_is(p, "DTSTART")) {
        parse_dt_prop(f, p, &e->start);
    } else if (name_is(p, "DTEND")) {
        parse_dt_prop(f, p, &e->end);
    } else if (name_is(p, "DURATION")) {
        e->has_duration = parse_duration(v, &e->duration);
    } else if (name_is(p, "RRULE")) {
        if (!e->has_rrule) {
            e->has_rrule = true;
            e->rrule_ok = cal_rrule_parse(v, &e->rrule);
        }
    } else if (name_is(p, "RECURRENCE-ID")) {
        parse_dt_prop(f, p, &e->rid);
    } else if (name_is(p, "EXDATE")) {
        /* Floating values are in the event's own zone when DTSTART came first (it does in practice). */
        const cal_tz_t *fl = e->start.kind == DT_ZONED ? &e->start.tz : &f->cal_tz;
        const char *s = v;
        while (*s) {
            const char *c = strchr(s, ',');
            size_t n = c ? (size_t)(c - s) : strlen(s);
            while (n && *s == ' ') s++, n--;
            while (n && s[n - 1] == ' ') n--;
            dt_t d;
            if (parse_dt(f, p, s, n, fl, &d)) {
                exdate_t x = {.is_date = d.kind == DT_DATE, .utc = d.utc};
                bool near;
                if (x.is_date) {
                    x.day = cal_days_from_civil(d.c.year, d.c.month, d.c.day);
                    near = (tb_epoch_t)x.day * 86400 >= f->ws - NEAR_BEFORE - 86400 &&
                           (tb_epoch_t)x.day * 86400 < f->we + 86400;
                } else {
                    near = d.utc >= f->ws - NEAR_BEFORE && d.utc < f->we + 86400;
                }
                if (near) {
                    if (e->n_ex < CAL_EXDATES_MAX) e->ex[e->n_ex++] = x;
                    else f->stats.dropped_exdates++;
                }
            }
            if (!c) break;
            s = c + 1;
        }
    } else if (name_is(p, "STATUS")) {
        e->cancelled = value_is(v, "CANCELLED");
    } else if (name_is(p, "TRANSP")) {
        e->transparent = value_is(v, "TRANSPARENT");
    } else if (name_is(p, "CLASS")) {
        e->priv = value_is(v, "PRIVATE") || value_is(v, "CONFIDENTIAL");
    } else if (name_is(p, "ATTENDEE") && f->self_email[0]) {
        bool me = !strncasecmp(v, "mailto:", 7) && !strcasecmp(v + 7, f->self_email);
        char email[128];
        if (!me && get_param(p, "EMAIL", email, sizeof(email))) me = !strcasecmp(email, f->self_email);
        char ps[24];
        if (me && get_param(p, "PARTSTAT", ps, sizeof(ps))) e->declined = value_is(ps, "DECLINED");
    }
}

static void finish_event(cal_feed_t *f)
{
    event_t *e = &f->ev;
    f->stats.events++;
    if (!e->has_uid) e->uid_hash = (uint32_t)fnv_time(fnv_str(FNV_OFFSET, "tinybar:no-uid:"), f->stats.events);
    bool is_override = e->rid.kind != DT_NONE;
    if (is_override) {
        f->stats.overrides++;
        if (e->rid.kind != DT_DATE && e->rid.utc >= f->ws - NEAR_BEFORE && e->rid.utc < f->we)
            add_override(f, fnv_time(e->uid_hash, e->rid.utc));
    }
    if (e->start.kind == DT_NONE || e->start.kind == DT_DATE) {
        if (e->has_rrule) f->stats.recurring++;
        return;     /* no start, or all-day: never counts */
    }
    tb_epoch_t start = e->start.utc, end = start;
    if (e->end.kind != DT_NONE && e->end.kind != DT_DATE) end = e->end.utc;
    else if (e->has_duration) end = start + e->duration;
    int32_t dur = end > start ? (end - start > MAX_DURATION ? MAX_DURATION : (int32_t)(end - start)) : 0;
    bool counts = !e->cancelled && !e->transparent && !e->declined;

    if (is_override) {
        uint32_t id = e->rid.kind != DT_DATE ? fnv_time(e->uid_hash, e->rid.utc) : fnv_time(e->uid_hash, start);
        if (counts) add_cand(f, id, start, start + dur, true);
        return;
    }
    if (e->has_rrule) {
        f->stats.recurring++;
        if (e->rrule_ok) {
            if (!counts) return;
            uint32_t budget = f->steps_left < CAL_RRULE_STEPS_EVENT ? f->steps_left : CAL_RRULE_STEPS_EVENT;
            uint32_t steps = budget;
            occ_ctx_t ctx = {.f = f, .dur = dur};
            int r = cal_rrule_expand(&e->rrule, &e->start.c, &e->start.tz, dur, f->ws, f->we, &steps, on_occurrence,
                                     &ctx);
            f->steps_left -= budget - steps;
            if (r < 0) f->stats.rrule_budget_hit++;
            return;
        }
        f->stats.unsupported_rrule++;
    }
    if (counts && !excluded(e, start, &e->start.c)) add_cand(f, fnv_time(e->uid_hash, start), start, start + dur, false);
}

/* ---------- logical lines ---------- */

static bool kept_name(const cal_feed_t *f, const char *name, size_t n)
{
    static const char *const keep[] = {
        "BEGIN", "END", "UID", "SUMMARY", "LOCATION", "DTSTART", "DTEND", "DURATION", "RRULE", "EXDATE",
        "RECURRENCE-ID", "STATUS", "TRANSP", "CLASS", "ATTENDEE", "TZID", "TZOFFSETTO", "X-WR-TIMEZONE",
    };
    if (n >= 3 && (unsigned char)name[0] == 0xEF && (unsigned char)name[1] == 0xBB && (unsigned char)name[2] == 0xBF) {
        name += 3;
        n -= 3;
    }
    for (size_t i = 0; i < sizeof(keep) / sizeof(keep[0]); i++) {
        if (strlen(keep[i]) == n && !strncasecmp(name, keep[i], n)) {
            if (!strcmp(keep[i], "ATTENDEE") && !f->self_email[0]) return false;  /* only matters to find you */
            return true;
        }
    }
    return false;
}

static void begin_component(cal_feed_t *f, const char *v)
{
    if (value_is(v, "VCALENDAR")) {
        f->seen_vcalendar = true;
        return;
    }
    if (f->in_event) {
        if (f->ev_sub == 0 && value_is(v, "VEVENT")) {
            finish_event(f);    /* a VEVENT that never ended: keep what it had and start the next */
            memset(&f->ev, 0, sizeof(f->ev));
        } else {
            f->ev_sub++;
        }
        return;
    }
    if (f->in_vtz) {
        if (f->vtz_sub == 0 && !f->vb.cur_kind && (value_is(v, "STANDARD") || value_is(v, "DAYLIGHT"))) {
            f->vb.cur_kind = value_is(v, "DAYLIGHT") ? 2 : 1;
            memset(&f->vb.cur, 0, sizeof(f->vb.cur));
        } else {
            f->vtz_sub++;
        }
        return;
    }
    if (f->other_depth) {
        f->other_depth++;
        return;
    }
    if (value_is(v, "VEVENT")) {
        memset(&f->ev, 0, sizeof(f->ev));
        f->in_event = true;
        f->ev_sub = 0;
    } else if (value_is(v, "VTIMEZONE")) {
        memset(&f->vb, 0, sizeof(f->vb));
        f->in_vtz = true;
        f->vtz_sub = 0;
    } else {
        f->other_depth = 1;     /* VTODO, VJOURNAL, VFREEBUSY...: skipped whole */
    }
}

static void end_component(cal_feed_t *f, const char *v)
{
    if (f->in_event) {
        if (f->ev_sub > 0) {
            f->ev_sub--;
        } else if (value_is(v, "VEVENT")) {
            finish_event(f);
            f->in_event = false;
        } else if (value_is(v, "VCALENDAR")) {
            f->in_event = false;    /* a truncated event: dropped */
            f->stats.ended = true;
        }
        return;
    }
    if (f->in_vtz) {
        if (f->vtz_sub > 0) {
            f->vtz_sub--;
        } else if (f->vb.cur_kind && (value_is(v, "STANDARD") || value_is(v, "DAYLIGHT"))) {
            vtz_commit_observance(f);
            f->vb.cur_kind = 0;
        } else if (value_is(v, "VTIMEZONE")) {
            vtz_finish(f);
            f->in_vtz = false;
        }
        return;
    }
    if (f->other_depth) {
        f->other_depth--;
        return;
    }
    if (value_is(v, "VCALENDAR")) f->stats.ended = true;
}

static void process_line(cal_feed_t *f)
{
    prop_t p;
    if (!split_prop(f->line, &p)) return;
    f->stats.lines++;
    if (name_is(&p, "BEGIN")) {
        begin_component(f, p.value);
    } else if (name_is(&p, "END")) {
        end_component(f, p.value);
    } else if (f->in_event) {
        if (f->ev_sub == 0) event_prop(f, &p);
    } else if (f->in_vtz) {
        if (f->vtz_sub == 0) vtz_prop(f, &p);
    } else if (!f->other_depth && name_is(&p, "X-WR-TIMEZONE")) {
        cal_tz_t tz;
        if (lookup_iana(p.value, &tz)) {
            f->cal_tz = tz;
            f->tzc_n = 0;       /* cached fallbacks used the old zone */
        }
    }
}

static void end_line(cal_feed_t *f)
{
    if (f->mode == M_KEEP) {
        f->line[f->len] = '\0';
        process_line(f);
    }
    f->len = 0;
    f->mode = M_NAME;
    f->cut = false;
}

static inline void put_byte(cal_feed_t *f, char c)
{
    switch (f->mode) {
    case M_SKIP:
        return;
    case M_NAME:
        if (c == ':' || c == ';') {
            if (!kept_name(f, f->line, f->len)) {
                f->mode = M_SKIP;
                return;
            }
            f->mode = M_KEEP;
        } else if (f->len >= NAME_MAX_LEN + 3) {
            f->mode = M_SKIP;
            return;
        }
        /* fall through */
    case M_KEEP:
        if (f->len < CAL_LINE_MAX) {
            f->line[f->len++] = c;
        } else if (!f->cut) {
            f->cut = true;
            f->stats.cut_lines++;
        }
        return;
    }
}

/* ---------- public ---------- */

cal_feed_t *cal_feed_new(tb_epoch_t window_start, tb_epoch_t window_end, const cal_tz_t *device_tz, const char *self_email)
{
    cal_feed_t *f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->ws = window_start;
    f->we = window_end;
    if (device_tz) f->device_tz = *device_tz;
    else cal_tz_utc(&f->device_tz);
    f->cal_tz = f->device_tz;
    if (self_email) strncpy(f->self_email, self_email, sizeof(f->self_email) - 1);
    f->steps_left = CAL_RRULE_STEPS_FEED;
    return f;
}

void cal_feed_free(cal_feed_t *f)
{
    free(f);
}

cal_err_t cal_feed_write(cal_feed_t *f, const char *data, size_t len)
{
    if (!f) return CAL_ERR_NO_MEMORY;
    if (f->not_cal) return CAL_ERR_NOT_A_CALENDAR;
    if (f->finished) return CAL_OK;
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        f->stats.bytes++;
        if (!f->seen_vcalendar && f->stats.bytes > NOT_CAL_AFTER) {
            f->not_cal = true;
            return CAL_ERR_NOT_A_CALENDAR;
        }
        if (c == '\r') continue;
        if (f->pending_nl) {
            f->pending_nl = false;
            if (c == ' ' || c == '\t') continue;    /* folded: the line goes on */
            end_line(f);
        }
        if (c == '\n') {
            f->pending_nl = true;
            continue;
        }
        put_byte(f, c);
    }
    return CAL_OK;
}

static bool is_overridden(const cal_feed_t *f, uint32_t id)
{
    for (int i = 0; i < f->n_ovr; i++)
        if (f->ovr[i] == id) return true;
    return false;
}

static int cmp_cand(const void *a, const void *b)
{
    const cand_t *x = a, *y = b;
    if (x->start != y->start) return x->start < y->start ? -1 : 1;
    if (x->end != y->end) return x->end < y->end ? -1 : 1;
    if (x->override != y->override) return x->override ? -1 : 1;
    return x->id < y->id ? -1 : x->id > y->id;
}

cal_err_t cal_feed_finish(cal_feed_t *f, tb_meeting_t *out, int max, int *n)
{
    *n = 0;
    if (!f) return CAL_ERR_NO_MEMORY;
    if (!f->finished) {
        f->finished = true;
        if (!f->not_cal && (f->len || f->pending_nl)) end_line(f);
        if (f->in_event) f->in_event = false;   /* cut off mid-event: dropped */
    }
    if (f->not_cal || !f->seen_vcalendar) {
        f->not_cal = true;
        return CAL_ERR_NOT_A_CALENDAR;
    }
    /* Master instances that an override replaces (moved, changed or cancelled) go. */
    int k = 0;
    for (int i = 0; i < f->n_cand; i++) {
        if (!f->cand[i].override && is_overridden(f, f->cand[i].id)) continue;
        if (k != i) f->cand[k] = f->cand[i];
        k++;
    }
    f->n_cand = k;
    qsort(f->cand, (size_t)f->n_cand, sizeof(f->cand[0]), cmp_cand);
    for (int i = 0; i < f->n_cand && *n < max; i++) {
        const cand_t *c = &f->cand[i];
        bool dup = false;
        for (int j = 0; j < *n && !dup; j++) dup = out[j].id == c->id;
        if (dup) continue;
        tb_meeting_t *m = &out[(*n)++];
        memset(m, 0, sizeof(*m));
        m->id = c->id;
        m->start = c->start;
        m->end = c->end;
        m->priv = c->priv;
        memcpy(m->title, c->title, sizeof(m->title));
        memcpy(m->location, c->location, sizeof(m->location));
    }
    return CAL_OK;
}

const cal_stats_t *cal_feed_stats(const cal_feed_t *f)
{
    return &f->stats;
}
