/*
 * jira_sync.c: the Jira service on the device. Saves the settings to NVS (nvs_sec for the token and email, nvs for the
 * rest), asks Jira over HTTPS for the filter's name and count, and tells the app task the screen's state over the bus.
 * Owner: lead developer.
 *
 * One task ("jira_sync", core 0, priority 2) makes every request, one at a time: a Test first (the typed settings, nothing
 * saved), then the periodic check (every TB_JIRA_EVERY_S while online, sooner after a failure, at once after a Save and
 * when Wi-Fi comes back). The name is asked on Save and Test, and at start-up while the label follows it; the periodic
 * check asks only the count. Every other function changes the shared state under s.mx and wakes the task, so none of
 * them waits for the network. Requests use esp_http_client over TLS with the certificate bundle, as the calendar does.
 * Redirects are not followed: the Authorization header must never leave the site.
 *
 * Privacy: the token and the email are never logged, the Authorization value is wiped after each request, and esp_http_client's
 * own URL log is silenced.
 *
 * RAM only, never NVS: the filter's name, the screen's state and the back-off. The saved blob (jira_cfg_t) is unchanged.
 */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include "jira_service.h"
#include "jira_proto.h"
#include "tb_bus.h"

static const char *TAG = "jira";
#define NVS_SEC_PART  "nvs_sec"
#define NVS_SEC_NS    "jirasec"    /* secure: token and email */
#define NVS_NS        "jira"       /* metadata and config */
#define NVS_CFG_KEY   "cfg"        /* the saved jira_cfg_t */

#define JIRA_NET_TIMEOUT_MS    5000             /* each connect or read: a test is two requests, so 10 s at most (the Remote polls for 20) */
#define JIRA_RX_MAX            2048             /* the answers are a few hundred bytes; a longer one is cut and the parsers cope */
#define JIRA_WAIT_MAX_MS       60000            /* the clock and Wi-Fi are looked at again at least this often */
#define JIRA_TASK_STACK        10240            /* mbedTLS's handshake runs on this stack */
#define JIRA_TASK_PRIO         2
#define JIRA_TASK_CORE         0
#define JIRA_CLOCK_VALID_AFTER 1735689600       /* 2025-01-01: earlier means the clock was never set */

static jira_cfg_t s_cfg;            /* the saved settings; written on the app task under s.mx */
static bool s_loaded = false;

/* Used only by the task, so kept out of its stack (the TLS handshake needs the room). */
static jira_cfg_t s_run_cfg;
static char s_auth[JIRA_AUTH_MAX];
static char s_rx[JIRA_RX_MAX];

static struct {
    SemaphoreHandle_t mx;
    TaskHandle_t task;
    bool online;
    uint32_t gen;                   /* bumped by a Save or Remove: a check started before it is thrown away */
    char filter_name[TB_JIRA_LABEL_BYTES];  /* Jira's name for the filter, "" until read: an automatic label follows it */
    bool name_due;                  /* ask for the name at the next check */
    tb_jira_t view;                 /* the screen's state, as the app task has it */
    int fails;                      /* checks in a row that didn't work */
    int64_t next_ms;                /* mono ms of the next periodic check; 0 = as soon as possible */
    /* Test: the request waits in test_cfg (wiped after use) until the task takes it */
    bool test_req;
    jira_cfg_t test_cfg;
    jira_test_state_t test_state;
    char test_name[TB_JIRA_LABEL_BYTES];
    int32_t test_count;
    const char *test_error;
} s;

static int64_t mono_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static tb_epoch_t wall_now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

static bool clock_ok(void)
{
    return wall_now() > JIRA_CLOCK_VALID_AFTER;
}

static void lock(void)
{
    xSemaphoreTake(s.mx, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s.mx);
}

static void wake(void)
{
    if (s.task) xTaskNotifyGive(s.task);
}

/* The screen's label: Jira's name for the filter when the label follows it and Jira has said it, else what is saved.
 * Called with s.mx held. */
static void label_of(char out[TB_JIRA_LABEL_BYTES])
{
    const char *l = s_cfg.label_auto && s.filter_name[0] ? s.filter_name : s_cfg.label;
    snprintf(out, TB_JIRA_LABEL_BYTES, "%s", l);
}

