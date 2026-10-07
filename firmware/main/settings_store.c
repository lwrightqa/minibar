/*
 * settings_store.c: see settings_store.h. Owner: lead developer.
 */
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "settings_store.h"

static const char *TAG = "store";
#define NS "tinybar"       /* the project's name before the rename to MiniBar; it stays (settings_store.h) */
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

/* Open one NVS partition. A partition that is full or written by an incompatible NVS version can't be read, so it's
 * erased and started again (settings go back to their defaults; the log says so). */
static esp_err_t open_partition(const char *label)
{
    esp_err_t err = nvs_flash_init_partition(label);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing %s (%s)", label, esp_err_to_name(err));
        err = nvs_flash_erase_partition(label);
        if (err == ESP_OK) err = nvs_flash_init_partition(label);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "%s unusable: %s", label, esp_err_to_name(err));
    return err;
}

esp_err_t settings_store_init(void)
{
    /* "nvs": settings, own state, Wi-Fi, the calendar's saved list (and the PHY calibration the Wi-Fi driver keeps).
     * "nvs_sec": the calendar address and token hashes. Encryption: see ARCHITECTURE.md "Secrets". */
    esp_err_t err = open_partition(NVS_DEFAULT_PART_NAME);
    esp_err_t err_sec = open_partition("nvs_sec");
    return err != ESP_OK ? err : err_sec;
}

void settings_store_load(tb_settings_t *out, const char *device_id)
{
    tb_settings_defaults(out, device_id);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    settings_blob_t b;
    size_t n = sizeof(b);
    /* tb_settings_unpack() takes this firmware's blob or the shorter one of 1.0.8 and earlier (the new fields keep
     * their defaults: 12-hour, chime on), so an update loses no setting. */
    if (nvs_get_blob(h, "settings", &b, &n) == ESP_OK && tb_settings_unpack(out, &b, n)) {
        if (tb_settings_sanitize(out)) ESP_LOGW(TAG, "settings out of range, fixed");
        if (tb_settings_migrate_name(out, device_id)) {
            /* The default name from before the rename (1.0.3). It's saved under the new one 2 s after start-up, as
             * any settings change is: the app's copy of these settings is what settings_store_poll() writes. */
            ESP_LOGI(TAG, "bar name was the old default; now \"%s\"", out->device.name);
            settings_store_mark_settings();
        }
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

static void write_blob(const char *key, const void *blob, size_t n)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, key, blob, n);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "saving %s failed: %s", key, esp_err_to_name(err));
}

static void write_settings(const tb_app_t *a)
{
    settings_blob_t b;
    memset(&b, 0, sizeof b);
    b.version = TB_SETTINGS_VERSION;
    memcpy(&b.s, &a->set, sizeof b.s);
    write_blob("settings", &b, sizeof b);
}

static void write_state(const tb_app_t *a)
{
    state_blob_t b;
    memset(&b, 0, sizeof b);    /* NVS skips a write whose bytes are unchanged, so keep the padding zero */
    b.version = STATE_VERSION;
    b.idx = (uint8_t)a->idx;
    b.last_status = (uint8_t)a->last_status;
    b.message_at = a->message_at;
    b.done_today = a->pomo.done_today;
    b.focused_ms = a->pomo.focused_ms;
    b.day = a->pomo.day;
    strncpy(b.message, a->message, sizeof(b.message) - 1);
    write_blob("state", &b, sizeof b);
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

/* Before the power goes or the chip restarts: write both, whether or not a change is due. The state signature leaves
 * out focused_ms (to spare flash), so a session's focus minutes would otherwise be lost, and the effects that queue a
 * save may come after TB_FX_POWER_OFF in the same batch. It costs no flash wear: NVS compares a blob with what's stored
 * and skips an unchanged write. */
void settings_store_flush(const tb_app_t *a)
{
    write_settings(a);
    write_state(a);
    s_settings_due = s_state_due = 0;
}
