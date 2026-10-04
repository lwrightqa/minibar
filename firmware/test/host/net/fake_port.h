/* fake_port.h: knobs of the host tests' net_port.h fake. Owner: net builder. */
#pragma once
#include "net_port.h"

extern tb_clock_t fake_now;
extern net_wifi_info_t fake_wifi;
extern net_time_source_t fake_time_source;
extern cal_status_t fake_cal;

/* setup network */
extern net_scan_entry_t fake_networks[8];
extern int fake_n_networks;
extern net_join_status_t fake_join;
extern bool fake_join_ok;               /* what net_port_setup_join returns */
extern int fake_join_calls;
extern char fake_join_ssid[33], fake_join_user[129], fake_join_pass[129], fake_join_cal[1100];

/* calendar */
extern int fake_cal_put_calls;
extern bool fake_cal_put_from_setup;
extern int fake_cal_sync_result;        /* 0, -1 none saved, -2 offline */

/* time */
extern int fake_mac_time_calls;
extern tb_epoch_t fake_mac_time;
extern bool fake_mac_time_accept;

/* token storage */
extern int fake_tokens_saves;

/* Put every knob back to its default (a bar on the office Wi-Fi with network time, no calendar). */
void fake_reset(void);
/* Forget the saved token table too. */
void fake_tokens_clear(void);
