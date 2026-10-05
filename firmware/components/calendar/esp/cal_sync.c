/*
 * cal_sync.c: the calendar service on the device: the secret address in NVS, the HTTPS fetch streamed through the
 * ICS reader, the 10-minute schedule, PUT / remove / Sync now, and the bus posts. Owner: calendar builder.
 * See cal_sync.h for the behavior; this file is the plumbing.
 *
 * One task ("cal_sync", core 0, priority 2, stack in internal RAM because it writes NVS) runs one job at a time:
 *   check   a PUT's (or the setup page's) new address: fetch it once; only a good fetch replaces the saved address
 *   sync    the saved address: every CAL_SYNC_EVERY_S while online, sooner after a failure, and on Sync now
 * Every other function only changes the shared state under s_mx and wakes the task, so none of them blocks on the
 * network (the HTTP server and the app task call them).
 *
 * The fetch: esp_http_client over TLS with the certificate bundle, redirects followed by hand (https only, at most
 * CAL_MAX_REDIRECTS), the body streamed in 2 KB reads into cal_feed_write() (nothing buffers the file), with a size
 * limit (CAL_FETCH_MAX_BYTES) and a time limit (CAL_FETCH_BUDGET_MS). A body that isn't iCal stops the fetch within
 * its first 4 KB.
 *
 * Privacy: the address is never logged. esp_http_client logs the URL in some of its messages, so its log tag is
 * silenced (cal_sync_init); this file logs results, sizes and error codes only.
 */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "cal_ics.h"
#include "cal_store.h"
#include "cal_sync.h"
#include "cal_today.h"
#include "cal_tz.h"
#include "cal_url.h"
#include "tb_app.h"
#include "tb_bus.h"

static const char *TAG = "cal";

#define CAL_SYNC_EVERY_S     TB_CAL_SYNC_EVERY_S    /* about every 10 minutes (decisions.md) */
#define CAL_RETRY_FIRST_S    60                     /* after a failed sync: 1, 2, 4, 8 minutes, then every 10 */
#define CAL_CHECK_KEEP_MS    (10 * 60 * 1000)       /* api.md 11.1: a PUT's result is kept 10 minutes */
#define CAL_CLOCK_WAIT_MS    15000                  /* Sync now waits this long for a valid clock */
#define CAL_NET_TIMEOUT_MS   20000                  /* each connect, header or read (Google can be slow to start) */
#define CAL_FETCH_BUDGET_MS  (3 * 60 * 1000)        /* a whole fetch */
#define CAL_FETCH_MAX_BYTES  (16 * 1024 * 1024)     /* Google feeds of heavy calendars reach a few MB */
#define CAL_MAX_REDIRECTS    5
#define CAL_READ_CHUNK       2048
#define CAL_YIELD_AFTER_US   (50 * 1000)            /* block a tick after this much work without blocking, for IDLE0 */
#define CAL_TASK_STACK       10240
#define CAL_TASK_PRIO        2
#define CAL_TASK_CORE        0
#define CAL_CLOCK_VALID_AFTER 1735689600            /* 2025-01-01: earlier means the clock was never set */

#define NVS_SEC_PART  "nvs_sec"
#define NVS_SEC_NS    "calsec"
#define NVS_SEC_KEY   "url"
#define NVS_NS        "cal"
#define NVS_LIST_KEY  "list"

typedef enum { JOB_NONE = 0, JOB_CHECK, JOB_SYNC } job_kind_t;

static pthread_mutex_t s_mx = PTHREAD_MUTEX_INITIALIZER;
static TaskHandle_t s_task;

static struct {
    bool inited;
    bool online;
    cal_tz_t tz;                /* the device zone (UTC until main sets it) */
    cal_url_info_t *saved;      /* the saved address and its masked form; NULL = none */
    /* requests */
    char *check_url;            /* a PUT waiting to be checked (normalized), or NULL */
    bool check_from_setup;
    bool sync_now;              /* Sync now asked (menu or API) */
    int64_t sync_now_at;        /* when (mono ms) */
    bool report;                /* the running or next sync reports SYNCED / SYNC_FAILED */
    int64_t next_sync_ms;       /* mono ms of the next background sync; 0 = as soon as possible */
    int failures;               /* background syncs failed in a row (back-off) */
    job_kind_t running;         /* the job the task is doing now */
    uint32_t gen;               /* bumped by remove: a fetch started before it throws its result away */
    /* status */
    cal_status_t st;
    int64_t check_done_ms;      /* when the last check finished */
    /* the list last handed to the app task, and the saved copy's fingerprint */
    tb_meeting_t *last;         /* TB_MEETINGS_MAX entries (8 KB, in PSRAM: allocated by init) */
    int n_last;
    bool have_last;
    uint32_t saved_blob_hash;
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
    return wall_now() > CAL_CLOCK_VALID_AFTER;
}

