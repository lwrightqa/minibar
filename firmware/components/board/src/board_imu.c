/*
 * board_imu.c: the QMI8658 accelerometer for flip detection. Owner: board builder.
 *
 * Register sequence from SensorLib's SensorQMI8658 (used by Waveshare's 03_I2C_QMI8658 and 11_FactoryProgram),
 * reduced to the accelerometer: soft reset (0x60 = 0xB0, wait for 0x4D = 0x80), CTRL1 = 0x40 (address
 * auto-increment, little-endian), CTRL2 = +-2 g at 62.5 Hz, CTRL5 = low-pass filter on (13.37% of ODR), CTRL7 =
 * accelerometer on; data at 0x35..0x3A. The gyroscope stays off.
 * The address is 0x6B in Waveshare's code (SA0 is tied to ground on the V2 schematic); 0x6A is tried too.
 * The decisions (axis, thresholds, hysteresis, steadiness) are brd_orient_*() in logic/, tested on the host.
 *
 * Upright is the way the user stands the bar, side buttons on top (verified 2026-10-05). The QMI8658's Y axis reading
 * about -1 g there is inferred from 1.0.1, whose steady pictures were right with +Y and no turn; so the default up
 * axis is -Y (Kconfig), and the picture is turned to match (board_display.c). The start-up log line confirms it.
 *
 * Before the first frame (board_imu_read_flipped), the reading waits for real data rather than a fixed time: it polls
 * STATUS0 (0x2E) bit 0, aDA, "accelerometer new data available; 0: no updates since last read" (QMI8658A datasheet
 * Rev A, table 22; SensorLib's getDataReady() reads the same bit when only the accelerometer runs), and feeds each
 * new sample to brd_boot_read_*() (logic/), which drops the turn-on and settling samples (3 ms + 3/ODR, datasheet
 * table 7) and anything that isn't 0.8 to 1.2 g (the flip detection's own range), and averages three good ones. If
 * the bit never shows by the end of turn-on plus one period, it reads at the data rate instead and lets the 1 g check
 * sort the samples. The whole wait ends 150 ms after the enable at the latest; the first frame waits on it.
 *
 * The last steady orientation is remembered in NVS (nvs, namespace "board", key "pose": the axis that pointed up, a
 * brd_up_axis_t) once the bar has stood still in a new one for 10 s, and used when the start-up reading can't tell
 * (lying flat) or the IMU doesn't answer. Reading it never blocks: NVS keeps its index in RAM, and an unreadable or
 * missing partition just means nothing is remembered.
 */
#include "esp_check.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

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
#define QMI_STATUS0     0x2E
#define QMI_STATUS0_ADA 0x01    /* accelerometer: new data since the last read */
#define QMI_AX_L        0x35
#define QMI_RST_RESULT  0x4D
#define QMI_RESET       0x60
#define QMI_ID          0x05

#define IMU_TASK_STACK  4096    /* logs on flips and read errors (the protocol-safe logger adds about 300 B), and the
                                   rare NVS write of the pose: a separate call chain, not on top of the logging */
#define IMU_TASK_PRIO   3
#define IMU_TASK_CORE   0

#define QMI_ODR_MS      16      /* 62.5 Hz */
#define QMI_POLL_MS     2       /* STATUS0 polling while the first frame waits */
/* No data-ready bit by the end of turn-on plus one period: read at the data rate instead. */
#define QMI_DRDY_LATE_MS (BRD_BOOT_SETTLE_MS + QMI_ODR_MS)

#define NVS_NS          "board"
#define NVS_KEY_POSE    "pose"

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

/* The V2 board's pair since 1.0.2. An sdkconfig made before then keeps +Y (Kconfig never changes a value it already
 * holds), and one made before TINYBAR_LCD_TURN_180 existed also takes the turn from sdkconfig.defaults: +Y with the
 * turn draws every pose upside down. If bring-up (board README, step 5) shows another pair, change it in Kconfig,
 * sdkconfig.defaults and here together. */
#if !(CONFIG_TINYBAR_IMU_UP_Y_NEG && CONFIG_TINYBAR_LCD_TURN_180)
#warning "IMU up axis and picture turn aren't 1.0.2's -Y with TINYBAR_LCD_TURN_180: an sdkconfig from before 1.0.2? Delete build-<name>/sdkconfig and rebuild"
#endif

static const char *const POSE_NAME[4] = {"+X", "-X", "+Y", "-Y"};   /* brd_up_axis_t order */

static i2c_master_dev_handle_t s_dev;
static int64_t s_accel_on_ms;   /* when CTRL7 turned the accelerometer on */
static bool s_boot_flipped;
static int s_saved = -1;        /* the remembered orientation (0, 1, or -1 for none), as NVS holds it */
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
    s_accel_on_ms = board_now_ms();                         /* board_imu_read_flipped() waits for its data */
    ESP_LOGI(TAG, "QMI8658 at 0x%02x, up axis %s (upright: buttons on top)", addr, IMU_UP_NAME);
    return ESP_OK;
}

/* The remembered pose (a brd_up_axis_t), or -1: nothing saved yet, or NVS unusable. Never blocks. */
static int pose_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return -1;
    uint8_t v = 0xff;
    esp_err_t err = nvs_get_u8(h, NVS_KEY_POSE, &v);
    nvs_close(h);
    return err == ESP_OK && v <= BRD_UP_Y_NEG ? v : -1;
}

static esp_err_t pose_save(brd_up_axis_t pose)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, NVS_KEY_POSE, (uint8_t)pose);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/* Wait for settled samples (at most BRD_BOOT_WAIT_MS after the enable) and classify their mean: 0 upright, 1 flipped,
 * -1 can't tell. Logs what it saw. */
