/* board_internal.h: shared between board's source files. Owner: board builder. */
#pragma once

#include "driver/i2c_master.h"
#include "esp_io_expander.h"

/* The system I2C bus and the TCA9554, created by board_power_hold(). */
i2c_master_bus_handle_t board_sys_bus(void);
esp_io_expander_handle_t board_exio(void);
