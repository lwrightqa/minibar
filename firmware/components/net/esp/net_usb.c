/*
 * net_usb.c: the USB Serial/JTAG port: one writer for logs and "@tb " lines, the line reader task. Owner: net builder.
 * Skeleton: installs nothing yet. TODO(net): usb_serial_jtag_driver_install, esp_log_set_vprintf through one mutex
 * that tracks the last byte, non-blocking writes (drop logs when nobody reads), the reader task (2,048-byte lines),
 * replies via tb_bus_exec(net_api_usb_line), the ready event, TB_EV_USB_LINK.
 */
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"

#include "net.h"

esp_err_t net_usb_init(void)
{
    return ESP_OK;
}

void net_usb_write_line(const char *line)
{
    (void)line;
}