static int boot_reading(void)
{
    brd_boot_read_t b;
    brd_boot_read_init(&b);
    bool done = false, drdy_seen = false, by_time = false;
    int64_t last_read = 0;
    int32_t x = 0, y = 0, z = 0;
    unsigned errors = 0;
    while (!done) {
        int64_t now = board_now_ms();
        if (now - s_accel_on_ms >= BRD_BOOT_WAIT_MS) break;
        bool fresh;
        if (by_time) {
            fresh = now - last_read >= QMI_ODR_MS;
        } else {
            uint8_t st = 0;
            if (rd(QMI_STATUS0, &st, 1) != ESP_OK) errors++;
            fresh = (st & QMI_STATUS0_ADA) != 0;
            drdy_seen = drdy_seen || fresh;
            if (!drdy_seen && now - s_accel_on_ms >= QMI_DRDY_LATE_MS) by_time = fresh = true;
        }
        if (fresh) {
            if (read_mg(&x, &y, &z) == ESP_OK) {
                last_read = now;
                done = brd_boot_read_feed(&b, x, y, z, (int32_t)(now - s_accel_on_ms));
            } else {
                errors++;
            }
        }
        if (!done) vTaskDelay(pdMS_TO_TICKS(QMI_POLL_MS));
    }
    long took = (long)(board_now_ms() - s_accel_on_ms);
    const char *how = by_time ? "; STATUS0 never showed data ready, so read at 62.5 Hz" : "";
    if (!brd_boot_read_mean(&b, &x, &y, &z)) {
        /* x, y, z still hold the last sample read, if any. */
        ESP_LOGW(TAG, "IMU at start: no settled sample of 0.8 to 1.2 g in %ld ms (%u read: %u settling, %u not 1 g, "
                      "last x=%ld y=%ld z=%ld mg; %u I2C errors%s) -> can't tell",
                 took, b.seen, b.early, b.implausible, (long)x, (long)y, (long)z, errors, how);
        return -1;
    }
    int c = brd_orient_classify(IMU_UP, x, y, z, BRD_OR_BOOT_MG);
    /* For bring-up: standing upright (buttons on top), the up axis reads about +1000 mg (README, step 5). */
    ESP_LOGI(TAG, "IMU at start: x=%ld y=%ld z=%ld mg, mean of %u samples, %ld ms after enabling it (%u read, %u while "
                  "settling%s) -> %s",
             (long)x, (long)y, (long)z, b.good, took, b.seen, b.early, how, brd_orient_name(c));
    return c;
}

bool board_imu_read_flipped(bool *flipped)
{
    int reading = s_dev ? boot_reading() : -1;
    int pose = pose_load();
    s_saved = brd_orient_from_pose(IMU_UP, pose);
    if (pose >= 0 && s_saved < 0)
        ESP_LOGW(TAG, "remembered pose %s up isn't on the %s axis: ignored", POSE_NAME[pose], IMU_UP_NAME);
    brd_boot_src_t src;
    *flipped = brd_orient_boot_choice(reading, s_saved, &src);
    s_boot_flipped = *flipped;
    if (src == BRD_BOOT_FROM_MEMORY)
        ESP_LOGI(TAG, "starting %s, as remembered from the last steady reading", brd_orient_name(*flipped));
    else if (src == BRD_BOOT_DEFAULT)
        ESP_LOGI(TAG, "starting %s (nothing remembered yet)", brd_orient_name(*flipped));
    return s_dev != NULL;
}

static void post(bool flipped, bool initial)
{
    tb_event_t ev = {.kind = TB_EV_ORIENTATION};
    ev.u.orient.flipped = flipped;
    ev.u.orient.initial = initial;
    tb_bus_post(&ev);
}

static void remember(int flipped)
{
    esp_err_t err = pose_save(brd_orient_pose(IMU_UP, flipped == 1));
    s_saved = flipped;      /* even if the write failed: the next change tries again, not every 40 ms */
    if (err == ESP_OK) ESP_LOGI(TAG, "remembered for a start lying flat: %s", brd_orient_name(flipped));
    else ESP_LOGW(TAG, "couldn't remember the orientation (%s)", esp_err_to_name(err));
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
        int64_t now = board_now_ms();
        bool flipped = false;
        switch (brd_orient_feed(&o, x, y, z, now, &flipped)) {
        case BRD_OR_INITIAL:
            if (o.vote < 0 || now - o.vote_since_ms < BRD_OR_STEADY_MS) {
                ESP_LOGI(TAG, "no steady reading in 2 s (lying flat?): staying %s", brd_orient_name(flipped));
            } else if (flipped == s_boot_flipped) {
                ESP_LOGI(TAG, "steady: %s, as drawn at start (x=%ld y=%ld z=%ld mg)", brd_orient_name(flipped),
                         (long)x, (long)y, (long)z);
            } else {
                /* The start-up picture was the wrong way up (the 1.0.1 report): worth a warning in the log. */
                ESP_LOGW(TAG, "steady: %s, so the start was drawn the wrong way up (x=%ld y=%ld z=%ld mg)",
                         brd_orient_name(flipped), (long)x, (long)y, (long)z);
            }
            post(flipped, true);
            break;
        case BRD_OR_CHANGED:
            ESP_LOGI(TAG, "turned over: %s (x=%ld y=%ld z=%ld mg)", brd_orient_name(flipped), (long)x, (long)y,
                     (long)z);
            post(flipped, false);
            break;
        default:
            break;
        }
        int want = brd_orient_to_remember(&o, s_saved, now);
        if (want >= 0) remember(want);
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
