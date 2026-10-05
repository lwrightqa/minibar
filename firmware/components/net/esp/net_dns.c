/*
 * net_dns.c: the setup network's DNS catch-all. Owner: net builder.
 *
 * While MiniBar-Setup is up, every A query from a phone on it is answered with the setup address (NET_SETUP_IP), so
 * its captive-portal check lands on the bar, where net_http.c answers it with a 302 to the setup page. Other types
 * (AAAA, HTTPS...) get an empty answer (NODATA), so phones use IPv4. The answers come from net_dns_answer()
 * (net_util.c, host-tested).
 *
 * The socket listens on every address (INADDR_ANY), as ESP-IDF's captive_portal example does, so it never depends on
 * the access point's address being up when it binds. It answers only while the setup network is up, and only
 * queries from the setup network's subnet: during "Set up again" the station side gets nothing.
 * One small task, created the first time; between setup sessions it closes its socket and sleeps. net_wifi.c starts
 * it on WIFI_EVENT_AP_START and stops it when the setup network closes.
 *
 * The log (INFO) has every query, the first 40 a minute: who asked, the name and type, and what it got, written
 * after the answer went out.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "net_internal.h"
#include "net_util.h"

static const char *TAG = "net.dns";

#define DNS_LOG_PER_MIN 40

static volatile bool s_run;
static TaskHandle_t s_task;
static volatile uint32_t s_ip, s_mask;
static net_log_quota_t s_quota;     /* the dns task's own */

static const char *type_name(uint16_t t, char buf[12])
{
    switch (t) {
    case 1: return "A";
    case 5: return "CNAME";
    case 12: return "PTR";
    case 16: return "TXT";
    case 28: return "AAAA";
    case 33: return "SRV";
    case 64: return "SVCB";
    case 65: return "HTTPS";
    case 255: return "ANY";
    default: snprintf(buf, 12, "type %u", (unsigned)t); return buf;
    }
}

/* After the answer went out (send_err: its errno, or 0), so the log never delays it: the logger can wait up to 20 ms
 * for the port. */
static void log_query(const uint8_t *q, int n, uint32_t from, const uint8_t *a, size_t m, int send_err)
{
    int dropped;
    if (!net_log_quota_take(&s_quota, esp_timer_get_time() / 1000, DNS_LOG_PER_MIN, &dropped)) return;
    if (dropped) ESP_LOGI(TAG, "(%d more queries in the last minute weren't logged)", dropped);
    char who[16], name[72], tb[12], ip[16];
    uint16_t type = 0;
    net_ip_str(from, who);
    if (!net_dns_question(q, (size_t)n, name, sizeof name, &type)) {
        ESP_LOGI(TAG, "%s sent %d bytes that aren't a query: no answer", who, n);
        return;
    }
    if (!m) ESP_LOGI(TAG, "%s asks %s %s: no answer (didn't fit)", who, type_name(type, tb), name);
    else if (send_err)
        ESP_LOGW(TAG, "%s asks %s %s: the answer wasn't sent (errno %d)", who, type_name(type, tb), name, send_err);
    else if (a[7]) ESP_LOGI(TAG, "%s asks %s %s: answered %s", who, type_name(type, tb), name, net_ip_str(s_ip, ip));
    else ESP_LOGI(TAG, "%s asks %s %s: no address of that type (NODATA)", who, type_name(type, tb), name);
}

static void serve(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket failed (errno %d); trying again in 1 s", errno);
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(53)};
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
        ESP_LOGE(TAG, "binding port 53 failed (errno %d); trying again in 1 s", errno);
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    uint8_t *q = malloc(512), *a = malloc(600);
    if (!q || !a) {
        ESP_LOGE(TAG, "no memory for the buffers; trying again in 1 s");
        free(q);
        free(a);
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }
    char ip[16];
    ESP_LOGI(TAG, "listening on port 53: every name is %s", net_ip_str(s_ip, ip));
    int errors = 0;
    while (s_run) {
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        int n = recvfrom(sock, q, 512, 0, (struct sockaddr *)&from, &fl);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;     /* the 1 s time-out: look at s_run again */
            /* Anything else waits a little rather than spinning on core 0, whatever the network does */
            if (++errors == 1 || errors % 60 == 0) ESP_LOGW(TAG, "receiving failed (errno %d)", errno);
            vTaskDelay(pdMS_TO_TICKS(errors < 10 ? 100 : 1000));
            continue;
        }
        errors = 0;
        if (fl < sizeof from || from.sin_family != AF_INET) continue;
        uint32_t me = s_ip, mask = s_mask;
        if (!net_ip_same_subnet(from.sin_addr.s_addr, me, mask)) continue;    /* not from MiniBar-Setup */
        size_t m = net_dns_answer(q, (size_t)n, me, a, 600);
        int send_err = 0;
        if (m && sendto(sock, a, m, 0, (struct sockaddr *)&from, sizeof from) < 0) send_err = errno ? errno : -1;
        log_query(q, n, from.sin_addr.s_addr, a, m, send_err);
    }
    free(q);
    free(a);
    close(sock);
    ESP_LOGI(TAG, "stopped");
}

static void dns_task(void *arg)
{
    (void)arg;
    for (;;) {
        while (!s_run) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        serve();
    }
}

void net_dns_start(uint32_t ip, uint32_t mask)
{
    if (!ip || !mask) {
        ESP_LOGE(TAG, "not started: the setup network has no address");
        return;
    }
    s_ip = ip;
    s_mask = mask;
    s_run = true;
    if (s_task) {
        xTaskNotifyGive(s_task);
        return;
    }
    /* Priority 5, as in ESP-IDF's captive_portal example (above the imu task's 3 on core 0). The stack is in PSRAM:
     * the task only uses sockets and the log, never flash, so it can be roomy (4 KB with the protocol-safe logger's
     * frame on top of lwIP's). */
    if (xTaskCreatePinnedToCoreWithCaps(dns_task, "dns", 4096, NULL, 5, &s_task, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS &&
        xTaskCreatePinnedToCore(dns_task, "dns", 4096, NULL, 5, &s_task, 0) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "not started: no memory for its task");
    }
}

void net_dns_stop(void)
{
    s_run = false;     /* the task sees it within a second, closes its socket and sleeps */
}
