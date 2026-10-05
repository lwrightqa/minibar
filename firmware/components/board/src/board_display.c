/*
 * board_display.c: the AXS15231B panel for LVGL 9, and the backlight. Owner: board builder.
 *
 * A port of Waveshare's 10_LVGL_V9_Test (V2 board), kept to its proven settings:
 *   - QSPI on SPI3 at 40 MHz, mode 3, 32-bit commands, CS on GPIO 9, no DC line;
 *   - init commands 0x11 (sleep out) and 0x29 (display on), 100 ms each, after the LCD reset on EXIO5
 *     (high 30 ms, low 250 ms, high 30 ms);
 *   - LVGL display at the native 172 x 640 with a full-frame RGB565 buffer in PSRAM (LV_DISPLAY_RENDER_MODE_FULL) and
 *     software rotation: LV_DISPLAY_ROTATION_90, or 270 when the bar is flipped (swapped by CONFIG_TINYBAR_LCD_TURN_180,
 *     which TinyBar ships on: upright, side buttons on top, is 270 on the V2 board; see brd_lcd_rotation()),
 *     turned by lv_draw_sw_rotate() into a second PSRAM frame. One draw buffer, not the example's two: the flush is
 *     synchronous (rotate, copy, send, then flush_ready), so a second buffer would never overlap any work;
 *   - the frame goes out in ten 64-line chunks through one internal DMA buffer, each chunk waiting for the previous
 *     transfer (on_color_trans_done). In QSPI mode the AXS15231B takes no row address: a chunk starting at row 0 is a
 *     RAMWR (0x2C) and the others continue it (RAMWRC, 0x3C), so a frame is always sent whole and in order.
 * Changes from the example: the RGB565 byte swap happens in the DMA buffer (LVGL's buffer is never modified), every
 * wait has a time-out so a dead panel can't hang the app task, there's no LVGL task or tick timer (the app task runs
 * lv_timer_handler(); lv_tick_set_cb() reads esp_timer), and the backlight waits for the first frame. While the
 * backlight is off (a dark screen), frames aren't sent: the panel can't be seen, and a running timer would otherwise
 * rotate and send 220 KB every second. The first frame after it comes back on is sent before the light.
 */
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board.h"
#include "board_internal.h"
#include "brd_logic.h"

static const char *TAG = "board.display";

#define LCD_CHUNK_LINES   64
#define LCD_CHUNK_PX      (BOARD_LCD_H_RES * LCD_CHUNK_LINES)
#define LCD_CHUNK_BYTES   (LCD_CHUNK_PX * 2)                         /* 22016 */
#define LCD_CHUNKS        (BOARD_LCD_V_RES / LCD_CHUNK_LINES)        /* 10 */
#define LCD_FRAME_BYTES   (BOARD_LCD_H_RES * BOARD_LCD_V_RES * 2)    /* 220160 */
#define LCD_WAIT_MS       100                                        /* one chunk takes about 1.1 ms */
#define LCD_OPCODE_CMD    0x02                                       /* QSPI "write command" opcode */

#define BL_LEDC_TIMER     LEDC_TIMER_3
#define BL_LEDC_CHANNEL   LEDC_CHANNEL_1
#define BL_LEDC_HZ        50000
#define BL_DEFAULT_PCT    70                                         /* the mock-up's default Light level */

_Static_assert(BOARD_LCD_V_RES % LCD_CHUNK_LINES == 0, "chunks must cover the panel exactly");

static const axs15231b_lcd_init_cmd_t s_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 100},
    {0x29, (uint8_t[]){0x00}, 0, 100},
};

static lv_display_t *s_disp;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_trans_done;
static uint16_t *s_dma;
static uint8_t *s_rot;
static bool s_panel_ok;
static uint32_t s_flush_errors;
static uint32_t s_flush_count;
static int64_t s_flush_us_total, s_flush_us_max;
static bool s_skipped_dark;         /* frames were skipped while the backlight was off: send one before the light */

/* Backlight state. */
static bool s_ledc_ok;
static bool s_frame_shown;          /* the first frame has reached the panel */
static bool s_bl_requested;         /* board_backlight_set() has been called */
static uint8_t s_bl_percent = BL_DEFAULT_PCT;
static int s_bl_duty_now = -1;

/* ---------- Backlight ---------- */

static void bl_write(uint8_t percent)
{
    uint8_t duty = brd_bl_duty(percent, CONFIG_TINYBAR_BL_ZERO_DUTY);
    if (s_ledc_ok && duty != s_bl_duty_now) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
        s_bl_duty_now = duty;
    }
    board_exio_set(BOARD_EXIO_BL_EN, percent > 0);
}

static void bl_init_pwm(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = BL_LEDC_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t c = {
        .gpio_num = BOARD_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BL_LEDC_TIMER,
        .duty = BRD_BL_DUTY_MAX,        /* dark until the first frame */
        .hpoint = 0,
    };
    s_ledc_ok = ledc_timer_config(&t) == ESP_OK && ledc_channel_config(&c) == ESP_OK;
    s_bl_duty_now = BRD_BL_DUTY_MAX;
    if (!s_ledc_ok) ESP_LOGE(TAG, "backlight PWM setup failed; BL_EN alone will switch it");
}

