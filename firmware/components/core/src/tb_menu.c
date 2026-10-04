/*
 * tb_menu.c: the hold menus' tiles (mock-up showMenu, calTile, showTimerSettings, showWifiMenu, showPowerMenu).
 * Owner: core builder. Skeleton stub: builds the Done tile only.
 */
#include <string.h>

#include "tb_internal.h"
#include "tb_menu.h"

void tb_menu_build(tb_app_t *a, tb_menu_kind_t kind, const tb_clock_t *now)
{
    tb_menu_t *m = &a->menu;
    memset(m, 0, sizeof(*m));
    m->kind = kind;
    if (kind == TB_MENU_NONE) return;
    m->closes_at = now->mono + TB_MENU_CLOSE_MS;
    /* TODO(core): every tile of every menu, in the mock-up's order, with its states. */
    tb_tile_t *t = &m->tiles[m->n++];
    t->action = TB_ACT_CLOSE;
    t->style = TB_TILE_DONE;
    tb_strlcpy(t->label, "Close", sizeof(t->label));
    tb_strlcpy(t->value, "Done", sizeof(t->value));
    tb_strlcpy(t->foot, "or wait 8 s", sizeof(t->foot));
}
