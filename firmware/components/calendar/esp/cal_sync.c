/*
 * cal_sync.c: the calendar service on the device: up to three secret addresses in NVS, the HTTPS fetch streamed
 * through the ICS reader, the 10-minute schedule, add / edit / remove / Sync now, the merge, and the bus posts.
 * Owner: calendar builder. See cal_sync.h for the behavior; this file is the plumbing. The list logic (names, tags,
 * the merge, the move from one address) is pure C in cal_list.c and cal_migrate.c, tested on the host.
 *
 * One task ("cal_sync", core 0, priority 2, stack in internal RAM because it writes NVS) runs one job at a time:
 *   check   a new or changed address: fetch it once; only a good fetch saves it
 *   sync    one calendar, in list order: every CAL_SYNC_EVERY_S while online, sooner after a failure (per calendar,
 *           so one dead address doesn't slow the others), and on Sync now (every calendar, one after another)
 * Every other function only changes the shared state under s_mx and wakes the task, so none of them blocks on the
 * network (the HTTP server and the app task call them). Calendars are read in sequence, so the fetch buffers (about
 * 28 KB, mostly TLS in PSRAM) are the same as for one.
 *
 * The fetch: esp_http_client over TLS with the certificate bundle, redirects followed by hand (https only, at most
 * CAL_MAX_REDIRECTS), the body streamed in 2 KB reads into cal_feed_write() (nothing buffers the file), with a size
 * limit (CAL_FETCH_MAX_BYTES) and a time limit (CAL_FETCH_BUDGET_MS). A body that isn't iCal stops the fetch within
 * its first 4 KB.
 *
 * Flash: an address is written when it is added or changed, a calendar's saved copy only when its meetings change
 * (a fingerprint without the sync time decides), at most 16 meetings each, and the names and tags only on an edit.
 *
 * Privacy: an address is never logged. esp_http_client logs the URL in some of its messages, so its log tag is
 * silenced (cal_sync_init); this file logs results, sizes and error codes, and a calendar's tag, never its address.
 */
#include <pthread.h>
#include <stdio.h>
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
#include "cal_list.h"
#include "cal_migrate.h"
#include "cal_store.h"
#include "cal_sync.h"
#include "cal_today.h"
#include "cal_tz.h"
#include "cal_url.h"
#include "tb_app.h"
#include "tb_bus.h"
#include "tb_text.h"

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
#define NVS_SEC_NS    "calsec"        /* "url0".."url2"; "url" is firmware before 1.0.8's single address */
#define NVS_NS        "cal"           /* "list0".."list2" saved copies, "meta" names and tags; "list" is the old copy */
#define NVS_META_KEY  "meta"

typedef enum { JOB_NONE = 0, JOB_CHECK, JOB_SYNC } job_kind_t;

/* One calendar. The name and tag are in s.list (cal_list_t); the address is here and nowhere else. */
typedef struct {
    cal_url_info_t *saved;      /* the saved address and its masked form; NULL = no calendar in this slot */
    tb_epoch_t last_sync;       /* 0 = never */
    bool syncing;
    bool sync_req;              /* Sync now asked: read it next */
    bool failing;               /* its last sync failed: left out of the merge until one works */
    const char *error;
    char error_message[128];
    tb_epoch_t error_at;
    int failures;               /* background syncs failed in a row (back-off) */
    int64_t next_sync_ms;       /* mono ms of the next background sync; 0 = as soon as possible */
    uint32_t gen;               /* bumped by remove or a new address: a fetch started before it throws its result away */
    tb_meeting_t *last;         /* CAL_COPY_MAX entries (4 KB, in PSRAM: allocated by init) */
    int n_last;
    bool have_last;             /* last was read or fetched (an empty calendar still counts) */
    uint32_t saved_blob_hash;
} cal_slot_t;

static pthread_mutex_t s_mx = PTHREAD_MUTEX_INITIALIZER;
static TaskHandle_t s_task;

static struct {
    bool inited;
    bool online;
    cal_tz_t tz;                /* the device zone (UTC until main sets it) */
    cal_list_t list;            /* names and tags by slot */
    cal_slot_t sl[TB_CALS_MAX];
    /* a check waiting or running: an address to try before it is saved */
    char *check_url;            /* normalized, or NULL */
    int check_slot;             /* -1 a new calendar, else the one whose address is being replaced */
    char check_name[TB_CAL_NAME_BYTES], check_tag[TB_CAL_TAG_BYTES];   /* "" = default (add) or keep (edit) */
    bool check_from_setup;
    /* Sync now */
    int64_t sync_now_at;        /* when (mono ms) */
    bool report;                /* the running or next syncs report SYNCED / SYNC_FAILED when the last one is done */
    bool report_ok;
    job_kind_t running;         /* the job the task is doing now */
    int running_slot;
    /* the last PUT or POST's result (api.md 11.1: kept 10 minutes) */
    cal_check_state_t check;
    int check_id;
    const char *check_error;
    char check_message[160];
    int64_t check_done_ms;
    /* what the app task was last told */
    tb_meeting_t *mbuf;         /* CAL_MERGE_CAP entries (12 KB, PSRAM): the merge's workspace */
    tb_meeting_t *pub;          /* TB_MEETINGS_MAX entries: the merged list last handed over */
    int n_pub;
    bool have_pub;
    tb_cal_info_t pub_info[TB_CALS_MAX];
    bool have_pub_info;
} s = {.check_slot = -1};

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

/* The address is a secret: copies are zeroed before they're freed. */
static void free_secret(char *p)
{
    if (!p) return;
    memset(p, 0, strlen(p));
    free(p);
}

