/*
 * test_todo.c: tests for tb_todo. Owner: core builder.
 */
#include <string.h>
#include <stdlib.h>
#include "tb_test.h"

#include "tb_todo.h"

/* ======================================================================================================== */
/* tb_todo_next_incomplete                                                                                 */
/* ======================================================================================================== */

TB_TEST(next_incomplete_empty)
{
    int idx = tb_todo_next_incomplete("");
    TB_EQ_INT(idx, -1);
}

TB_TEST(next_incomplete_null)
{
    int idx = tb_todo_next_incomplete(NULL);
    TB_EQ_INT(idx, -1);
}

TB_TEST(next_incomplete_first_task)
{
    const char *list = "[ ] Task 1\n[x] Task 2\n";
    int idx = tb_todo_next_incomplete(list);
    TB_EQ_INT(idx, 0);
}

TB_TEST(next_incomplete_second_task)
{
    const char *list = "[x] Task 1\n[ ] Task 2\n";
    int idx = tb_todo_next_incomplete(list);
    TB_EQ_INT(idx, 1);
}

TB_TEST(next_incomplete_all_done)
{
    const char *list = "[x] Task 1\n[x] Task 2\n";
    int idx = tb_todo_next_incomplete(list);
    TB_EQ_INT(idx, -1);
}

TB_TEST(next_incomplete_no_newline)
{
    const char *list = "[ ] Task without newline";
    int idx = tb_todo_next_incomplete(list);
    TB_EQ_INT(idx, 0);
}

/* ======================================================================================================== */
/* tb_todo_count                                                                                            */
/* ======================================================================================================== */

TB_TEST(count_empty)
{
    int total = 0;
    int incomplete = tb_todo_count("", &total);
    TB_EQ_INT(incomplete, 0);
    TB_EQ_INT(total, 0);
}

TB_TEST(count_mixed)
{
    const char *list = "[ ] Task 1\n[x] Task 2\n[ ] Task 3\n";
    int total = 0;
    int incomplete = tb_todo_count(list, &total);
    TB_EQ_INT(incomplete, 2);
    TB_EQ_INT(total, 3);
}

TB_TEST(count_all_incomplete)
{
    const char *list = "[ ] Task 1\n[ ] Task 2\n";
    int total = 0;
    int incomplete = tb_todo_count(list, &total);
    TB_EQ_INT(incomplete, 2);
    TB_EQ_INT(total, 2);
}

TB_TEST(count_case_insensitive)
{
    const char *list = "[ ] Task 1\n[X] Task 2\n";
    int total = 0;
    int incomplete = tb_todo_count(list, &total);
    TB_EQ_INT(incomplete, 1);
    TB_EQ_INT(total, 2);
}

/* ======================================================================================================== */
/* tb_todo_toggle_item                                                                                     */
/* ======================================================================================================== */

TB_TEST(toggle_incomplete_to_done)
{
    const char *list = "[ ] Task 1\n[x] Task 2\n";
    char *result = tb_todo_toggle_item(list, 0);
    TB_TRUE(result != NULL);
    TB_TRUE(strstr(result, "[x] Task 1") != NULL);
    TB_TRUE(strstr(result, "[x] Task 2") != NULL);
    free(result);
}

TB_TEST(toggle_done_to_incomplete)
{
    const char *list = "[x] Task 1\n[ ] Task 2\n";
    char *result = tb_todo_toggle_item(list, 0);
    TB_TRUE(result != NULL);
    TB_TRUE(strstr(result, "[ ] Task 1") != NULL);
    free(result);
}

TB_TEST(toggle_out_of_range)
{
    const char *list = "[ ] Task 1\n";
    char *result = tb_todo_toggle_item(list, 5);
    TB_TRUE(result == NULL);
}

TB_TEST(toggle_negative_index)
{
    const char *list = "[ ] Task 1\n";
    char *result = tb_todo_toggle_item(list, -1);
    TB_TRUE(result == NULL);
}

/* ======================================================================================================== */
/* tb_todo_validate                                                                                        */
/* ======================================================================================================== */

