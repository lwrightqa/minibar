/*
 * test_assets.c: the generated assets keep their contracts. Owner: ui builder.
 *   - the tomato images (gen_tomatoes.py) match the mock-up's buildRipenFrames() pixel for pixel (check_ripen.py);
 *   - the converted fonts carry exactly the characters tb_text_drawable() accepts, tabular digits where the mock-up
 *     has them, and no kerning between digits (check_fonts.py).
 * Both checks are Python scripts next to this file (the generators are Python); they need python3 on the PATH.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tb_test.h"
#include "tb_text.h"

static void here(char *dir, size_t cap)
{
    snprintf(dir, cap, "%s", __FILE__);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    else snprintf(dir, cap, ".");
}

TB_TEST(tomato_frames_match_the_mockup)
{
    char dir[512], cmd[1200];
    here(dir, sizeof dir);
    snprintf(cmd, sizeof cmd, "python3 '%s/check_ripen.py'", dir);
    TB_EQ_INT(system(cmd), 0);
}

TB_TEST(fonts_carry_the_drawable_characters)
{
    char dir[512], cmd[1200], list[] = "/tmp/tb_drawable_XXXXXX";
    here(dir, sizeof dir);
    int fd = mkstemp(list);
    TB_TRUE(fd >= 0);
    FILE *f = fdopen(fd, "w");
    for (uint32_t cp = 0; cp < 0x3000; cp++)
        if (tb_text_drawable(cp)) fprintf(f, "%04X\n", (unsigned)cp);
    fclose(f);
    snprintf(cmd, sizeof cmd, "python3 '%s/check_fonts.py' '%s'", dir, list);
    TB_EQ_INT(system(cmd), 0);
    remove(list);
}
