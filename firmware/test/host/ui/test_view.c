/* Seed test for the view model (lead). Owner from here: ui builder. */
#include "tb_test.h"
#include "ui_view.h"

TB_TEST(view_paints_the_status_color)
{
    static tb_app_t a;
    tb_settings_t s;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    tb_clock_t now = {.mono = 10000, .wall = 1791148920, .valid = true};
    tb_app_init(&a, &s, true, &now);
    a.booting = false;
    a.idx = TB_ST_BUSY;
    ui_view_t v;
    ui_view_build(&a, &now, &v);
    TB_EQ_INT(v.key, TB_KEY_BUSY);
    TB_EQ_INT(v.layout, UI_LAYOUT_STATUS);
    TB_TRUE(v.side);
}
