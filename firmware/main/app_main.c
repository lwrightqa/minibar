/*
 * app_main.c: TinyBar's start-up order. Owner: lead developer. See ARCHITECTURE.md "Start-up".
 *
 * The order matters:
 *   - Power hold comes FIRST: on battery, the board turns itself off again unless SYS_EN (TCA9554 EXIO6) is driven
 *     high. Nothing slow may come before it.
 *   - The USB writer comes next, so every later log line goes through the protocol-safe path (api.md 6.4).
 *   - The IMU is read before the display so the splash is drawn the right way up.
 *   - The app task starts as soon as the model and the ui exist, so the splash is up within a few hundred ms; the
 *     slower parts (audio codec, Wi-Fi, calendar) start after it and only talk to it through the bus.
 */
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "lvgl.h"

#include "app.h"
#include "board.h"
#include "cal_sync.h"
#include "net.h"
#include "settings_store.h"
#include "tb_bus.h"
#include "ui.h"

static const char *TAG = "main";

void app_main(void)
{
    /* 1. Keep the power on. */
    ESP_ERROR_CHECK(board_power_hold());
    board_backlight_early_off();

    /* 2. The bus, and logging through the USB protocol-safe writer. */
    tb_bus_init();
    net_usb_init();
#if CONFIG_TINYBAR_RELEASE
    esp_log_level_set("*", ESP_LOG_WARN);
#endif
    ESP_LOGI(TAG, "TinyBar %s starting", esp_app_get_description()->version);

    /* 3. NVS, identity, settings, time zone. */
    ESP_ERROR_CHECK(settings_store_init());
    char device_id[13];
    board_device_id(device_id);
    tb_settings_t settings;
    settings_store_load(&settings, device_id);
    app_apply_time_zone(settings.device.time_zone);

    /* 4. Which way up, before the first frame. */
    bool flipped = false;
    if (board_imu_init() == ESP_OK) board_imu_read_flipped(&flipped);

    /* 5. Display, touch, LVGL, ui. */
    lv_init();
    lv_display_t *disp = board_display_init(flipped);
    if (!disp) {
        ESP_LOGE(TAG, "display failed; restarting");
        board_restart();
    }
    board_touch_init(disp);
    ui_init(disp);

    /* 6. The wall clock from the RTC, if it holds a valid time. */
    bool rtc_valid = false;
    board_rtc_init(&rtc_valid);
    app_clock_set_valid(rtc_valid);

    /* 7. The model: settings, the saved own status and tallies, the starting orientation. */
    tb_clock_t now = app_clock_now();
    tb_app_init(&g_app, &settings, net_wifi_configured(), &now);
    settings_store_restore(&g_app);
    tb_app_flip(&g_app, flipped, true, &now);

    /* 8. The app task: from here on only it touches g_app, LVGL and the ui. */
    app_task_start();

    /* 9. Inputs and sound. */
    board_buttons_init();
    board_audio_init();
    board_imu_start();

    /* 10. Network, then the calendar (which waits for Wi-Fi). */
    ESP_ERROR_CHECK(net_init(&g_app, device_id));
    ESP_ERROR_CHECK(net_start());
    cal_sync_init();

    /* The image runs: confirm it, so OTA rollback keeps it (later, after a self-test). */
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "started");
}
