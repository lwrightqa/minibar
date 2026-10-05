/* net_fixture.c: see net_fixture.h. Owner: net builder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_fixture.h"
#include "net_pair.h"
#include "tb_settings.h"

tb_app_t nf_app;
uint32_t nf_ip;
const char *nf_host;
char nf_last_line[NET_REPLY_MAX + 16];

/* Core's effects, handled as main/app_task.c does for the ones that reach net. */
static void drain(void)
{
    tb_effect_t fx[TB_EFFECTS_MAX];
    int n;
    while ((n = tb_app_take_effects(&nf_app, fx, TB_EFFECTS_MAX)) > 0) {
        for (int i = 0; i < n; i++) {
            if (fx[i].kind == TB_FX_PAIRING_CANCELED) net_api_pairing_canceled(&fake_now);
            else if (fx[i].kind == TB_FX_FORGET_DEVICES) net_api_forget_devices(&fake_now);
            else if (fx[i].kind == TB_FX_PAIRING_RESET) net_api_pairing_reset(&fake_now);
        }
    }
}

void nf_advance(tb_ms_t ms)
{
    while (ms > 0) {
        tb_ms_t step = ms > 50 ? 50 : ms;
        fake_now.mono += step;
        /* the wall clock follows in whole seconds */
        static tb_ms_t frac;
        frac += step;
        fake_now.wall += frac / 1000;
        frac %= 1000;
        tb_app_tick(&nf_app, &fake_now);
        net_api_tick(&fake_now);
        drain();
        ms -= step;
    }
}

void nf_setup(void)
{
    fake_reset();
    fake_tokens_clear();
    tb_settings_t s;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    tb_app_init(&nf_app, &s, TB_WIFI_OK, &fake_now);
    net_api_bind(&nf_app);
    net_api_set_auth(true);
    net_api_init();
    nf_ip = 0x1104000a;     /* 10.0.4.17 */
    nf_host = "tinybar.local";
    nf_advance(1600);       /* past the splash */
}

nf_resp_t nf_req(net_req_t *req)
{
    nf_resp_t out;
    memset(&out, 0, sizeof out);
    net_api_handle(req, &out.r);
    out.status = out.r.status;
    if (out.r.body) out.j = cJSON_Parse(out.r.body);
    nf_advance(210);        /* keeps tests under the rate limits */
    return out;
}

nf_resp_t nf_http(const char *method, const char *path, const char *body, const char *token)
{
    net_req_t req = {
        .via = NET_VIA_HTTP,
        .method = method,
        .path = path,
        .body = body,
        .body_len = body ? strlen(body) : 0,
        .content_type_json = true,
        .host = nf_host,
        .bearer = token,
        .peer_ip = nf_ip,
    };
    return nf_req(&req);
}

void nf_free(nf_resp_t *r)
{
    cJSON_Delete(r->j);
    free(r->r.body);
    memset(r, 0, sizeof *r);
}

cJSON *nf_usb(const char *line)
{
    nf_last_line[0] = '\0';
    bool replied = net_api_usb_line(line, strlen(line), false, nf_last_line, sizeof nf_last_line);
    nf_advance(10);
    if (!replied) return NULL;
    if (strncmp(nf_last_line, "@tb ", 4)) return NULL;
    return cJSON_Parse(nf_last_line + 4);
}

const char *nf_pair(const char *kind, const char *scope, const char *client)
{
    static char token[NET_TOKEN_LEN + 1];
    char body[256];
    snprintf(body, sizeof body, "{\"kind\": \"%s\", \"scope\": \"%s\"%s%s%s}", kind, scope, client ? ", \"client\": \"" : "",
             client ? client : "", client ? "\"" : "");
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", body, NULL);
    if (r.status != 202) {
        nf_free(&r);
        return NULL;
    }
    char pid[32];
    snprintf(pid, sizeof pid, "%s", nf_str(r.j, "pairing_id"));
    nf_free(&r);
    nf_advance(1100);
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"%s\"}", pid, nf_app.pairing.code);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    const char *t = r.status == 200 ? nf_str(r.j, "token") : NULL;
    if (t) snprintf(token, sizeof token, "%s", t);
    nf_free(&r);
    nf_advance(1100);
    return t ? token : NULL;
}

cJSON *nf_get(cJSON *j, const char *path)
{
    char buf[128];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *part = strtok(buf, "."); part && j; part = strtok(NULL, ".")) {
        if (cJSON_IsArray(j)) j = cJSON_GetArrayItem(j, atoi(part));
        else j = cJSON_GetObjectItemCaseSensitive(j, part);
    }
    return j;
}

const char *nf_str(cJSON *j, const char *path)
{
    cJSON *x = nf_get(j, path);
    return cJSON_IsString(x) ? x->valuestring : NULL;
}

double nf_num(cJSON *j, const char *path)
{
    cJSON *x = nf_get(j, path);
    return cJSON_IsNumber(x) ? x->valuedouble : -9999;
}

bool nf_true(cJSON *j, const char *path)
{
    return cJSON_IsTrue(nf_get(j, path));
}

bool nf_null(cJSON *j, const char *path)
{
    return cJSON_IsNull(nf_get(j, path));
}

const char *nf_err(nf_resp_t *r)
{
    return nf_str(r->j, "error");
}