static void lock(void)
{
    pthread_mutex_lock(&s_mx);
}

static void unlock(void)
{
    pthread_mutex_unlock(&s_mx);
}

static void wake(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}

/* ---------- bus ---------- */

static void post_status(bool saved, bool checking, tb_epoch_t last_sync)
{
    tb_event_t ev = {.kind = TB_EV_CAL_STATUS};
    ev.u.cal.saved = saved;
    ev.u.cal.checking = checking;
    ev.u.cal.last_sync = last_sync;
    if (!tb_bus_post(&ev)) ESP_LOGW(TAG, "bus full: status dropped");
}

static void post_event(tb_cal_event_t e)
{
    if (!tb_bus_post_kind(TB_EV_CAL_EVENT, (int32_t)e)) ESP_LOGW(TAG, "bus full: event %d dropped", (int)e);
}

static void post_meetings(const tb_meeting_t *m, int n)
{
    tb_cal_meetings_t *p = calloc(1, sizeof(*p));
    if (!p) {
        ESP_LOGE(TAG, "no memory for the meetings list");
        return;
    }
    p->n = (uint8_t)(n < TB_MEETINGS_MAX ? n : TB_MEETINGS_MAX);
    memcpy(p->m, m, (size_t)p->n * sizeof(p->m[0]));
    tb_event_t ev = {.kind = TB_EV_CAL_MEETINGS};
    ev.u.ptr = p;
    if (!tb_bus_post(&ev)) {
        ESP_LOGW(TAG, "bus full: meetings dropped");
        free(p);
    }
}

/* A snapshot of what the app task should know, posted outside the lock. */
static void post_current_status(void)
{
    lock();
    bool saved = s.st.saved, syncing = s.st.syncing;
    tb_epoch_t last = s.st.last_sync;
    unlock();
    post_status(saved, syncing, last);
}

/* ---------- NVS ---------- */

static char *load_url(void)
{
    nvs_handle_t h;
    if (nvs_open_from_partition(NVS_SEC_PART, NVS_SEC_NS, NVS_READONLY, &h) != ESP_OK) return NULL;
    size_t len = 0;
    char *url = NULL;
    if (nvs_get_str(h, NVS_SEC_KEY, NULL, &len) == ESP_OK && len > 1 && len <= CAL_URL_MAX + 1) {
        url = calloc(1, len);
        if (url && nvs_get_str(h, NVS_SEC_KEY, url, &len) != ESP_OK) {
            memset(url, 0, len);
            free(url);
            url = NULL;
        }
    }
    nvs_close(h);
    return url;
}

