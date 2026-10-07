/* test_list.c: the calendars list (add, edit, remove, cap, saved form) and the merge. Owner: calendar builder. */
#include <string.h>

#include "cal_list.h"
#include "tb_test.h"

TB_TEST(list_add_defaults_and_cap)
{
    cal_list_t l;
    cal_list_init(&l);
    int s = -1;
    TB_EQ_INT(cal_list_add(&l, NULL, NULL, &s), CAL_LIST_OK);
    TB_EQ_INT(s, 0);
    TB_EQ_STR(l.c[0].name, "Calendar 1");
    TB_EQ_STR(l.c[0].tag, "C1");
    TB_EQ_INT(cal_list_add(&l, "", "  ", &s), CAL_LIST_OK);
    TB_EQ_INT(s, 1);
    TB_EQ_STR(l.c[1].name, "Calendar 2");
    TB_EQ_INT(cal_list_add(&l, "Team", "tm", &s), CAL_LIST_OK);
    TB_EQ_STR(l.c[2].tag, "TM");           /* kept in capitals */
    TB_EQ_INT(cal_list_count(&l), 3);
    TB_EQ_INT(cal_list_add(&l, "Four", "C4", &s), CAL_LIST_FULL);      /* the cap is 3 */
    TB_EQ_INT(cal_list_count(&l), 3);
}

TB_TEST(list_remove_frees_the_lowest_number_again)
{
    cal_list_t l;
    cal_list_init(&l);
    for (int i = 0; i < 3; i++) cal_list_add(&l, NULL, NULL, NULL);
    TB_TRUE(cal_list_remove(&l, 0));
    TB_FALSE(cal_list_remove(&l, 0));      /* already free */
    TB_FALSE(cal_list_remove(&l, 7));
    TB_FALSE(cal_list_remove(&l, -1));
    int s = -1;
    TB_EQ_INT(cal_list_add(&l, NULL, NULL, &s), CAL_LIST_OK);
    TB_EQ_INT(s, 0);                      /* the freed slot, "Calendar 1" and C1 again */
    TB_EQ_STR(l.c[0].name, "Calendar 1");
    TB_EQ_STR(l.c[0].tag, "C1");
}

TB_TEST(list_names_and_tags_are_checked)
{
    cal_list_t l;
    cal_list_init(&l);
    int s;
    TB_EQ_INT(cal_list_add(&l, "Work", "WRK", &s), CAL_LIST_OK);
    TB_EQ_INT(cal_list_add(&l, "work", "W2", &s), CAL_LIST_DUP);        /* name without case */
    TB_EQ_INT(cal_list_add(&l, "Home", "wrk", &s), CAL_LIST_DUP);       /* tag without case */
    TB_EQ_INT(cal_list_add(&l, "Home", "TOOLONG", &s), CAL_LIST_BAD_TAG);
    TB_EQ_INT(cal_list_add(&l, "Home", "A-B", &s), CAL_LIST_BAD_TAG);
    TB_EQ_INT(cal_list_add(&l, "Home", "\xC3\x89", &s), CAL_LIST_BAD_TAG);  /* not an ASCII letter */
    TB_EQ_INT(cal_list_add(&l, "A name that is far too long for it", "H", &s), CAL_LIST_BAD_NAME);
    TB_EQ_INT(cal_list_add(&l, "Emoji \xF0\x9F\x98\x80", "H", &s), CAL_LIST_BAD_NAME);   /* the fonts can't draw it */
    TB_EQ_INT(cal_list_add(&l, "<b>x</b>", "H", &s), CAL_LIST_OK);       /* markup is only text */
    TB_EQ_INT(cal_list_count(&l), 2);
    TB_EQ_INT(cal_list_add(&l, "Ctl\x01", "Z", &s), CAL_LIST_OK);       /* control characters are removed */
    TB_EQ_STR(l.c[2].name, "Ctl");
    TB_TRUE(cal_list_add(&l, "x", "y", &s) == CAL_LIST_FULL);
}

TB_TEST(list_edit_keeps_what_is_empty_and_rolls_back_on_error)
{
    cal_list_t l;
    cal_list_init(&l);
    cal_list_add(&l, "Work", "WRK", NULL);
    cal_list_add(&l, "Home", "HOM", NULL);
    TB_EQ_INT(cal_list_edit(&l, 0, "Office", NULL), CAL_LIST_OK);
    TB_EQ_STR(l.c[0].name, "Office");
    TB_EQ_STR(l.c[0].tag, "WRK");
    TB_EQ_INT(cal_list_edit(&l, 0, NULL, "of"), CAL_LIST_OK);
    TB_EQ_STR(l.c[0].tag, "OF");
    TB_EQ_INT(cal_list_edit(&l, 0, "Office", "OF"), CAL_LIST_OK);       /* its own name and tag aren't a clash */
    TB_EQ_INT(cal_list_edit(&l, 0, "HOME", NULL), CAL_LIST_DUP);
    TB_EQ_INT(cal_list_edit(&l, 0, "Fine", "hom"), CAL_LIST_DUP);
    TB_EQ_STR(l.c[0].name, "Office");     /* nothing changed, not even the name that was fine */
    TB_EQ_STR(l.c[0].tag, "OF");
    TB_EQ_INT(cal_list_edit(&l, 2, "x", NULL), CAL_LIST_NO_SUCH);
    TB_EQ_INT(cal_list_edit(&l, -1, "x", NULL), CAL_LIST_NO_SUCH);
    TB_EQ_INT(cal_list_edit(&l, 9, "x", NULL), CAL_LIST_NO_SUCH);
}

