# net: Wi-Fi, setup network, mDNS, HTTP API and Remote page, USB serial protocol, SNTP.
# proto/ is pure C (router, Mac table, pairing) and also builds on Linux for the host tests; esp/ is the device side.
# Owner: net builder.
set(TB_NET_PROTO_SRCS
    proto/net_api.c
    proto/net_macs.c
    proto/net_pair.c
    proto/net_util.c
)

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${TB_NET_PROTO_SRCS}
             "esp/net_main.c"
             "esp/net_wifi.c"
             "esp/net_dns.c"
             "esp/net_http.c"
             "esp/net_usb.c"
             "esp/net_port_esp.c"
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS "esp"
        REQUIRES core calendar esp_common
        PRIV_REQUIRES bus json esp_wifi esp_netif esp_event esp_http_server wpa_supplicant lwip nvs_flash
                      esp_driver_usb_serial_jtag esp_timer esp_app_format mbedtls log espressif__mdns
                      bootloader_support freertos)

    # The Remote and setup pages, gzipped at build time and embedded (served with Content-Encoding: gzip).
    foreach(page remote setup)
        set(src ${CMAKE_CURRENT_SOURCE_DIR}/web/${page}.html)
        set(gz ${CMAKE_CURRENT_BINARY_DIR}/${page}.html.gz)
        add_custom_command(OUTPUT ${gz}
                           COMMAND ${python} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gzip_asset.py ${src} ${gz}
                           DEPENDS ${src} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gzip_asset.py
                           VERBATIM)
        add_custom_target(net_web_${page} DEPENDS ${gz})
        target_add_binary_data(${COMPONENT_LIB} ${gz} BINARY DEPENDS net_web_${page})
    endforeach()
else()
    # Host: the protocol code with IDF's copy of cJSON.
    if(NOT DEFINED IDF_PATH)
        set(IDF_PATH $ENV{IDF_PATH})
    endif()
    add_library(tb_net_proto STATIC ${TB_NET_PROTO_SRCS} ${IDF_PATH}/components/json/cJSON/cJSON.c)
    target_include_directories(tb_net_proto PUBLIC include ${IDF_PATH}/components/json/cJSON)
    target_link_libraries(tb_net_proto PUBLIC tb_core tb_calendar)
    target_compile_options(tb_net_proto PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
    # A bar on Linux for testing the Remote and setup pages in a browser (not a test; see host/fakebar.c).
    add_executable(tb_fakebar host/fakebar.c)
    target_link_libraries(tb_fakebar PRIVATE tb_net_proto tb_calendar tb_core)
    target_compile_definitions(tb_fakebar PRIVATE TB_WEB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/web")
    target_compile_options(tb_fakebar PRIVATE -Wall -Wextra)
endif()