static esp_err_t save_url(const char *url)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_SEC_PART, NVS_SEC_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = url ? nvs_set_str(h, NVS_SEC_KEY, url) : nvs_erase_key(h, NVS_SEC_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static uint32_t fnv(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

/* Save the list if it changed (called from the task only). */
static void save_list(const tb_meeting_t *m, int n, tb_epoch_t last_sync)
{
    uint8_t *buf = malloc(CAL_STORE_BYTES_MAX);
    if (!buf) return;
    /* The fingerprint leaves out last_sync, so a sync that changes nothing doesn't write flash. */
    size_t len = cal_store_pack(m, n, 0, buf, CAL_STORE_BYTES_MAX);
    uint32_t h = fnv(buf, len);
    lock();
    bool same = h == s.saved_blob_hash;
    unlock();
    if (len && !same) {
        len = cal_store_pack(m, n, last_sync, buf, CAL_STORE_BYTES_MAX);
        nvs_handle_t nh;
        if (nvs_open(NVS_NS, NVS_READWRITE, &nh) == ESP_OK) {
            if (nvs_set_blob(nh, NVS_LIST_KEY, buf, len) == ESP_OK && nvs_commit(nh) == ESP_OK) {
                lock();
                s.saved_blob_hash = h;
                unlock();
            } else {
                ESP_LOGW(TAG, "couldn't save the meetings list");
            }
            nvs_close(nh);
        }
    }
    free(buf);
}

static int load_list(tb_meeting_t *out, int max, tb_epoch_t *last_sync)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return -1;
    size_t len = 0;
    int n = -1;
    if (nvs_get_blob(h, NVS_LIST_KEY, NULL, &len) == ESP_OK && len <= CAL_STORE_BYTES_MAX) {
        uint8_t *buf = malloc(len ? len : 1);
        if (buf && nvs_get_blob(h, NVS_LIST_KEY, buf, &len) == ESP_OK) {
            n = cal_store_unpack(buf, len, out, max, last_sync);
            if (n >= 0) {
                /* the fingerprint as save_list computes it (without last_sync), so an unchanged list isn't rewritten */
                uint8_t *b2 = malloc(CAL_STORE_BYTES_MAX);
                if (b2) {
                    size_t l2 = cal_store_pack(out, n, 0, b2, CAL_STORE_BYTES_MAX);
                    lock();
                    s.saved_blob_hash = fnv(b2, l2);
                    unlock();
                }
                free(b2);
            }
        }
        free(buf);
    }
    nvs_close(h);
    return n;
}

static void erase_list(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_LIST_KEY);
        nvs_commit(h);
        nvs_close(h);
    }
    lock();
    s.saved_blob_hash = 0;
    unlock();
}

/* ---------- the fetch ---------- */

/*
 * The reader's tick (cal_feed_set_tick) and the fetch loop's: block for a tick once CAL_YIELD_AFTER_US of work has
 * gone by without one. The fetch decrypts and parses back to back at priority 2, above core 0's idle task, which the
 * task watchdog (10 s, panic) watches, and one cal_feed_write() can expand recurrences for seconds on a hostile feed:
 * the reader calls this from inside the expansion too, so no stretch runs much past 50 ms.
 */
typedef struct {
    int64_t since_us;   /* when the task last blocked */
} yield_t;

static void yield_tick(void *ctx)
{
    yield_t *y = ctx;
    int64_t now = esp_timer_get_time();
    if (now - y->since_us < CAL_YIELD_AFTER_US) return;
    vTaskDelay(1);
    y->since_us = esp_timer_get_time();
}

/* The address is a secret: copies are zeroed before they're freed. */
static void free_secret(char *s)
{
    if (!s) return;
    memset(s, 0, strlen(s));
    free(s);
}

static void free_info(cal_url_info_t *info)
{
    if (!info) return;
    memset(info, 0, sizeof(*info));
    free(info);
}

typedef struct {
    tb_epoch_t ws, we, now;
    cal_tz_t tz;
    char self_email[128];
    uint32_t gen;
    bool cancelable;    /* syncs stop when the address is removed; a PUT's check goes on */
} fetch_args_t;

static bool is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static bool canceled(const fetch_args_t *a)
{
    if (!a->cancelable) return false;
    lock();
    bool c = s.gen != a->gen;
    unlock();
    return c;
}

/*
 * Fetch url and read it into out (at most TB_MEETINGS_MAX, sorted). Returns CAL_SYNC_OK or why not. *gone is set when
 * remove canceled the fetch (its result must be thrown away).
 */
