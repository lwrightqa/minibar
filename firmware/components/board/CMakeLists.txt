# board: board support for the Waveshare ESP32-S3-Touch-LCD-3.49 V2. Owner: board builder.
idf_component_register(
    SRCS "src/board_power.c"
         "src/board_display.c"
         "src/board_inputs.c"
         "src/board_audio.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_driver_i2c esp_lcd lvgl__lvgl
    PRIV_REQUIRES bus core esp_driver_gpio esp_driver_ledc esp_driver_spi esp_driver_i2s esp_timer esp_psram
                  esp_hw_support esp_system
                  espressif__esp_lcd_axs15231b espressif__esp_io_expander_tca9554 espressif__esp_codec_dev)