static void free_info(cal_url_info_t *info)
{
    if (!info) return;
    memset(info, 0, sizeof(*info));
    free(info);
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

static void post_list(const tb_cal_info_t info[TB_CALS_MAX])
{
    tb_cal_list_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    memcpy(p->c, info, sizeof(p->c));
    tb_event_t ev = {.kind = TB_EV_CAL_LIST};
    ev.u.ptr = p;
    if (!tb_bus_post(&ev)) {
        ESP_LOGW(TAG, "bus full: calendar list dropped");
        free(p);
    }
}

/* ---------- the aggregate, the merge and what the app task is told (all with s_mx held) ---------- */

static bool any_saved(void)
{
    for (int i = 0; i < TB_CALS_MAX; i++)
        if (s.sl[i].saved) return true;
    return false;
}

static bool any_syncing(void)
{
    if (s.check_url || s.check == CAL_CHECK_CHECKING) return true;
    for (int i = 0; i < TB_CALS_MAX; i++)
        if (s.sl[i].saved && (s.sl[i].syncing || s.sl[i].sync_req)) return true;
    return false;
}

static tb_epoch_t latest_sync(void)
{
    tb_epoch_t t = 0;
    for (int i = 0; i < TB_CALS_MAX; i++)
        if (s.sl[i].saved && s.sl[i].last_sync > t) t = s.sl[i].last_sync;
    return t;
}

/* The merged list into s.mbuf (count returned) and the calendars' info by slot. */
static int merge_locked(tb_cal_info_t info[TB_CALS_MAX])
{
    cal_source_t src[TB_CALS_MAX];
    int n_src = 0;
    for (int i = 0; i < TB_CALS_MAX; i++) {
        memset(&info[i], 0, sizeof info[i]);
        if (!s.list.c[i].used || !s.sl[i].saved) continue;
        info[i] = s.list.c[i];
        info[i].failing = s.sl[i].failing;
        src[n_src++] = (cal_source_t){i, s.sl[i].failing, s.sl[i].have_last ? s.sl[i].last : NULL, s.sl[i].n_last};
    }
    if (!s.mbuf) return 0;
    return cal_merge(src, n_src, s.mbuf, TB_MEETINGS_MAX, wall_now());
}

/* Tell the app task what changed: the calendars' names, tags and who can't sync, and the merged meetings. force sends
 * both whatever changed (a new address always hands its list over). */
static void publish(bool force)
{
    tb_cal_info_t info[TB_CALS_MAX];
    lock();
    int n = merge_locked(info);
    bool new_m = force || !s.have_pub || !s.pub || !cal_meetings_equal(s.pub, s.n_pub, s.mbuf, n);
    bool new_i = force || !s.have_pub_info || memcmp(s.pub_info, info, sizeof info) != 0;
    if (s.pub && s.mbuf) {
        memcpy(s.pub, s.mbuf, (size_t)n * sizeof(s.pub[0]));
        s.n_pub = n;
        s.have_pub = true;
    }
    memcpy(s.pub_info, info, sizeof info);
    s.have_pub_info = true;
    if (new_i) post_list(info);
    if (new_m && s.mbuf) post_meetings(s.mbuf, n);
    unlock();
}

static void post_current_status(void)
{
    lock();
    bool saved = any_saved(), syncing = any_syncing();
    tb_epoch_t last = latest_sync();
    unlock();
    post_status(saved, syncing, last);
}

/* ---------- NVS ---------- */

static void key_name(char *out, size_t cap, const char *base, int slot)
{
    if (slot < 0) snprintf(out, cap, "%s", base);
    else snprintf(out, cap, "%s%d", base, slot);
}

/* The address of one slot (or the old single one, slot -1): malloc'ed text, or NULL. The caller zeroes and frees it. */
static char *load_url(int slot)
{
    char key[16];
    key_name(key, sizeof key, "url", slot);
    nvs_handle_t h;
    if (nvs_open_from_partition(NVS_SEC_PART, NVS_SEC_NS, NVS_READONLY, &h) != ESP_OK) return NULL;
    size_t len = 0;
    char *url = NULL;
    if (nvs_get_str(h, key, NULL, &len) == ESP_OK && len > 1 && len <= CAL_URL_MAX + 1) {
        url = calloc(1, len);
        if (url && nvs_get_str(h, key, url, &len) != ESP_OK) {
            memset(url, 0, len);
            free(url);
            url = NULL;
        }
    }
    nvs_close(h);
    return url;
}

static esp_err_t save_url(int slot, const char *url)
{
    char key[16];
    key_name(key, sizeof key, "url", slot);
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_SEC_PART, NVS_SEC_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = url ? nvs_set_str(h, key, url) : nvs_erase_key(h, key);
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

/* Save one calendar's meetings if they changed (called from the task only). */
static void save_list(int slot, const tb_meeting_t *m, int n, tb_epoch_t last_sync)
{
    uint8_t *buf = malloc(CAL_STORE_BYTES_MAX);
    if (!buf) return;
    /* The fingerprint leaves out last_sync, so a sync that changes nothing doesn't write flash. */
    size_t len = cal_store_pack(m, n, 0, buf, CAL_STORE_BYTES_MAX);
    uint32_t h = fnv(buf, len);
    lock();
    bool same = h == s.sl[slot].saved_blob_hash;
    unlock();
    if (len && !same) {
        len = cal_store_pack(m, n, last_sync, buf, CAL_STORE_BYTES_MAX);
        char key[16];
        key_name(key, sizeof key, "list", slot);
        nvs_handle_t nh;
        if (nvs_open(NVS_NS, NVS_READWRITE, &nh) == ESP_OK) {
            if (nvs_set_blob(nh, key, buf, len) == ESP_OK && nvs_commit(nh) == ESP_OK) {
                lock();
                s.sl[slot].saved_blob_hash = h;
                unlock();
            } else {
                ESP_LOGW(TAG, "couldn't save the meetings list");
            }
            nvs_close(nh);
        }
    }
    free(buf);
}

static int load_list(int slot, tb_meeting_t *out, int max, tb_epoch_t *last_sync)
{
    char key[16];
    key_name(key, sizeof key, "list", slot);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return -1;
    size_t len = 0;
    int n = -1;
    if (nvs_get_blob(h, key, NULL, &len) == ESP_OK && len <= CAL_STORE_BYTES_MAX) {
        uint8_t *buf = malloc(len ? len : 1);
        if (buf && nvs_get_blob(h, key, buf, &len) == ESP_OK) {
            n = cal_store_unpack(buf, len, out, max, last_sync);
            if (n >= 0) {
                /* the fingerprint as save_list computes it (without last_sync), so an unchanged list isn't rewritten */
                uint8_t *b2 = malloc(CAL_STORE_BYTES_MAX);
                if (b2) {
                    size_t l2 = cal_store_pack(out, n, 0, b2, CAL_STORE_BYTES_MAX);
                    s.sl[slot].saved_blob_hash = fnv(b2, l2);
                }
                free(b2);
            }
        }
        free(buf);
    }
    nvs_close(h);
    return n;
}

static void erase_list(int slot)
{
    char key[16];
    key_name(key, sizeof key, "list", slot);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, key);
        nvs_commit(h);
        nvs_close(h);
    }
    s.sl[slot].saved_blob_hash = 0;
}