/* Hand the app task the screen's whole state. Takes s.mx itself: call without it. */
static void post_view(void)
{
    tb_event_t ev = {.kind = TB_EV_JIRA};
    lock();
    ev.u.jira = s.view;
    label_of(ev.u.jira.label);
    unlock();
    if (!tb_bus_post(&ev)) ESP_LOGW(TAG, "bus full: Jira state dropped");
}

/* ---------- NVS (the saved settings) ---------- */

static void read_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    size_t len = sizeof(s_cfg);
    if (nvs_get_blob(h, NVS_CFG_KEY, &s_cfg, &len) != ESP_OK || len != sizeof(s_cfg)) {
        memset(&s_cfg, 0, sizeof(s_cfg));
        s_cfg.alert_above = -1;
    }

    nvs_close(h);
}

/* ---------- the requests (task only) ---------- */

/* One request. JIRA_HTTP_ANSWER: the body's first part is in s_rx, *len bytes. Anything else: why not, and *retry_s is the
 * Retry-After header (0 when none). */
static jira_http_t ask(const jira_request_t *req, size_t *len, int32_t *retry_s)
{
    jira_http_t res = JIRA_HTTP_UNREACHABLE;
    *len = 0;
    *retry_s = 0;
    esp_http_client_config_t cfg = {
        .url = req->url,
        .method = req->post ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = JIRA_NET_TIMEOUT_MS,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .user_agent = "MiniBar/1 (ESP32-S3)",
        .disable_auto_redirect = true,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return res;

    esp_http_client_set_header(c, "Authorization", s_auth);
    esp_http_client_set_header(c, "Accept", "application/json");
    int body_len = req->post ? (int)strlen(req->body) : 0;
    if (req->post) esp_http_client_set_header(c, "Content-Type", "application/json");

    if (esp_http_client_open(c, body_len) == ESP_OK &&
        (body_len == 0 || esp_http_client_write(c, req->body, body_len) == body_len) &&
        esp_http_client_fetch_headers(c) >= 0) {
        int status = esp_http_client_get_status_code(c);
        ESP_LOGI(TAG, "Jira answered %d for %s", status, req->url);
        res = jira_http_class(status);
        char *ra = NULL;
        if (esp_http_client_get_header(c, "Retry-After", &ra) == ESP_OK && ra) *retry_s = jira_parse_retry_after(ra);
        if (res == JIRA_HTTP_ANSWER) {
            size_t got = 0;
            while (got < sizeof s_rx - 1) {
                int r = esp_http_client_read(c, s_rx + got, sizeof s_rx - 1 - got);
                if (r <= 0) break;
                got += (size_t)r;
            }
            s_rx[got] = '\0';
            *len = got;
        }
    } else {
        ESP_LOGW(TAG, "no answer from Jira");
    }

    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return res;
}

/* The filter's name (when want_name), then the count, with the settings in c. name and count are filled when the answer is
 * good. Returns JIRA_HTTP_ANSWER, or why not. */
static jira_http_t ask_count(const jira_cfg_t *c, bool want_name, char *name, size_t name_cap, int32_t *count, int32_t *retry_s)
{
    jira_request_t req;
    size_t len = 0;
    jira_http_t res = JIRA_HTTP_UNREACHABLE;
    name[0] = '\0';
    *count = -1;
    *retry_s = 0;
    if (jira_basic_auth(c->email, c->token, s_auth, sizeof s_auth)) {
        res = JIRA_HTTP_ANSWER;
        if (want_name) {
            res = jira_filter_request(c->host, c->filter, &req) ? ask(&req, &len, retry_s) : JIRA_HTTP_UNREACHABLE;
            if (res == JIRA_HTTP_ANSWER && !jira_parse_filter_name(s_rx, len, name, name_cap)) name[0] = '\0';
            if (res == JIRA_HTTP_NOFILTER) {
                name[0] = '\0';
                res = JIRA_HTTP_ANSWER;   /* the name is optional: the count decides whether the filter works */
            }
        }
        if (res == JIRA_HTTP_ANSWER) {
            res = jira_count_request(c->host, c->filter, &req) ? ask(&req, &len, retry_s) : JIRA_HTTP_UNREACHABLE;
            if (res == JIRA_HTTP_ANSWER && !jira_parse_count(s_rx, len, count)) res = JIRA_HTTP_UNREACHABLE;
        }
    }
    jira_wipe(s_auth, sizeof s_auth);
    return res;
}

/* A Test: the typed settings (copied from test_cfg, which is wiped), the filter's name then the count. */
static void run_test(void)
{
    char name[128];
    int32_t count = -1, retry = 0;
    lock();
    memcpy(&s_run_cfg, &s.test_cfg, sizeof s_run_cfg);
    jira_wipe(&s.test_cfg, sizeof s.test_cfg);
    s.test_req = false;
    unlock();

    jira_http_t r = ask_count(&s_run_cfg, true, name, sizeof name, &count, &retry);

    lock();
    if (r == JIRA_HTTP_ANSWER) {
        s.test_state = JIRA_TEST_OK;
        s.test_error = NULL;
        if (name[0]) jira_label_from_name(name, s.test_name);
        else snprintf(s.test_name, sizeof s.test_name, "Filter %s", s_run_cfg.filter);
        s.test_count = count;
    } else {
        s.test_state = JIRA_TEST_ERROR;
        s.test_error = r == JIRA_HTTP_TOKEN ? JIRA_ERR_TOKEN : r == JIRA_HTTP_NOFILTER ? JIRA_ERR_FILTER : JIRA_ERR_UNREACHABLE;
    }
    jira_wipe(&s_run_cfg, sizeof s_run_cfg);
    unlock();
    ESP_LOGI(TAG, "test: %s", r == JIRA_HTTP_ANSWER ? "ok" : "failed");
}

/* The periodic check (and the one after a Save): the count, and the name while it is due. Its result goes to the screen
 * only if the settings haven't changed since it started. */
static void run_check(void)
{
    char name[128];
    int32_t count = -1, retry = 0;
    lock();
    memcpy(&s_run_cfg, &s_cfg, sizeof s_run_cfg);
    bool want_name = s.name_due;
    uint32_t gen = s.gen;
    unlock();

    jira_http_t r = ask_count(&s_run_cfg, want_name, name, sizeof name, &count, &retry);
    jira_wipe(&s_run_cfg, sizeof s_run_cfg);

    tb_jira_result_t res = r == JIRA_HTTP_ANSWER ? TB_JIRA_RES_OK : r == JIRA_HTTP_TOKEN ? TB_JIRA_RES_TOKEN
                         : r == JIRA_HTTP_NOFILTER ? TB_JIRA_RES_NOFILTER : TB_JIRA_RES_UNREACHABLE;
    bool posted = false;
    lock();
    if (gen == s.gen) {
        if (want_name && r == JIRA_HTTP_ANSWER) {
            s.name_due = false;
            if (name[0]) jira_label_from_name(name, s.filter_name);
        }
        tb_jira_apply(&s.view, res, count, wall_now());
        if (r == JIRA_HTTP_ANSWER) {
            s.fails = 0;
            s.next_ms = mono_ms() + TB_JIRA_EVERY_S * 1000LL;
        } else {
            s.fails++;
            s.next_ms = mono_ms() + tb_jira_next_s(s.fails, retry) * 1000LL;
        }
        posted = true;
        ESP_LOGI(TAG, "check: %s (next in %lld s)", r == JIRA_HTTP_ANSWER ? "ok" : "failed",
                 (long long)((s.next_ms - mono_ms()) / 1000));
    }
    unlock();
    if (posted) post_view();
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        bool test = false, due = false;
        int64_t wait_ms = JIRA_WAIT_MAX_MS;
        lock();
        if (s.test_req) {
            test = true;
        } else if (s_cfg.site[0] && s.online && clock_ok()) {
            int64_t left = s.next_ms - mono_ms();
            if (left <= 0) due = true;
            else if (left < wait_ms) wait_ms = left;
        }
        unlock();

        if (test) run_test();
        else if (due) run_check();
        else ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    }
}

