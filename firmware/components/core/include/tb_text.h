/*
 * tb_text.h: text that reaches the screen. The one place that says which characters the bar's fonts can draw.
 *
 * Owner: core builder. Pure C.
 *
 * THE CHARACTER SET IS A CONTRACT WITH THE UI BUILDER: every font that draws user text (the 62 px headline for
 * messages and meeting titles, the 28 px side value, the 19 px sub line, the 14 and 15 px small text) must contain
 * exactly the code points tb_text_drawable() accepts. The 112, 100 and 78 px headline fonts only carry what their
 * fixed copy needs (A to Z, 0 to 9, colon, space, middle dot; see decisions.md "Firmware fonts").
 *   U+0020..U+007E   printable ASCII
 *   U+00A0..U+00FF   Latin-1 supplement (é, ü, ñ, ·, ...)
 *   U+2013 U+2014    en and em dash (time ranges use the en dash)
 *   U+2026           ellipsis (cut titles and app names)
 *   U+2018 U+2019 U+201C U+201D are never drawn: tb_text_clean() maps them to ' and " first (api.md 2.3).
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* True if the fonts have this code point. */
bool tb_text_drawable(uint32_t cp);

/*
 * api.md 2.3: copy src into dst (cap bytes, always NUL-terminated, cut on a UTF-8 boundary), removing control
 * characters and mapping typographic punctuation to what the fonts have (’ ‘ to ', “ ” to ", – — to -, … to ...).
 * Trims leading and trailing spaces. Invalid UTF-8 bytes are dropped. Tab, line feed and carriage return become a
 * space (so "two\nlines" doesn't run the words together); every other control character (U+0000..U+001F, U+007F,
 * U+0080..U+009F) is removed. Returns the length in characters ("..." counts as three).
 * (Note: the mapping turns – and — into "-" for typed text, as the API says; the bar's own copy may still use the
 * en dash, which the fonts carry.)
 */
size_t tb_text_clean(char *dst, size_t cap, const char *src);

/* Collect up to max distinct code points in s the fonts can't draw, in order of first appearance. Returns how many
 * distinct ones were found (may exceed max). An invalid UTF-8 byte counts as U+FFFD. */
int tb_text_unsupported(const char *s, uint32_t *out, int max);

/* Replace characters the fonts can't draw with "?" (calendar titles and locations, which aren't refused). */
void tb_text_replace_unsupported(char *s, size_t cap);

/* Copy src into dst (cap bytes), always NUL-terminated, never ending in half a UTF-8 character. NULL copies "". */
void tb_strlcpy(char *dst, const char *src, size_t cap);

/* Count UTF-8 characters. */
size_t tb_utf8_len(const char *s);

/* Cut s to at most n characters; if it was longer, keep n-1 and add "…" (the app name's 24-character rule:
 * c.app.slice(0, 23) + '…'). Keeps fewer characters if cap can't hold them plus the 3-byte ellipsis. */
void tb_text_ellipsize(char *s, size_t cap, size_t n);

#ifdef __cplusplus
}
#endif
