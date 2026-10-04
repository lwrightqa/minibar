/* core_fixture.c: see core_fixture.h. Owner: core builder. */
#include <string.h>

#include "core_fixture.h"

static bench_t s_bench;

void bench_drain(bench_t *b)
{
    tb_effect_t fx[TB_EFFECTS_MAX];
    int n;
    while ((n = tb_app_take_effects(&b->a, fx, TB_EFFECTS_MAX)) > 0)
        for (int i = 0; i < n && b->nlog < FX_LOG_MAX; i++) b->log[b->nlog++] = fx[i];
}

void bench_clear_log(bench_t *b)
{
    bench_drain(b);
    b->nlog = 0;
}

static void step(bench_t *b, tb_ms_t ms)
{
    b->now.mono += ms;
    b->now.wall = T0_WALL + (b->now.mono - T0_MONO) / 1000;
}

void bench_jump(bench_t *b, tb_ms_t ms)
{
    step(b, ms);
}

void bench_run(bench_t *b, tb_ms_t ms)
{
    while (ms > 0) {
        tb_ms_t d = ms < 50 ? ms : 50;
        step(b, d);
        ms -= d;
        tb_app_pointer_poll(&b->a, &b->now);
        tb_app_tick(&b->a, &b->now);
        bench_drain(b);
    }
}

bench_t *bench_new_opts(bool wifi_configured, bool skip_splash)
{
    bench_t *b = &s_bench;
    memset(b, 0, sizeof(*b));
    b->now = (tb_clock_t){.mono = T0_MONO, .wall = T0_WALL, .valid = true};
    tb_settings_t s;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    tb_app_init(&b->a, &s, wifi_configured, &b->now);
    tb_app_restore(&b->a, TB_ST_BUSY, TB_ST_BUSY, "", 0, 0, 0, tb_local_yyyymmdd(T0_WALL));
    if (wifi_configured) tb_app_wifi_link(&b->a, true, "10.0.4.42", "tinybar.local", &b->now);
    if (wifi_configured) tb_strlcpy_test(b->a.wifi_ssid, "Office-WiFi", sizeof(b->a.wifi_ssid));
    if (skip_splash) {
        bench_run(b, TB_BOOT_SPLASH_MS + 50);
        bench_run(b, TB_TOAST_MS + 50);     /* "Ready" goes */
    }
    bench_clear_log(b);
    return b;
}

bench_t *bench_new(void)
{
    return bench_new_opts(true, true);
}

int fx_count(const bench_t *b, tb_effect_kind_t k)
{
    int n = 0;
    for (int i = 0; i < b->nlog; i++) n += b->log[i].kind == k;
    return n;
}

int32_t fx_last(const bench_t *b, tb_effect_kind_t k)
{
    for (int i = b->nlog - 1; i >= 0; i--)
        if (b->log[i].kind == k) return b->log[i].arg;
    return -999;
}

static void ptr(bench_t *b, bool pressed, int x, int y, int tile)
{
    tb_app_pointer(&b->a, pressed, (int16_t)x, (int16_t)y, (int8_t)tile, &b->now);
    bench_drain(b);
}

void tap(bench_t *b)
{
    ptr(b, true, 200, 80, TB_TILE_NONE);
    bench_run(b, 50);
    ptr(b, false, 200, 80, TB_TILE_NONE);
}

void tap_tile(bench_t *b, int tile)
{
    ptr(b, true, 60 + 128 * tile, 80, tile);
    bench_run(b, 50);
    ptr(b, false, 60 + 128 * tile, 80, tile);
}

int tile_index(const bench_t *b, tb_action_t act)
{
    for (int i = 0; i < b->a.menu.n; i++)
        if (b->a.menu.tiles[i].action == act) return i;
    return -1;
}

const tb_tile_t *tile_with(const bench_t *b, tb_action_t act)
{
    int i = tile_index(b, act);
    return i < 0 ? NULL : &b->a.menu.tiles[i];
}

