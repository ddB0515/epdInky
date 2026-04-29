#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdbool.h>

// STC3115 I2C Address (7-bit)
#define STC3115_I2C_ADDRESS     0x70

// STC3115 Register Addresses
#define STC3115_REG_MODE        0x00    // Mode register
#define STC3115_REG_CTRL        0x01    // Control and status register
#define STC3115_REG_SOC_L       0x02    // SOC value (LSB)
#define STC3115_REG_SOC_H       0x03    // SOC value (MSB)
#define STC3115_REG_COUNTER_L   0x04    // Conversion counter (LSB)
#define STC3115_REG_COUNTER_H   0x05    // Conversion counter (MSB)
#define STC3115_REG_CURRENT_L   0x06    // Battery current (LSB)
#define STC3115_REG_CURRENT_H   0x07    // Battery current (MSB)
#define STC3115_REG_VOLTAGE_L   0x08    // Battery voltage (LSB)
#define STC3115_REG_VOLTAGE_H   0x09    // Battery voltage (MSB)
#define STC3115_REG_TEMPERATURE 0x0A    // Temperature
#define STC3115_REG_AVG_CURRENT_L 0x0B  // Average current (LSB) - CC mode only
#define STC3115_REG_AVG_CURRENT_H 0x0C  // Average current (MSB) - CC mode only
#define STC3115_REG_OCV_L       0x0D    // OCV register (LSB)
#define STC3115_REG_OCV_H       0x0E    // OCV register (MSB)
#define STC3115_REG_CC_CNF_L    0x0F    // CC configuration (LSB)
#define STC3115_REG_CC_CNF_H    0x10    // CC configuration (MSB)
#define STC3115_REG_VM_CNF_L    0x11    // VM configuration (LSB)
#define STC3115_REG_VM_CNF_H    0x12    // VM configuration (MSB)
#define STC3115_REG_ALARM_SOC   0x13    // SOC alarm level
#define STC3115_REG_ALARM_VOLTAGE 0x14  // Voltage alarm level
#define STC3115_REG_CURRENT_THRES 0x15  // Current threshold for relaxation
#define STC3115_REG_RELAX_COUNT 0x16    // Relaxation counter
#define STC3115_REG_RELAX_MAX   0x17    // Maximum relaxation counter value
#define STC3115_REG_ID          0x18    // Device ID register
#define STC3115_REG_CC_ADJ_H    0x1B    // CC adjustment high
#define STC3115_REG_CC_ADJ_L    0x1C    // CC adjustment low  
#define STC3115_REG_VM_ADJ_H    0x1D    // VM adjustment high
#define STC3115_REG_VM_ADJ_L    0x1E    // VM adjustment low
#define STC3115_REG_RAM0        0x20    // RAM area start (16 bytes: 0x20-0x2F)
#define STC3115_REG_OCV_TAB0    0x30    // OCV table start (16 entries: 0x30-0x3F)

// RAM size
#define STC3115_RAM_SIZE        16

// MODE register bits (REG_MODE = 0x00)
#define STC3115_MODE_VMODE      (1 << 0)  // Voltage mode (0 = mixed mode, 1 = voltage mode)
#define STC3115_MODE_CLR_VM_ADJ (1 << 1)  // Clear VM adjustment
#define STC3115_MODE_CLR_CC_ADJ (1 << 2)  // Clear CC adjustment
#define STC3115_MODE_ALM_ENA    (1 << 3)  // Alarm enabled
#define STC3115_MODE_GG_RUN     (1 << 4)  // Gas gauge running
#define STC3115_MODE_FORCE_CC   (1 << 5)  // Force CC mode
#define STC3115_MODE_FORCE_VM   (1 << 6)  // Force VM mode

// CTRL register bits (REG_CTRL = 0x01)
#define STC3115_CTRL_IO0DATA    (1 << 0)  // IO0 pin data
#define STC3115_CTRL_GG_RST     (1 << 1)  // Gas gauge reset
#define STC3115_CTRL_GG_VM      (1 << 2)  // Gas gauge voltage mode active
#define STC3115_CTRL_BATFAIL    (1 << 3)  // Battery removal detected (UVLO < 2.6V)
#define STC3115_CTRL_PORDET     (1 << 4)  // Power-on-reset detected (POR < 2V)
#define STC3115_CTRL_ALM_SOC    (1 << 5)  // SOC alarm
#define STC3115_CTRL_ALM_VOLT   (1 << 6)  // Voltage alarm

// Device ID
#define STC3115_ID              0x14