void board_backlight_set(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_bl_percent = percent;
    s_bl_requested = true;
    if (!s_frame_shown) return;
    if (percent > 0 && s_skipped_dark && s_panel_ok && s_disp) {
        /* The panel still holds the frame from before the screen went dark: redraw first, light after (flush_cb). */
        lv_obj_invalidate(lv_display_get_screen_active(s_disp));
        return;
    }
    bl_write(percent);
}

void board_backlight_raw_duty(uint8_t duty)
{
    if (!s_ledc_ok) return;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
    s_bl_duty_now = duty;
}

void board_backlight_reapply(void)
{
    s_bl_duty_now = -1;
    if (s_frame_shown) bl_write(s_bl_percent);
}

void board_backlight_off_now(void)
{
    if (s_ledc_ok) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, BRD_BL_DUTY_MAX);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
        s_bl_duty_now = BRD_BL_DUTY_MAX;
    } else {
        /* Before the PWM exists: drive the pin high (the dark end) as a plain output. */
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << BOARD_LCD_BL,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_set_level(BOARD_LCD_BL, 1);
        gpio_config(&io);
        gpio_set_level(BOARD_LCD_BL, 1);
    }
    board_exio_set(BOARD_EXIO_BL_EN, false);   /* does nothing before the expander is set up */
}

/* ---------- Panel ---------- */

static bool IRAM_ATTR on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    (void)io;
    (void)edata;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_trans_done, &woken);
    return woken == pdTRUE;
}

static void lcd_reset(void)
{
    board_exio_set(BOARD_EXIO_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(30));
    board_exio_set(BOARD_EXIO_LCD_RST, false);
    vTaskDelay(pdMS_TO_TICKS(250));
    board_exio_set(BOARD_EXIO_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(30));
}

static esp_err_t lcd_cmd(uint8_t cmd)
{
    if (!s_io) return ESP_ERR_INVALID_STATE;
    return esp_lcd_panel_io_tx_param(s_io, (LCD_OPCODE_CMD << 24) | ((int)cmd << 8), NULL, 0);
}

static esp_err_t panel_init(void)
{
    spi_bus_config_t bus = {
        .sclk_io_num = BOARD_LCD_PCLK,
        .data0_io_num = BOARD_LCD_D0,
        .data1_io_num = BOARD_LCD_D1,
        .data2_io_num = BOARD_LCD_D2,
        .data3_io_num = BOARD_LCD_D3,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = LCD_CHUNK_BYTES,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize((spi_host_device_t)BOARD_LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "QSPI bus");

    esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = BOARD_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 3,
        .pclk_hz = CONFIG_TINYBAR_LCD_PCLK_MHZ * 1000 * 1000,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_trans_done,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_HOST, &io, &s_io), TAG,
                        "panel IO");

    axs15231b_vendor_config_t vendor = {
        .init_cmds = s_init_cmds,
        .init_cmds_size = sizeof s_init_cmds / sizeof s_init_cmds[0],
        .flags.use_qspi_interface = 1,
    };
    esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_axs15231b(s_io, &dev, &s_panel), TAG, "AXS15231B");
    lcd_reset();
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    return ESP_OK;
}

/* Send one native 172 x 640 RGB565 frame (LVGL's byte order) to the panel. */
static bool send_frame(const uint8_t *frame)
{
    xSemaphoreTake(s_trans_done, 0);    /* drop a stale completion */
    for (int i = 0; i < LCD_CHUNKS; i++) {
        if (i > 0 && xSemaphoreTake(s_trans_done, pdMS_TO_TICKS(LCD_WAIT_MS)) != pdTRUE) return false;
        memcpy(s_dma, frame + (size_t)i * LCD_CHUNK_BYTES, LCD_CHUNK_BYTES);
        lv_draw_sw_rgb565_swap(s_dma, LCD_CHUNK_PX);    /* the panel wants big-endian pixels */
        if (esp_lcd_panel_draw_bitmap(s_panel, 0, i * LCD_CHUNK_LINES, BOARD_LCD_H_RES, (i + 1) * LCD_CHUNK_LINES,
                                      s_dma) != ESP_OK) {
            return false;
        }
    }
    return xSemaphoreTake(s_trans_done, pdMS_TO_TICKS(LCD_WAIT_MS)) == pdTRUE;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    if (s_frame_shown && s_bl_percent == 0 && s_panel_ok) {
        /* Dark screen: nothing to see, so nothing to send (see the top of the file). */
        s_skipped_dark = true;
        lv_display_flush_ready(disp);
        return;
    }
    int64_t t0 = esp_timer_get_time();
    const uint8_t *frame = px;
    lv_display_rotation_t rot = lv_display_get_rotation(disp);
    if (rot != LV_DISPLAY_ROTATION_0) {
        lv_area_t turned = *area;
        lv_display_rotate_area(disp, &turned);
        int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
        uint32_t src_stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);
        uint32_t dst_stride = lv_draw_buf_width_to_stride(lv_area_get_width(&turned), LV_COLOR_FORMAT_RGB565);
        lv_draw_sw_rotate(px, s_rot, w, h, src_stride, dst_stride, rot, LV_COLOR_FORMAT_RGB565);
        frame = s_rot;
    }
    if (s_panel_ok && send_frame(frame)) {
        int64_t us = esp_timer_get_time() - t0;
        if (!s_frame_shown) {
            s_frame_shown = true;
            if (!s_bl_requested) ESP_LOGW(TAG, "no backlight level asked for by the first frame; using %u%%", s_bl_percent);
            bl_write(s_bl_percent);
            ESP_LOGI(TAG, "first frame shown (rotate + send %lld ms), backlight %u%%", (long long)(us / 1000),
                     s_bl_percent);
        }
        if (s_skipped_dark) {
            s_skipped_dark = false;
            bl_write(s_bl_percent);     /* the light comes back with a fresh frame on the panel */
        }
        s_flush_us_total += us;
        if (us > s_flush_us_max) s_flush_us_max = us;
        s_flush_count++;
    } else if (s_panel_ok && (s_flush_errors++ % 100) == 0) {
        ESP_LOGE(TAG, "panel transfer timed out (%lu so far)", (unsigned long)s_flush_errors);
    }
    lv_display_flush_ready(disp);
}