static cal_sync_err_t fetch(const char *url, const fetch_args_t *a, tb_meeting_t *out, int *n, bool *gone)
{
    *n = 0;
    *gone = false;
    int64_t t0 = mono_ms();
    cal_sync_err_t res = CAL_SYNC_UNREACHABLE;
    cal_feed_t *feed = cal_feed_new(a->ws, a->we, &a->tz, a->self_email);
    char *buf = malloc(CAL_READ_CHUNK);
    char *where = calloc(1, CAL_URL_MAX + 1);     /* a redirect's address: zeroed when freed */
    tb_meeting_t *all = malloc(sizeof(tb_meeting_t) * CAL_CANDIDATES_MAX);
    esp_http_client_handle_t c = NULL;
    yield_t y = {.since_us = esp_timer_get_time()};
    if (!feed || !buf || !where || !all) {
        ESP_LOGE(TAG, "no memory for a fetch");
        goto done;
    }
    cal_feed_set_tick(feed, yield_tick, &y);
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = CAL_NET_TIMEOUT_MS,
        .buffer_size = CAL_READ_CHUNK,
        .buffer_size_tx = 1024,
        .user_agent = "MiniBar/1 (ESP32-S3)",
        .disable_auto_redirect = true,
        .max_redirection_count = CAL_MAX_REDIRECTS,
        .keep_alive_enable = false,
    };
    c = esp_http_client_init(&cfg);
    if (!c) goto done;
    esp_http_client_set_header(c, "Accept", "text/calendar, */*;q=0.1");

    int redirects = 0, status = 0;
    int64_t len = 0;
    for (;;) {
        esp_err_t err = esp_http_client_open(c, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "connect failed: %s", esp_err_to_name(err));
            goto done;      /* DNS, TCP or TLS: unreachable */
        }
        len = esp_http_client_fetch_headers(c);
        if (len < 0) {
            ESP_LOGW(TAG, "no answer (headers)");
            goto done;
        }
        status = esp_http_client_get_status_code(c);
        if (!is_redirect(status)) break;
        if (++redirects > CAL_MAX_REDIRECTS) {
            ESP_LOGW(TAG, "too many redirects");
            goto done;
        }
        /* The redirect's own body isn't read: closing drops it (flushing could wait on a silent server). */
        esp_http_client_close(c);
        if (esp_http_client_set_redirection(c) != ESP_OK) {
            ESP_LOGW(TAG, "redirect without a usable Location");
            goto done;
        }
        /* Never follow a redirect to an unencrypted address. */
        if (esp_http_client_get_url(c, where, CAL_URL_MAX + 1) != ESP_OK || strncasecmp(where, "https://", 8)) {
            ESP_LOGW(TAG, "redirect to a non-https address refused");
            goto done;
        }
        if (canceled(a)) {
            *gone = true;
            goto done;
        }
    }
    res = cal_sync_err_from_http(status);
    if (res != CAL_SYNC_OK) {
        ESP_LOGW(TAG, "server answered %d", status);
        goto done;
    }
    if (len > CAL_FETCH_MAX_BYTES) {
        res = CAL_SYNC_TOO_LARGE;
        goto done;
    }
    res = CAL_SYNC_UNREACHABLE;
    uint64_t total = 0;
    for (;;) {
        if (canceled(a)) {
            *gone = true;
            goto done;
        }
        if (mono_ms() - t0 > CAL_FETCH_BUDGET_MS) {
            ESP_LOGW(TAG, "fetch took too long");
            res = CAL_SYNC_TOO_LARGE;
            goto done;
        }
        int r = esp_http_client_read(c, buf, CAL_READ_CHUNK);
        if (r < 0) {
            ESP_LOGW(TAG, "read failed (errno %d)", esp_http_client_get_errno(c));
            goto done;
        }
        if (r == 0) {
            if (esp_http_client_is_complete_data_received(c)) break;
            ESP_LOGW(TAG, "transfer cut off after %llu bytes", (unsigned long long)total);
            goto done;
        }
        total += (uint64_t)r;
        if (total > CAL_FETCH_MAX_BYTES) {
            res = CAL_SYNC_TOO_LARGE;
            goto done;
        }
        if (cal_feed_write(feed, buf, (size_t)r) == CAL_ERR_NOT_A_CALENDAR) {
            res = CAL_SYNC_NOT_A_CALENDAR;
            goto done;
        }
        yield_tick(&y);     /* the decryption of a buffered feed counts too */
    }
    int k = 0;
    if (cal_feed_finish(feed, all, CAL_CANDIDATES_MAX, &k) != CAL_OK) {
        res = CAL_SYNC_NOT_A_CALENDAR;
        goto done;
    }
    const cal_stats_t *st = cal_feed_stats(feed);
    if (!st->ended) ESP_LOGW(TAG, "the feed has no END:VCALENDAR; using what it had");
    k = cal_today_trim(all, k, TB_MEETINGS_MAX, a->now);
    memcpy(out, all, (size_t)k * sizeof(out[0]));
    *n = k;
    res = CAL_SYNC_OK;
    ESP_LOGI(TAG, "read %llu bytes, %u events (%u recurring, %u overrides) in %d ms: %d meetings; skipped: "
             "%u rrules, %u zones, %u cut lines, %u candidates; %u bytes of stack left",
             (unsigned long long)st->bytes, (unsigned)st->events, (unsigned)st->recurring, (unsigned)st->overrides,
             (int)(mono_ms() - t0), k, (unsigned)st->unsupported_rrule, (unsigned)st->unknown_tzid,
             (unsigned)st->cut_lines, (unsigned)st->dropped_candidates, (unsigned)uxTaskGetStackHighWaterMark(NULL));