TB_TEST(list_saved_form_round_trips)
{
    cal_list_t l, r;
    cal_list_init(&l);
    cal_list_add(&l, "Work", "WRK", NULL);
    cal_list_add(&l, "Home", "HOM", NULL);
    cal_list_add(&l, "Kids", "K", NULL);
    cal_list_remove(&l, 1);
    uint8_t b[CAL_LIST_BLOB_MAX];
    size_t n = cal_list_encode(&l, b, sizeof b);
    TB_TRUE(n > 2 && n <= CAL_LIST_BLOB_MAX);
    TB_EQ_INT(cal_list_encode(&l, b, 5), 0);       /* too small a buffer */
    cal_list_decode(&r, b, n);
    TB_TRUE(r.c[0].used && !r.c[1].used && r.c[2].used);
    TB_EQ_STR(r.c[0].name, "Work");
    TB_EQ_STR(r.c[2].tag, "K");
}

TB_TEST(list_decode_trusts_nothing)
{
    cal_list_t l;
    uint8_t b[64];
    /* wrong version, empty, NULL, a mask with bits that aren't slots */
    memset(b, 0, sizeof b);
    cal_list_decode(&l, b, 2);
    TB_EQ_INT(cal_list_count(&l), 0);
    cal_list_decode(&l, NULL, 0);
    TB_EQ_INT(cal_list_count(&l), 0);
    b[0] = 1; b[1] = 0xFF;
    cal_list_decode(&l, b, 40);
    TB_EQ_INT(cal_list_count(&l), 0);
    /* a string that never ends */
    b[1] = 1;
    memset(b + 2, 'A', 30);
    cal_list_decode(&l, b, 32);
    TB_EQ_INT(cal_list_count(&l), 0);
    /* two with the same name and a tag with a bad character: defaults replace the bad ones */
    const uint8_t two[] = {1, 3, 'A', 0, 'X', 0, 'a', 0, 'Y', 0};
    cal_list_decode(&l, two, sizeof two);
    TB_EQ_INT(cal_list_count(&l), 2);
    TB_EQ_STR(l.c[0].name, "A");
    TB_TRUE(strcmp(l.c[1].name, "a") != 0);
    const uint8_t bad[] = {1, 1, 'O', 'k', 0, 'a', '<', 0};
    cal_list_decode(&l, bad, sizeof bad);
    TB_EQ_STR(l.c[0].name, "Ok");
    TB_EQ_STR(l.c[0].tag, "C1");
    /* extra bytes after the last string are ignored; the names are always cut short enough */
    uint8_t big[200];
    memset(big, 'Q', sizeof big);
    big[0] = 1; big[1] = 1; big[100] = 0; big[150] = 0;
    cal_list_decode(&l, big, sizeof big);
    TB_EQ_STR(l.c[0].name, "Calendar 1");
    TB_TRUE(strlen(l.c[0].tag) < TB_CAL_TAG_BYTES);
}

TB_TEST(list_reconcile_follows_the_addresses)
{
    cal_list_t l;
    cal_list_init(&l);
    cal_list_add(&l, "Work", "WRK", NULL);       /* slot 0, but its address is gone */
    cal_list_reconcile(&l, 0x6);                 /* addresses in slots 1 and 2 */
    TB_FALSE(l.c[0].used);
    TB_TRUE(l.c[1].used && l.c[2].used);
    TB_EQ_STR(l.c[1].name, "Calendar 1");
    TB_EQ_STR(l.c[2].name, "Calendar 2");
    TB_EQ_STR(l.c[2].tag, "C2");
}

/* ---------- merge ---------- */

static tb_meeting_t mt(uint32_t id, tb_epoch_t start, int mins, const char *title)
{
    tb_meeting_t m;
    memset(&m, 0, sizeof m);
    m.id = id;
    m.start = start;
    m.end = start + mins * 60;
    snprintf(m.title, sizeof m.title, "%s", title);
    return m;
}

#define T0 1800000000

