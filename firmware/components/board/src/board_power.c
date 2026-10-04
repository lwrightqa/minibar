/*
 * board_power.c: power hold, power off, restart, device id, watchdog. Owner: board builder.
 *
 * Skeleton: board_power_hold() follows 11_FactoryProgram's tca9554_init() and example_lcd_exio_init() so the start-up
 * order can be exercised; check it on the board. Power off, USB detection and the PWR wake are TODO(board).
 */
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_io_expander_tca9554.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_task_wdt.h"

#include "board.h"
#include "board_internal.h"

static const char *TAG = "board.power";

static i2c_master_bus_handle_t s_sys_bus;
static esp_io_expander_handle_t s_exio;

i2c_master_bus_handle_t board_sys_bus(void) { return s_sys_bus; }
esp_io_expander_handle_t board_exio(void) { return s_exio; }

esp_err_t board_power_hold(void)
{
    if (s_exio) return ESP_OK;
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BOARD_I2C_SYS_PORT,
        .scl_io_num = BOARD_I2C_SYS_SCL,
        .sda_io_num = BOARD_I2C_SYS_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_sys_bus), TAG, "system I2C bus");
    ESP_RETURN_ON_ERROR(esp_io_expander_new_i2c_tca9554(s_sys_bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &s_exio),
                        TAG, "TCA9554");
    /* Power hold first, then the amplifier mode pin, as tca9554_init() does. */
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(s_exio, BOARD_EXIO_SYS_EN | BOARD_EXIO_NS_MODE, IO_EXPANDER_OUTPUT), TAG, "dir");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_exio, BOARD_EXIO_SYS_EN | BOARD_EXIO_NS_MODE, 1), TAG, "SYS_EN");
    /* The panel's lines, as example_lcd_exio_init(): touch interrupt in; backlight enable off, LCD reset high. */
    esp_io_expander_set_dir(s_exio, BOARD_EXIO_TOUCH_INT, IO_EXPANDER_INPUT);
    esp_io_expander_set_dir(s_exio, BOARD_EXIO_BL_EN | BOARD_EXIO_LCD_RST, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(s_exio, BOARD_EXIO_BL_EN, 0);
    esp_io_expander_set_level(s_exio, BOARD_EXIO_LCD_RST, 1);
    ESP_LOGI(TAG, "power held");
    return ESP_OK;
}

bool board_on_usb(void)
{
    return true;    /* TODO(board): tell USB from battery on the V2 board (decisions.md: USB for now) */
}

void board_power_off(void)
{
    /* TODO(board): backlight and panel off, amplifier off, then deep sleep with ext0 wake on GPIO 16 low (USB) or
     * SYS_EN low (battery). */
    ESP_LOGW(TAG, "power off (skeleton: deep sleep without a wake source configured)");
    esp_deep_sleep_start();
}

void board_restart(void)
{
    esp_restart();
}

bool board_woke_from_pwr(void)
{
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
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