/* ---------- the app task's side ---------- */

/* Loads the saved settings and starts the task, once. */
static void load_config(void)
{
    if (s_loaded) return;
    s_loaded = true;
    s.mx = xSemaphoreCreateMutex();
    configASSERT(s.mx);
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.alert_above = -1;
    tb_jira_init(&s.view);
    s.test_count = -1;
    read_nvs();

    if (s_cfg.site[0]) {
        tb_jira_configure(&s.view, s_cfg.label, s_cfg.alert_above, s_cfg.goal_type, s_cfg.goal_value);
        s.name_due = s_cfg.label_auto;      /* an automatic label follows the filter's name, read at start-up */
    }
    BaseType_t ok = xTaskCreatePinnedToCore(task, "jira_sync", JIRA_TASK_STACK, NULL, JIRA_TASK_PRIO, &s.task,
                                            JIRA_TASK_CORE);
    if (ok != pdPASS) ESP_LOGE(TAG, "no task: Jira checks are off");
}

void jira_get_info(jira_info_t *out)
{
    if (!out) return;

    load_config();
    memset(out, 0, sizeof(*out));
    out->configured = s_cfg.site[0] != '\0';
    out->alert_above = s_cfg.alert_above;
    out->goal_type = s_cfg.goal_type;
    out->goal_value = s_cfg.goal_value;

    strncpy(out->site, s_cfg.site, sizeof(out->site) - 1);
    strncpy(out->filter_id, s_cfg.filter, sizeof(out->filter_id) - 1);

    jira_email_hint(s_cfg.email, out->email_hint, sizeof(out->email_hint));
    out->token_saved = s_cfg.token[0] != '\0';

    lock();
    label_of(out->label);
    strncpy(out->filter_name, s.filter_name, sizeof(out->filter_name) - 1);
    out->state = s.view.state;
    out->count = s.view.count;
    out->updated_at = s.view.ok_at;
    out->test.state = s.test_state;
    strncpy(out->test.name, s.test_name, sizeof(out->test.name) - 1);
    out->test.count = s.test_count;
    out->test.error = s.test_error;
    unlock();
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
        ESP_LOGI(TAG, "config saved");
        /* The new settings are live: the screen follows them and a check (with the name) runs at once. */
        lock();
        memcpy(&s_cfg, &new_cfg, sizeof(s_cfg));
        s.gen++;
        s.filter_name[0] = '\0';
        s.name_due = true;
        s.fails = 0;
        s.next_ms = 0;
        tb_jira_configure(&s.view, s_cfg.label, s_cfg.alert_above, s_cfg.goal_type, s_cfg.goal_value);
        unlock();
        post_view();
        wake();
    } else {
        ESP_LOGE(TAG, "save failed");
    }

    nvs_close(h);
    return JIRA_OK;
}