/* The names and tags (not secret). Called with s_mx held, on an add, edit or remove only. */
static void save_meta_locked(void)
{
    uint8_t buf[CAL_LIST_BLOB_MAX];
    size_t n = cal_list_encode(&s.list, buf, sizeof buf);
    nvs_handle_t h;
    if (n && nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_blob(h, NVS_META_KEY, buf, n) != ESP_OK || nvs_commit(h) != ESP_OK)
            ESP_LOGW(TAG, "couldn't save the calendars' names");
        nvs_close(h);
    }
}

/* cal_migrate.h's store, over the two namespaces. */
static int kv_get(void *ctx, cal_kv_kind_t kind, int slot, uint8_t *buf, size_t cap)
{
    (void)ctx;
    char key[16];
    nvs_handle_t h;
    size_t len = 0;
    int ret = -1;
    if (kind == CAL_KV_URL) {
        key_name(key, sizeof key, "url", slot);
        if (nvs_open_from_partition(NVS_SEC_PART, NVS_SEC_NS, NVS_READONLY, &h) != ESP_OK) return -1;
        if (nvs_get_str(h, key, NULL, &len) == ESP_OK && len >= 1) {
            ret = (int)len - 1;
            if (len <= cap && nvs_get_str(h, key, (char *)buf, &len) != ESP_OK) ret = -1;
        }
    } else {
        key_name(key, sizeof key, "list", slot);
        if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return -1;
        if (nvs_get_blob(h, key, NULL, &len) == ESP_OK) {
            ret = (int)len;
            if (len <= cap && nvs_get_blob(h, key, buf, &len) != ESP_OK) ret = -1;
        }
    }
    nvs_close(h);
    return ret;
}

static bool kv_put(void *ctx, cal_kv_kind_t kind, int slot, const uint8_t *buf, size_t len)
{
    (void)ctx;
    if (kind == CAL_KV_URL) {
        char *t = calloc(1, len + 1);
        if (!t) return false;
        memcpy(t, buf, len);
        bool ok = save_url(slot, t) == ESP_OK;
        free_secret(t);
        return ok;
    }
    char key[16];
    key_name(key, sizeof key, "list", slot);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, key, buf, len) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