done:
    if (c) {
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
    }
    cal_feed_free(feed);
    free(buf);
    free_secret(where);
    free(all);
    if (res != CAL_SYNC_OK && !*gone) ESP_LOGW(TAG, "fetch failed: %s", cal_sync_err_code(res));
    return res;
}

/* ---------- jobs ---------- */

static void fill_args(fetch_args_t *a, const char *self_email, bool cancelable)
{
    a->cancelable = cancelable;
    lock();
    a->tz = s.tz;
    a->gen = s.gen;
    unlock();
    a->now = wall_now();
    cal_today_window(&a->tz, a->now, &a->ws, &a->we);
    strncpy(a->self_email, self_email ? self_email : "", sizeof(a->self_email) - 1);
    a->self_email[sizeof(a->self_email) - 1] = '\0';
}

static void set_error(cal_sync_err_t e, bool google)
{
    s.st.error = cal_sync_err_code(e);
    strncpy(s.st.error_message, e ? cal_sync_err_message(e, google, false) : "", sizeof(s.st.error_message) - 1);
    s.st.error_at = e && clock_ok() ? wall_now() : 0;
}

/* Hand a new list to the app task when it changed (or always), and keep the saved copy current. */
static void deliver(const tb_meeting_t *m, int n, bool always, tb_epoch_t last_sync, uint32_t gen)
{
    lock();
    if (s.gen != gen || !s.saved) {     /* removed meanwhile */
        unlock();
        return;
    }
    bool changed = !s.last || !s.have_last || !cal_meetings_equal(s.last, s.n_last, m, n);
    if (s.last) {
        memcpy(s.last, m, (size_t)n * sizeof(m[0]));
        s.n_last = n;
        s.have_last = true;
    }
    unlock();
    if (changed || always) post_meetings(m, n);
    save_list(m, n, last_sync);
}

static void run_check(char *url, bool from_setup)
{
    cal_url_info_t *info = calloc(1, sizeof(*info));
    tb_meeting_t *m = malloc(sizeof(tb_meeting_t) * TB_MEETINGS_MAX);
    cal_sync_err_t res = CAL_SYNC_UNREACHABLE;
    int n = 0;
    bool gone = false;
    if (info && m && cal_url_check(url, info) == CAL_URL_OK) {
        lock();
        bool online = s.online;
        unlock();
        if (!online) {
            res = CAL_SYNC_OFFLINE;
        } else {
            fetch_args_t a;
            fill_args(&a, info->self_email, false);
            res = fetch(info->url, &a, m, &n, &gone);
        }
    }
    bool valid_clock = clock_ok();
    if (res == CAL_SYNC_OK && save_url(info->url) != ESP_OK) {
        ESP_LOGE(TAG, "couldn't save the address");
        res = CAL_SYNC_UNREACHABLE;
    }

    lock();
    bool superseded = s.check_url != NULL;     /* another PUT came in meanwhile: its check reports instead */
    s.st.syncing = superseded || s.sync_now;
    s.check_done_ms = mono_ms();
    bool google = info && info->google;
    if (res == CAL_SYNC_OK) {
        free_info(s.saved);
        s.saved = info;
        info = NULL;
        s.st.saved = true;
        strncpy(s.st.host, s.saved->host, sizeof(s.st.host) - 1);
        strncpy(s.st.file, s.saved->file, sizeof(s.st.file) - 1);
        strncpy(s.st.ending, s.saved->ending, sizeof(s.st.ending) - 1);
        s.st.check = superseded ? CAL_CHECK_CHECKING : CAL_CHECK_SAVED;
        s.st.check_error = NULL;
        s.st.check_message[0] = '\0';
        set_error(CAL_SYNC_OK, google);
        s.failures = 0;
        s.have_last = false;    /* a new address: always hand its list over */
        if (valid_clock) {
            s.st.last_sync = wall_now();
            s.next_sync_ms = mono_ms() + CAL_SYNC_EVERY_S * 1000LL;
        } else {
            s.next_sync_ms = 0;     /* read it again once the clock is known */
        }
    } else if (!gone) {
        s.st.check = superseded ? CAL_CHECK_CHECKING : CAL_CHECK_FAILED;
        s.st.check_error = cal_sync_err_code(res);
        strncpy(s.st.check_message, cal_sync_err_message(res, google, true), sizeof(s.st.check_message) - 1);
    }
    bool saved = s.st.saved, syncing = s.st.syncing;
    tb_epoch_t last = s.st.last_sync;
    uint32_t gen = s.gen;
    unlock();

    if (res == CAL_SYNC_OK) {
        /* ORDER (tb_app.h): the meetings, then SAVED (its toast counts them), then the status. */
        if (valid_clock) deliver(m, n, true, last, gen);
        post_event(TB_CALEV_SAVED);
        ESP_LOGI(TAG, "address saved (%d meetings)", n);
    } else if (!gone) {
        if (from_setup) post_event(TB_CALEV_SETUP_FAILED);
        ESP_LOGW(TAG, "address check failed: %s", cal_sync_err_code(res));
    }
    post_status(saved, syncing, last);
    free_info(info);
    free(m);
    free_secret(url);
}

