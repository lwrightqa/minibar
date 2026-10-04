/* ui_fonts.c: role -> font. Owner: ui builder. Skeleton: Montserrat 14 for every role until fonts/ is generated. */
#include "ui_fonts.h"

const lv_font_t *ui_font(ui_font_role_t role)
{
    (void)role;
    return &lv_font_montserrat_14;
}
