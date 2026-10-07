/*
 * board_audio.c: the ES8311 codec and NS4150B amplifier: the chime, the alarm's repeats of it, and the focus tick.
 * Owner: board builder.
 *
 * Codec setup follows 08_Audio_Test's codec_board entry "S3_LCD_3_49" (ES8311 at 0x18 on the system I2C bus, I2S
 * MCLK 7, BCLK 15, WS 46, DOUT 45, MCLK used, 24 kHz 16-bit stereo), through esp_codec_dev. Differences: only the
 * output is set up (standard I2S rather than the 4-slot TDM Waveshare uses to share the bus with the ES7210
 * microphones, which MiniBar never uses), and the amplifier (EXIO7, the NS4150B's CTRL, pulled low on the board) is
 * switched on only around sounds when CONFIG_TINYBAR_AUDIO_AMP_GATE is set.
 *
 * The sounds are synthesized once at start (brd_chime_render, brd_tick_render; about 170 KB of PSRAM) at the mock-up's
 * digital levels; CONFIG_TINYBAR_AUDIO_VOLUME sets the loudness. A small task (core 1, priority 6) mixes them in
 * 10 ms chunks through brd_player and writes them to I2S; the channel runs all the time and sends silence between
 * sounds (auto_clear), so there's no click from starting and stopping it. Callers only post to a queue: nothing here
 * blocks the app task.
 */
#include <math.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "board.h"
#include "board_internal.h"
#include "brd_logic.h"

static const char *TAG = "board.audio";

#define AUDIO_CHUNK       240         /* samples: 10 ms at 24 kHz */
#define AUDIO_TASK_STACK  4096
#define AUDIO_TASK_PRIO   6
#define AUDIO_TASK_CORE   1
#define AUDIO_QUEUE_LEN   8

typedef enum { CMD_CHIME, CMD_TICKING, CMD_STOP, CMD_MEETING } cmd_kind_t;
typedef struct {
    cmd_kind_t kind;
    int arg;
} cmd_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static esp_codec_dev_handle_t s_codec;
static i2s_chan_handle_t s_tx;
static brd_player_t s_player;
static bool s_amp_on;

static void amp_set(bool on)
{
#if CONFIG_TINYBAR_AUDIO_AMP_GATE
    if (on == s_amp_on) return;
    if (board_exio_set(BOARD_EXIO_NS_MODE, on) != ESP_OK) return;
    s_amp_on = on;
    if (on) vTaskDelay(pdMS_TO_TICKS(BRD_AMP_LEAD_MS));    /* let it settle before the sound */
#else
    (void)on;
    s_amp_on = true;
#endif
}

static void apply(const cmd_t *c)
{
    int64_t now = board_now_ms();
    switch (c->kind) {
    case CMD_CHIME: brd_player_chime(&s_player, c->arg != 0); break;
    case CMD_TICKING: brd_player_set_ticking(&s_player, c->arg, now); break;
    case CMD_STOP: brd_player_stop(&s_player, now); break;
    case CMD_MEETING: brd_player_meeting_chime(&s_player); break;
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    static int16_t mono[AUDIO_CHUNK];
    static int16_t stereo[AUDIO_CHUNK * 2];
    for (;;) {
        int64_t wait = brd_player_wait_ms(&s_player, board_now_ms());
        TickType_t to = wait < 0 ? pdMS_TO_TICKS(1000) : pdMS_TO_TICKS(wait > 1000 ? 1000 : wait);
        cmd_t c;
        if (xQueueReceive(s_queue, &c, to) == pdTRUE) {
            do apply(&c);
            while (xQueueReceive(s_queue, &c, 0) == pdTRUE);
        }
        esp_task_wdt_reset();
        amp_set(brd_player_amp_wanted(&s_player, board_now_ms()));
        size_t n = brd_player_fill(&s_player, board_now_ms(), mono, AUDIO_CHUNK);
        if (n == 0) continue;
        for (size_t i = 0; i < n; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[i];
        esp_codec_dev_write(s_codec, stereo, (int)(n * 2 * sizeof(int16_t)));
    }
}

static esp_err_t codec_init(void)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear = true;     /* silence when there's nothing to play */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &s_tx, NULL), TAG, "I2S channel");
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BRD_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK,
            .bclk = BOARD_I2S_BCLK,
            .ws = BOARD_I2S_WS,
            .dout = BOARD_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "I2S std mode");

    audio_codec_i2s_cfg_t i2s_cfg = {.port = I2S_NUM_0, .rx_handle = NULL, .tx_handle = s_tx};
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BOARD_I2C_SYS_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = board_sys_bus(),
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(data_if && ctrl_if && gpio_if, ESP_FAIL, TAG, "codec interfaces");
    es8311_codec_cfg_t es = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,               /* the amplifier is on the expander, switched here */
        .use_mclk = true,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es);
    ESP_RETURN_ON_FALSE(codec_if, ESP_FAIL, TAG, "ES8311 not answering");
    esp_codec_dev_cfg_t dev = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&dev);
    ESP_RETURN_ON_FALSE(s_codec, ESP_FAIL, TAG, "codec device");
    esp_codec_dev_sample_info_t fs = {.sample_rate = BRD_AUDIO_RATE, .channel = 2, .bits_per_sample = 16};
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_codec, &fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "codec open");
    esp_codec_dev_set_out_vol(s_codec, CONFIG_TINYBAR_AUDIO_VOLUME);
    return ESP_OK;
}