static void run_sync(bool *report_out)
{
    lock();
    if (!s.saved) {
        unlock();
        return;
    }
    char *url = strdup(s.saved->url);
    char email[128];
    strncpy(email, s.saved->self_email, sizeof(email) - 1);
    email[sizeof(email) - 1] = '\0';
    bool google = s.saved->google;
    s.st.syncing = true;
    uint32_t gen = s.gen;
    unlock();

    tb_meeting_t *m = malloc(sizeof(tb_meeting_t) * TB_MEETINGS_MAX);
    cal_sync_err_t res = CAL_SYNC_UNREACHABLE;
    int n = 0;
    bool gone = false;
    if (url && m) {
        fetch_args_t a;
        fill_args(&a, email, true);
        res = fetch(url, &a, m, &n, &gone);
    }
    free_secret(url);

    lock();
    gone = gone || s.gen != gen;
    s.st.syncing = s.check_url != NULL || s.sync_now;
    bool report = s.report;
    s.report = false;
    if (gone) {
        report = false;
    } else if (res == CAL_SYNC_OK) {
        s.st.last_sync = wall_now();
        set_error(CAL_SYNC_OK, google);
        s.failures = 0;
        s.next_sync_ms = mono_ms() + CAL_SYNC_EVERY_S * 1000LL;
    } else {
        set_error(res, google);
        /* Back off on failures the network may fix; a rejected or wrong address waits the full interval. */
        int wait_s = CAL_SYNC_EVERY_S;
        if (res == CAL_SYNC_UNREACHABLE) {
            wait_s = CAL_RETRY_FIRST_S << (s.failures < 4 ? s.failures : 4);
            if (wait_s > CAL_SYNC_EVERY_S) wait_s = CAL_SYNC_EVERY_S;
        }
        s.failures++;
        s.next_sync_ms = mono_ms() + wait_s * 1000LL;
    }
    bool saved = s.st.saved, syncing = s.st.syncing;
    tb_epoch_t last = s.st.last_sync;
    unlock();

    if (!gone && res == CAL_SYNC_OK) deliver(m, n, false, last, gen);
    if (report) post_event(res == CAL_SYNC_OK ? TB_CALEV_SYNCED : TB_CALEV_SYNC_FAILED);
    if (!gone) post_status(saved, syncing, last);
    free(m);
    *report_out = report;
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        job_kind_t job = JOB_NONE;
        char *check_url = NULL;
        bool from_setup = false, fail_now = false;
        TickType_t wait = portMAX_DELAY;

        lock();
        int64_t now = mono_ms();
        if (s.check_url) {
            job = JOB_CHECK;
            check_url = s.check_url;
            from_setup = s.check_from_setup;
            s.check_url = NULL;
            s.st.syncing = true;
            s.running = JOB_CHECK;
        } else if (s.saved && s.online) {
            bool due = s.sync_now || now >= s.next_sync_ms;
            if (due && clock_ok()) {
                job = JOB_SYNC;
                if (s.sync_now) s.report = true;
                s.sync_now = false;
                s.running = JOB_SYNC;
            } else if (due) {
                /* The window needs the date: wait for SNTP (or the Mac's hello). Sync now gives up after a while. */
                if (s.sync_now && now - s.sync_now_at > CAL_CLOCK_WAIT_MS) {
                    s.sync_now = false;
                    fail_now = true;
                }
                wait = pdMS_TO_TICKS(1000);
            } else {
                wait = pdMS_TO_TICKS(s.next_sync_ms - now + 10);
            }
        } else if (s.sync_now) {
            s.sync_now = false;     /* removed or offline since it was asked */
            fail_now = true;
        }
        if (fail_now) s.st.syncing = false;
        unlock();

        if (fail_now) {
            post_event(TB_CALEV_SYNC_FAILED);
            post_current_status();
            continue;
        }
        if (job == JOB_CHECK || job == JOB_SYNC) {
            if (job == JOB_CHECK) {
                run_check(check_url, from_setup);
            } else {
                bool reported;
                run_sync(&reported);
            }
            lock();
            s.running = JOB_NONE;
            unlock();
        } else {
            ulTaskNotifyTake(pdTRUE, wait);
        }
    }
}

