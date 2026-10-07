/*
 * jira_sync.c: Jira sync service (stub). Owner: lead developer.
 * The full implementation will be added by the background agent.
 */
#include <string.h>
#include <esp_timer.h>
#include "jira_service.h"

/* Stub: placeholder while the full implementation is in progress. */

void jira_get_info(jira_info_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
}

jira_err_t jira_save(const jira_input_t *in)
{
    (void)in;
    return JIRA_OK;
}

jira_err_t jira_test(const jira_input_t *in)
{
    (void)in;
    return JIRA_OK;
}

bool jira_remove(void)
{
    return false;
}
