/*
 * board_rtc.c: the PCF85063 clock chip (0x51 on the system bus). It keeps UTC. Owner: board builder.
 *
 * Registers as in SensorLib's SensorPCF85063 (Waveshare 02_I2C_PCF85063): Control_1 0x00 (bit 5 STOP, bit 1 12/24),
 * the one-byte RAM 0x03, the time 0x04..0x0A in BCD with the oscillator-stop flag in bit 7 of the seconds.
 * The chip is powered from 3V3 or the battery (diode-OR on the schematic), so on USB with no battery it forgets the
 * time whenever the bar is unplugged, and sets OS.
 *
 * Trust: a time is used only if OS is clear, the clock isn't stopped, the fields are valid (brd_rtc_decode), and the
 * RAM byte holds MiniBar's mark. The mark is written with every time MiniBar saves: Waveshare's factory program sets
 * a fixed local time (2025-07-07 18:43:30) at every start, which would otherwise pass as a valid UTC time.
 */
#include <sys/time.h>

#include "esp_check.h"
#include "esp_log.h"

#include "board.h"
#include "board_internal.h"
#include "brd_logic.h"

static const char *TAG = "board.rtc";

#define PCF_CTRL1        0x00
#define PCF_RAM          0x03
#define PCF_SECONDS      0x04
#define PCF_CTRL1_STOP   0x20
#define PCF_CTRL1_12H    0x02
#define PCF_CTRL1_TEST   0x80
#define TB_RTC_MARK      0x54    /* 'T' */

static i2c_master_dev_handle_t s_dev;

static esp_err_t wr(uint8_t reg, const uint8_t *data, size_t n)
{
    uint8_t b[9];
    if (n > sizeof b - 1) return ESP_ERR_INVALID_SIZE;
    b[0] = reg;
    for (size_t i = 0; i < n; i++) b[1 + i] = data[i];
    return i2c_master_transmit(s_dev, b, n + 1, BOARD_I2C_TIMEOUT_MS);
}

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, BOARD_I2C_TIMEOUT_MS);
}

esp_err_t board_rtc_init(bool *valid)
{
    *valid = false;
    i2c_master_bus_handle_t bus = board_sys_bus();
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "board_power_hold() first");
    if (!s_dev) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = BOARD_ADDR_RTC,
            .scl_speed_hz = BOARD_I2C_HZ,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add PCF85063");
    }
    uint8_t ctrl1 = 0;
    ESP_RETURN_ON_ERROR(rd(PCF_CTRL1, &ctrl1, 1), TAG, "PCF85063 not answering");
    bool was_stopped = ctrl1 & PCF_CTRL1_STOP;
    uint8_t want = ctrl1 & (uint8_t)~(PCF_CTRL1_STOP | PCF_CTRL1_12H | PCF_CTRL1_TEST);  /* running, 24-hour */
    if (want != ctrl1) wr(PCF_CTRL1, &want, 1);

    uint8_t b[8];   /* RAM byte, then the seven time registers */
    ESP_RETURN_ON_ERROR(rd(PCF_RAM, b, sizeof b), TAG, "read time");
    int64_t utc = 0;
    bool decoded = brd_rtc_decode(&b[1], &utc);
    if (!was_stopped && decoded && b[0] == TB_RTC_MARK) {
        struct timeval tv = {.tv_sec = (time_t)utc, .tv_usec = 0};
        settimeofday(&tv, NULL);
        *valid = true;
        ESP_LOGI(TAG, "clock set from the RTC: %lld UTC", (long long)utc);
    } else {
        ESP_LOGW(TAG, "RTC time not trusted (%s): the clock waits for the network or the Mac",
                 was_stopped ? "clock was stopped" : (b[1] & 0x80) ? "oscillator stopped (power lost)"
                 : b[0] != TB_RTC_MARK ? "never set by MiniBar" : "invalid date");
    }
    return ESP_OK;
}

esp_err_t board_rtc_save_now(void)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t utc = (int64_t)tv.tv_sec + (tv.tv_usec >= 500000 ? 1 : 0);
    if (utc < brd_days_from_civil(BRD_RTC_MIN_YEAR, 1, 1) * 86400) {
        ESP_LOGW(TAG, "system clock not set; RTC left alone");
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t ctrl1 = 0;
    ESP_RETURN_ON_ERROR(rd(PCF_CTRL1, &ctrl1, 1), TAG, "read control");
    ctrl1 &= (uint8_t)~(PCF_CTRL1_12H | PCF_CTRL1_TEST);
    uint8_t stop = ctrl1 | PCF_CTRL1_STOP, run = ctrl1 & (uint8_t)~PCF_CTRL1_STOP;
    uint8_t b[8];
    b[0] = TB_RTC_MARK;
    brd_rtc_encode(utc, &b[1]);
    /* Stop, write mark and time in one burst (writing the seconds clears OS), start: the datasheet's way to set it
     * without a carry between registers. */
    esp_err_t err = wr(PCF_CTRL1, &stop, 1);
    if (err == ESP_OK) err = wr(PCF_RAM, b, sizeof b);
    esp_err_t err2 = wr(PCF_CTRL1, &run, 1);
    if (err == ESP_OK) err = err2;
    if (err == ESP_OK) ESP_LOGI(TAG, "RTC set to %lld UTC", (long long)utc);
    else ESP_LOGW(TAG, "RTC write failed: %s", esp_err_to_name(err));
    return err;
}
