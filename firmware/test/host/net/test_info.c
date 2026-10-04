/* Seed test for the router (lead): GET /api/v1/info over HTTP. Owner from here: net builder. */
#include <stdlib.h>

#include "fake_port.h"
#include "net_api.h"
#include "tb_test.h"

static tb_app_t s_app;

TB_TEST(info_answers_with_the_device_id)
{
    tb_settings_t s;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    tb_app_init(&s_app, &s, true, &fake_now);
    net_api_bind(&s_app);
    net_api_init();
    net_req_t req = {.via = NET_VIA_HTTP, .method = "GET", .path = "/api/v1/info", .host = "tinybar.local"};
    net_resp_t resp;
    net_api_handle(&req, &resp);
    TB_EQ_INT(resp.status, 200);
    TB_TRUE(resp.body && strstr(resp.body, "\"device_id\":\"f412fa3f2a1c\""));
    TB_TRUE(resp.body && strstr(resp.body, "\"name\":\"TinyBar 2A1C\""));
    TB_TRUE(resp.body && strstr(resp.body, "\"api\":\"1.0\""));
    free(resp.body);
}
