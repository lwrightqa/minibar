/*
 * tb_todo.h: todo list management. Owner: core builder.
 *
 * Stores and manipulates a todo list in a simple text format:
 *   [ ] Incomplete task
 *   [x] Completed task
 *
 * List persists in NVS key "tb_todo" in "nvs_sec" namespace.
 * Max size: 4096 bytes (fits ~100 tasks easily).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TB_TODO_MAX_BYTES   4096    /* max list size */
#define TB_TODO_MAX_TASKS   100     /* rough estimate for UI feedback */

/* Parse the todo list and find the next incomplete task.
 * Returns the 0-based index of the next incomplete task, or -1 if all done or empty.
 */
int tb_todo_next_incomplete(const char *list);

/* Count total tasks and incomplete tasks in the list.
 * Returns incomplete count (remaining) and sets *total if not NULL.
 */
int tb_todo_count(const char *list, int *total);

/* Toggle a task at the given visual index (0-based).
 * Returns the modified list (must be freed by caller if newly allocated).
 * Returns NULL if index is out of range or parsing fails.
 */
char *tb_todo_toggle_item(const char *list, int index);

/* Get the current todo list from NVS.
 * Returns pointer to static buffer, or empty string if not found.
 * Does not fail; missing key means no todo list yet.
 */
const char *tb_todo_load(void);

/* Save the todo list to NVS.
 * Returns 0 on success, -1 on failure (e.g., too large, NVS error).
 */
int tb_todo_save(const char *list);

/* Validate the todo list format.
 * Checks for valid [ ]/[x] syntax and reasonable task text.
 * Returns 0 if valid, -1 if invalid.
 */
int tb_todo_validate(const char *list);

/* Clear the todo list from NVS. */
void tb_todo_clear(void);

/* Check if the todo list has unsaved changes. */
bool tb_todo_is_dirty(void);

/* Mark the todo list as saved (used after NVS write). */
void tb_todo_mark_clean(void);

#ifdef __cplusplus
}
#endif
