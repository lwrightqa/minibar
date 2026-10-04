/*
 * board_buttons.c: BOOT and PWR. Owner: board builder.
 *
 * Both are active low with pull-ups (BOOT: GPIO 0, 10k on the board; PWR: SYS_OUT on GPIO 16, 10k to 3V3, pulled
 * low through D3 by the key). An esp_timer polls them every 5 ms and brd_debounce_feed() debounces them (20 ms), as
 * 11_FactoryProgram's multi_button does with its 5 ms tick. Events go to the app task:
 *   TB_BTN_BOOT      on BOOT's release (a click)
 *   TB_BTN_PWR_DOWN  on PWR's press, TB_BTN_PWR_UP on its release
 * core times PWR itself (400 ms shows "Keep holding", 3 s powers off), which is why PWR sends both edges.
 * A button that is down when this starts (the PWR press that switched the bar on or woke it) sends nothing until it
 * has been released, and its release isn't sent either, so the press that turned the bar on can't also turn the
 * screen off or start a power-off hold.
 */
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board.h"
#include "board_internal.h"
#include "brd_logic.h"
#include "tb_app.h"
#include "tb_bus.h"

static const char *TAG = "board.buttons";

static esp_timer_handle_t s_timer;
static brd_debounce_t s_boot, s_pwr;

bool board_pwr_pressed_raw(void)
{
    return gpio_get_level(BOARD_BTN_PWR) == 0;
}

static void post(tb_button_t b)
{
#if CONFIG_TINYBAR_BOARD_BRINGUP
    ESP_LOGW(TAG, "button: %s", b == TB_BTN_BOOT ? "BOOT click" : b == TB_BTN_PWR_DOWN ? "PWR down" : "PWR up");
#endif
    tb_event_t ev = {.kind = TB_EV_BUTTON};
    ev.u.button = b;
    tb_bus_post(&ev);
}

static void poll_cb(void *arg)
{
    (void)arg;
    if (brd_debounce_feed(&s_boot, gpio_get_level(BOARD_BTN_BOOT) == 0) == BRD_EDGE_UP) post(TB_BTN_BOOT);
    switch (brd_debounce_feed(&s_pwr, board_pwr_pressed_raw())) {
    case BRD_EDGE_DOWN: post(TB_BTN_PWR_DOWN); break;
    case BRD_EDGE_UP: post(TB_BTN_PWR_UP); break;
    default: break;
    }
}

esp_err_t board_buttons_init(void)
{
    if (s_timer) return ESP_OK;
    /* After a deep-sleep wake GPIO 16 is still routed to the RTC (the ext0 wake source): hand it back. */
    rtc_gpio_deinit((gpio_num_t)BOARD_BTN_PWR);
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_BTN_BOOT) | (1ULL << BOARD_BTN_PWR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio");
    bool boot_down = gpio_get_level(BOARD_BTN_BOOT) == 0;
    bool pwr_down = board_pwr_pressed_raw();
    brd_debounce_init(&s_boot, boot_down);
    brd_debounce_init(&s_pwr, pwr_down);
    if (pwr_down) ESP_LOGI(TAG, "PWR is down since start-up: ignored until it's released");
    if (boot_down) ESP_LOGI(TAG, "BOOT is down since start-up: ignored until it's released");

    const esp_timer_create_args_t args = {
        .callback = poll_cb,
        .name = "buttons",
        .dispatch_method = ESP_TIMER_TASK,
        .skip_unhandled_events = true,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_timer, BRD_BUTTON_POLL_MS * 1000), TAG, "timer start");
    return ESP_OK;
}