static bool kv_erase(void *ctx, cal_kv_kind_t kind, int slot)
{
    (void)ctx;
    if (kind == CAL_KV_URL) return save_url(slot, NULL) == ESP_OK;
    char key[16];
    key_name(key, sizeof key, "list", slot);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_erase_key(h, key);
    bool ok = (e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
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

typedef struct {
    tb_epoch_t ws, we, now;
    cal_tz_t tz;
    char tag[TB_CAL_TAG_BYTES];     /* for the log: a calendar may be named by its tag, never by its address */
    char self_email[128];
    uint32_t gen;
    int slot;
    bool cancelable;    /* syncs stop when the calendar is removed or its address replaced; a check goes on */
} fetch_args_t;

static bool is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static bool canceled(const fetch_args_t *a)
{
    if (!a->cancelable) return false;
    lock();
    bool c = s.sl[a->slot].gen != a->gen;
    unlock();
    return c;
}

/*
 * Fetch url and read it into out (at most CAL_COPY_MAX, sorted). Returns CAL_SYNC_OK or why not. *gone is set when
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
    k = cal_today_trim(all, k, CAL_COPY_MAX, a->now);       /* a calendar's copy is capped at 16 meetings */
    memcpy(out, all, (size_t)k * sizeof(out[0]));
    *n = k;
    res = CAL_SYNC_OK;
    ESP_LOGI(TAG, "%s: read %llu bytes, %u events (%u recurring, %u overrides) in %d ms: %d meetings; skipped: "
             "%u rrules, %u zones, %u cut lines, %u candidates; %u bytes of stack left",
             a->tag, (unsigned long long)st->bytes, (unsigned)st->events, (unsigned)st->recurring, (unsigned)st->overrides,
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

static void fill_args(fetch_args_t *a, const char *self_email, const char *tag, int slot, bool cancelable)
{
    a->cancelable = cancelable;
    a->slot = slot < 0 ? 0 : slot;
    lock();
    a->tz = s.tz;
    a->gen = s.sl[a->slot].gen;
    unlock();
    a->now = wall_now();
    cal_today_window(&a->tz, a->now, &a->ws, &a->we);
    tb_strlcpy(a->tag, tag ? tag : "", sizeof a->tag);
    strncpy(a->self_email, self_email ? self_email : "", sizeof(a->self_email) - 1);
    a->self_email[sizeof(a->self_email) - 1] = '\0';
}

static void set_error(cal_slot_t *c, cal_sync_err_t e, bool google)
{
    c->error = cal_sync_err_code(e);
    strncpy(c->error_message, e ? cal_sync_err_message(e, google, false) : "", sizeof(c->error_message) - 1);
    c->error_message[sizeof(c->error_message) - 1] = '\0';
    c->error_at = e && clock_ok() ? wall_now() : 0;
}

/* Keep a calendar's list (RAM and flash) and hand the merge to the app task. */
static void deliver(int slot, const tb_meeting_t *m, int n, bool force, tb_epoch_t last_sync, uint32_t gen)
{
    lock();
    cal_slot_t *c = &s.sl[slot];
    if (c->gen != gen || !c->saved) {     /* removed or replaced meanwhile */
        unlock();
        return;
    }
    if (n > CAL_COPY_MAX) n = CAL_COPY_MAX;
    if (c->last) {
        memcpy(c->last, m, (size_t)n * sizeof(m[0]));
        c->n_last = n;
        c->have_last = true;
    }
    unlock();
    publish(force);
    save_list(slot, m, n, last_sync);
}

static void run_check(char *url, int slot, const char *name, const char *tag, bool from_setup)
{
    cal_url_info_t *info = calloc(1, sizeof(*info));
    tb_meeting_t *m = malloc(sizeof(tb_meeting_t) * TB_MEETINGS_MAX);
    cal_sync_err_t res = CAL_SYNC_UNREACHABLE;
    int n = 0;
    bool gone = false;
    const char *list_err = NULL;       /* set when the name or tag no longer fits by the time the address passed */
    char tagbuf[TB_CAL_TAG_BYTES] = "";
    if (slot >= 0) {
        lock();
        tb_strlcpy(tagbuf, s.list.c[slot].tag, sizeof tagbuf);
        unlock();
    } else {
        tb_strlcpy(tagbuf, tag, sizeof tagbuf);
    }
    if (info && m && cal_url_check(url, info) == CAL_URL_OK) {
        lock();
        bool online = s.online;
        unlock();
        if (!online) {
            res = CAL_SYNC_OFFLINE;
        } else {
            fetch_args_t a;
            fill_args(&a, info->self_email, tagbuf[0] ? tagbuf : "new", slot, false);
            res = fetch(info->url, &a, m, &n, &gone);
        }
    }
    bool valid_clock = clock_ok();
    bool google = info && info->google;
    bool fresh_slot = false;
    uint32_t gen = 0;
    if (res == CAL_SYNC_OK) {
        /* Commit under the lock, so a remove or another add can't slip between choosing the slot and saving the address:
         * the list, the address (nvs_sec) and the names (nvs) change together. */
        lock();
        int sl = slot;
        if (sl >= 0 && !s.list.c[sl].used) {
            res = CAL_SYNC_UNREACHABLE;     /* the calendar was removed while its new address was being checked */
            list_err = "That calendar was removed.";
        } else if (sl >= 0) {
            cal_list_edit(&s.list, sl, name, tag);
        } else {
            cal_list_err_t le = cal_list_add(&s.list, name, tag, &sl);
            if (le != CAL_LIST_OK) {
                list_err = cal_list_err_message(le);
                res = CAL_SYNC_UNREACHABLE;
            } else {
                fresh_slot = true;
            }
        }
        if (res == CAL_SYNC_OK && save_url(sl, info->url) != ESP_OK) {
            ESP_LOGE(TAG, "couldn't save the address");
            if (fresh_slot) cal_list_remove(&s.list, sl);
            res = CAL_SYNC_UNREACHABLE;
        }
        if (res == CAL_SYNC_OK) {
            cal_slot_t *c = &s.sl[sl];
            free_info(c->saved);
            c->saved = info;
            info = NULL;
            c->gen++;                       /* a sync of the address it replaces throws its result away */
            gen = c->gen;
            c->last_sync = valid_clock ? wall_now() : 0;
            c->next_sync_ms = valid_clock ? mono_ms() + CAL_SYNC_EVERY_S * 1000LL : 0;  /* no clock: read it once it's known */
            c->failing = false;
            c->failures = 0;
            c->sync_req = false;
            c->n_last = 0;
            c->have_last = false;
            set_error(c, CAL_SYNC_OK, google);
            save_meta_locked();
            slot = sl;
        }
        unlock();
    }

    lock();
    bool superseded = s.check_url != NULL;     /* another PUT came in meanwhile: its check reports instead */
    s.check_done_ms = mono_ms();
    s.check_id = slot >= 0 && res == CAL_SYNC_OK ? slot + 1 : s.check_id;
    if (res == CAL_SYNC_OK) {
        s.check = superseded ? CAL_CHECK_CHECKING : CAL_CHECK_SAVED;
        s.check_error = NULL;
        s.check_message[0] = '\0';
    } else if (!gone) {
        s.check = superseded ? CAL_CHECK_CHECKING : CAL_CHECK_FAILED;
        s.check_error = list_err ? "bad_value" : cal_sync_err_code(res);
        strncpy(s.check_message, list_err ? list_err : cal_sync_err_message(res, google, true), sizeof(s.check_message) - 1);
        s.check_message[sizeof(s.check_message) - 1] = '\0';
    }
    bool saved = any_saved(), syncing = any_syncing() || superseded;
    tb_epoch_t last = latest_sync();
    unlock();

    if (res == CAL_SYNC_OK) {
        /* ORDER (tb_app.h): the calendars and meetings, then SAVED (its toast counts them), then the status. */
        if (valid_clock) deliver(slot, m, n, true, last, gen);
        else publish(true);
        post_event(TB_CALEV_SAVED);
        ESP_LOGI(TAG, "address saved (%d meetings)", n);
    } else if (!gone) {
        if (from_setup) post_event(TB_CALEV_SETUP_FAILED);
        ESP_LOGW(TAG, "address check failed: %s", list_err ? "bad_value" : cal_sync_err_code(res));
    }
    post_status(saved, syncing, last);
    free_info(info);
    free(m);
    free_secret(url);
}

static void run_sync(int slot)
{
    lock();
    cal_slot_t *c = &s.sl[slot];
    if (!c->saved) {
        unlock();
        return;
    }
    char *url = strdup(c->saved->url);
    char email[128];
    strncpy(email, c->saved->self_email, sizeof(email) - 1);
    email[sizeof(email) - 1] = '\0';
    char tag[TB_CAL_TAG_BYTES];
    tb_strlcpy(tag, s.list.c[slot].tag, sizeof tag);
    bool google = c->saved->google;
    c->syncing = true;
    c->sync_req = false;
    uint32_t gen = c->gen;
    unlock();

    tb_meeting_t *m = malloc(sizeof(tb_meeting_t) * TB_MEETINGS_MAX);
    cal_sync_err_t res = CAL_SYNC_UNREACHABLE;
    int n = 0;
    bool gone = false;
    if (url && m) {
        fetch_args_t a;
        fill_args(&a, email, tag, slot, true);
        res = fetch(url, &a, m, &n, &gone);
    }
    free_secret(url);

    lock();
    c = &s.sl[slot];
    gone = gone || c->gen != gen || !c->saved;
    bool fin = false, fin_ok = true;
    if (!gone) {
        c->syncing = false;
        if (res == CAL_SYNC_OK) {
            c->last_sync = wall_now();
            set_error(c, CAL_SYNC_OK, google);
            c->failing = false;
            c->failures = 0;
            c->next_sync_ms = mono_ms() + CAL_SYNC_EVERY_S * 1000LL;
        } else {
            set_error(c, res, google);
            c->failing = true;      /* its meetings are left out of the bar until a sync works (no stale copy) */
            /* Back off on failures the network may fix; a rejected or wrong address waits the full interval. */
            int wait_s = CAL_SYNC_EVERY_S;
            if (res == CAL_SYNC_UNREACHABLE) {
                wait_s = CAL_RETRY_FIRST_S << (c->failures < 4 ? c->failures : 4);
                if (wait_s > CAL_SYNC_EVERY_S) wait_s = CAL_SYNC_EVERY_S;
            }
            c->failures++;
            c->next_sync_ms = mono_ms() + wait_s * 1000LL;
        }
        if (s.report) {
            s.report_ok = s.report_ok && res == CAL_SYNC_OK;
            bool more = s.check_url != NULL;
            for (int i = 0; i < TB_CALS_MAX; i++) more = more || s.sl[i].sync_req || s.sl[i].syncing;
            if (!more) {
                fin = true;
                fin_ok = s.report_ok;
                s.report = false;
                s.report_ok = true;
            }
        }
    }
    bool saved = any_saved(), syncing = any_syncing();
    tb_epoch_t last = latest_sync();
    unlock();

    if (!gone && res == CAL_SYNC_OK) deliver(slot, m, n, false, last, gen);
    else if (!gone) publish(false);     /* it can't sync now: the merge leaves it out */
    if (fin) post_event(fin_ok ? TB_CALEV_SYNCED : TB_CALEV_SYNC_FAILED);
    if (!gone) post_status(saved, syncing, last);
    free(m);
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        job_kind_t job = JOB_NONE;
        char *check_url = NULL;
        char name[TB_CAL_NAME_BYTES] = "", tag[TB_CAL_TAG_BYTES] = "";
        int slot = -1;
        bool from_setup = false, fail_now = false;
        TickType_t wait = portMAX_DELAY;

        lock();
        int64_t now = mono_ms();
        bool want = false;      /* a Sync now is waiting */
        for (int i = 0; i < TB_CALS_MAX; i++) want = want || (s.sl[i].saved && s.sl[i].sync_req);
        if (s.check_url) {
            job = JOB_CHECK;
            check_url = s.check_url;
            slot = s.check_slot;
            memcpy(name, s.check_name, sizeof name);
            memcpy(tag, s.check_tag, sizeof tag);
            from_setup = s.check_from_setup;
            s.check_url = NULL;
            s.running = JOB_CHECK;
        } else if (any_saved() && s.online) {
            /* The first calendar that is due, in list order. */
            int64_t soonest = INT64_MAX;
            bool clock_wait = false;
            for (int i = 0; i < TB_CALS_MAX && job == JOB_NONE; i++) {
                cal_slot_t *c = &s.sl[i];
                if (!c->saved) continue;
                bool due = c->sync_req || now >= c->next_sync_ms;
                if (due && clock_ok()) {
                    job = JOB_SYNC;
                    slot = i;
                    s.running = JOB_SYNC;
                    s.running_slot = i;
                } else if (due) {
                    clock_wait = true;
                } else if (c->next_sync_ms < soonest) {
                    soonest = c->next_sync_ms;
                }
            }
            if (job == JOB_NONE) {
                if (clock_wait) {
                    /* The window needs the date: wait for SNTP (or the Mac's hello). Sync now gives up after a while. */
                    if (want && now - s.sync_now_at > CAL_CLOCK_WAIT_MS) fail_now = true;
                    wait = pdMS_TO_TICKS(1000);
                } else if (soonest != INT64_MAX) {
                    wait = pdMS_TO_TICKS(soonest - now + 10);
                }
            }
        } else if (want) {
            fail_now = true;        /* removed or offline since it was asked */
        }
        if (fail_now) {
            for (int i = 0; i < TB_CALS_MAX; i++) s.sl[i].sync_req = false;
            s.report = false;
            s.report_ok = true;
        }
        unlock();

        if (fail_now) {
            post_event(TB_CALEV_SYNC_FAILED);
            post_current_status();
            continue;
        }
        if (job == JOB_CHECK) run_check(check_url, slot, name, tag, from_setup);
        else if (job == JOB_SYNC) run_sync(slot);
        if (job != JOB_NONE) {
            lock();
            s.running = JOB_NONE;
            unlock();
        } else {
            ulTaskNotifyTake(pdTRUE, wait);
        }
    }
}

/* ---------- public ---------- */

static void alloc_buffers(void)
{
    for (int i = 0; i < TB_CALS_MAX; i++) {
        size_t sz = sizeof(tb_meeting_t) * CAL_COPY_MAX;
        s.sl[i].last = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s.sl[i].last) s.sl[i].last = malloc(sz);
    }
    size_t mb = sizeof(tb_meeting_t) * CAL_MERGE_CAP, pb = sizeof(tb_meeting_t) * TB_MEETINGS_MAX;
    s.mbuf = heap_caps_malloc(mb, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s.mbuf) s.mbuf = malloc(mb);
    s.pub = heap_caps_malloc(pb, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s.pub) s.pub = malloc(pb);
}

esp_err_t cal_sync_init(void)
{
    /* esp_http_client prints the URL in some of its messages; the address is a secret (api.md 15). */
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);

    lock();
    if (s.inited) {
        unlock();
        return ESP_OK;
    }
    s.inited = true;
    alloc_buffers();
    unlock();

    /* Firmware before 1.0.8 kept one address: it becomes a calendar of its own, once (write, read back, then erase). */
    cal_kv_ops_t kv = {NULL, kv_get, kv_put, kv_erase};
    cal_mig_t mig = cal_migrate_legacy(&kv);
    if (mig.res != CAL_MIG_NOTHING) ESP_LOGI(TAG, "address from the earlier firmware: result %d, slot %d", (int)mig.res, mig.slot);

    unsigned mask = 0;
    tb_epoch_t last_sync[TB_CALS_MAX] = {0};
    int n_copy[TB_CALS_MAX];
    tb_meeting_t *tmp = malloc(sizeof(tb_meeting_t) * TB_MEETINGS_MAX);
    for (int i = 0; i < TB_CALS_MAX; i++) {
        n_copy[i] = -1;
        char *url = load_url(i);
        cal_url_info_t *info = NULL;
        if (url) {
            info = calloc(1, sizeof(*info));
            if (info && cal_url_check(url, info) != CAL_URL_OK) {
                ESP_LOGW(TAG, "calendar %d: the saved address no longer passes the format check; ignoring it", i + 1);
                free_info(info);
                info = NULL;
            }
            free_secret(url);
        }
        if (!info) continue;
        s.sl[i].saved = info;
        mask |= 1u << i;
        if (tmp && s.sl[i].last) {
            n_copy[i] = load_list(i, tmp, CAL_COPY_MAX, &last_sync[i]);
            if (n_copy[i] >= 0) {
                memcpy(s.sl[i].last, tmp, (size_t)n_copy[i] * sizeof(tmp[0]));
                s.sl[i].n_last = n_copy[i];
                s.sl[i].have_last = true;
                s.sl[i].last_sync = last_sync[i];
            }
        }
        s.sl[i].next_sync_ms = 0;
    }
    free(tmp);

    /* Names and tags: what was saved, made to agree with the addresses that are there. */
    uint8_t meta[CAL_LIST_BLOB_MAX + 8];
    size_t mlen = sizeof meta;
    nvs_handle_t h;
    cal_list_t loaded;
    cal_list_init(&loaded);
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, NVS_META_KEY, meta, &mlen) == ESP_OK) cal_list_decode(&loaded, meta, mlen);
        nvs_close(h);
    }
    lock();
    s.list = loaded;
    cal_list_reconcile(&s.list, mask);
    if (memcmp(&s.list, &loaded, sizeof loaded) != 0) save_meta_locked();
    unlock();

    publish(true);      /* the saved copies first, so a restart without Wi-Fi still follows today's meetings */
    post_current_status();

    BaseType_t ok = xTaskCreatePinnedToCore(task, "cal_sync", CAL_TASK_STACK, NULL, CAL_TASK_PRIO, &s_task, CAL_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "couldn't start the sync task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "%d calendar(s) saved", cal_list_count(&s.list));
    return ESP_OK;
}

