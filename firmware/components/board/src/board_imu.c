/*
 * board_imu.c: the QMI8658 accelerometer for flip detection. Owner: board builder.
 *
 * Register sequence from SensorLib's SensorQMI8658 (used by Waveshare's 03_I2C_QMI8658 and 11_FactoryProgram),
 * reduced to the accelerometer: soft reset (0x60 = 0xB0, wait for 0x4D = 0x80), CTRL1 = 0x40 (address
 * auto-increment, little-endian), CTRL2 = +-2 g at 62.5 Hz, CTRL5 = low-pass filter on (13.37% of ODR), CTRL7 =
 * accelerometer on; data at 0x35..0x3A. The gyroscope stays off.
 * The address is 0x6B in Waveshare's code (SA0 is tied to ground on the V2 schematic); 0x6A is tried too.
 * The decisions (axis, thresholds, hysteresis, steadiness) are brd_orient_*() in logic/, tested on the host.
 */
#include "esp_check.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "board_internal.h"
#include "brd_logic.h"
#include "tb_bus.h"

static const char *TAG = "board.imu";

#define QMI_WHO_AM_I    0x00
#define QMI_CTRL1       0x02
#define QMI_CTRL2       0x03
#define QMI_CTRL5       0x06
#define QMI_CTRL7       0x08
#define QMI_AX_L        0x35
#define QMI_RST_RESULT  0x4D
#define QMI_RESET       0x60
#define QMI_ID          0x05

#define IMU_TASK_STACK  4096    /* logs on flips and read errors (the protocol-safe logger adds about 300 B) */
#define IMU_TASK_PRIO   3
#define IMU_TASK_CORE   0

#if CONFIG_TINYBAR_IMU_UP_X_POS
#define IMU_UP BRD_UP_X_POS
#define IMU_UP_NAME "+X"
#elif CONFIG_TINYBAR_IMU_UP_X_NEG
#define IMU_UP BRD_UP_X_NEG
#define IMU_UP_NAME "-X"
#elif CONFIG_TINYBAR_IMU_UP_Y_NEG
#define IMU_UP BRD_UP_Y_NEG
#define IMU_UP_NAME "-Y"
#else
#define IMU_UP BRD_UP_Y_POS
#define IMU_UP_NAME "+Y"
#endif

static i2c_master_dev_handle_t s_dev;
static bool s_boot_flipped;
static TaskHandle_t s_task;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, 2, BOARD_I2C_TIMEOUT_MS);
}

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, BOARD_I2C_TIMEOUT_MS);
}

/* One accelerometer sample in mg. */
static esp_err_t read_mg(int32_t *x, int32_t *y, int32_t *z)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t b[6];
    esp_err_t err = rd(QMI_AX_L, b, sizeof b);
    if (err != ESP_OK) return err;
    *x = brd_qmi_raw_to_mg((int16_t)(b[0] | (b[1] << 8)));
    *y = brd_qmi_raw_to_mg((int16_t)(b[2] | (b[3] << 8)));
    *z = brd_qmi_raw_to_mg((int16_t)(b[4] | (b[5] << 8)));
    return ESP_OK;
}

esp_err_t board_imu_read_mg(int32_t *x, int32_t *y, int32_t *z)
{
    return read_mg(x, y, z);
}

static bool probe(i2c_master_bus_handle_t bus, uint8_t addr)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BOARD_I2C_HZ,
    };
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) return false;
    uint8_t id = 0;
    if (rd(QMI_WHO_AM_I, &id, 1) == ESP_OK && id == QMI_ID) return true;
    i2c_master_bus_rm_device(s_dev);
    s_dev = NULL;
    return false;
}

