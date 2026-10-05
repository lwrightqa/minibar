# main: start-up order, the app task, persistence. Owner: lead developer.
idf_component_register(SRCS "app_main.c" "app_task.c" "settings_store.c"
                       INCLUDE_DIRS "."
                       REQUIRES board bus calendar core net ui lvgl__lvgl nvs_flash esp_timer esp_wifi app_update
                                esp_app_format log)
# MiniBar's own code builds without warnings; keep it that way.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
