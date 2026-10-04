/* fake_port.h: knobs of the host tests' net_port.h fake. Owner: net builder. */
#pragma once
#include "net_port.h"

extern tb_clock_t fake_now;
extern net_wifi_info_t fake_wifi;
extern net_time_source_t fake_time_source;
extern cal_status_t fake_cal;
