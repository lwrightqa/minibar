/*
 * net_api.c: the router (api.md). Owner: net builder.
 * Skeleton: GET /api/v1/info answers for real (so the wiring can be checked end to end); everything else is 404.
 * TODO(net): every endpoint in api.md Appendix A, the checks listed in net_api.h, and net_api_usb_line().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "net_api.h"
#include "net_macs.h"
#include "net_pair.h"
#include "net_port.h"
#include "tb_fmt.h"

static tb_app_t *s_app;
static net_macs_t s_macs;
static net_pair_t s_pair;

void net_api_bind(tb_app_t *app)
{
    s_app = app;
}

void net_api_init(void)
{
    net_macs_init(&s_macs);
    net_pair_init(&s_pair);
}

static void reply_json(net_resp_t *resp, int status, cJSON *obj)
{
    resp->status = status;
    resp->body = cJSON_PrintUnformatted(obj);
    resp->len = resp->body ? strlen(resp->body) : 0;
    cJSON_Delete(obj);
}

static void reply_error(net_resp_t *resp, int status, const char *code, const char *message, const char *field)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", false);
    cJSON_AddStringToObject(o, "error", code);
    cJSON_AddStringToObject(o, "message", message);
    if (field) cJSON_AddStringToObject(o, "field", field);
    else cJSON_AddNullToObject(o, "field");
    reply_json(resp, status, o);
}

static cJSON *info_object(void)
{
    char id[13], t[40];
    tb_clock_t now = net_port_now();
    net_port_device_id(id);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddStringToObject(o, "device", "TinyBar");
    cJSON_AddStringToObject(o, "device_id", id);
    cJSON_AddStringToObject(o, "name", s_app ? s_app->set.device.name : "TinyBar");
    cJSON_AddStringToObject(o, "fw", net_port_fw_version());
    cJSON_AddStringToObject(o, "api", "1.0");
    cJSON_AddStringToObject(o, "host", s_app ? s_app->wifi_host : "tinybar.local");
    cJSON_AddStringToObject(o, "auth", "bearer");   /* TODO(net): "none" under CONFIG_TINYBAR_API_AUTH_NONE */
    cJSON_AddStringToObject(o, "pairing", net_pair_state(&s_pair, &now));
    cJSON_AddNumberToObject(o, "heartbeat_s", 30);
    cJSON_AddNumberToObject(o, "timeout_s", TB_MAC_TIMEOUT_S);
    if (now.valid) cJSON_AddStringToObject(o, "time", tb_fmt_rfc3339(t, sizeof t, now.wall));
    else cJSON_AddNullToObject(o, "time");
    static const char *const src[] = {"none", "ntp", "rtc", "mac"};
    cJSON_AddStringToObject(o, "time_source", src[net_port_time_source()]);
    net_wifi_info_t w;
    net_port_wifi(&w);
    cJSON_AddStringToObject(o, "wifi", w.state == NET_WIFI_CONNECTED ? "connected" : w.state == NET_WIFI_SETUP ? "setup" : "offline");
    return o;
}

void net_api_handle(const net_req_t *req, net_resp_t *resp)
{
    memset(resp, 0, sizeof(*resp));
    if (!strcmp(req->method, "GET") && !strcmp(req->path, "/api/v1/info")) {
        reply_json(resp, 200, info_object());
        return;
    }
    reply_error(resp, 404, "not_found", "Not built yet.", NULL);
}

void net_api_tick(const tb_clock_t *now)
{
    (void)net_macs_tick(&s_macs, now);  /* TODO(net): on a change, tb_app_set_call() with "Lost contact with your Mac" */
    (void)net_pair_tick(&s_pair, now);  /* TODO(net): tb_app_pairing_end(TIMEOUT) */
}

void net_api_pairing_canceled(const tb_clock_t *now)
{
    (void)now;
    net_pair_cancel(&s_pair);
}

void net_api_forget_devices(const tb_clock_t *now)
{
    (void)now;
    net_pair_forget_all(&s_pair);
}

bool net_api_usb_line(const char *line, size_t len, bool too_long, char *out, size_t cap)
{
    (void)too_long;
    if (len < 4 || strncmp(line, "@tb ", 4) != 0) return false;
    snprintf(out, cap, "@tb {\"id\": null, \"ok\": false, \"error\": \"unknown_cmd\", \"message\": \"Not built yet.\", \"field\": null}");
    return true;    /* TODO(net) */
}

void net_api_usb_ready_line(char *out, size_t cap)
{
    char id[13];
    net_port_device_id(id);
    snprintf(out, cap, "@tb {\"event\": \"ready\", \"device_id\": \"%s\", \"api\": \"1.0\", \"fw\": \"%s\"}", id, net_port_fw_version());
}
