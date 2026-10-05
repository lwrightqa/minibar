/*
 * tb_test.h: a tiny test harness for MiniBar's host tests (no dependencies). Owner: lead developer.
 *
 *   #include "tb_test.h"
 *   TB_TEST(settings_defaults) {
 *       tb_settings_t s;
 *       tb_settings_defaults(&s, "f412fa3f2a1c");
 *       TB_EQ_INT(s.pomodoro.focus_min, 25);
 *       TB_EQ_STR(s.device.name, "MiniBar 2A1C");
 *   }
 *
 * Every TB_TEST in a module directory (every .c file in test/host/<module>/) is linked into that module's runner and run by
 * ctest. A failed check prints file:line and the values, and the test goes on to its end; the runner exits non-zero
 * if any check failed. Run one test: test_core <name-substring>.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void (*tb_test_fn)(void);
void tb_test_register(const char *name, const char *file, tb_test_fn fn);
extern int tb_test_failures;

#define TB_TEST(name)                                                                       \
    static void tb_test_##name(void);                                                       \
    __attribute__((constructor)) static void tb_test_reg_##name(void)                       \
    {                                                                                       \
        tb_test_register(#name, __FILE__, tb_test_##name);                                  \
    }                                                                                       \
    static void tb_test_##name(void)

#define TB_FAIL_AT(fmt, ...)                                                                \
    do {                                                                                    \
        tb_test_failures++;                                                                 \
        printf("    FAIL %s:%d: " fmt "\n", __FILE__, __LINE__, __VA_ARGS__);               \
    } while (0)

#define TB_TRUE(c)                                                                          \
    do {                                                                                    \
        if (!(c)) TB_FAIL_AT("%s", #c);                                                     \
    } while (0)
#define TB_FALSE(c) TB_TRUE(!(c))

#define TB_EQ_INT(a, b)                                                                     \
    do {                                                                                    \
        long long _a = (long long)(a), _b = (long long)(b);                                 \
        if (_a != _b) TB_FAIL_AT("%s == %s: %lld != %lld", #a, #b, _a, _b);                 \
    } while (0)

#define TB_EQ_STR(a, b)                                                                     \
    do {                                                                                    \
        const char *_a = (a), *_b = (b);                                                    \
        if (!_a || !_b || strcmp(_a, _b))                                                   \
            TB_FAIL_AT("%s == %s: \"%s\" != \"%s\"", #a, #b, _a ? _a : "(null)", _b ? _b : "(null)"); \
    } while (0)