/* ---------- public ---------- */

esp_err_t cal_sync_init(void)
{
    /* esp_http_client prints the URL in some of its messages; the address is a secret (api.md 15). */
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);

    char *url = load_url();
    cal_url_info_t *info = NULL;
    if (url) {
        info = calloc(1, sizeof(*info));
        if (info && cal_url_check(url, info) != CAL_URL_OK) {
            ESP_LOGW(TAG, "the saved address no longer passes the format check; ignoring it");
            free_info(info);
            info = NULL;
        }
        free_secret(url);
    }
    tb_meeting_t *m = malloc(sizeof(tb_meeting_t) * TB_MEETINGS_MAX);
    tb_epoch_t last_sync = 0;
    int n = -1;
    if (info && m) n = load_list(m, TB_MEETINGS_MAX, &last_sync);

    lock();
    if (s.inited) {
        unlock();
        free_info(info);
        free(m);
        return ESP_OK;
    }
    s.inited = true;
    s.saved = info;
    s.last = heap_caps_malloc(sizeof(tb_meeting_t) * TB_MEETINGS_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s.last) s.last = malloc(sizeof(tb_meeting_t) * TB_MEETINGS_MAX);
    if (info) {
        s.st.saved = true;
        strncpy(s.st.host, info->host, sizeof(s.st.host) - 1);
        strncpy(s.st.file, info->file, sizeof(s.st.file) - 1);
        strncpy(s.st.ending, info->ending, sizeof(s.st.ending) - 1);
        s.st.last_sync = n >= 0 ? last_sync : 0;
    }
    if (n >= 0 && s.last) {
        memcpy(s.last, m, (size_t)n * sizeof(m[0]));
        s.n_last = n;
        s.have_last = true;
    }
    s.next_sync_ms = 0;
    bool saved = s.st.saved;
    unlock();

    /* The saved copy first, so a restart without Wi-Fi still follows today's meetings (decisions.md). */
    if (n >= 0) post_meetings(m, n);
    post_status(saved, false, saved ? last_sync : 0);
    free(m);

    BaseType_t ok = xTaskCreatePinnedToCore(task, "cal_sync", CAL_TASK_STACK, NULL, CAL_TASK_PRIO, &s_task, CAL_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "couldn't start the sync task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "calendar %s; %d saved meetings", saved ? "saved" : "not set up", n < 0 ? 0 : n);
    return ESP_OK;
}

void cal_sync_set_online(bool online)
{
    lock();
    bool was = s.online;
    s.online = online;
    if (online && !was) {
        /* Coming up: sync now if the last good one is over the interval old (or there never was one). */
        tb_epoch_t age = s.st.last_sync && clock_ok() ? wall_now() - s.st.last_sync : CAL_SYNC_EVERY_S;
        if (age < 0 || age >= CAL_SYNC_EVERY_S) s.next_sync_ms = 0;
        else s.next_sync_ms = mono_ms() + (CAL_SYNC_EVERY_S - age) * 1000LL;
        s.failures = 0;
    }
    unlock();
    wake();
}