// Conversion constants
// Note: Datasheet says 2.44 mV/LSB, calibrated based on actual measurements
#define STC3115_VOLTAGE_FACTOR  2.2f    // mV per LSB (calibrated)
#define STC3115_CURRENT_FACTOR  5.88f    // uA per LSB (with 10 mOhm sense resistor)
#define STC3115_TEMP_OFFSET     30       // Temperature offset in Celsius

// RAM magic values for validity check
#define STC3115_RAM_TEST_WORD   0x53  // 'S' - Test word to verify RAM validity

// Default configuration values
#define STC3115_DEFAULT_CURRENT_THRES   10   // Default current threshold
#define STC3115_DEFAULT_RELAX_MAX       24   // Default relaxation max

// Battery voltage thresholds for software SOC calculation (in mV)
#define STC3115_BATT_VOLTAGE_FULL   4200   // 100% SOC
#define STC3115_BATT_VOLTAGE_EMPTY  3000   // 0% SOC

/**
 * @brief OCV table for Li-ion battery (3.0V - 4.2V)
 * Formula: OCV(mV) = value × 5.5 + 2500
 * Max representable voltage: 255 × 5.5 + 2500 = 3902 mV
 * 
 * IMPORTANT: This table can only represent up to 3.9V due to hardware limitation.
 * For voltages above 3.9V, the software SOC calculation takes over.
 * 
 * This table reflects the typical Li-ion discharge curve:
 * - Steep drop at start (4.2V->4.0V)
 * - Flat plateau (4.0V->3.7V) 
 * - Steep drop at end (3.7V->3.0V)
 * 
 * value = (OCV_mV - 2500) / 5.5
 */
#define STC3115_DEFAULT_OCV_TABLE { \
    0x5B, /* 0%     - 3000 mV (empty)         */ \
    0x73, /* 6.25%  - 3130 mV                 */ \
    0x87, /* 12.5%  - 3240 mV                 */ \
    0x96, /* 18.75% - 3330 mV                 */ \
    0xA5, /* 25%    - 3410 mV                 */ \
    0xB4, /* 31.25% - 3500 mV                 */ \
    0xC0, /* 37.5%  - 3570 mV                 */ \
    0xC9, /* 43.75% - 3620 mV                 */ \
    0xD1, /* 50%    - 3665 mV (plateau)       */ \
    0xD9, /* 56.25% - 3710 mV                 */ \
    0xE1, /* 62.5%  - 3755 mV                 */ \
    0xE9, /* 68.75% - 3800 mV                 */ \
    0xF1, /* 75%    - 3845 mV                 */ \
    0xF9, /* 81.25% - 3890 mV                 */ \
    0xFF, /* 87.5%  - 3902 mV (table max)     */ \
    0xFF  /* 93.75% - 3902 mV (capped, actual ~4.0V+) */ \
}

/**
 * @brief STC3115 battery configuration
 */
typedef struct {
    uint16_t battery_capacity_mah;  // Battery capacity in mAh
    uint8_t  sense_resistor_mohm;   // Sense resistor value in mOhm (typically 10 or 30)
    uint8_t  alarm_soc;             // Low SOC alarm threshold (0-100%)
    uint16_t alarm_voltage_mv;      // Low voltage alarm threshold in mV
    uint8_t  current_thres;         // Current threshold for relaxation (default: 10)
    uint8_t  relax_max;             // Maximum relaxation counter (default: 24)
    bool     voltage_mode;          // true = voltage mode, false = mixed mode
    const uint8_t *ocv_table;       // Pointer to 16-entry OCV table (NULL for default)
} stc3115_config_t;

/**
 * @brief Charging status enum
 */
typedef enum {
    STC3115_NOT_CHARGING = 0,  // Battery discharging or idle
    STC3115_CHARGING,          // Battery is charging (current > 0 or voltage rising)
    STC3115_FULLY_CHARGED,     // Battery at full charge voltage
    STC3115_UNKNOWN            // Cannot determine charging status
} stc3115_charge_status_t;

/**
 * @brief STC3115 battery data
 */
typedef struct {
    uint16_t voltage_mv;     // Battery voltage in mV
    int32_t  current_ua;     // Battery current in uA (positive = charging)
    int32_t  avg_current_ua; // Average current in uA
    uint16_t soc_permille;   // State of charge in 0.1% (0-1000)
    int8_t   temperature_c;  // Temperature in Celsius
    uint16_t ocv_mv;         // Open circuit voltage in mV
    uint16_t counter;        // Conversion counter
    bool     alarm_soc;      // SOC alarm flag
    bool     alarm_voltage;  // Voltage alarm flag
    bool     battery_fail;   // Battery failure/removal detected
    bool     por_detect;     // Power-on-reset detected
    stc3115_charge_status_t charge_status;  // Charging status
    int16_t  voltage_trend;  // Voltage change in mV (positive = rising)
} stc3115_data_t;

