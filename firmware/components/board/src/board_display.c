/*
 * board_display.c: the AXS15231B panel and touch for LVGL 9. Owner: board builder.
 *
 * Skeleton: creates an LVGL display of the right size and rotation with PSRAM buffers and a flush callback that
 * draws nothing, so the ui can run end to end. TODO(board): port 10_LVGL_V9_Test's QSPI bus, panel IO, panel init,
 * LCD reset, rotated full-frame flush in 64-line DMA chunks, the backlight PWM (LEDC, inverted) and the touch read
 * (I2C_NUM_1, command b5 ab a5 5a 00 00 00 0e 00 00 00, 32-byte reply).
 */
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board.h"
#include "board_internal.h"

static const char *TAG = "board.display";

static lv_display_t *s_disp;
static SemaphoreHandle_t s_lock;

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)area;
    (void)px;
    lv_display_flush_ready(disp);   /* TODO(board): rotate and send to the panel */
}

lv_display_t *board_display_init(bool flipped)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    lv_tick_set_cb(tick_ms);
    s_disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    if (!s_disp) return NULL;
    size_t sz = (size_t)BOARD_LCD_H_RES * BOARD_LCD_V_RES * 2;
    void *b1 = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    void *b2 = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!b1 || !b2) {
        ESP_LOGE(TAG, "no PSRAM for the frame buffers");
        return NULL;
    }
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, b1, b2, sz, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(s_disp, flush_cb);
    board_display_set_flipped(flipped);
    ESP_LOGW(TAG, "skeleton display: nothing reaches the panel yet");
    return s_disp;
}

void board_display_set_flipped(bool flipped)
{
    if (!s_disp) return;
    lv_display_set_rotation(s_disp, flipped ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90);
}

void board_backlight_set(uint8_t percent)
{
    (void)percent;  /* TODO(board): LEDC on GPIO 42 (inverted) and BL_EN on EXIO1 */
}

void board_backlight_early_off(void)
{
    /* TODO(board): GPIO 42 as in example_lcd_pwm_off_early(); BL_EN is already low after board_power_hold(). */
}

lv_indev_t *board_touch_init(lv_display_t *disp)
{
    (void)disp;
    return NULL;    /* TODO(board): lv_indev_create() with the AXS15231B read callback */
}

bool board_display_lock(uint32_t timeout_ms)
{
    return s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void board_display_unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}
