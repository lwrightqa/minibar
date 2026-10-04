/*
 * board_touch.c: the AXS15231B touch controller as an LVGL pointer. Owner: board builder.
 *
 * Its own I2C bus (I2C_NUM_1, SDA 17, SCL 18, address 0x3B), read by polling from LVGL's input timer in the app task,
 * as in 10_LVGL_V9_Test: write b5 ab a5 5a 00 00 00 0e 00 00 00, read 32 bytes. brd_touch_decode() turns the report
 * into native panel coordinates; LVGL applies the display's rotation itself (lv_display_rotate_point), so a flip
 * needs nothing here. The interrupt line (EXIO0) isn't used.
 */
#include "esp_check.h"
#include "esp_log.h"

#include "board.h"
#include "board_internal.h"
#include "brd_logic.h"

static const char *TAG = "board.touch";

#define TOUCH_TIMEOUT_MS   20
#define TOUCH_KEEP_ERRORS  3      /* read errors in a row that keep a held finger down before it counts as lifted */

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static lv_indev_state_t s_state = LV_INDEV_STATE_RELEASED;
static lv_point_t s_last;
static uint8_t s_err_run;
static uint32_t s_errors;

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static const uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00};
    uint8_t buf[32] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_dev, cmd, sizeof cmd, buf, sizeof buf, TOUCH_TIMEOUT_MS);
    if (err != ESP_OK) {
        /* A glitch mid-press shouldn't read as a lift (it would end a hold or make a tap); a dead bus soon does. */
        if (++s_err_run > TOUCH_KEEP_ERRORS) s_state = LV_INDEV_STATE_RELEASED;
        if ((s_errors++ % 200) == 0) ESP_LOGW(TAG, "read failed: %s (%lu so far)", esp_err_to_name(err),
                                             (unsigned long)s_errors);
    } else {
        s_err_run = 0;
        int16_t x, y;
        if (brd_touch_decode(buf, sizeof buf, &x, &y)) {
#if CONFIG_TINYBAR_BOARD_BRINGUP
            if (s_state != LV_INDEV_STATE_PRESSED) {
                ESP_LOGW(TAG, "touch down: raw x=%u y=%u -> native %d,%d", ((buf[2] & 0x0f) << 8) | buf[3],
                         ((buf[4] & 0x0f) << 8) | buf[5], x, y);
            }
#endif
            s_state = LV_INDEV_STATE_PRESSED;
            s_last.x = x;
            s_last.y = y;
        } else {
            s_state = LV_INDEV_STATE_RELEASED;
        }
    }
    data->state = s_state;
    data->point = s_last;     /* a release reports where the finger last was */
}

lv_indev_t *board_touch_init(lv_display_t *disp)
{
    if (!s_bus) {
        i2c_master_bus_config_t cfg = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = BOARD_I2C_TOUCH_PORT,
            .scl_io_num = BOARD_I2C_TOUCH_SCL,
            .sda_io_num = BOARD_I2C_TOUCH_SDA,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        if (i2c_new_master_bus(&cfg, &s_bus) != ESP_OK) {
            ESP_LOGE(TAG, "touch I2C bus failed");
            return NULL;
        }
        i2c_device_config_t dev = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = BOARD_ADDR_TOUCH,
            .scl_speed_hz = BOARD_I2C_HZ,
        };
        if (i2c_master_bus_add_device(s_bus, &dev, &s_dev) != ESP_OK) {
            ESP_LOGE(TAG, "touch device failed");
            return NULL;
        }
    }
    lv_indev_t *indev = lv_indev_create();
    if (!indev) return NULL;
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);
    if (disp) lv_indev_set_display(indev, disp);
    ESP_LOGI(TAG, "touch ready (I2C 1, 0x%02x)", BOARD_ADDR_TOUCH);
    return indev;
}
