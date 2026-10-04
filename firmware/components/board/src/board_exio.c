/*
 * board_exio.c: the TCA9554 I/O expander (SYS_EN, BL_EN, LCD reset, amplifier). Owner: board builder.
 *
 * Registers: 0 input, 1 output, 2 polarity inversion, 3 configuration (1 = input). See board_internal.h for why this
 * isn't esp_io_expander_tca9554: its constructor resets all pins to inputs, which would drop SYS_EN for a moment on
 * every software restart.
 */
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board_internal.h"

static const char *TAG = "board.exio";

#define REG_INPUT    0x00
#define REG_OUTPUT   0x01
#define REG_POLARITY 0x02
#define REG_CONFIG   0x03

static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t s_mux;
static uint8_t s_out = 0xff;

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, sizeof b, BOARD_I2C_TIMEOUT_MS);
}

esp_err_t board_exio_init(i2c_master_bus_handle_t bus, uint8_t outputs_mask, uint8_t initial_levels)
{
    if (s_dev) return ESP_OK;
    if (!s_mux) s_mux = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_mux, ESP_ERR_NO_MEM, TAG, "mutex");
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_ADDR_EXIO,
        .scl_speed_hz = BOARD_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add TCA9554");
    /* Output levels first (pins that are still inputs take them the moment they become outputs), then polarity, then
     * the direction. Inputs keep a 1 in the output register, the chip's reset value. */
    uint8_t out = (uint8_t)((initial_levels & outputs_mask) | (uint8_t)~outputs_mask);
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 3 && err != ESP_OK; attempt++) {
        err = write_reg(REG_OUTPUT, out);
        if (err == ESP_OK) err = write_reg(REG_POLARITY, 0x00);
        if (err == ESP_OK) err = write_reg(REG_CONFIG, (uint8_t)~outputs_mask);
        if (err != ESP_OK) vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 not answering (%s)", esp_err_to_name(err));
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return err;
    }
    s_out = out;
    return ESP_OK;
}

bool board_exio_ready(void)
{
    return s_dev != NULL;
}

esp_err_t board_exio_set(uint8_t mask, bool high)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mux, portMAX_DELAY);
    uint8_t out = high ? (uint8_t)(s_out | mask) : (uint8_t)(s_out & ~mask);
    esp_err_t err = ESP_OK;
    if (out != s_out) {
        err = write_reg(REG_OUTPUT, out);
        if (err == ESP_OK) s_out = out;
    }
    xSemaphoreGive(s_mux);
    if (err != ESP_OK) ESP_LOGW(TAG, "set 0x%02x=%d: %s", mask, high, esp_err_to_name(err));
    return err;
}

bool board_exio_get_output(uint8_t mask)
{
    return (s_out & mask) == mask;
}

esp_err_t board_exio_read_inputs(uint8_t *levels)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t reg = REG_INPUT;
    xSemaphoreTake(s_mux, portMAX_DELAY);
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, levels, 1, BOARD_I2C_TIMEOUT_MS);
    xSemaphoreGive(s_mux);
    return err;
}
