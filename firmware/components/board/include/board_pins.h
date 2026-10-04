/*
 * board_pins.h: the V2 board's pin map, from Waveshare's ESP-IDF examples (11_FactoryProgram/main/user_config.h,
 * 08_Audio_Test codec_board/board_cfg.txt "S3_LCD_3_49") and docs/decisions.md "Hardware notes for the firmware (V2)".
 * Owner: board builder.
 */
#pragma once

/* System I2C bus (I2C_NUM_0): TCA9554, PCF85063 RTC, QMI8658 IMU, ES8311 codec. */
#define BOARD_I2C_SYS_PORT      0
#define BOARD_I2C_SYS_SDA       47
#define BOARD_I2C_SYS_SCL       48
/* Touch I2C bus (I2C_NUM_1): AXS15231B touch at 0x3B. */
#define BOARD_I2C_TOUCH_PORT    1
#define BOARD_I2C_TOUCH_SDA     17
#define BOARD_I2C_TOUCH_SCL     18

#define BOARD_ADDR_TOUCH        0x3B
#define BOARD_ADDR_RTC          0x51    /* PCF85063 */
#define BOARD_ADDR_IMU          0x6B    /* QMI8658 */
/* TCA9554 at ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000 (0x20). ES8311 at its default 0x18. */

/* LCD: AXS15231B over QSPI on SPI3, 172 x 640 native (portrait), used as 640 x 172. */
#define BOARD_LCD_HOST          3       /* SPI3_HOST */
#define BOARD_LCD_CS            9
#define BOARD_LCD_PCLK          10
#define BOARD_LCD_D0            11
#define BOARD_LCD_D1            12
#define BOARD_LCD_D2            13
#define BOARD_LCD_D3            14
#define BOARD_LCD_TE            21
#define BOARD_LCD_BL            42      /* PWM; inverted: duty 0 = full brightness (LCD_PWM_MODE_255 = 0xff - 255) */
#define BOARD_LCD_H_RES         172     /* native */
#define BOARD_LCD_V_RES         640
#define BOARD_SCREEN_W          640     /* logical, landscape */
#define BOARD_SCREEN_H          172

/* TCA9554 I/O expander pins (bit masks for esp_io_expander). */
#define BOARD_EXIO_TOUCH_INT    (1u << 0)
#define BOARD_EXIO_BL_EN        (1u << 1)
#define BOARD_EXIO_IMU_INT1     (1u << 2)
#define BOARD_EXIO_IMU_INT2     (1u << 3)
#define BOARD_EXIO_RTC_INT      (1u << 4)
#define BOARD_EXIO_LCD_RST      (1u << 5)
#define BOARD_EXIO_SYS_EN       (1u << 6)   /* power hold on battery: set high FIRST at boot */
#define BOARD_EXIO_NS_MODE      (1u << 7)   /* amplifier mode */
#define BOARD_EXIO_INT_GPIO     8           /* the expander's interrupt line */

/* Buttons and power. */
#define BOARD_BTN_BOOT          0           /* active low */
#define BOARD_BTN_PWR           16          /* SYS_OUT: the PWR button, active low; also the deep-sleep wake pin */
#define BOARD_BAT_ADC           4

/* Audio: ES8311 over I2S (from codec_board's S3_LCD_3_49 entry). */
#define BOARD_I2S_MCLK          7
#define BOARD_I2S_BCLK          15
#define BOARD_I2S_WS            46
#define BOARD_I2S_DOUT          45          /* to the ES8311 */
#define BOARD_I2S_DIN           6           /* from the microphone codec; unused by TinyBar */
