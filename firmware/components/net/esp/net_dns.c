/*
 * net_dns.c: the setup network's DNS catch-all. Owner: net builder.
 *
 * While TinyBar-Setup is up, every name a phone looks up resolves to 192.168.4.1, so its captive-portal check lands
 * on the setup page (net_http.c sends foreign Host headers there with a 302). The answers come from net_dns_answer()
 * (net_util.c, host-tested). One small task, created the first time; between setup sessions it closes its socket
 * and sleeps until the next one.
 */
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "net_internal.h"
#include "net_util.h"

static const char *TAG = "net.dns";
static volatile bool s_run;
static TaskHandle_t s_task;
static volatile uint32_t s_ip;

static void serve(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket failed");
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(53)};
    addr.sin_addr.s_addr = s_ip;
    if (bind(sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
        ESP_LOGE(TAG, "bind failed");
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ESP_LOGI(TAG, "answering every name with the setup address");
    uint8_t *q = malloc(512), *a = malloc(600);
    while (s_run && q && a) {
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        int n = recvfrom(sock, q, 512, 0, (struct sockaddr *)&from, &fl);
        if (n <= 0) continue;
        size_t m = net_dns_answer(q, (size_t)n, s_ip, a, 600);
        if (m) sendto(sock, a, m, 0, (struct sockaddr *)&from, fl);
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

void net_dns_start(uint32_t ip)
{
    if (!ip) return;
    s_ip = ip;
    s_run = true;
    if (!s_task) xTaskCreatePinnedToCore(dns_task, "dns", 3072, NULL, 3, &s_task, 0);
    else xTaskNotifyGive(s_task);
}

void net_dns_stop(void)
{
    s_run = false;     /* the task sees it within a second, closes its socket and sleeps */
}
