/*
 * net.h: TinyBar's networking on the device: Wi-Fi (station incl. WPA2-Enterprise PEAP; the TinyBar-Setup access
 * point with a DNS catch-all and the setup page), mDNS, the HTTP server and Remote page, the USB serial protocol,
 * SNTP. ESP-IDF only; the protocol logic is in net_api.h, net_macs.h and net_pair.h.
 *
 * Owner: net builder.
 *
 * Start-up (main/app_main.c):
 *   net_usb_init()   early, right after the bus: installs the USB Serial/JTAG driver and routes ESP-IDF logging
 *                    through the protocol-safe writer (api.md 6.4: protocol lines are whole, start a line, and logging
 *                    never blocks when nobody reads the port).
 *   net_init()       netif, event loop, Wi-Fi driver, stored credentials, the token table, binds the router.
 *   net_start()      after the app task runs: station or setup mode, HTTP server, mDNS, SNTP, the USB reader task,
 *                    and the "ready" event line.
 * Events posted: TB_EV_WIFI (setup progress, link up/down), TB_EV_TIME_SET, TB_EV_USB_LINK.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "tb_app.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t net_usb_init(void);
esp_err_t net_init(tb_app_t *app, const char *device_id);
esp_err_t net_start(void);

/* How the Wi-Fi starts, for tb_app_init(): TB_WIFI_OFFLINE when Skip was the last choice (remembered in NVS, so an
 * offline bar starts offline with the radio off), TB_WIFI_OK when a network is saved, else TB_WIFI_SETUP (the QR code).
 * It only reads NVS, so it works before net_init(). */
tb_wifi_mode_t net_wifi_start_mode(void);

/* core's Wi-Fi effects (main forwards them). */
void net_setup_begin(void);     /* TB_FX_WIFI_SETUP: start TinyBar-Setup (open), 192.168.4.1, DNS catch-all, page;
                                 * forgets a saved skip */
void net_setup_skip(void);      /* TB_FX_WIFI_SKIP: stop it; stay offline (station off), and remember it */
void net_setup_done(void);      /* TB_FX_WIFI_DONE: the Connected screen moved on; stop the setup network */

/* settings.device.name changed: update the mDNS instance name. */
void net_name_changed(const char *name);

/* Write one protocol line ("@tb {...}") to USB, whole, after a line break if needed. Never blocks for long. */
void net_usb_write_line(const char *line);

#ifdef __cplusplus
}
#endif