void tap_tile_named(bench_t *b, tb_action_t act)
{
    int i = tile_index(b, act);
    if (i < 0) {
        TB_FAIL_AT("no tile with action %d in menu %d", (int)act, (int)b->a.menu.kind);
        return;
    }
    tap_tile(b, i);
}

void swipe(bench_t *b, int dx)
{
    int x0 = 320, y = 80;
    ptr(b, true, x0, y, TB_TILE_NONE);
    for (int k = 1; k <= 4; k++) {
        bench_run(b, 20);
        ptr(b, true, x0 + dx * k / 4, y, TB_TILE_NONE);
    }
    ptr(b, false, x0 + dx, y, TB_TILE_NONE);
}

void hold(bench_t *b)
{
    ptr(b, true, 200, 80, TB_TILE_NONE);
    bench_run(b, 700);
    ptr(b, false, 200, 80, TB_TILE_NONE);
}

void boot_btn(bench_t *b)
{
    tb_app_button(&b->a, TB_BTN_BOOT, &b->now);
    bench_drain(b);
}

void pwr_hold(bench_t *b, tb_ms_t ms)
{
    tb_app_button(&b->a, TB_BTN_PWR_DOWN, &b->now);
    bench_run(b, ms);
    tb_app_button(&b->a, TB_BTN_PWR_UP, &b->now);
    bench_drain(b);
}

void pwr_press(bench_t *b)
{
    pwr_hold(b, 100);
}

void flip(bench_t *b)
{
    tb_app_flip(&b->a, !b->a.flipped, false, &b->now);
    bench_drain(b);
}

void go_status(bench_t *b, tb_status_t st)
{
    TB_EQ_INT(tb_app_remote_status(&b->a, st, NULL, NULL, true, &b->now), TB_OK);
    bench_drain(b);
}

void call_start(bench_t *b, const char *app)
{
    tb_call_t c = {0};
    c.active = true;
    c.id = ++b->call_seq;
    if (app) tb_strlcpy_test(c.app, app, sizeof(c.app));
    c.via = TB_LINK_USB;
    c.since_ms = b->now.mono;
    c.since = b->now.wall;
    tb_app_set_mac_link(&b->a, TB_LINK_USB, &b->now);
    tb_app_set_call(&b->a, &c, NULL, &b->now);
    bench_drain(b);
}

void call_end(bench_t *b, const char *lead)
{
    tb_app_set_call(&b->a, NULL, lead, &b->now);
    bench_drain(b);
}

static int s_mt_seq = 100;

void cal_meetings(bench_t *b, const mt_t *m, int n)
{
    static tb_meeting_t list[TB_MEETINGS_MAX];
    memset(list, 0, sizeof(list));
    for (int i = 0; i < n; i++) {
        list[i].id = (uint32_t)++s_mt_seq;
        list[i].start = b->now.wall + m[i].start_min * 60;
        list[i].end = list[i].start + m[i].len_min * 60;
        if (m[i].title) tb_strlcpy_test(list[i].title, m[i].title, sizeof(list[i].title));
        if (m[i].loc) tb_strlcpy_test(list[i].location, m[i].loc, sizeof(list[i].location));
        list[i].priv = m[i].priv;
    }
    tb_app_set_meetings(&b->a, list, n, &b->now);
    bench_drain(b);
}

void cal_save(bench_t *b, const mt_t *m, int n)
{
    cal_meetings(b, m, n);
    tb_app_calendar_event(&b->a, TB_CALEV_SAVED, &b->now);
    tb_app_set_calendar(&b->a, true, false, b->now.wall, &b->now);
    bench_drain(b);
}

void pomo_start(bench_t *b)
{
    go_status(b, TB_ST_POMODORO);
    tap(b);
}

void tb_strlcpy_test(char *dst, const char *src, size_t cap)
{
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}
