/*
 * app_main.c: TinyBar's start-up order. Owner: lead developer. See ARCHITECTURE.md section 4, "Start-up order".
 *
 * The order matters:
 *   - Power hold comes FIRST: on battery, the board turns itself off again unless SYS_EN (TCA9554 EXIO6) is driven
 *     high. Nothing slow may come before it. It also drives the backlight pin dark, so nothing flashes.
 *   - The USB writer comes next, so every later log line goes through the protocol-safe path (api.md 6.4).
 *   - The IMU is read before the display so the splash is drawn the right way up.
 *   - The app task starts as soon as the model and the ui exist, so the splash is up within a few hundred ms; the
 *     slower parts (audio codec, Wi-Fi, calendar) start after it and only talk to it through the bus.
 *
 * Nothing here gives up on a failing part: a missing IMU, codec, RTC or NVS leaves the bar working without it (and
 * says so in the log). Only running out of memory for the display restarts it.
 */
#include <string.h>
#include <sys/time.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "lvgl.h"

#include "app.h"
#include "board.h"
#include "cal_sync.h"
#include "net.h"
#include "settings_store.h"
#include "tb_bus.h"
#include "ui.h"

static const char *TAG = "main";

static const char *reset_reason(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "restart";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_PANIC: return "crash (core dump saved)";
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "USB";
    default: return "other";
    }
}

#define WARN_IF(expr, what)                                                                       \
    do {                                                                                          \
        esp_err_t err_ = (expr);                                                                  \
        if (err_ != ESP_OK) ESP_LOGE(TAG, "%s failed (%s); carrying on without it", what, esp_err_to_name(err_)); \
    } while (0)

void app_main(void)
{
    /* 1. Keep the power on, and the panel dark until the first frame. */
    esp_err_t hold = board_power_hold();
    board_backlight_early_off();

    /* 2. The bus, and logging through the USB protocol-safe writer. */
    tb_bus_init();
    WARN_IF(net_usb_init(), "USB serial");
#if CONFIG_TINYBAR_RELEASE
    esp_log_level_set("*", ESP_LOG_WARN);
#endif
    ESP_LOGI(TAG, "TinyBar %s starting (%s%s)", esp_app_get_description()->version, reset_reason(),
             board_woke_from_pwr() ? ", PWR pressed" : "");
    if (hold != ESP_OK)
        ESP_LOGE(TAG, "power hold failed (%s): on battery the bar switches off when PWR is let go",
                 esp_err_to_name(hold));

    /* 3. NVS, identity, settings, time zone. */
    WARN_IF(settings_store_init(), "NVS (settings won't be kept)");
    char device_id[13];
    board_device_id(device_id);
    tb_settings_t settings;
    settings_store_load(&settings, device_id);
    app_apply_time_zone(settings.device.time_zone);
    ESP_LOGI(TAG, "device %s, \"%s\", time zone %s", device_id, settings.device.name,
             settings.device.time_zone[0] ? settings.device.time_zone : "not set (UTC)");

    /* 4. Which way up, before the first frame. Without an IMU the bar starts upright and never flips. */
    bool flipped = false;
    if (board_imu_init() == ESP_OK) board_imu_read_flipped(&flipped);
    else ESP_LOGW(TAG, "no IMU: the layout stays upright");

    /* 5. LVGL, display, touch, ui. */
    lv_init();
    lv_display_t *disp = board_display_init(flipped);
    if (!disp) {
        ESP_LOGE(TAG, "no memory for the display; restarting");
        board_restart();
    }
    if (!board_touch_init(disp)) ESP_LOGE(TAG, "touch failed; BOOT and PWR still work");
    ui_init(disp);

    /* 6. The wall clock from the RTC, if it holds a time TinyBar wrote (or kept through a restart or deep sleep). */
    bool rtc_valid = false;
    WARN_IF(board_rtc_init(&rtc_valid), "RTC");
    app_clock_set_valid(rtc_valid);

    /* 7. The model: settings, the saved own status and tallies, the starting orientation. net_wifi_configured()
     * only reads NVS, so it works before net_init(). */
    tb_clock_t now = app_clock_now();
    tb_app_init(&g_app, &settings, net_wifi_configured(), &now);
    settings_store_restore(&g_app);
    tb_app_flip(&g_app, flipped, true, &now);

    /* 8. The app task: from here on only it touches g_app, LVGL and the ui. The splash shows on its first frame and
     * the backlight comes on after it (core's first TB_FX_BACKLIGHT). */
    app_task_start();

    /* 9. Inputs and sound (they post to the bus, so they come after the app task). */
    WARN_IF(board_buttons_init(), "buttons");
    WARN_IF(board_audio_init(), "audio (the bar stays silent)");
    if (board_imu_start() != ESP_OK) ESP_LOGW(TAG, "no IMU: no flips");

    /* 10. Network (station or setup network, HTTP, mDNS, SNTP, the USB reader and its ready line), then the calendar
     * (posts the saved meetings, then waits for Wi-Fi). The splash (1.5 s) normally outlasts this, so core's first
     * Wi-Fi effects find net up; earlier ones wait in the app task until app_net_ready(). */
    esp_err_t net = net_init(&g_app, device_id);
    if (net == ESP_OK) net = net_start();
    if (net == ESP_OK) app_net_ready();
    else ESP_LOGE(TAG, "network failed (%s): statuses and the Pomodoro still work", esp_err_to_name(net));
    WARN_IF(cal_sync_init(), "calendar");

    /* 11. The image runs: confirm it, so OTA rollback keeps it (later: only after a self-test passes). */
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "started");
}
