#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

// TCA6408 Registers
#define TCA6408_INPUT_REG       0x00
#define TCA6408_OUTPUT_REG      0x01
#define TCA6408_POLARITY_REG    0x02
#define TCA6408_CONFIG_REG      0x03

typedef struct tca6408_dev_t *tca6408_handle_t;

/**
 * @brief Initialize TCA6408 device
 * 
 * @param bus_handle I2C master bus handle
 * @param address I2C address of the device (e.g. 0x20 or 0x21)
 * @param ret_handle Pointer to return the device handle
 * @return esp_err_t 
 */
esp_err_t tca6408_init(i2c_master_bus_handle_t bus_handle, uint8_t address, tca6408_handle_t *ret_handle);

/**
 * @brief Set output value for all pins
 * 
 * @param handle Device handle
 * @param val Value to set (bitmask)
 * @return esp_err_t 
 */
esp_err_t tca6408_set_output_val(tca6408_handle_t handle, uint8_t val);

/**
 * @brief Set specific output pin level
 * 
 * @param handle Device handle
 * @param pin Pin number (0-7)
 * @param level Level to set (0 or 1)
 * @return esp_err_t 
 */
esp_err_t tca6408_set_output_pin(tca6408_handle_t handle, int pin, int level);

/**
 * @brief Get input value from all pins
 * 
 * @param handle Device handle
 * @param val Pointer to store the value
 * @return esp_err_t 
 */
esp_err_t tca6408_get_input_val(tca6408_handle_t handle, uint8_t *val);

/**
 * @brief Set configuration (direction) for all pins
 * 
 * @param handle Device handle
 * @param val Configuration value (1 = Input, 0 = Output)
 * @return esp_err_t 
 */
esp_err_t tca6408_set_config(tca6408_handle_t handle, uint8_t val);

/**
 * @brief Set polarity inversion for all pins
 * 
 * @param handle Device handle
 * @param val Polarity value (1 = Inverted, 0 = Normal)
 * @return esp_err_t 
 */
esp_err_t tca6408_set_polarity(tca6408_handle_t handle, uint8_t val);