void cal_sync_set_online(bool online)
{
    lock();
    bool was = s.online;
    s.online = online;
    if (online && !was) {
        /* Coming up: sync each calendar now if its last good one is over the interval old (or there never was one). */
        for (int i = 0; i < TB_CALS_MAX; i++) {
            cal_slot_t *c = &s.sl[i];
            tb_epoch_t age = c->last_sync && clock_ok() ? wall_now() - c->last_sync : CAL_SYNC_EVERY_S;
            if (age < 0 || age >= CAL_SYNC_EVERY_S || c->failing) c->next_sync_ms = 0;
            else c->next_sync_ms = mono_ms() + (CAL_SYNC_EVERY_S - age) * 1000LL;
            c->failures = 0;
        }
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
    if (changed && s.inited)
        for (int i = 0; i < TB_CALS_MAX; i++) s.sl[i].next_sync_ms = 0;    /* "today" moved: read the window again */
    unlock();
    if (changed) wake();
}

/* Queue a check of url for a new calendar (slot -1) or to replace slot's address. Consumes nothing; copies. */
static esp_err_t queue_check(const char *url, int slot, const char *name, const char *tag, bool from_setup, cal_res_t *res)
{
    cal_url_info_t *info = calloc(1, sizeof(*info));
    if (!info) return ESP_ERR_NO_MEM;
    cal_url_err_t e = cal_url_check(url, info);
    if (res) res->url_err = (int)e;
    if (e != CAL_URL_OK) {
        free_info(info);
        return ESP_ERR_INVALID_ARG;
    }
    char *copy = strdup(info->url);
    free_info(info);
    if (!copy) return ESP_ERR_NO_MEM;
    lock();
    /* The name and tag are checked now (on a copy of the list), so a mistake answers at once instead of after the fetch. */
    cal_list_t trial = s.list;
    cal_list_err_t le = slot < 0 ? cal_list_add(&trial, name, tag, NULL) : cal_list_edit(&trial, slot, name, tag);
    if (le != CAL_LIST_OK) {
        unlock();
        free_secret(copy);
        if (res) res->list_err = (int)le;
        return le == CAL_LIST_NO_SUCH ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_ARG;
    }
    free_secret(s.check_url);  /* a newer address replaces one still waiting */
    s.check_url = copy;
    s.check_slot = slot;
    tb_strlcpy(s.check_name, name ? name : "", sizeof s.check_name);
    tb_strlcpy(s.check_tag, tag ? tag : "", sizeof s.check_tag);
    s.check_from_setup = from_setup;
    s.check = CAL_CHECK_CHECKING;
    s.check_id = slot < 0 ? 0 : slot + 1;
    s.check_error = NULL;
    s.check_message[0] = '\0';
    bool saved = any_saved();
    tb_epoch_t last = latest_sync();
    unlock();
    post_status(saved, true, last);
    wake();
    return ESP_OK;
}

esp_err_t cal_sync_add(const char *url, const char *name, const char *tag, bool from_setup, cal_res_t *res)
{
    cal_res_t local = {0, 0};
    if (!res) res = &local;
    memset(res, 0, sizeof *res);
    lock();
    bool full = cal_list_count(&s.list) >= TB_CALS_MAX;
    unlock();
    if (full) {
        res->list_err = CAL_LIST_FULL;
        if (from_setup) {
            /* The setup page's address with three calendars already saved: leave them alone and say so. */
            tb_bus_notify("3 calendars already \xC2\xB7 address not added");
            return ESP_OK;
        }
        return ESP_ERR_INVALID_ARG;
    }
    return queue_check(url, -1, name, tag, from_setup, res);
}

esp_err_t cal_sync_edit(int id, const char *url, const char *name, const char *tag, cal_res_t *res)
{
    cal_res_t local = {0, 0};
    if (!res) res = &local;
    memset(res, 0, sizeof *res);
    int slot = id - 1;
    if (slot < 0 || slot >= TB_CALS_MAX) {
        res->list_err = CAL_LIST_NO_SUCH;
        return ESP_ERR_NOT_FOUND;
    }
    if (url && url[0]) return queue_check(url, slot, name, tag, false, res);
    /* Only the name and/or tag: nothing to check, so it takes effect at once (and is saved: names are the one thing
     * an edit writes to flash). */
    lock();
    cal_list_err_t le = cal_list_edit(&s.list, slot, name, tag);
    if (le == CAL_LIST_OK && !s.sl[slot].saved) le = CAL_LIST_NO_SUCH;
    if (le == CAL_LIST_OK) save_meta_locked();
    unlock();
    res->list_err = (int)le;
    if (le != CAL_LIST_OK) return le == CAL_LIST_NO_SUCH ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_ARG;
    publish(false);     /* the new tag reaches the bar at once */
    return ESP_OK;
}

esp_err_t cal_sync_put(const char *url, bool from_setup, cal_url_err_t *fmt_err)
{
    cal_res_t res = {0, 0};
    esp_err_t err;
    if (from_setup) {
        err = cal_sync_add(url, NULL, NULL, true, &res);
    } else {
        lock();
        int n = cal_list_count(&s.list), only = -1;
        for (int i = 0; i < TB_CALS_MAX; i++)
            if (s.list.c[i].used) only = i;
        unlock();
        if (n > 1) return ESP_ERR_INVALID_STATE;
        err = n == 0 ? cal_sync_add(url, NULL, NULL, false, &res) : cal_sync_edit(only + 1, url, NULL, NULL, &res);
    }
    if (fmt_err) *fmt_err = (cal_url_err_t)res.url_err;
    return err;
}

esp_err_t cal_sync_remove_id(int id)
{
    int slot = id - 1;
    if (slot < 0 || slot >= TB_CALS_MAX) return ESP_ERR_NOT_FOUND;
    tb_cal_removed_t *p = calloc(1, sizeof(*p));
    lock();
    cal_slot_t *c = &s.sl[slot];
    if (!c->saved) {
        unlock();
        free(p);
        return ESP_ERR_NOT_FOUND;
    }
    char name[TB_CAL_NAME_BYTES];
    tb_strlcpy(name, s.list.c[slot].name, sizeof name);
    free_info(c->saved);
    c->saved = NULL;
    c->gen++;                   /* a running sync of it throws its result away */
    c->syncing = c->sync_req = c->failing = false;
    c->failures = 0;
    c->n_last = 0;
    c->have_last = false;
    c->last_sync = 0;
    c->error = NULL;
    c->error_message[0] = '\0';
    cal_list_remove(&s.list, slot);
    bool more = s.check_url != NULL;
    for (int i = 0; i < TB_CALS_MAX; i++) more = more || s.sl[i].sync_req || s.sl[i].syncing;
    if (!more) {                /* a Sync now that was waiting only for this one has nothing left to report */
        s.report = false;
        s.report_ok = true;
    }
    if (s.check_url && s.check_slot == slot) {      /* its new address was waiting for a check */
        free_secret(s.check_url);
        s.check_url = NULL;
        s.check = CAL_CHECK_NONE;
    }
    if (save_url(slot, NULL) != ESP_OK) ESP_LOGW(TAG, "couldn't erase an address");
    erase_list(slot);
    save_meta_locked();

    /* One piece for the app task: the list and the merge as they are without it, so the meeting that only it supplied
     * ends at once and one another calendar also has carries on. */
    tb_cal_info_t info[TB_CALS_MAX];
    int n = merge_locked(info);
    if (s.pub && s.mbuf) {
        memcpy(s.pub, s.mbuf, (size_t)n * sizeof(s.pub[0]));
        s.n_pub = n;
        s.have_pub = true;
    }
    memcpy(s.pub_info, info, sizeof info);
    s.have_pub_info = true;
    if (p) {
        tb_strlcpy(p->name, name, sizeof p->name);
        memcpy(p->list.c, info, sizeof info);
        p->meetings.n = (uint8_t)n;
        if (s.mbuf) memcpy(p->meetings.m, s.mbuf, (size_t)n * sizeof(p->meetings.m[0]));
    }
    bool saved = any_saved(), syncing = any_syncing();
    tb_epoch_t last = latest_sync();
    unlock();

    if (p) {
        tb_event_t ev = {.kind = TB_EV_CAL_REMOVED};
        ev.u.ptr = p;
        if (!tb_bus_post(&ev)) {
            ESP_LOGW(TAG, "bus full: removal dropped");
            free(p);
        }
    } else {
        ESP_LOGE(TAG, "no memory to announce a removal");
    }
    post_status(saved, syncing, last);
    ESP_LOGI(TAG, "%s removed", name);
    return ESP_OK;
}

esp_err_t cal_sync_remove(void)
{
    lock();
    int n = cal_list_count(&s.list), only = -1;
    for (int i = 0; i < TB_CALS_MAX; i++)
        if (s.list.c[i].used) only = i;
    unlock();
    if (n == 0) return ESP_ERR_NOT_FOUND;
    if (n > 1) return ESP_ERR_INVALID_STATE;
    return cal_sync_remove_id(only + 1);
}

esp_err_t cal_sync_now(void)
{
    lock();
    esp_err_t err = ESP_OK;
    if (!any_saved()) err = ESP_ERR_NOT_FOUND;
    else if (!s.online) err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        s.report = true;                    /* every calendar, one after another; the last one reports */
        s.report_ok = true;
        s.sync_now_at = mono_ms();
        for (int i = 0; i < TB_CALS_MAX; i++)
            if (s.sl[i].saved && !(s.running == JOB_SYNC && s.running_slot == i)) s.sl[i].sync_req = true;
    }
    bool saved = any_saved(), syncing = any_syncing();
    tb_epoch_t last = latest_sync();
    unlock();
    /* Refused: still tell the app task, so a "Sync…" tile from the quick menu doesn't stay up. */
    post_status(saved, syncing, last);
    if (err == ESP_OK) wake();
    return err;
}

