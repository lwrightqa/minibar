/*
 * board_power.c: power hold, power off, restart, device id, watchdog. Owner: board builder.
 *
 * The V2 schematic ("KEY&POWER"): on battery, VBAT reaches the system through a P-MOSFET (Q2) whose gate is pulled
 * low either by the PWR key itself (through D4) or by T1, driven from SYS_EN (TCA9554 EXIO6). So a PWR press powers
 * the board for as long as it's held, and the firmware keeps it on by driving SYS_EN high; letting go of SYS_EN
 * switches the board off once PWR is released. On USB, the system rail comes from VBUS and SYS_EN changes nothing.
 * PWR also pulls SYS_OUT (GPIO 16, 10k pull-up to 3V3) low through D3: that's how the firmware reads it.
 * There's no VBUS sense pin, so power off doesn't ask where the power comes from: it lets go of SYS_EN, waits for PWR
 * to be released, and if the bar is still running a moment later it's on USB and goes into deep sleep instead,
 * woken by the next PWR press (ext0 on GPIO 16, low).
 */
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "board_internal.h"

static const char *TAG = "board.power";

static i2c_master_bus_handle_t s_sys_bus;

i2c_master_bus_handle_t board_sys_bus(void)
{
    return s_sys_bus;
}

int64_t board_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

esp_err_t board_power_hold(void)
{
    if (s_sys_bus && board_exio_ready()) return ESP_OK;
    /* LCD_BL to its dark level before anything else: BL_EN has a 100k pull-up to VSYS, so until the expander drives
     * it low the backlight driver is enabled. */
    board_backlight_off_now();
    if (!s_sys_bus) {
        i2c_master_bus_config_t cfg = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = BOARD_I2C_SYS_PORT,
            .scl_io_num = BOARD_I2C_SYS_SCL,
            .sda_io_num = BOARD_I2C_SYS_SDA,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_sys_bus), TAG, "system I2C bus");
    }
    /* SYS_EN high (power held), LCD reset high (not in reset), BL_EN low (dark), and the amplifier off when it's
     * gated (board_audio switches it on around each sound) or on, as Waveshare's tca9554_init() leaves it. */
    uint8_t outputs = BOARD_EXIO_SYS_EN | BOARD_EXIO_NS_MODE | BOARD_EXIO_BL_EN | BOARD_EXIO_LCD_RST;
    uint8_t levels = BOARD_EXIO_SYS_EN | BOARD_EXIO_LCD_RST;
#if !CONFIG_TINYBAR_AUDIO_AMP_GATE
    levels |= BOARD_EXIO_NS_MODE;
#endif
    ESP_RETURN_ON_ERROR(board_exio_init(s_sys_bus, outputs, levels), TAG, "TCA9554");
    ESP_LOGI(TAG, "power held (SYS_EN high)%s", board_woke_from_pwr() ? ", woken by PWR" : "");
    return ESP_OK;
}

void board_backlight_early_off(void)
{
    board_backlight_off_now();
}

bool board_on_usb(void)
{
    /* No pin tells (see the top of this file); decisions.md: the bar runs on USB. board_power_off() doesn't rely on
     * this: it finds out by letting go of SYS_EN. */
    return true;
}

bool board_woke_from_pwr(void)
{
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
}

/* Wait (feeding the watchdog) until PWR has been up for 50 ms in a row, or max_ms has passed. */
static void wait_pwr_released(int max_ms)
{
    int up_ms = 0;
    for (int t = 0; t < max_ms && up_ms < 50; t += 10) {
        up_ms = board_pwr_pressed_raw() ? 0 : up_ms + 10;
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void board_power_off(void)
{
    ESP_LOGW(TAG, "power off");
    board_audio_silence_now();
    board_display_shutdown();
    if (board_exio_ready()) board_exio_set(BOARD_EXIO_NS_MODE, false);

    /* On battery the power goes as soon as PWR is let go (it may still be held from the 3 s hold). */
    if (board_exio_ready()) board_exio_set(BOARD_EXIO_SYS_EN, false);
    wait_pwr_released(60 * 1000);
    for (int i = 0; i < 30; i++) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* Still running: the board is on USB. Sleep until the next PWR press. The wake pin has its own 10k pull-up; the
     * internal one is a second guard. If PWR is somehow still down, ext0 would wake us at once, so wait for it. */
    ESP_LOGW(TAG, "still powered, so on USB: deep sleep until PWR is pressed");
    wait_pwr_released(60 * 1000);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BOARD_BTN_PWR, 0);
    rtc_gpio_pullup_en((gpio_num_t)BOARD_BTN_PWR);
    rtc_gpio_pulldown_dis((gpio_num_t)BOARD_BTN_PWR);
    esp_deep_sleep_start();
}

void board_restart(void)
{
    /* SYS_EN stays high through the restart (the expander keeps its state and board_exio_init() never glitches it).
     * Dark and silent first, so the restart doesn't show a stale frame or click. */
    board_audio_silence_now();
    board_backlight_off_now();
    esp_restart();
}

void board_device_id(char out[13])
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, 13, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t board_wdt_add_self(void)
{
    return esp_task_wdt_add(NULL);
}

void board_wdt_feed(void)
{
    esp_task_wdt_reset();
}

void board_wdt_remove_self(void)
{
    esp_task_wdt_delete(NULL);
}
