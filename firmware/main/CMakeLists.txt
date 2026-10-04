# main: start-up order, the app task, persistence. Owner: lead developer.
idf_component_register(SRCS "app_main.c" "app_task.c" "settings_store.c"
                       INCLUDE_DIRS "."
                       REQUIRES board bus calendar core net ui lvgl__lvgl nvs_flash esp_timer app_update esp_app_format log)
