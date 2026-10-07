/* fake_port.h: knobs of the host tests' net_port.h fake. Owner: net builder. */
#pragma once
#include "jira_service.h"
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
/* several calendars: the list the fake reports, and what the last add or edit was asked (an add or an edit with an
 * address only starts a check; the test finishes it by changing fake_items and fake_cal.check itself) */
extern cal_items_t fake_items;
extern int fake_cal_add_calls, fake_cal_edit_calls, fake_cal_remove_calls;
extern char fake_cal_last_url[1100], fake_cal_last_name[100], fake_cal_last_tag[40];
extern int fake_cal_last_id;

/* Jira (jira_service.h's fake): what is "saved", what the last check said, and what the router asked. A test finishes a
 * Test by filling in fake_jira.test itself. */
typedef struct {
    bool saved;
    jira_cfg_t cfg;
    tb_jira_state_t state;
    int32_t count;
    tb_epoch_t updated_at;
    char filter_name[TB_JIRA_LABEL_BYTES];
    jira_test_state_t test_state;
    char test_name[TB_JIRA_LABEL_BYTES];
    int32_t test_count;
    const char *test_error;
    jira_cfg_t last_test_cfg;
    int save_calls, test_calls, remove_calls;
} fake_jira_t;
extern fake_jira_t fake_jira;

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
