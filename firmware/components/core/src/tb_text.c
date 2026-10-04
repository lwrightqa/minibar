/*
 * tb_text.c: the drawable character set and text cleaning (api.md 2.3). Owner: core builder.
 * The lead wrote tb_text_drawable() and tb_utf8_len(); the rest are stubs to finish and test.
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

size_t tb_text_clean(char *dst, size_t cap, const char *src)
{
    /* TODO(core): decode UTF-8, drop control characters and invalid bytes, map ’ ‘ “ ” – — … (api.md 2.3),
     * trim, and cut on a character boundary. This placeholder only copies. */
    tb_strlcpy(dst, src, cap);
    return tb_utf8_len(dst);
}

int tb_text_unsupported(const char *s, uint32_t *out, int max)
{
    (void)s;
    (void)out;
    (void)max;
    return 0;   /* TODO(core) */
}

void tb_text_replace_unsupported(char *s, size_t cap)
{
    (void)s;
    (void)cap;  /* TODO(core) */
}

void tb_text_ellipsize(char *s, size_t cap, size_t n)
{
    (void)s;
    (void)cap;
    (void)n;    /* TODO(core): keep n-1 characters and append U+2026 when longer than n */
}

void tb_strlcpy(char *dst, const char *src, size_t cap)
{
    if (!cap) return;
    size_t n = src ? strlen(src) : 0;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}
