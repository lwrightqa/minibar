/* net_internal.h: shared between net's device files. Owner: net builder. */
#pragma once

#include "tb_app.h"

const char *net_device_id(void);
tb_app_t *net_app(void);
