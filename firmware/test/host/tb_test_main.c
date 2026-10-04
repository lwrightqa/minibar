/* tb_test_main.c: the runner for tb_test.h. Owner: lead developer. */
#include <stdlib.h>
#include <time.h>

#include "tb_test.h"

#define MAX_TESTS 1024
static struct { const char *name, *file; tb_test_fn fn; } s_tests[MAX_TESTS];
static int s_n;
int tb_test_failures;

void tb_test_register(const char *name, const char *file, tb_test_fn fn)
{
    if (s_n < MAX_TESTS) {
        s_tests[s_n].name = name;
        s_tests[s_n].file = file;
        s_tests[s_n].fn = fn;
        s_n++;
    }
}

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : NULL;
    /* Tests that format local times use this zone unless they set their own. */
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    tzset();
    int run = 0, failed = 0;
    for (int i = 0; i < s_n; i++) {
        if (only && !strstr(s_tests[i].name, only)) continue;
        int before = tb_test_failures;
        s_tests[i].fn();
        run++;
        bool ok = tb_test_failures == before;
        failed += !ok;
        printf("%s %s\n", ok ? "  ok  " : "  FAIL", s_tests[i].name);
    }
    printf("%d tests, %d failed, %d failed checks\n", run, failed, tb_test_failures);
    return tb_test_failures ? 1 : 0;
}
