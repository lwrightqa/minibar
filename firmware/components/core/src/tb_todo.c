/*
 * tb_todo.c: todo list management. Owner: core builder.
 * Pure C: no ESP-IDF, no allocation after initialization.
 * NVS integration: the static buffer is loaded at startup by tb_todo_load_from_nvs() (in firmware only, main/app_task.c).
 * Save/clear operations mark dirty; the app task polls and flushes to NVS as debounced writes.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "tb_todo.h"

/* Static buffer for the todo list. NVS integration in main/app_task.c via effects. */
static char s_todo_buffer[TB_TODO_MAX_BYTES];
static bool s_todo_dirty = false;

/* Core functions: these work with the static buffer. NVS load/save/clear are deferred to firmware. */

const char *tb_todo_load(void)
{
    return s_todo_buffer[0] ? s_todo_buffer : "";
}

int tb_todo_save(const char *list)
{
    if (list == NULL) {
        tb_todo_clear();
        return 0;
    }

    size_t len = strlen(list);
    if (len > TB_TODO_MAX_BYTES - 1) {
        return -1;  /* Too large */
    }

    memcpy(s_todo_buffer, list, len + 1);
    s_todo_dirty = true;
    return 0;
}

void tb_todo_clear(void)
{
    s_todo_buffer[0] = '\0';
    s_todo_dirty = true;
}

bool tb_todo_is_dirty(void)
{
    return s_todo_dirty;
}

void tb_todo_mark_clean(void)
{
    s_todo_dirty = false;
}

/* ======================================================================================================== */
/* List Parsing and Manipulation                                                                           */
/* ======================================================================================================== */

/* Parse a single line: returns true if it's a valid task line, and sets *done if provided.
 * Valid format: "[ ] ..." or "[x] ..." at the start of the line.
 */
static bool parse_line(const char *line, bool *done)
{
    if (line[0] != '[') return false;
    if (line[1] != ' ' && line[1] != 'x' && line[1] != 'X') return false;
    if (line[2] != ']') return false;
    if (line[3] != ' ') return false;  /* Space after bracket */

    if (done) {
        *done = (line[1] == 'x' || line[1] == 'X');
    }
    return true;
}

/* Copy s (at most n bytes, cut on a UTF-8 character boundary) into out. */
static void copy_chars(char *out, size_t cap, const char *s, size_t n)
{
    if (cap == 0) return;
    if (n > cap - 1) n = cap - 1;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;   /* don't split a character (n <= strlen(s)) */
    memcpy(out, s, n);
    out[n] = '\0';
}

/* The number of UTF-8 characters in s[0..n). */
static size_t chars_in(const char *s, size_t n)
{
    size_t c = 0;
    for (size_t i = 0; i < n; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) c++;
    return c;
}

bool tb_todo_split_line(const char *line, char *short_name, size_t short_cap, char *task, size_t task_cap)
{
    if (!line || !parse_line(line, NULL)) return false;
    const char *text = line + 4;                         /* after "[ ] " */
    const char *bar = strchr(text, '|');
    if (bar) {
        /* The short name: the text before the "|", trimmed, cut to TB_TODO_SHORT_CHARS characters. */
        const char *name_end = bar;
        const char *name = text;
        while (name < name_end && *name == ' ') name++;
        while (name_end > name && name_end[-1] == ' ') name_end--;
        size_t n = (size_t)(name_end - name);
        if (n > 0) {                                             /* an empty name falls back to the words below */
            while (chars_in(name, n) > TB_TODO_SHORT_CHARS) {    /* cut by characters, not bytes */
                n--;
                while (n > 0 && ((unsigned char)name[n] & 0xC0) == 0x80) n--;
            }
            copy_chars(short_name, short_cap, name, n);
            const char *rest = bar + 1;
            while (*rest == ' ') rest++;
            copy_chars(task, task_cap, rest, strlen(rest));
            return true;
        }
    }
    /* No name before a "|": the first two words of the task, cut as above. */
    const char *p = bar ? bar + 1 : text;
    while (*p == ' ') p++;
    const char *w = p;
    int words = 0;
    while (*w && words < 2) {
        while (*w && *w != ' ') w++;
        words++;
        if (*w == ' ' && words < 2) w++;
    }
    size_t n = (size_t)(w - p);
    while (chars_in(p, n) > TB_TODO_SHORT_CHARS) {
        n--;
        while (n > 0 && ((unsigned char)p[n] & 0xC0) == 0x80) n--;
    }
    copy_chars(short_name, short_cap, p, n);
    copy_chars(task, task_cap, text, strlen(text));
    return true;
}