void cal_sync_get_status(cal_status_t *out)
{
    memset(out, 0, sizeof *out);
    lock();
    out->saved = any_saved();
    out->last_sync = latest_sync();
    out->syncing = any_syncing();
    for (int i = 0; i < TB_CALS_MAX && !out->error; i++) {
        if (s.sl[i].saved && s.sl[i].failing && s.sl[i].error) {
            out->error = s.sl[i].error;
            memcpy(out->error_message, s.sl[i].error_message, sizeof out->error_message);
            out->error_at = s.sl[i].error_at;
        }
    }
    out->check = s.check;
    out->check_id = s.check_id;
    out->check_error = s.check_error;
    memcpy(out->check_message, s.check_message, sizeof out->check_message);
    if ((out->check == CAL_CHECK_SAVED || out->check == CAL_CHECK_FAILED) && mono_ms() - s.check_done_ms > CAL_CHECK_KEEP_MS) {
        out->check = CAL_CHECK_NONE;
        out->check_error = NULL;
        out->check_message[0] = '\0';
    }
    unlock();
}

void cal_sync_get_items(cal_items_t *out)
{
    memset(out, 0, sizeof *out);
    lock();
    tb_epoch_t now = wall_now();
    for (int i = 0; i < TB_CALS_MAX; i++) {
        const cal_slot_t *c = &s.sl[i];
        if (!s.list.c[i].used || !c->saved) continue;
        cal_item_t *it = &out->c[i];
        it->used = true;
        it->id = i + 1;
        memcpy(it->name, s.list.c[i].name, sizeof it->name);
        memcpy(it->tag, s.list.c[i].tag, sizeof it->tag);
        it->last_sync = c->last_sync;
        it->syncing = c->syncing || c->sync_req;
        it->failing = c->failing;
        it->error = c->failing ? c->error : NULL;
        if (it->error) {
            memcpy(it->error_message, c->error_message, sizeof it->error_message);
            it->error_at = c->error_at;
        }
        it->left_today = !c->failing && c->have_last && clock_ok() ? cal_today_left(c->last, c->n_last, now, &s.tz) : 0;
        out->n++;
    }
    unlock();
}