/* Upright (buttons on top) is LVGL rotation 90 and flipped 270, unless CONFIG_TINYBAR_LCD_TURN_180 swaps them, as it
 * does by default: brd_lcd_rotation() in logic/, where the host tests check that inverting the IMU's up axis and this
 * together leaves every steady picture as it was. */
#ifndef CONFIG_TINYBAR_LCD_TURN_180
#define CONFIG_TINYBAR_LCD_TURN_180 0
#endif
static lv_display_rotation_t rotation_for(bool flipped)
{
    return brd_lcd_rotation(flipped, CONFIG_TINYBAR_LCD_TURN_180) == 270 ? LV_DISPLAY_ROTATION_270
                                                                        : LV_DISPLAY_ROTATION_90;
}

static uint32_t tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

lv_display_t *board_display_init(bool flipped)
{
    if (s_disp) return s_disp;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    s_trans_done = xSemaphoreCreateBinary();
    if (!s_lock || !s_trans_done) return NULL;

    bl_init_pwm();
    board_exio_set(BOARD_EXIO_BL_EN, false);

    s_dma = heap_caps_malloc(LCD_CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint8_t *b1 = heap_caps_aligned_alloc(64, LCD_FRAME_BYTES, MALLOC_CAP_SPIRAM);
    s_rot = heap_caps_aligned_alloc(64, LCD_FRAME_BYTES, MALLOC_CAP_SPIRAM);
    if (!s_dma || !b1 || !s_rot) {
        ESP_LOGE(TAG, "no memory for the frame buffers");
        return NULL;
    }

    esp_err_t err = panel_init();
    s_panel_ok = err == ESP_OK;
    if (!s_panel_ok) {
        /* Keep going with a display that draws nowhere, so the rest of the bar (network, Remote, Mac) still works
         * and the log says why the screen is blank. */
        ESP_LOGE(TAG, "panel init failed (%s): running without a screen", esp_err_to_name(err));
    }

    lv_tick_set_cb(tick_cb);
    s_disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    if (!s_disp) return NULL;
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, b1, NULL, LCD_FRAME_BYTES, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(s_disp, flush_cb);
    board_display_set_flipped(flipped);
    ESP_LOGI(TAG, "display %dx%d, rotation %d for %s%s", BOARD_SCREEN_W, BOARD_SCREEN_H,
             brd_lcd_rotation(flipped, CONFIG_TINYBAR_LCD_TURN_180), brd_orient_name(flipped),
             CONFIG_TINYBAR_LCD_TURN_180 ? " (CONFIG_TINYBAR_LCD_TURN_180)" : "");
    return s_disp;
}

void board_display_set_flipped(bool flipped)
{
    if (!s_disp) return;
    lv_display_rotation_t want = rotation_for(flipped);
    if (lv_display_get_rotation(s_disp) != want) lv_display_set_rotation(s_disp, want);
}

uint32_t board_display_frame_count(void)
{
    return s_flush_count;
}

void board_display_stats(uint32_t *frames, uint32_t *avg_flush_ms, uint32_t *max_flush_ms)
{
    *frames = s_flush_count;
    *avg_flush_ms = s_flush_count ? (uint32_t)(s_flush_us_total / s_flush_count / 1000) : 0;
    *max_flush_ms = (uint32_t)(s_flush_us_max / 1000);
    s_flush_us_max = 0;
}

void board_display_shutdown(void)
{
    board_backlight_off_now();
    if (s_panel_ok) {
        lcd_cmd(0x28);      /* display off */
        lcd_cmd(0x10);      /* sleep in */
    }
}

bool board_display_lock(uint32_t timeout_ms)
{
    return s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void board_display_unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}
