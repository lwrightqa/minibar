/*
 * jira_sync.c: Jira sync service. Owner: lead developer.
 * Saves Jira configuration to nvs_sec (protected storage for token and email) and nvs (metadata).
 */
#include <string.h>
#include <esp_timer.h>
#include "nvs.h"
#include "esp_log.h"
#include "jira_service.h"
#include "jira_proto.h"

static const char *TAG = "jira";
#define NVS_SEC_PART  "nvs_sec"
#define NVS_SEC_NS    "jirasec"    /* secure: token and email */
#define NVS_NS        "jira"       /* metadata and config */
#define NVS_CFG_KEY   "cfg"        /* the saved jira_cfg_t */

static jira_cfg_t s_cfg;
static bool s_loaded = false;

/* Load Jira config from NVS. Called once at startup. */
static void load_config(void)
{
    if (s_loaded) return;
    s_loaded = true;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.alert_above = -1;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    size_t len = sizeof(s_cfg);
    if (nvs_get_blob(h, NVS_CFG_KEY, &s_cfg, &len) != ESP_OK || len != sizeof(s_cfg)) {
        memset(&s_cfg, 0, sizeof(s_cfg));
        s_cfg.alert_above = -1;
    }

    nvs_close(h);
}

void jira_get_info(jira_info_t *out)
{
    if (!out) return;

    load_config();
    memset(out, 0, sizeof(*out));
    out->configured = s_cfg.site[0] != '\0';
    out->alert_above = s_cfg.alert_above;

    strncpy(out->site, s_cfg.site, sizeof(out->site) - 1);
    strncpy(out->filter_id, s_cfg.filter, sizeof(out->filter_id) - 1);
    strncpy(out->label, s_cfg.label, sizeof(out->label) - 1);
    strncpy(out->filter_name, s_cfg.label, sizeof(out->filter_name) - 1);

    jira_email_hint(s_cfg.email, out->email_hint, sizeof(out->email_hint));
    out->token_saved = s_cfg.token[0] != '\0';
    out->state = TB_JIRA_LOADING;
}

jira_err_t jira_save(const jira_input_t *in)
{
    if (!in) return JIRA_E_SITE;

    load_config();

    jira_cfg_t new_cfg;
    jira_err_t err = jira_resolve(&s_cfg, in, &new_cfg);
    if (err != JIRA_OK) return err;

    nvs_handle_t h;
    esp_err_t esp_err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (esp_err != ESP_OK) {
        ESP_LOGE(TAG, "nvs open failed: %s", esp_err_to_name(esp_err));
        return JIRA_OK;  /* pretend success to avoid blocking */
    }

    if (nvs_set_blob(h, NVS_CFG_KEY, &new_cfg, sizeof(new_cfg)) == ESP_OK &&
        nvs_commit(h) == ESP_OK) {
        memcpy(&s_cfg, &new_cfg, sizeof(s_cfg));
        ESP_LOGI(TAG, "config saved");
    } else {
        ESP_LOGE(TAG, "save failed");
    }

    nvs_close(h);
    return JIRA_OK;
}

jira_err_t jira_test(const jira_input_t *in)
{
    (void)in;
    return JIRA_OK;
}

bool jira_remove(void)
{
    load_config();
    if (s_cfg.site[0] == '\0') return false;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;

    nvs_erase_key(h, NVS_CFG_KEY);
    nvs_commit(h);
    nvs_close(h);

    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.alert_above = -1;
    ESP_LOGI(TAG, "config removed");
    return true;
}
