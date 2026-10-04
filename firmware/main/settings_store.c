/*
 * settings_store.c: see settings_store.h. Owner: lead developer.
 */
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "settings_store.h"

static const char *TAG = "store";
#define NS "tinybar"
#define DEBOUNCE_MS 2000

typedef struct {
    uint32_t version;
    tb_settings_t s;
} settings_blob_t;

#define STATE_VERSION 1
typedef struct {
    uint32_t version;
    uint8_t idx, last_status;
    char message[TB_MESSAGE_BYTES];
    int64_t message_at;
    uint16_t done_today;
    int64_t focused_ms;
    int32_t day;
} state_blob_t;

static tb_ms_t s_settings_due, s_state_due;   /* 0 = nothing pending */
static tb_ms_t s_now;

esp_err_t settings_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing nvs (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    /* The secrets partition (calendar address, token hashes). Encryption: see ARCHITECTURE.md "Secrets". */
    err = nvs_flash_init_partition("nvs_sec");
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase_partition("nvs_sec"));
        err = nvs_flash_init_partition("nvs_sec");
    }
    return err;
}

void settings_store_load(tb_settings_t *out, const char *device_id)
{
    tb_settings_defaults(out, device_id);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    settings_blob_t b;
    size_t n = sizeof(b);
    if (nvs_get_blob(h, "settings", &b, &n) == ESP_OK && n == sizeof(b) && b.version == TB_SETTINGS_VERSION) {
        *out = b.s;
        if (tb_settings_sanitize(out)) ESP_LOGW(TAG, "settings out of range, fixed");
    }
    nvs_close(h);
}

void settings_store_restore(tb_app_t *a)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    state_blob_t b;
    size_t n = sizeof(b);
    if (nvs_get_blob(h, "state", &b, &n) == ESP_OK && n == sizeof(b) && b.version == STATE_VERSION) {
        b.message[sizeof(b.message) - 1] = '\0';
        tb_app_restore(a, (tb_status_t)b.idx, (tb_status_t)b.last_status, b.message, b.message_at, b.done_today,
                       b.focused_ms, b.day);
    }
    nvs_close(h);
}

void settings_store_mark_settings(void)
{
    s_settings_due = s_now + DEBOUNCE_MS;
    if (!s_settings_due) s_settings_due = 1;
}

void settings_store_mark_state(void)
{
    s_state_due = s_now + DEBOUNCE_MS;
    if (!s_state_due) s_state_due = 1;
}

static void write_settings(const tb_app_t *a)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    settings_blob_t b = {.version = TB_SETTINGS_VERSION, .s = a->set};
    if (nvs_set_blob(h, "settings", &b, sizeof(b)) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

static void write_state(const tb_app_t *a)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    state_blob_t b = {
        .version = STATE_VERSION,
        .idx = (uint8_t)a->idx,
        .last_status = (uint8_t)a->last_status,
        .message_at = a->message_at,
        .done_today = a->pomo.done_today,
        .focused_ms = a->pomo.focused_ms,
        .day = a->pomo.day,
    };
    strncpy(b.message, a->message, sizeof(b.message) - 1);
    if (nvs_set_blob(h, "state", &b, sizeof(b)) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

void settings_store_poll(const tb_app_t *a, tb_ms_t now)
{
    s_now = now;
    if (s_settings_due && now >= s_settings_due) {
        s_settings_due = 0;
        write_settings(a);
    }
    if (s_state_due && now >= s_state_due) {
        s_state_due = 0;
        write_state(a);
    }
}

void settings_store_flush(const tb_app_t *a)
{
    if (s_settings_due) write_settings(a);
    if (s_state_due) write_state(a);
    s_settings_due = s_state_due = 0;
}
