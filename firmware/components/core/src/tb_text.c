/*
 * tb_text.c: the drawable character set and text cleaning (api.md 2.3). Owner: core builder.
 *
 * Strings inside the firmware are UTF-8. tb_text_clean() is the gate for text from outside (the API, the setup page,
 * calendar titles); after it, a string is valid UTF-8 with no control characters, so the other functions here can
 * walk it without re-checking.
 */
#include <string.h>

#include "tb_internal.h"
#include "tb_text.h"

bool tb_text_drawable(uint32_t cp)
{
    if (cp >= 0x20 && cp <= 0x7E) return true;
    if (cp >= 0xA0 && cp <= 0xFF) return true;
    return cp == 0x2013 || cp == 0x2014 || cp == 0x2026;
}

size_t tb_utf8_len(const char *s)
{
    size_t n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

/*
 * Decode one code point at s (strictly: no overlong forms, no surrogates, nothing above U+10FFFF).
 * Returns the number of bytes used (at least 1); *cp is the code point, or -1 for an invalid byte (skip 1).
 */
static int utf8_decode(const unsigned char *s, int32_t *cp)
{
    unsigned c = s[0];
    if (c < 0x80) {
        *cp = (int32_t)c;
        return 1;
    }
    int n;
    uint32_t v, min;
    if ((c & 0xE0) == 0xC0) { n = 2; v = c & 0x1F; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { n = 3; v = c & 0x0F; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { n = 4; v = c & 0x07; min = 0x10000; }
    else { *cp = -1; return 1; }
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *cp = -1; return 1; }  /* also stops at the NUL */
        v = (v << 6) | (s[i] & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) { *cp = -1; return 1; }
    *cp = (int32_t)v;
    return n;
}

static int utf8_encode(uint32_t cp, char out[4])
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static bool is_control(uint32_t cp)
{
    return cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F);
}

size_t tb_text_clean(char *dst, size_t cap, const char *src)
{
    if (!cap) return 0;
    dst[0] = '\0';
    if (!src) return 0;
    size_t len = 0;         /* bytes written */
    size_t keep = 0;        /* bytes up to the last character that isn't a space (for the trailing trim) */
    size_t chars = 0, keep_chars = 0;
    bool started = false;   /* leading spaces are dropped */
    const unsigned char *s = (const unsigned char *)src;
    while (*s) {
        int32_t cp;
        s += utf8_decode(s, &cp);
        if (cp < 0) continue;                       /* invalid byte: dropped */
        const char *rep = NULL;
        char buf[4];
        int n;
        if (cp == '\t' || cp == '\n' || cp == '\r') cp = ' ';   /* whitespace controls keep the words apart */
        if (is_control((uint32_t)cp)) continue;     /* other control characters: removed */
        switch (cp) {
        case 0x2018: case 0x2019: rep = "'"; break;
        case 0x201C: case 0x201D: rep = "\""; break;
        case 0x2013: case 0x2014: rep = "-"; break;
        case 0x2026: rep = "..."; break;
        default: break;
        }
        if (cp == ' ' && !started) continue;
        started = true;
        if (rep) {
            n = (int)strlen(rep);
            memcpy(buf, rep, (size_t)n);
        } else {
            n = utf8_encode((uint32_t)cp, buf);
        }
        if (len + (size_t)n + 1 > cap) break;      /* cut on a character boundary */
        memcpy(dst + len, buf, (size_t)n);
        len += (size_t)n;
        chars += rep ? (size_t)n : 1;               /* "..." is three characters */
        if (cp != ' ') {
            keep = len;
            keep_chars = chars;
        }
    }
    dst[keep] = '\0';                               /* trailing spaces trimmed */
    return keep_chars;
}

int tb_text_unsupported(const char *s, uint32_t *out, int max)
{
    int found = 0;
    if (!s) return 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int32_t cp;
        p += utf8_decode(p, &cp);
        if (cp < 0) cp = 0xFFFD;                    /* an invalid byte can't be drawn either */
        if (tb_text_drawable((uint32_t)cp)) continue;
        bool dup = false;
        for (int i = 0; i < found && i < max; i++)
            if (out[i] == (uint32_t)cp) dup = true;
        if (dup) continue;
        if (found < max) out[found] = (uint32_t)cp;
        found++;
    }
    return found;
}

void tb_text_replace_unsupported(char *s, size_t cap)
{
    (void)cap;  /* the result is never longer than the input */
    if (!s) return;
    const unsigned char *r = (const unsigned char *)s;
    char *w = s;
    while (*r) {
        int32_t cp;
        int n = utf8_decode(r, &cp);
        if (cp >= 0 && tb_text_drawable((uint32_t)cp)) {
            memmove(w, r, (size_t)n);
            w += n;
        } else {
            *w++ = '?';
        }
        r += n;
    }
    *w = '\0';
}

/* Byte offset of the character at index n (or the end of the string). */
static size_t char_offset(const char *s, size_t n)
{
    size_t i = 0, c = 0;
    while (s[i]) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) {
            if (c == n) return i;
            c++;
        }
        i++;
    }
    return i;
}

void tb_text_ellipsize(char *s, size_t cap, size_t n)
{
    if (!s || !cap || n == 0) return;
    if (tb_utf8_len(s) <= n) return;
    size_t keep = char_offset(s, n - 1);
    while (keep + 3 + 1 > cap && keep > 0) {        /* make room for the 3-byte ellipsis */
        do keep--; while (keep > 0 && ((unsigned char)s[keep] & 0xC0) == 0x80);
    }
    if (keep + 3 + 1 > cap) {                       /* not even the ellipsis fits */
        s[0] = '\0';
        return;
    }
    memcpy(s + keep, "\xE2\x80\xA6", 4);            /* U+2026 and the NUL */
}

void tb_strlcpy(char *dst, const char *src, size_t cap)
{
    if (!cap) return;
    size_t n = src ? strlen(src) : 0;
    if (n >= cap) {
        n = cap - 1;
        /* never leave half a UTF-8 character at the end */
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}
