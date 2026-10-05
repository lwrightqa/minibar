/*
 * board_bringup.c: a self-test for the first runs on a real board (CONFIG_TINYBAR_BOARD_BRINGUP, off by default).
 * Owner: board builder.
 *
 * Started by board_imu_start() (the last board call in main's start-up order). Over about a minute, it logs what
 * components/board/README.md's checklist asks for: the devices on the system I2C bus, the accelerometer's raw reading
 * (to pick the IMU axis), a backlight duty sweep (to find where it goes dark), and the sounds (chime both ways, then
 * Soft and Medium ticking). With it on, the buttons and touch also log every press. The bar keeps working meanwhile,
 * but the sweep overrides the brightness for 20 s. Never ship with it on.
 */
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "board_internal.h"
#include "brd_logic.h"

#if CONFIG_TINYBAR_BOARD_BRINGUP

static const char *TAG = "board.bringup";

static void scan_bus(void)
{
    i2c_master_bus_handle_t bus = board_sys_bus();
    if (!bus) return;
    char line[160];
    int len = 0;
    for (uint16_t a = 0x08; a < 0x78 && len < (int)sizeof line - 6; a++) {
        if (i2c_master_probe(bus, a, 20) == ESP_OK) len += snprintf(line + len, sizeof line - len, " 0x%02x", a);
    }
    line[len] = 0;
    /* Expected: 0x18 ES8311, 0x20 TCA9554, 0x40 ES7210, 0x51 PCF85063, 0x6b QMI8658. */
    ESP_LOGW(TAG, "system I2C bus answers at:%s", len ? line : " nothing");
}

static void imu_log(int seconds)
{
    for (int i = 0; i < seconds * 4; i++) {
        int32_t x, y, z;
        if (board_imu_read_mg(&x, &y, &z) == ESP_OK) {
            ESP_LOGW(TAG, "IMU x=%5ld y=%5ld z=%5ld mg (standing upright, buttons on top: an axis near +1000 is the up "
                          "axis; near -1000, its negative)", (long)x, (long)y, (long)z);
        } else {
            ESP_LOGW(TAG, "IMU not answering");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static void backlight_sweep(void)
{
    ESP_LOGW(TAG, "backlight sweep: note the first duty where the screen is fully dark (TINYBAR_BL_ZERO_DUTY)");
    for (int d = 0; d <= 256; d += 16) {
        uint8_t duty = d > 255 ? 255 : (uint8_t)d;
        board_backlight_raw_duty(duty);
        ESP_LOGW(TAG, "LCD_BL duty %3u of 255 (%d%% high)", duty, duty * 100 / 255);
        vTaskDelay(pdMS_TO_TICKS(1200));
    }
    board_backlight_reapply();
}

static void sound_test(void)
{
    ESP_LOGW(TAG, "chime: rising (to a break)");
    board_audio_chime(true);
    vTaskDelay(pdMS_TO_TICKS(2500));
    ESP_LOGW(TAG, "chime: falling (back to focus)");
    board_audio_chime(false);
    vTaskDelay(pdMS_TO_TICKS(2500));
    ESP_LOGW(TAG, "ticking: Soft for 5 s (about 18 dB below the chime)");
    board_audio_set_ticking(1);
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGW(TAG, "ticking: Medium for 5 s (about 10 dB below the chime)");
    board_audio_set_ticking(2);
    vTaskDelay(pdMS_TO_TICKS(5000));
    board_audio_set_ticking(0);
    ESP_LOGW(TAG, "sound test done: listen for hiss now, with the amplifier off (gated) after 1.5 s");
}

static void bringup_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(3000));    /* past the splash */
    scan_bus();
    imu_log(10);
    backlight_sweep();
    sound_test();
    imu_log(20);
    ESP_LOGW(TAG, "bring-up self-test finished; buttons and touch keep logging");
    vTaskDelete(NULL);
}

void board_bringup_start(void)
{
    static bool started;
    if (started) return;
    started = true;
    ESP_LOGW(TAG, "CONFIG_TINYBAR_BOARD_BRINGUP is on: running the board self-test (not for shipping)");
    xTaskCreatePinnedToCore(bringup_task, "bringup", 4096, NULL, 2, NULL, 0);
}

#else

void board_bringup_start(void)
{
}

#endif
