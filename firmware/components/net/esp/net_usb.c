/*
 * net_usb.c: the USB Serial/JTAG port (api.md section 6). Owner: net builder.
 *
 * One writer for everything that goes out of the port:
 *   - ESP-IDF's log (esp_log_set_vprintf) and stdout (printf, e.g. LVGL's warnings) go through log_write(): it never
 *     blocks. When no program reads the port the driver's buffer fills, and log output is dropped rather than stall
 *     the app task or the router. No line of log output starts with the "@tb " marker, not even after a line break
 *     inside one write (an SSID can carry one) or after ANSI color codes (net_log_scan).
 *   - protocol lines go through net_usb_write_line(): whole (the writer's lock keeps log output out of them), always
 *     at the start of a line (a line break first if the last byte written wasn't one), with a short time-out.
 * The reader task assembles lines (net_lines_feed, 2,048 bytes at most), sends "@tb " lines to the router on the app
 * task (tb_bus_exec), and writes the one reply each gets, in order. Lines without the marker get no reply.
 * Never logs tokens, the calendar address, Wi-Fi passwords or raw protocol lines.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reent.h>

#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "net.h"
#include "net_api.h"
#include "net_internal.h"
#include "net_util.h"
#include "tb_bus.h"

#define TX_BUF        4096
#define RX_BUF        1024
#define CHUNK         1024      /* at most this much per driver write (each write is all or nothing) */
#define LINE_WAIT_MS  300       /* how long a protocol line may wait for room, per chunk */
#define LOG_WAIT_MS   20        /* how long a log line may wait for the writer */
#define OUT_CAP       (NET_REPLY_MAX + 8)

static SemaphoreHandle_t s_wlock;
static volatile bool s_line_start = true;
static bool s_installed, s_started;
static volatile bool s_link;
static char *s_out;

/* ======================================================================================================== */
/* The writer                                                                                               */
/* ======================================================================================================== */

/* Write in chunks; stops at the first chunk that doesn't fit in time. Returns the bytes written. */
static size_t raw_write(const char *p, size_t n, TickType_t wait)
{
    size_t done = 0;
    while (done < n) {
        size_t k = n - done > CHUNK ? CHUNK : n - done;
        int w = usb_serial_jtag_write_bytes(p + done, k, wait);
        if (w <= 0) break;
        done += (size_t)w;
    }
    if (done) s_line_start = p[done - 1] == '\n';
    return done;
}

/* Where the log output is in its line, for net_log_scan (under s_wlock). A protocol line always ends its line. */
static net_log_state_t s_log_st = NET_LOG_LINE_START;

/* Log output never starts a line with the marker: an '@' at the start of any line in it (after a line break inside
 * a write too, and after the ANSI color codes the Mac app strips) gets a space before it. A write that's cut short
 * drops the rest, so no later part of it can land at a line start the scan didn't expect; the state then assumes a
 * line start, which can only cost a needless space. */
static void log_write(const char *buf, size_t len)
{
    if (!s_installed || !len || !usb_serial_jtag_is_connected()) return;    /* nobody there: drop */
    if (xSemaphoreGetMutexHolder(s_wlock) == xTaskGetCurrentTaskHandle()) return;  /* logged from inside the writer */
    if (xSemaphoreTake(s_wlock, pdMS_TO_TICKS(LOG_WAIT_MS)) != pdTRUE) return;
    net_log_state_t st = s_log_st;
    size_t i = 0;
    while (i < len) {
        size_t k = net_log_scan(&st, buf + i, len - i);
        if (k && raw_write(buf + i, k, 0) != k) break;
        i += k;
        if (i < len) {      /* an '@' would start a line: never the marker */
            if (raw_write(" ", 1, 0) != 1) break;
            st = NET_LOG_MID;
        }
    }
    s_log_st = i < len ? NET_LOG_LINE_START : st;
    xSemaphoreGive(s_wlock);
}

static int log_vprintf(const char *fmt, va_list ap)
{
    char buf[256];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n < 0) return n;
    size_t len = (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1;
    if ((size_t)n >= sizeof buf) buf[len - 1] = '\n';     /* cut, but still ends its line */
    log_write(buf, len);
    return n;
}

static int stdout_write(void *cookie, const char *buf, int n)
{
    (void)cookie;
    if (n > 0) log_write(buf, (size_t)n);
    return n;
}

void net_usb_write_line(const char *line)
{
    if (!s_installed || !line) return;
    if (xSemaphoreTake(s_wlock, pdMS_TO_TICKS(LINE_WAIT_MS)) != pdTRUE) return;
    TickType_t wait = pdMS_TO_TICKS(LINE_WAIT_MS);
    if (!s_line_start) raw_write("\n", 1, wait);
    size_t n = strlen(line);
    if (raw_write(line, n, wait) == n) raw_write("\n", 1, wait);
    else s_line_start = false;      /* cut short: whatever comes next starts on a new line */
    s_log_st = NET_LOG_LINE_START;  /* either way, log output after it is checked as a line's start */
    xSemaphoreGive(s_wlock);
}

