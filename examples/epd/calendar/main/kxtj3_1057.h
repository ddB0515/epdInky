#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// KXTJ3-1057 default I2C addresses depend on SA0 strap.
// Common values: 0x0E (SA0=0) or 0x0F (SA0=1)
#define KXTJ3_I2C_ADDR         0x0F

// Registers (KXTJ3 family)
#define KXTJ3_REG_XOUT_L       0x06
#define KXTJ3_REG_WHO_AM_I     0x0F
#define KXTJ3_REG_CTRL_REG1    0x1B
#define KXTJ3_REG_CTRL_REG2    0x1D
#define KXTJ3_REG_DATA_CTRL    0x21

// WHO_AM_I expected value for KXTJ3-1057 (commonly 0x35)
#define KXTJ3_WHO_AM_I_VALUE   0x35

// CTRL_REG1 bits
#define KXTJ3_CTRL1_PC1        0x80
#define KXTJ3_CTRL1_RES        0x40
#define KXTJ3_CTRL1_DRDYE      0x20
#define KXTJ3_CTRL1_GSEL_2G    0x00
#define KXTJ3_CTRL1_GSEL_4G    0x08
#define KXTJ3_CTRL1_GSEL_8G    0x10

typedef struct kxtj3_dev_t *kxtj3_handle_t;

typedef enum {
    KXTJ3_RANGE_2G = 2,
    KXTJ3_RANGE_4G = 4,
    KXTJ3_RANGE_8G = 8,
} kxtj3_range_t;

typedef struct {
    uint8_t _reserved;
} kxtj3_config_t; // kept for compatibility if referenced elsewhere

/**
 * @brief Initialize KXTJ3-1057 (adds device to I2C bus).
 *
 * After init, call kxtj3_configure() to start measurements.
 */
esp_err_t kxtj3_init(i2c_master_bus_handle_t bus_handle, uint8_t address, kxtj3_handle_t *ret_handle);

/**
 * @brief Configure sensor and start measurements.
 */
esp_err_t kxtj3_configure(kxtj3_handle_t handle, kxtj3_range_t range, uint16_t odr_hz, bool high_res, bool data_ready_int);

/**
 * @brief Remove device from I2C bus and free handle.
 */
esp_err_t kxtj3_delete(kxtj3_handle_t handle, i2c_master_bus_handle_t bus_handle);

/**
 * @brief Read raw acceleration samples.
 *
 * Outputs are in device raw counts (already right-shifted for 12/14-bit mode).
 */
esp_err_t kxtj3_read_raw(kxtj3_handle_t handle, int16_t *x, int16_t *y, int16_t *z);

/**
 * @brief Convert raw counts to milli-g based on configured range/resolution.
 */
esp_err_t kxtj3_raw_to_mg(kxtj3_handle_t handle, int16_t x, int16_t y, int16_t z, float *x_mg, float *y_mg, float *z_mg);
