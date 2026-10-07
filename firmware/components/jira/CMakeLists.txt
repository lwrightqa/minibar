# jira: the Jira issue count (decisions.md "Jira issue count (2026-10-07)"). src/ is pure C (checking what the Remote
# sends, building and signing the request, reading the answer) and also builds on Linux for the host tests; esp/ is the
# device service that asks Jira over HTTPS every 5 minutes. Owner: lead developer.
set(TB_JIRA_PURE_SRCS
    src/jira_proto.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${TB_JIRA_PURE_SRCS} "esp/jira_sync.c"
                           INCLUDE_DIRS "include"
                           REQUIRES core esp_common
                           PRIV_REQUIRES bus calendar esp_http_client esp-tls mbedtls nvs_flash esp_timer log freertos heap)
else()
    add_library(tb_jira STATIC ${TB_JIRA_PURE_SRCS})
    target_include_directories(tb_jira PUBLIC include)
    target_link_libraries(tb_jira PUBLIC tb_core)
    target_compile_options(tb_jira PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
endif()
