/*
 * tb_jira.c: the Jira count's state machine and words. Owner: lead developer. See tb_jira.h.
 */
#include <stdio.h>
#include <string.h>

#include "tb_internal.h"
#include "tb_jira.h"

void tb_jira_init(tb_jira_t *j)
{
    memset(j, 0, sizeof *j);
    j->count = -1;
    j->alert_above = -1;
}

void tb_jira_configure(tb_jira_t *j, const char *label, int32_t alert_above)
{
    tb_jira_init(j);
    j->configured = true;
    j->state = TB_JIRA_LOADING;
    tb_strlcpy(j->label, label ? label : "", sizeof j->label);
    j->alert_above = alert_above >= 0 && alert_above <= TB_JIRA_ALERT_MAX ? alert_above : -1;
}

void tb_jira_clear(tb_jira_t *j)
{
    tb_jira_init(j);
}

void tb_jira_apply(tb_jira_t *j, tb_jira_result_t res, int32_t count, tb_epoch_t now)
{
    if (!j->configured) return;
    switch (res) {
    case TB_JIRA_RES_OK:
        if (count < 0) count = 0;
        j->state = TB_JIRA_OK;
        j->count = count;
        j->ok_at = now;
        break;
    case TB_JIRA_RES_UNREACHABLE:
        j->state = TB_JIRA_UNREACHABLE;     /* the last count and its time stay */
        break;
    case TB_JIRA_RES_TOKEN:
        j->state = TB_JIRA_TOKEN;
        j->count = -1;
        j->ok_at = 0;
        break;
    case TB_JIRA_RES_NOFILTER:
        j->state = TB_JIRA_NOFILTER;
        j->count = -1;
        j->ok_at = 0;
        break;
    }
}

bool tb_jira_count_shown(const tb_jira_t *j, tb_epoch_t now, bool now_valid, int32_t *count)
{
    if (!j->configured || j->count < 0) return false;
    bool ok = j->state == TB_JIRA_OK;
    if (j->state == TB_JIRA_UNREACHABLE) {
        /* The last count, while it is under 2 hours old; with the clock unknown there is nothing to measure it by. */
        ok = !now_valid || !j->ok_at || (now >= j->ok_at && now - j->ok_at < TB_JIRA_KEEP_S);
    }
    if (ok && count) *count = j->count;
    return ok;
}

bool tb_jira_over(const tb_jira_t *j, tb_epoch_t now, bool now_valid)
{
    int32_t c;
    return j->alert_above >= 0 && tb_jira_count_shown(j, now, now_valid, &c) && c > j->alert_above;
}

char *tb_jira_count_text(char *buf, size_t cap, int32_t count)
{
    if (count >= TB_JIRA_BIG) snprintf(buf, cap, "10k+");
    else snprintf(buf, cap, "%d", (int)(count < 0 ? 0 : count));
    return buf;
}

char *tb_jira_ago_text(char *buf, size_t cap, int64_t seconds)
{
    int64_t m = (seconds + 30) / 60;
    if (m < 1) m = 1;
    if (m < 60) snprintf(buf, cap, "%d min", (int)m);
    else if (m % 60) snprintf(buf, cap, "%d h %d min", (int)(m / 60), (int)(m % 60));
    else snprintf(buf, cap, "%d h", (int)(m / 60));
    return buf;
}

int32_t tb_jira_next_s(int fails, int32_t retry_after_s)
{
    int32_t s = fails <= 1 ? TB_JIRA_EVERY_S : fails == 2 ? 600 : fails == 3 ? 1200 : 1800;
    if (retry_after_s > s) s = retry_after_s > 86400 ? 86400 : retry_after_s;
    return s;
}