esp_err_t board_imu_init(void)
{
    if (s_dev) return ESP_OK;
    i2c_master_bus_handle_t bus = board_sys_bus();
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "board_power_hold() first");
    uint8_t addr = BOARD_ADDR_IMU;
    if (!probe(bus, addr)) {
        addr = 0x6A;
        if (!probe(bus, addr)) {
            ESP_LOGE(TAG, "QMI8658 not found at 0x6B or 0x6A: no flip detection");
            return ESP_ERR_NOT_FOUND;
        }
    }
    wr(QMI_RESET, 0xB0);
    uint8_t r = 0;
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
        if (rd(QMI_RST_RESULT, &r, 1) == ESP_OK && r == 0x80) break;
    }
    esp_err_t err = wr(QMI_CTRL1, 0x40);                    /* ADDR_AI, little-endian, oscillator on */
    if (err == ESP_OK) err = wr(QMI_CTRL2, (0 << 4) | 0x07);  /* +-2 g, 62.5 Hz */
    if (err == ESP_OK) err = wr(QMI_CTRL5, (3 << 1) | 0x01);  /* accelerometer low-pass on, 13.37% of ODR */
    if (err == ESP_OK) err = wr(QMI_CTRL7, 0x01);             /* accelerometer on, gyroscope off */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658 setup failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(40));                          /* first samples */
    ESP_LOGI(TAG, "QMI8658 at 0x%02x, up axis %s", addr, IMU_UP_NAME);
    return ESP_OK;
}

bool board_imu_read_flipped(bool *flipped)
{
    *flipped = false;
    s_boot_flipped = false;
    if (!s_dev) return false;
    int32_t sx = 0, sy = 0, sz = 0;
    int n = 0;
    for (int i = 0; i < 4; i++) {
        int32_t x, y, z;
        if (read_mg(&x, &y, &z) == ESP_OK) {
            sx += x;
            sy += y;
            sz += z;
            n++;
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
    if (n == 0) return false;
    sx /= n;
    sy /= n;
    sz /= n;
    int c = brd_orient_classify(IMU_UP, sx, sy, sz, BRD_OR_BOOT_MG);
    /* For bring-up: standing the normal way up, the up axis should read about +1000 mg (README). */
    ESP_LOGI(TAG, "IMU at start: x=%ld y=%ld z=%ld mg -> %s", (long)sx, (long)sy, (long)sz,
             c == 0 ? "upright" : c == 1 ? "upside down" : "can't tell (upright assumed)");
    *flipped = c == 1;
    s_boot_flipped = *flipped;
    return true;
}

static void post(bool flipped, bool initial)
{
    tb_event_t ev = {.kind = TB_EV_ORIENTATION};
    ev.u.orient.flipped = flipped;
    ev.u.orient.initial = initial;
    tb_bus_post(&ev);
}

static void imu_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    brd_orient_t o;
    brd_orient_init(&o, IMU_UP, s_boot_flipped, board_now_ms());
    TickType_t last = xTaskGetTickCount();
    uint32_t errors = 0;
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(BRD_OR_PERIOD_MS));
        esp_task_wdt_reset();
        int32_t x, y, z;
        if (read_mg(&x, &y, &z) != ESP_OK) {
            if ((errors++ % 250) == 0) ESP_LOGW(TAG, "read failed (%lu so far)", (unsigned long)errors);
            continue;
        }
        bool flipped = false;
        switch (brd_orient_feed(&o, x, y, z, board_now_ms(), &flipped)) {
        case BRD_OR_INITIAL:
            post(flipped, true);
            break;
        case BRD_OR_CHANGED:
            ESP_LOGI(TAG, "flipped: %s (x=%ld y=%ld z=%ld mg)", flipped ? "upside down" : "upright", (long)x, (long)y,
                     (long)z);
            post(flipped, false);
            break;
        default:
            break;
        }
    }
}

esp_err_t board_imu_start(void)
{
#if CONFIG_TINYBAR_BOARD_BRINGUP
    board_bringup_start();
#endif
    if (s_task) return ESP_OK;
    if (!s_dev) return ESP_ERR_INVALID_STATE;   /* no IMU: main's starting orientation stays */
    BaseType_t ok = xTaskCreatePinnedToCore(imu_task, "imu", IMU_TASK_STACK, NULL, IMU_TASK_PRIO, &s_task,
                                            IMU_TASK_CORE);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
