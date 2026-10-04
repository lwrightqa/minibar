# calendar: the secret iCal address, a streaming ICS reader with recurrence and time zones, and the sync service.
# src/ is pure C and also builds on Linux for the host tests; esp/ is the device service.
# Owner: calendar builder.
set(TB_CAL_PURE_SRCS
    src/cal_ics.c
    src/cal_rrule.c
    src/cal_tz.c
    src/cal_url.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${TB_CAL_PURE_SRCS} "esp/cal_sync.c"
                           INCLUDE_DIRS "include"
                           REQUIRES core esp_common
                           PRIV_REQUIRES bus esp_http_client mbedtls nvs_flash esp_timer log freertos)
else()
    add_library(tb_calendar STATIC ${TB_CAL_PURE_SRCS})
    target_include_directories(tb_calendar PUBLIC include)
    target_link_libraries(tb_calendar PUBLIC tb_core)
    target_compile_options(tb_calendar PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
endif()