TB_TEST(validate_empty)
{
    int result = tb_todo_validate("");
    TB_EQ_INT(result, 0);
}

TB_TEST(validate_null)
{
    int result = tb_todo_validate(NULL);
    TB_EQ_INT(result, 0);
}

TB_TEST(validate_valid_list)
{
    const char *list = "[ ] Task 1\n[x] Task 2\n";
    int result = tb_todo_validate(list);
    TB_EQ_INT(result, 0);
}

TB_TEST(validate_invalid_bracket)
{
    const char *list = "[  ] Invalid\n";
    int result = tb_todo_validate(list);
    TB_TRUE(result != 0);
}

TB_TEST(validate_missing_space_after_bracket)
{
    const char *list = "[x]No space\n";
    int result = tb_todo_validate(list);
    TB_TRUE(result != 0);
}

TB_TEST(validate_empty_task)
{
    const char *list = "[ ] \n";
    int result = tb_todo_validate(list);
    TB_TRUE(result != 0);
}

TB_TEST(validate_with_comments)
{
    const char *list = "# This is a comment\n[ ] Task 1\n";
    int result = tb_todo_validate(list);
    TB_EQ_INT(result, 0);
}

/* ======================================================================================================== */
/* tb_todo_split_line: "[ ] Name | Task" gives the short name and the task; no "|" takes the first two words */
/* ======================================================================================================== */

TB_TEST(split_with_bar)
{
    char name[64], task[256];
    TB_TRUE(tb_todo_split_line("[ ] Budget | Send the budget draft to Priya", name, sizeof name, task, sizeof task));
    TB_EQ_STR(name, "Budget");
    TB_EQ_STR(task, "Send the budget draft to Priya");
}

TB_TEST(split_trims_spaces_around_bar)
{
    char name[64], task[256];
    TB_TRUE(tb_todo_split_line("[x]   Jira   |   Review the notes  ", name, sizeof name, task, sizeof task));
    TB_EQ_STR(name, "Jira");
    TB_EQ_STR(task, "Review the notes  ");
}

TB_TEST(split_no_bar_takes_two_words)
{
    char name[64], task[256];
    TB_TRUE(tb_todo_split_line("[ ] Send the budget draft", name, sizeof name, task, sizeof task));
    TB_EQ_STR(name, "Send the");
    TB_EQ_STR(task, "Send the budget draft");
}

TB_TEST(split_single_word)
{
    char name[64], task[256];
    TB_TRUE(tb_todo_split_line("[ ] Sleep", name, sizeof name, task, sizeof task));
    TB_EQ_STR(name, "Sleep");
    TB_EQ_STR(task, "Sleep");
}

TB_TEST(split_empty_name_falls_back_to_words)
{
    char name[64], task[256];
    TB_TRUE(tb_todo_split_line("[ ] | Book the room", name, sizeof name, task, sizeof task));
    TB_EQ_STR(name, "Book the");
}

TB_TEST(split_cuts_name_to_18_characters)
{
    char name[64], task[256];
    TB_TRUE(tb_todo_split_line("[ ] Reviewing the quarterly budget forecast | x", name, sizeof name, task, sizeof task));
    TB_TRUE(strlen(name) <= 18);
    TB_EQ_STR(name, "Reviewing the quar");
}

TB_TEST(split_cuts_by_characters_not_bytes)
{
    char line[256] = "[ ] ", name[64], task[256];
    for (int i = 0; i < 25; i++) strcat(line, "é");   /* 25 "é" (2 bytes each) */
    strcat(line, " | x");
    TB_TRUE(tb_todo_split_line(line, name, sizeof name, task, sizeof task));
    TB_EQ_INT((int)strlen(name), 36);   /* 18 characters, 2 bytes each: never cut in the middle of a character */
}

TB_TEST(split_rejects_non_task_lines)
{
    char name[64], task[256];
    TB_TRUE(!tb_todo_split_line("Not a task | x", name, sizeof name, task, sizeof task));
    TB_TRUE(!tb_todo_split_line("", name, sizeof name, task, sizeof task));
}
