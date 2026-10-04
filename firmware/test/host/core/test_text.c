/* tb_text: the drawable set, api.md 2.3 cleaning, unsupported characters, "?" replacement, ellipsis. */
#include <string.h>

#include "tb_text.h"
#include "tb_test.h"

TB_TEST(text_drawable_set)
{
    TB_TRUE(tb_text_drawable(' '));
    TB_TRUE(tb_text_drawable('~'));
    TB_FALSE(tb_text_drawable(0x1F));
    TB_FALSE(tb_text_drawable(0x7F));
    TB_FALSE(tb_text_drawable(0x9F));
    TB_TRUE(tb_text_drawable(0xA0));
    TB_TRUE(tb_text_drawable(0xE9));        /* é */
    TB_TRUE(tb_text_drawable(0xFF));
    TB_FALSE(tb_text_drawable(0x100));      /* Ā */
    TB_TRUE(tb_text_drawable(0x2013));
    TB_TRUE(tb_text_drawable(0x2014));
    TB_TRUE(tb_text_drawable(0x2026));
    TB_FALSE(tb_text_drawable(0x2019));     /* mapped by clean(), never drawn */
    TB_FALSE(tb_text_drawable(0x1F355));    /* 🍕 */
}

TB_TEST(text_clean_maps_typography)
{
    char out[128];
    TB_EQ_INT(tb_text_clean(out, sizeof out, "Don\xE2\x80\x99t \xE2\x80\x9Cinterrupt\xE2\x80\x9D \xE2\x80\x98now\xE2\x80\x98"), 23);
    TB_EQ_STR(out, "Don't \"interrupt\" 'now'");
    tb_text_clean(out, sizeof out, "2\xE2\x80\x93" "3 \xE2\x80\x94 later\xE2\x80\xA6");
    TB_EQ_STR(out, "2-3 - later...");
}

TB_TEST(text_clean_controls_trim_and_invalid)
{
    char out[64];
    TB_EQ_INT(tb_text_clean(out, sizeof out, "  \tHi\x01 there\x7F\n  "), 8);
    TB_EQ_STR(out, "Hi there");
    tb_text_clean(out, sizeof out, "two\nlines");
    TB_EQ_STR(out, "two lines");
    tb_text_clean(out, sizeof out, "a\xC2\x85" "b");               /* C1 control U+0085 */
    TB_EQ_STR(out, "ab");
    tb_text_clean(out, sizeof out, "x\xFFy\xC3z");                 /* stray bytes */
    TB_EQ_STR(out, "xyz");
    tb_text_clean(out, sizeof out, "o\xC0\xAFk");                  /* overlong "/" */
    TB_EQ_STR(out, "ok");
    tb_text_clean(out, sizeof out, "s\xED\xA0\x80" "t");          /* a UTF-16 surrogate */
    TB_EQ_STR(out, "st");
    tb_text_clean(out, sizeof out, "Caf\xC3\xA9 \xF0\x9F\x8D\x95");   /* Latin-1 and an emoji are kept */
    TB_EQ_STR(out, "Caf\xC3\xA9 \xF0\x9F\x8D\x95");
    TB_EQ_INT(tb_text_clean(out, sizeof out, "   "), 0);
    TB_EQ_STR(out, "");
    TB_EQ_INT(tb_text_clean(out, sizeof out, NULL), 0);
}

TB_TEST(text_clean_cuts_on_a_character_boundary)
{
    char out[6];
    tb_text_clean(out, sizeof out, "ab\xC3\xA9\xC3\xA9");          /* "abéé": 6 bytes, 5 fit */
    TB_EQ_STR(out, "ab\xC3\xA9");
    TB_EQ_INT(tb_utf8_len(out), 3);
    char tiny[4];
    tb_text_clean(tiny, sizeof tiny, "\xE2\x80\xA6");              /* "..." needs 4 bytes with the NUL */
    TB_EQ_STR(tiny, "...");
}

TB_TEST(text_unsupported_lists_distinct_in_order)
{
    uint32_t cps[4];
    TB_EQ_INT(tb_text_unsupported("Lunch \xF0\x9F\x8D\x95 then \xF0\x9F\x8D\x95 \xE2\x9C\x93", cps, 4), 2);
    TB_EQ_INT(cps[0], 0x1F355);
    TB_EQ_INT(cps[1], 0x2713);
    TB_EQ_INT(tb_text_unsupported("Caf\xC3\xA9 \xE2\x80\x93 ok\xE2\x80\xA6", cps, 4), 0);
    uint32_t one[1];
    TB_EQ_INT(tb_text_unsupported("\xC4\x80\xC4\x81\xC4\x82", one, 1), 3);   /* more than max: still counted */
    TB_EQ_INT(one[0], 0x100);
}

TB_TEST(text_replace_unsupported)
{
    char s[64];
    strcpy(s, "Standup \xF0\x9F\x9A\x80 with J\xC3\xBCrgen \xE2\x80\x93 \xE4\xB8\xAD");
    tb_text_replace_unsupported(s, sizeof s);
    TB_EQ_STR(s, "Standup ? with J\xC3\xBCrgen \xE2\x80\x93 ?");
}

TB_TEST(text_ellipsize_app_name_rule)
{
    char s[80];
    strcpy(s, "Microsoft Teams (work or school)");      /* 32 characters */
    tb_text_ellipsize(s, sizeof s, 24);
    TB_EQ_STR(s, "Microsoft Teams (work o\xE2\x80\xA6");    /* slice(0, 23) + '…' */
    TB_EQ_INT(tb_utf8_len(s), 24);
    strcpy(s, "Exactly twenty-four char");
    TB_EQ_INT(strlen(s), 24);
    tb_text_ellipsize(s, sizeof s, 24);
    TB_EQ_STR(s, "Exactly twenty-four char");
    strcpy(s, "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9");      /* "éééé" to 3 */
    tb_text_ellipsize(s, sizeof s, 3);
    TB_EQ_STR(s, "\xC3\xA9\xC3\xA9\xE2\x80\xA6");
    char small[7];
    strcpy(small, "abcdef");
    tb_text_ellipsize(small, sizeof small, 4);          /* "abc…" is 6 bytes + NUL = 7: fits */
    TB_EQ_STR(small, "abc\xE2\x80\xA6");
    char smaller[6];
    strcpy(smaller, "abcde");
    tb_text_ellipsize(smaller, sizeof smaller, 4);      /* only 5 bytes: keeps fewer */
    TB_EQ_STR(smaller, "ab\xE2\x80\xA6");
}

TB_TEST(text_strlcpy_never_splits_a_character)
{
    char d[4];
    tb_strlcpy(d, "a\xE2\x80\x93", sizeof d);           /* "a–" is 4 bytes: the dash doesn't fit */
    TB_EQ_STR(d, "a");
    tb_strlcpy(d, "abc", sizeof d);
    TB_EQ_STR(d, "abc");
    tb_strlcpy(d, NULL, sizeof d);
    TB_EQ_STR(d, "");
}
