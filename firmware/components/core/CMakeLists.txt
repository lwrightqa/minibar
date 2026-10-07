# core: MiniBar's pure-logic state machines (status engine, Pomodoro, settings, menus, gestures, formatting).
# Builds as an ESP-IDF component and, unchanged, as a plain static library on Linux for the host tests.
# Owner: core builder. Rule: no ESP-IDF, FreeRTOS or LVGL headers anywhere under this directory.
set(TB_CORE_SRCS
    src/tb_app.c
    src/tb_menu.c
    src/tb_pomodoro.c
    src/tb_settings.c
    src/tb_gesture.c
    src/tb_fmt.c
    src/tb_jira.c
    src/tb_text.c
    src/tb_todo.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${TB_CORE_SRCS}
                           INCLUDE_DIRS "include"
                           PRIV_INCLUDE_DIRS "src")
else()
    add_library(tb_core STATIC ${TB_CORE_SRCS})
    target_include_directories(tb_core PUBLIC include PRIVATE src)
    target_compile_options(tb_core PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
endif()