/**
 * @brief STC3115 initialization status
 */
typedef enum {
    STC3115_INIT_NEW,        // Fresh initialization performed
    STC3115_INIT_RESTORED,   // State restored from RAM
    STC3115_INIT_FAILED      // Initialization failed
} stc3115_init_status_t;

typedef struct stc3115_dev_t *stc3115_handle_t;

/**
 * @brief Initialize STC3115 device following official initialization sequence
 * 
 * This function follows the official STC3115 initialization steps:
 * 1. Read chip ID
 * 2. Check RAM memory status (test and CRC words)
 * 3. If RAM invalid or BATFAIL/PORDET set: full initialization
 * 4. If RAM valid and no failures: restore from RAM
 * 
 * @param bus_handle I2C master bus handle
 * @param config Pointer to battery configuration
 * @param ret_handle Pointer to return the device handle
 * @param init_status Pointer to return initialization status (can be NULL)
 * @return esp_err_t 
 */
esp_err_t stc3115_init(i2c_master_bus_handle_t bus_handle, const stc3115_config_t *config, 
                       stc3115_handle_t *ret_handle, stc3115_init_status_t *init_status);

/**
 * @brief Delete STC3115 device and free resources
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t stc3115_delete(stc3115_handle_t handle);

/**
 * @brief Start gas gauge operation
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t stc3115_start(stc3115_handle_t handle);

/**
 * @brief Stop gas gauge operation and save state to RAM
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t stc3115_stop(stc3115_handle_t handle);

/**
 * @brief Reset the gas gauge
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t stc3115_reset(stc3115_handle_t handle);

/**
 * @brief Read battery data
 * 
 * @param handle Device handle
 * @param data Pointer to store battery data
 * @return esp_err_t 
 */
esp_err_t stc3115_read_data(stc3115_handle_t handle, stc3115_data_t *data);

/**
 * @brief Read battery voltage
 * 
 * @param handle Device handle
 * @param voltage_mv Pointer to store voltage in mV
 * @return esp_err_t 
 */
esp_err_t stc3115_read_voltage(stc3115_handle_t handle, uint16_t *voltage_mv);

/**
 * @brief Read battery current
 * 
 * @param handle Device handle
 * @param current_ua Pointer to store current in uA
 * @return esp_err_t 
 */
esp_err_t stc3115_read_current(stc3115_handle_t handle, int16_t *current_ua);

/**
 * @brief Read state of charge
 * 
 * @param handle Device handle
 * @param soc_permille Pointer to store SOC in 0.1% units (0-1000)
 * @return esp_err_t 
 */
esp_err_t stc3115_read_soc(stc3115_handle_t handle, uint16_t *soc_permille);

/**
 * @brief Read temperature
 * 
 * @param handle Device handle
 * @param temp_c Pointer to store temperature in Celsius
 * @return esp_err_t 
 */
esp_err_t stc3115_read_temperature(stc3115_handle_t handle, int8_t *temp_c);

/**
 * @brief Set SOC alarm threshold
 * 
 * @param handle Device handle
 * @param soc_percent SOC threshold in percent (0-100)
 * @return esp_err_t 
 */
esp_err_t stc3115_set_alarm_soc(stc3115_handle_t handle, uint8_t soc_percent);

/**
 * @brief Set voltage alarm threshold
 * 
 * @param handle Device handle
 * @param voltage_mv Voltage threshold in mV
 * @return esp_err_t 
 */
esp_err_t stc3115_set_alarm_voltage(stc3115_handle_t handle, uint16_t voltage_mv);

/**
 * @brief Clear alarm flags
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t stc3115_clear_alarms(stc3115_handle_t handle);

/**
 * @brief Read device ID
 * 
 * @param handle Device handle
 * @param id Pointer to store device ID
 * @return esp_err_t 
 */
esp_err_t stc3115_read_id(stc3115_handle_t handle, uint8_t *id);

/**
 * @brief Check if device is present and responding
 * 
 * @param handle Device handle
 * @return esp_err_t ESP_OK if device responds correctly
 */
esp_err_t stc3115_probe(stc3115_handle_t handle);

/**
 * @brief Save current state to RAM for later restoration
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t stc3115_save_state(stc3115_handle_t handle);

