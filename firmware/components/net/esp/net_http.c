/*
 * net_http.c: esp_http_server: every /api/v1/ path through the router (on the app task via tb_bus_exec), and the gzipped
 * Remote and setup pages. Owner: net builder. Skeleton: references the embedded pages so the build checks them.
 */
#include <stdint.h>

#include "esp_http_server.h"
#include "esp_log.h"

extern const uint8_t remote_html_gz_start[] asm("_binary_remote_html_gz_start");
extern const uint8_t remote_html_gz_end[] asm("_binary_remote_html_gz_end");
extern const uint8_t setup_html_gz_start[] asm("_binary_setup_html_gz_start");
extern const uint8_t setup_html_gz_end[] asm("_binary_setup_html_gz_end");

size_t net_http_remote_size(void)
{
    return (size_t)(remote_html_gz_end - remote_html_gz_start) + (size_t)(setup_html_gz_end - setup_html_gz_start);
}