int tb_todo_next_incomplete(const char *list)
{
    if (!list || *list == '\0') return -1;

    int index = 0;
    const char *line = list;

    while (*line != '\0') {
        /* Find end of line */
        const char *end = strchr(line, '\n');
        if (end == NULL) end = line + strlen(line);

        /* Skip empty lines */
        if (end > line) {
            bool done = false;
            if (parse_line(line, &done)) {
                if (!done) {
                    return index;
                }
                index++;
            }
        }

        /* Move to next line */
        line = end;
        if (*line == '\n') line++;
    }

    return -1;  /* All done or no tasks */
}

int tb_todo_count(const char *list, int *total)
{
    if (!list || *list == '\0') {
        if (total) *total = 0;
        return 0;
    }

    int incomplete = 0;
    int count = 0;
    const char *line = list;

    while (*line != '\0') {
        const char *end = strchr(line, '\n');
        if (end == NULL) end = line + strlen(line);

        if (end > line) {
            bool done = false;
            if (parse_line(line, &done)) {
                count++;
                if (!done) incomplete++;
            }
        }

        line = end;
        if (*line == '\n') line++;
    }

    if (total) *total = count;
    return incomplete;
}

char *tb_todo_toggle_item(const char *list, int index)
{
    if (!list || index < 0) return NULL;

    /* Allocate a new buffer for the result */
    char *result = malloc(TB_TODO_MAX_BYTES);
    if (!result) return NULL;

    char *out = result;
    int current_index = 0;
    const char *line = list;

    while (*line != '\0' && (out - result) < TB_TODO_MAX_BYTES - 10) {
        const char *end = strchr(line, '\n');
        if (end == NULL) end = line + strlen(line);

        size_t line_len = end - line;

        if (line_len > 0) {
            bool done = false;
            if (parse_line(line, &done)) {
                if (current_index == index) {
                    /* Toggle this line */
                    char toggle = done ? ' ' : 'x';
                    out += snprintf(out, TB_TODO_MAX_BYTES - (out - result),
                                   "[%c] %s", toggle, line + 4);
                } else {
                    /* Copy as-is */
                    memcpy(out, line, line_len);
                    out += line_len;
                }
                current_index++;
            } else {
                /* Not a valid task line, copy as-is */
                memcpy(out, line, line_len);
                out += line_len;
            }
        } else {
            /* Empty line, copy as-is */
            memcpy(out, line, line_len);
            out += line_len;
        }

        /* Add newline if original had it */
        if (*end == '\n') {
            *out++ = '\n';
            line = end + 1;
        } else {
            line = end;
        }
    }

    *out = '\0';

    if (current_index <= index) {
        /* Index out of range */
        free(result);
        return NULL;
    }

    return result;
}

int tb_todo_validate(const char *list)
{
    if (!list) return 0;  /* NULL is valid (empty list) */

    if (*list == '\0') return 0;  /* Empty string is valid */

    size_t len = strlen(list);
    if (len > TB_TODO_MAX_BYTES - 1) return -1;  /* Too large */

    const char *line = list;
    int task_count = 0;

    while (*line != '\0' && task_count <= TB_TODO_MAX_TASKS) {
        const char *end = strchr(line, '\n');
        if (end == NULL) end = line + strlen(line);

        size_t line_len = end - line;

        /* Empty lines are OK */
        if (line_len > 0) {
            bool done = false;
            if (parse_line(line, &done)) {
                task_count++;

                /* Check task text (everything after "[ ] " or "[x] ") */
                const char *text = line + 4;
                const char *text_end = end;

                /* Skip whitespace at the end of the line (before newline) */
                while (text_end > text && (text_end[-1] == ' ' || text_end[-1] == '\t')) {
                    text_end--;
                }

                if (text_end <= text || (text_end - text == 0)) {
                    /* Empty task text is invalid */
                    return -1;
                }
            } else if (line[0] != '#' && line[0] != ' ' && line[0] != '\t') {
                /* Non-empty lines that aren't tasks or comments are invalid */
                return -1;
            }
        }

        line = end;
        if (*line == '\n') line++;
    }

    if (task_count > TB_TODO_MAX_TASKS) return -1;  /* Too many tasks */

    return 0;  /* Valid */
}