esp_err_t net_usb_init(void)
{
    if (s_installed) return ESP_OK;
    s_wlock = xSemaphoreCreateMutex();
    if (!s_wlock) return ESP_ERR_NO_MEM;
    usb_serial_jtag_driver_config_t cfg = {.tx_buffer_size = TX_BUF, .rx_buffer_size = RX_BUF};
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) return err;
    /* stdio through the driver too (its own writes give up after 50 ms when nobody reads) */
    usb_serial_jtag_vfs_use_driver();
    s_installed = true;
    esp_log_set_vprintf(log_vprintf);
    /* printf from any task created from here on (and this one) goes through the same writer */
    FILE *f = funopen(NULL, NULL, stdout_write, NULL, NULL);
    if (f) {
        setvbuf(f, NULL, _IOLBF, 256);
        _REENT_STDOUT(_GLOBAL_REENT) = f;
        stdout = f;
    }
    return ESP_OK;
}

/* ======================================================================================================== */
/* The reader                                                                                               */
/* ======================================================================================================== */

typedef struct {
    const char *line;
    size_t len;
    bool too_long;
    bool replied;
} line_job_t;

static void line_job(void *ctx)
{
    line_job_t *j = ctx;
    j->replied = net_api_usb_line(j->line, j->len, j->too_long, s_out, OUT_CAP);
}

static void post_link(bool up)
{
    tb_event_t ev = {.kind = TB_EV_USB_LINK};
    ev.u.flag = up;
    tb_bus_post(&ev);
}

static void on_line(const char *line, size_t len, bool too_long, void *ctx)
{
    (void)ctx;
    if (len < 4 || memcmp(line, "@tb ", 4)) return;     /* not a protocol line: no reply */
    if (!s_link) {
        s_link = true;
        post_link(true);
    }
    line_job_t j = {.line = line, .len = len, .too_long = too_long};
    if (!tb_bus_exec(line_job, &j, 900)) {
        /* the app task didn't get to it: still exactly one reply, with the request's id if it has one */
        char id[16] = "null";
        /* This parse runs on this task's 4 KB stack: never a deeply nested line (NET_JSON_DEPTH_MAX) */
        bool shallow = !too_long && net_json_depth_ok(line + 4, len - 4, NET_JSON_DEPTH_MAX);
        cJSON *o = shallow ? cJSON_ParseWithLength(line + 4, len - 4) : NULL;
        const cJSON *jid = o ? cJSON_GetObjectItemCaseSensitive(o, "id") : NULL;
        if (cJSON_IsNumber(jid) && jid->valuedouble >= 1 && jid->valuedouble <= 2147483647 &&
            jid->valuedouble == (double)(int64_t)jid->valuedouble)
            snprintf(id, sizeof id, "%ld", (long)jid->valuedouble);
        cJSON_Delete(o);
        snprintf(s_out, OUT_CAP, "@tb {\"id\":%s,\"ok\":false,\"error\":\"busy\",\"message\":\"MiniBar is busy. Try again in a second.\",\"field\":null,\"retry_after_s\":1}", id);
        j.replied = true;
    }
    if (j.replied) net_usb_write_line(s_out);
}

static void usb_rx_task(void *arg)
{
    (void)arg;
    net_lines_t *lines = heap_caps_malloc(sizeof *lines, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!lines) lines = malloc(sizeof *lines);
    if (!lines) vTaskDelete(NULL);
    net_lines_init(lines);
    bool wdt = esp_task_wdt_add(NULL) == ESP_OK;
    uint8_t buf[128];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof buf, pdMS_TO_TICKS(500));
        if (n > 0) net_lines_feed(lines, buf, (size_t)n, on_line, NULL);
        if (s_link && !usb_serial_jtag_is_connected()) {
            s_link = false;
            net_lines_init(lines);      /* a half line from before the cable came out isn't finished later */
            post_link(false);
        }
        if (wdt) esp_task_wdt_reset();
    }
}

void net_usb_start(void)
{
    if (!s_installed || s_started) return;
    s_out = heap_caps_malloc(OUT_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_out) s_out = malloc(OUT_CAP);
    if (!s_out) return;
    /* The reader's stack is in PSRAM: it only reads the port and hands lines to the app task, never writes flash. */
    if (xTaskCreatePinnedToCoreWithCaps(usb_rx_task, "usb_rx", 4096, NULL, 4, NULL, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS &&
        xTaskCreatePinnedToCore(usb_rx_task, "usb_rx", 4096, NULL, 4, NULL, 0) != pdPASS)
        return;
    s_started = true;
    char ready[160];
    net_api_usb_ready_line(ready, sizeof ready);
    net_usb_write_line(ready);
}