jira_err_t jira_test(const jira_input_t *in)
{
    if (!in) return JIRA_E_SITE;

    load_config();

    jira_cfg_t cfg;
    jira_err_t err = jira_resolve(&s_cfg, in, &cfg);
    if (err != JIRA_OK) return err;

    lock();
    if (s.test_state == JIRA_TEST_ASKING) {         /* one is already running: its answer is the one shown */
        unlock();
        jira_wipe(&cfg, sizeof cfg);
        return JIRA_OK;
    }
    s.test_error = NULL;
    s.test_name[0] = '\0';
    s.test_count = -1;
    if (!s.online || !clock_ok()) {
        s.test_state = JIRA_TEST_ERROR;
        s.test_error = JIRA_ERR_OFFLINE;
        unlock();
        jira_wipe(&cfg, sizeof cfg);
        return JIRA_OK;
    }
    s.test_state = JIRA_TEST_ASKING;
    memcpy(&s.test_cfg, &cfg, sizeof s.test_cfg);
    s.test_req = true;
    unlock();
    jira_wipe(&cfg, sizeof cfg);
    wake();
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

    /* Gone: the screen leaves the swipe order (the app task takes it out), and any check in flight is thrown away. */
    lock();
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.alert_above = -1;
    s.gen++;
    s.filter_name[0] = '\0';
    s.name_due = false;
    tb_jira_clear(&s.view);
    unlock();
    post_view();
    ESP_LOGI(TAG, "config removed");
    return true;
}

void jira_set_online(bool online)
{
    load_config();
    lock();
    s.online = online;
    if (online) s.next_ms = 0;      /* back online: check at once */
    unlock();
    wake();
}