esp_err_t board_audio_init(void)
{
    if (s_task) return ESP_OK;
    size_t chime_n = brd_chime_samples(BRD_AUDIO_RATE), tick_n = brd_tick_samples(BRD_AUDIO_RATE);
    size_t meet_n = brd_meeting_chime_samples(BRD_AUDIO_RATE);
    int16_t *meeting = heap_caps_malloc(meet_n * sizeof(int16_t), MALLOC_CAP_SPIRAM);     /* 40 KB */
    int16_t *chime_focus = heap_caps_malloc(chime_n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    int16_t *chime_break = heap_caps_malloc(chime_n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    int16_t *tick_a = heap_caps_malloc(tick_n * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    int16_t *tick_b = heap_caps_malloc(tick_n * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(chime_focus && chime_break && meeting && tick_a && tick_b, ESP_ERR_NO_MEM, TAG, "sound buffers");
    brd_meeting_chime_render(meeting, meet_n, BRD_AUDIO_RATE);
    brd_chime_render(chime_focus, chime_n, false, BRD_AUDIO_RATE);
    brd_chime_render(chime_break, chime_n, true, BRD_AUDIO_RATE);
    brd_tick_render(tick_a, tick_n, 2000, BRD_AUDIO_RATE);
    brd_tick_render(tick_b, tick_n, 1700, BRD_AUDIO_RATE);
    brd_player_init(&s_player, chime_focus, chime_break, chime_n, tick_a, tick_b, tick_n, board_now_ms());
    brd_player_set_meeting_chime(&s_player, meeting, meet_n);

    esp_err_t err = codec_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio unavailable (%s): the bar stays silent", esp_err_to_name(err));
        return err;
    }
    s_queue = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(cmd_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "queue");
    BaseType_t ok = xTaskCreatePinnedToCore(audio_task, "audio", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIO, &s_task,
                                            AUDIO_TASK_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    ESP_LOGI(TAG, "audio ready: volume %d, chime %.1f dBFS, ticks %.1f / %.1f dBFS", CONFIG_TINYBAR_AUDIO_VOLUME,
             (double)brd_peak_dbfs(chime_break, chime_n),
             (double)(brd_peak_dbfs(tick_a, tick_n) + 20.0f * log10f(BRD_TICK_GAIN_SOFT)),
             (double)(brd_peak_dbfs(tick_a, tick_n) + 20.0f * log10f(BRD_TICK_GAIN_MEDIUM)));
    return ESP_OK;
}

static void send(cmd_kind_t kind, int arg)
{
    if (!s_queue) return;
    cmd_t c = {.kind = kind, .arg = arg};
    if (xQueueSend(s_queue, &c, 0) != pdTRUE) ESP_LOGW(TAG, "queue full, sound dropped");
}

void board_audio_chime(bool to_break)
{
    send(CMD_CHIME, to_break);
}

void board_audio_meeting_chime(void)
{
    send(CMD_MEETING, 0);
}

void board_audio_set_ticking(int level)
{
    send(CMD_TICKING, level < 0 ? 0 : level > 2 ? 2 : level);
}

void board_audio_stop(void)
{
    send(CMD_STOP, 0);
}

void board_audio_silence_now(void)
{
    board_audio_stop();
    if (s_codec) esp_codec_dev_set_out_mute(s_codec, true);
    if (board_exio_ready()) board_exio_set(BOARD_EXIO_NS_MODE, false);
    s_amp_on = false;
}
