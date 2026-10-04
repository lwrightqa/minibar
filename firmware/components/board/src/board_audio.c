/*
 * board_audio.c: ES8311 chime and tick. Owner: board builder.
 * Skeleton stubs; the include proves esp_codec_dev resolves. TODO(board): see board_audio.h.
 */
#include "esp_codec_dev.h"
#include "esp_log.h"

#include "board.h"

static const char *TAG = "board.audio";

esp_err_t board_audio_init(void)
{
    ESP_LOGW(TAG, "audio: skeleton, silent");
    return ESP_OK;
}

void board_audio_chime(bool to_break)
{
    ESP_LOGI(TAG, "chime (%s)", to_break ? "to a break" : "back to focus");
}

void board_audio_set_ticking(int level)
{
    ESP_LOGI(TAG, "ticking %d", level);
}

void board_audio_stop(void)
{
}
