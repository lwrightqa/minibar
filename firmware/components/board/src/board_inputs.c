/*
 * board_inputs.c: BOOT and PWR buttons, the QMI8658 flip detector and the PCF85063 RTC. Owner: board builder.
 * Skeleton stubs. TODO(board): see board_power.h and board_sensors.h for what each must do.
 */
#include "esp_log.h"

#include "board.h"
#include "board_internal.h"
#include "tb_bus.h"

static const char *TAG = "board.inputs";

esp_err_t board_buttons_init(void)
{
    ESP_LOGW(TAG, "buttons: skeleton, no events yet");
    return ESP_OK;
}

esp_err_t board_imu_init(void)
{
    return ESP_OK;
}

bool board_imu_read_flipped(bool *flipped)
{
    *flipped = false;
    return false;
}

esp_err_t board_imu_start(void)
{
    tb_event_t ev = {.kind = TB_EV_ORIENTATION};
    ev.u.orient.flipped = false;
    ev.u.orient.initial = true;
    tb_bus_post(&ev);
    return ESP_OK;
}

esp_err_t board_rtc_init(bool *valid)
{
    *valid = false;
    return ESP_OK;
}

esp_err_t board_rtc_save_now(void)
{
    return ESP_OK;
}