TB_TEST(merge_sorts_tags_and_keeps_the_earlier_calendar_in_ties)
{
    tb_meeting_t a[] = {mt(1, T0 + 3600, 30, "A late"), mt(2, T0 + 7200, 30, "A last")};
    tb_meeting_t b[] = {mt(3, T0 + 1800, 30, "B early"), mt(4, T0 + 3600, 30, "B same time")};
    cal_source_t src[] = {{0, false, a, 2}, {1, false, b, 2}};
    tb_meeting_t out[CAL_MERGE_CAP];
    int n = cal_merge(src, 2, out, TB_MEETINGS_MAX, T0);
    TB_EQ_INT(n, 4);
    TB_EQ_STR(out[0].title, "B early");
    TB_EQ_INT(out[0].cal, 1);
    TB_EQ_STR(out[1].title, "A late");      /* same start: the earlier calendar first */
    TB_EQ_STR(out[2].title, "B same time");
    TB_EQ_STR(out[3].title, "A last");
    TB_EQ_INT(out[3].cal, 0);
}

TB_TEST(merge_same_event_in_two_calendars_shows_once_with_the_earlier_tag)
{
    tb_meeting_t a[] = {mt(77, T0 + 3600, 30, "Standup")};
    tb_meeting_t b[] = {mt(77, T0 + 3600, 30, "Standup"), mt(78, T0 + 5400, 30, "Other")};
    tb_meeting_t c[] = {mt(77, T0 + 3600, 30, "Standup")};
    cal_source_t src[] = {{0, false, a, 1}, {1, false, b, 2}, {2, false, c, 1}};
    tb_meeting_t out[CAL_MERGE_CAP];
    int n = cal_merge(src, 3, out, TB_MEETINGS_MAX, T0);
    TB_EQ_INT(n, 2);
    TB_EQ_INT(out[0].cal, 0);
    /* when the earlier calendar is the one failing, the later copy stands in for it with its own tag */
    src[0].failing = true;
    n = cal_merge(src, 3, out, TB_MEETINGS_MAX, T0);
    TB_EQ_INT(n, 2);
    TB_EQ_INT(out[0].cal, 1);
    /* the same UID at another start (a recurring series) is another meeting */
    tb_meeting_t d[] = {mt(77, T0 + 90000, 30, "Standup")};
    cal_source_t s2[] = {{0, false, a, 1}, {1, false, d, 1}};
    TB_EQ_INT(cal_merge(s2, 2, out, TB_MEETINGS_MAX, T0), 2);
}

TB_TEST(merge_leaves_a_failing_calendar_out_entirely)
{
    tb_meeting_t a[] = {mt(1, T0 + 3600, 30, "A")};
    tb_meeting_t b[] = {mt(2, T0 + 1800, 30, "B")};
    tb_meeting_t c[] = {mt(3, T0 + 900, 30, "C")};
    cal_source_t src[] = {{0, false, a, 1}, {1, true, b, 1}, {2, true, c, 1}};
    tb_meeting_t out[CAL_MERGE_CAP];
    TB_EQ_INT(cal_merge(src, 3, out, TB_MEETINGS_MAX, T0), 1);
    TB_EQ_STR(out[0].title, "A");
    src[0].failing = true;
    TB_EQ_INT(cal_merge(src, 3, out, TB_MEETINGS_MAX, T0), 0);      /* every one failing: nothing */
    TB_EQ_INT(cal_merge(src, 0, out, TB_MEETINGS_MAX, T0), 0);
    cal_source_t nul[] = {{0, false, NULL, 5}};
    TB_EQ_INT(cal_merge(nul, 1, out, TB_MEETINGS_MAX, T0), 0);
}

TB_TEST(merge_trims_to_keep_over_ones_first)
{
    tb_meeting_t a[CAL_COPY_MAX], b[CAL_COPY_MAX], c[CAL_COPY_MAX];
    for (int i = 0; i < CAL_COPY_MAX; i++) {
        a[i] = mt(100 + i, T0 - 7200 + i * 1000, 10, "a");
        b[i] = mt(200 + i, T0 - 7000 + i * 1000, 10, "b");
        c[i] = mt(300 + i, T0 - 6800 + i * 1000, 10, "c");
    }
    cal_source_t src[] = {{0, false, a, CAL_COPY_MAX}, {1, false, b, CAL_COPY_MAX}, {2, false, c, CAL_COPY_MAX}};
    tb_meeting_t out[CAL_MERGE_CAP];
    int n = cal_merge(src, 3, out, TB_MEETINGS_MAX, T0);
    TB_EQ_INT(n, TB_MEETINGS_MAX);
    for (int i = 1; i < n; i++) TB_TRUE(out[i - 1].start <= out[i].start);
    /* the ones still to come survive the trim: 48 meetings, T0 is after 7 of the 16 per calendar, the last 32 stay */
    TB_TRUE(out[n - 1].start >= T0 - 7200 + 15 * 1000);
    TB_EQ_INT(cal_merge(src, 3, out, 5, T0), 5);
    TB_EQ_INT(cal_merge(src, 3, out, 1000, T0), TB_MEETINGS_MAX);     /* keep is capped */
}
