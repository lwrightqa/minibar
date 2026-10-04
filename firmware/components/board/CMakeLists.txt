# board: board support for the Waveshare ESP32-S3-Touch-LCD-3.49 V2. Owner: board builder.
# On the device: an ESP-IDF component (src/ drivers + logic/). On Linux: logic/ alone (tb_board_logic), the pure
# parts (backlight curve, debouncer, flip detection, sound synthesis and player, RTC registers, touch decode) for the
# host tests in test/host/board/. Rule: no ESP-IDF, FreeRTOS or LVGL headers under logic/.
set(TB_BOARD_LOGIC_SRCS
    logic/brd_inputs.c
    logic/brd_sound.c
    logic/brd_rtc.c
)

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${TB_BOARD_LOGIC_SRCS}
             "src/board_exio.c"
             "src/board_power.c"
             "src/board_display.c"
             "src/board_touch.c"
             "src/board_buttons.c"
             "src/board_imu.c"
             "src/board_rtc.c"
             "src/board_audio.c"
             "src/board_bringup.c"
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS "src" "logic/include"
        REQUIRES esp_driver_i2c esp_lcd lvgl__lvgl
        PRIV_REQUIRES bus core esp_driver_gpio esp_driver_ledc esp_driver_spi esp_driver_i2s esp_timer esp_psram
                      esp_hw_support esp_system
                      espressif__esp_lcd_axs15231b espressif__esp_codec_dev)
else()
    add_library(tb_board_logic STATIC ${TB_BOARD_LOGIC_SRCS})
    target_include_directories(tb_board_logic PUBLIC logic/include)
    target_compile_options(tb_board_logic PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
    target_link_libraries(tb_board_logic PUBLIC m)
endif()