void cal_sync_set_time_zone(const char *posix)
{
    cal_tz_t tz;
    if (!posix || !cal_tz_parse(posix, &tz)) cal_tz_utc(&tz);
    lock();
    bool changed = memcmp(&tz, &s.tz, sizeof(tz)) != 0;
    s.tz = tz;
    if (changed && s.inited) s.next_sync_ms = 0;    /* "today" moved: read the window again */
    unlock();
    if (changed) wake();
}

esp_err_t cal_sync_put(const char *url, bool from_setup, cal_url_err_t *fmt_err)
{
    cal_url_info_t *info = calloc(1, sizeof(*info));
    if (!info) return ESP_ERR_NO_MEM;
    cal_url_err_t e = cal_url_check(url, info);
    if (fmt_err) *fmt_err = e;
    if (e != CAL_URL_OK) {
        free_info(info);
        return ESP_ERR_INVALID_ARG;
    }
    char *copy = strdup(info->url);
    free_info(info);
    if (!copy) return ESP_ERR_NO_MEM;
    lock();
    free_secret(s.check_url);  /* a newer address replaces one still waiting */
    s.check_url = copy;
    s.check_from_setup = from_setup;
    s.st.check = CAL_CHECK_CHECKING;
    s.st.check_error = NULL;
    s.st.check_message[0] = '\0';
    s.st.syncing = true;
    bool saved = s.st.saved;
    tb_epoch_t last = s.st.last_sync;
    unlock();
    post_status(saved, true, last);
    wake();
    return ESP_OK;
}

esp_err_t cal_sync_remove(void)
{
    lock();
    if (!s.saved) {
        unlock();
        return ESP_ERR_NOT_FOUND;
    }
    free_info(s.saved);
    s.saved = NULL;
    s.gen++;                    /* a running sync throws its result away */
    s.sync_now = false;
    s.report = false;
    s.failures = 0;
    cal_check_state_t check = s.st.check;
    const char *check_error = s.st.check_error;
    char check_message[sizeof(s.st.check_message)];
    memcpy(check_message, s.st.check_message, sizeof(check_message));
    bool checking = s.check_url != NULL || check == CAL_CHECK_CHECKING;
    memset(&s.st, 0, sizeof(s.st));
    if (checking) {
        /* A PUT still being checked goes on: if it passes, it becomes the new address. */
        s.st.check = check;
        s.st.check_error = check_error;
        memcpy(s.st.check_message, check_message, sizeof(check_message));
        s.st.syncing = true;
    }
    s.n_last = 0;
    s.have_last = false;
    unlock();

    if (save_url(NULL) != ESP_OK) ESP_LOGW(TAG, "couldn't erase the address");
    erase_list();
    /* core forgets the meetings, turns Calendar meetings and Show meeting titles off, toasts "Calendar removed" */
    post_event(TB_CALEV_REMOVED);
    post_status(false, checking, 0);
    ESP_LOGI(TAG, "address removed");
    return ESP_OK;
}

esp_err_t cal_sync_now(void)
{
    lock();
    esp_err_t err = ESP_OK;
    if (!s.saved) err = ESP_ERR_NOT_FOUND;
    else if (!s.online) err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        if (s.running == JOB_SYNC) {
            s.report = true;    /* a sync is running: it reports when it ends */
        } else {
            s.sync_now = true;  /* the next job (after a PUT's check, if one is running) */
            s.sync_now_at = mono_ms();
        }
        s.st.syncing = true;
    }
    bool saved = s.st.saved, syncing = s.st.syncing;
    tb_epoch_t last = s.st.last_sync;
    unlock();
    /* Refused: still tell the app task, so a "Sync…" tile from the quick menu doesn't stay up. */
    post_status(saved, syncing, last);
    if (err == ESP_OK) wake();
    return err;
}

void cal_sync_get_status(cal_status_t *out)
{
    lock();
    *out = s.st;
    if ((out->check == CAL_CHECK_SAVED || out->check == CAL_CHECK_FAILED) &&
        mono_ms() - s.check_done_ms > CAL_CHECK_KEEP_MS) {
        out->check = CAL_CHECK_NONE;
        out->check_error = NULL;
        out->check_message[0] = '\0';
    }
    unlock();
}
