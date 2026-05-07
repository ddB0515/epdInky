#ifndef TPS65185_H
#define TPS65185_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * TPS65185 - PMIC for E-Ink/E-Paper Displays
 * 
 * Provides: VPOS (+15V), VNEG (-15V), VEE (-20V), VDDH (+22V), VCOM (programmable)
 ******************************************************************************/

// I2C Configuration
#define TPS65185_I2C_ADDR           0x68    // 7-bit I2C address
#define TPS65185_I2C_CLK_SPEED      400000  // 400kHz

/*******************************************************************************
 * TPS65185 Register Map
 ******************************************************************************/

// Identification
#define TPS65185_REG_TMST_VALUE     0x00    // Thermistor value (read-only)
#define TPS65185_REG_ENABLE         0x01    // Enable register
#define TPS65185_REG_VADJ           0x02    // VCOM Adjust register
#define TPS65185_REG_VCOM1          0x03    // VCOM1 setting (bits 7:0)
#define TPS65185_REG_VCOM2          0x04    // VCOM2 setting (bit 0 = bit 8 of VCOM)
#define TPS65185_REG_INT_EN1        0x05    // Interrupt enable 1
#define TPS65185_REG_INT_EN2        0x06    // Interrupt enable 2
#define TPS65185_REG_INT1           0x07    // Interrupt status 1 (read to clear)
#define TPS65185_REG_INT2           0x08    // Interrupt status 2 (read to clear)
#define TPS65185_REG_UPSEQ0         0x09    // Power-up sequence timing 0
#define TPS65185_REG_UPSEQ1         0x0A    // Power-up sequence timing 1
#define TPS65185_REG_DWNSEQ0        0x0B    // Power-down sequence timing 0
#define TPS65185_REG_DWNSEQ1        0x0C    // Power-down sequence timing 1
#define TPS65185_REG_TMST1          0x0D    // Thermistor configuration 1
#define TPS65185_REG_TMST2          0x0E    // Thermistor configuration 2
#define TPS65185_REG_PG             0x0F    // Power good status
#define TPS65185_REG_REVID          0x10    // Revision ID

/*******************************************************************************
 * Register Bit Definitions
 ******************************************************************************/

// ENABLE Register (0x01)
#define TPS65185_ENABLE_ACTIVE      (1 << 7)    // Active (read-only status)
#define TPS65185_ENABLE_STANDBY     (1 << 6)    // Enter standby mode
#define TPS65185_ENABLE_V3P3_EN     (1 << 5)    // Enable 3.3V LDO (TPS65186 only)
#define TPS65185_ENABLE_VCOM_EN     (1 << 4)    // Enable VCOM buffer
#define TPS65185_ENABLE_VDDH_EN     (1 << 3)    // Enable VDDH charge pump
#define TPS65185_ENABLE_VPOS_EN     (1 << 2)    // Enable VPOS boost
#define TPS65185_ENABLE_VEE_EN      (1 << 1)    // Enable VEE charge pump
#define TPS65185_ENABLE_VNEG_EN     (1 << 0)    // Enable VNEG LDO

// VADJ Register (0x02) - VCOM Voltage Adjust
#define TPS65185_VADJ_VSET_MASK     0x03        // VPOS/VNEG voltage set
#define TPS65185_VADJ_VSET_15V      0x00        // VPOS=+15V, VNEG=-15V (default)
#define TPS65185_VADJ_VSET_14V      0x01        // VPOS=+14V, VNEG=-14V
#define TPS65185_VADJ_VSET_13V      0x02        // VPOS=+13V, VNEG=-13V
#define TPS65185_VADJ_VSET_12V      0x03        // VPOS=+12V, VNEG=-12V

// VCOM2 Register (0x04)
#define TPS65185_VCOM2_ACQ          (1 << 7)    // Start VCOM acquisition
#define TPS65185_VCOM2_PROG         (1 << 6)    // Program VCOM to NVM
#define TPS65185_VCOM2_HIZ          (1 << 5)    // VCOM Hi-Z mode
#define TPS65185_VCOM2_AVG_MASK     (0x03 << 3) // VCOM ADC averaging
#define TPS65185_VCOM2_VCOM8        (1 << 0)    // VCOM bit 8

// INT_EN1 Register (0x05)
#define TPS65185_INT_EN1_DTX        (1 << 7)    // Thermistor hot temp interrupt
#define TPS65185_INT_EN1_TSD        (1 << 6)    // Thermal shutdown interrupt
#define TPS65185_INT_EN1_HOT        (1 << 5)    // Thermistor hot interrupt
#define TPS65185_INT_EN1_TMST_COLD  (1 << 4)    // Thermistor cold interrupt
#define TPS65185_INT_EN1_UVLO       (1 << 3)    // Under-voltage lockout interrupt
#define TPS65185_INT_EN1_ACQC       (1 << 2)    // VCOM acquisition complete interrupt
#define TPS65185_INT_EN1_PRGC       (1 << 1)    // VCOM programming complete interrupt
#define TPS65185_INT_EN1_EOC        (1 << 0)    // Thermistor end of conversion interrupt

// INT_EN2 Register (0x06)
#define TPS65185_INT_EN2_VB_UV      (1 << 7)    // VB under-voltage interrupt
#define TPS65185_INT_EN2_VDDH_UV    (1 << 6)    // VDDH under-voltage interrupt
#define TPS65185_INT_EN2_VN_UV      (1 << 5)    // VN under-voltage interrupt
#define TPS65185_INT_EN2_VPOS_UV    (1 << 4)    // VPOS under-voltage interrupt
#define TPS65185_INT_EN2_VEE_UV     (1 << 3)    // VEE under-voltage interrupt
#define TPS65185_INT_EN2_VMINUS_UV  (1 << 2)    // VMINUS under-voltage interrupt (TPS65186)
#define TPS65185_INT_EN2_VNEG_UV    (1 << 1)    // VNEG under-voltage interrupt
#define TPS65185_INT_EN2_EOC        (1 << 0)    // End of conversion interrupt

// INT1 Register (0x07) - Same bit definitions as INT_EN1
#define TPS65185_INT1_DTX           (1 << 7)
#define TPS65185_INT1_TSD           (1 << 6)
#define TPS65185_INT1_HOT           (1 << 5)
#define TPS65185_INT1_TMST_COLD     (1 << 4)
#define TPS65185_INT1_UVLO          (1 << 3)
#define TPS65185_INT1_ACQC          (1 << 2)
#define TPS65185_INT1_PRGC          (1 << 1)
#define TPS65185_INT1_EOC           (1 << 0)

// INT2 Register (0x08) - Same bit definitions as INT_EN2
#define TPS65185_INT2_VB_UV         (1 << 7)
#define TPS65185_INT2_VDDH_UV       (1 << 6)
#define TPS65185_INT2_VN_UV         (1 << 5)
#define TPS65185_INT2_VPOS_UV       (1 << 4)
#define TPS65185_INT2_VEE_UV        (1 << 3)
#define TPS65185_INT2_VMINUS_UV     (1 << 2)
#define TPS65185_INT2_VNEG_UV       (1 << 1)
#define TPS65185_INT2_EOC           (1 << 0)

// UPSEQ0 Register (0x09) - Power-up sequence timing
#define TPS65185_UPSEQ0_VEE_STROBE_MASK     (0x03 << 6)
#define TPS65185_UPSEQ0_VEE_STROBE_SHIFT    6
#define TPS65185_UPSEQ0_VNEG_STROBE_MASK    (0x03 << 4)
#define TPS65185_UPSEQ0_VNEG_STROBE_SHIFT   4
#define TPS65185_UPSEQ0_VPOS_STROBE_MASK    (0x03 << 2)
#define TPS65185_UPSEQ0_VPOS_STROBE_SHIFT   2
#define TPS65185_UPSEQ0_VDDH_STROBE_MASK    (0x03 << 0)
#define TPS65185_UPSEQ0_VDDH_STROBE_SHIFT   0

// UPSEQ1 Register (0x0A)
#define TPS65185_UPSEQ1_DFCTR_MASK          (0x03 << 2)
#define TPS65185_UPSEQ1_DFCTR_SHIFT         2
#define TPS65185_UPSEQ1_DLY_MASK            (0x03 << 0)
#define TPS65185_UPSEQ1_DLY_SHIFT           0

// DWNSEQ0 Register (0x0B) - Power-down sequence timing
#define TPS65185_DWNSEQ0_VDDH_STROBE_MASK   (0x03 << 6)
#define TPS65185_DWNSEQ0_VDDH_STROBE_SHIFT  6
#define TPS65185_DWNSEQ0_VPOS_STROBE_MASK   (0x03 << 4)
#define TPS65185_DWNSEQ0_VPOS_STROBE_SHIFT  4
#define TPS65185_DWNSEQ0_VEE_STROBE_MASK    (0x03 << 2)
#define TPS65185_DWNSEQ0_VEE_STROBE_SHIFT   2
#define TPS65185_DWNSEQ0_VNEG_STROBE_MASK   (0x03 << 0)
#define TPS65185_DWNSEQ0_VNEG_STROBE_SHIFT  0

// DWNSEQ1 Register (0x0C)
#define TPS65185_DWNSEQ1_DFCTR_MASK         (0x03 << 2)
#define TPS65185_DWNSEQ1_DFCTR_SHIFT        2
#define TPS65185_DWNSEQ1_DLY_MASK           (0x03 << 0)
#define TPS65185_DWNSEQ1_DLY_SHIFT          0

// TMST1 Register (0x0D) - Thermistor configuration
#define TPS65185_TMST1_READ_THERM   (1 << 7)    // Start thermistor conversion
#define TPS65185_TMST1_DT_MASK      0x7F        // Temperature threshold for DTX

// TMST2 Register (0x0E)
#define TPS65185_TMST2_CONV_END     (1 << 7)    // Conversion complete flag
#define TPS65185_TMST2_TMST_MASK    0x7F        // Thermistor reading

// PG Register (0x0F) - Power Good Status
#define TPS65185_PG_VB_PG           (1 << 7)    // VB power good
#define TPS65185_PG_VDDH_PG         (1 << 6)    // VDDH power good
#define TPS65185_PG_VN_PG           (1 << 5)    // VN power good
#define TPS65185_PG_VPOS_PG         (1 << 4)    // VPOS power good
#define TPS65185_PG_VEE_PG          (1 << 3)    // VEE power good
#define TPS65185_PG_VMINUS_PG       (1 << 2)    // VMINUS power good (TPS65186)
#define TPS65185_PG_VNEG_PG         (1 << 1)    // VNEG power good
#define TPS65185_PG_ALL             (1 << 0)    // All power good

/*******************************************************************************
 * VCOM Voltage Definitions
 ******************************************************************************/

// VCOM range: 0V to -5.11V in 10mV steps (9-bit value)
// VCOM (mV) = -10 * VCOM_REG (where VCOM_REG is 0 to 511)
#define TPS65185_VCOM_MIN_MV        0       // 0mV
#define TPS65185_VCOM_MAX_MV        5110    // -5110mV (stored as positive)
#define TPS65185_VCOM_STEP_MV       10      // 10mV steps

/*******************************************************************************
 * Timing Definitions
 ******************************************************************************/

// Strobe timing values (for UPSEQ and DWNSEQ registers)
typedef enum {
    TPS65185_STROBE_3MS = 0,    // 3ms delay
    TPS65185_STROBE_6MS = 1,    // 6ms delay
    TPS65185_STROBE_9MS = 2,    // 9ms delay
    TPS65185_STROBE_12MS = 3    // 12ms delay
} tps65185_strobe_t;

// Delay factor values
typedef enum {
    TPS65185_DELAY_3MS = 0,     // 3ms delay
    TPS65185_DELAY_6MS = 1,     // 6ms delay
    TPS65185_DELAY_9MS = 2,     // 9ms delay
    TPS65185_DELAY_12MS = 3     // 12ms delay
} tps65185_delay_t;

/*******************************************************************************
 * VPOS/VNEG Voltage Settings
 ******************************************************************************/

typedef enum {
    TPS65185_VSET_15V = 0,      // VPOS = +15V, VNEG = -15V
    TPS65185_VSET_14V = 1,      // VPOS = +14V, VNEG = -14V
    TPS65185_VSET_13V = 2,      // VPOS = +13V, VNEG = -13V
    TPS65185_VSET_12V = 3       // VPOS = +12V, VNEG = -12V
} tps65185_vset_t;

/*******************************************************************************
 * GPIO Pin Configuration
 ******************************************************************************/

typedef struct {
    gpio_num_t wakeup_pin;      // WAKEUP pin (input to TPS65185, output from MCU)
    gpio_num_t pwrup_pin;       // PWRUP pin (optional, can be -1)
    gpio_num_t vcom_ctrl_pin;   // VCOM_CTRL pin (optional, can be -1)
    gpio_num_t int_pin;         // INT pin (optional, can be -1)
    gpio_num_t pwr_good_pin;    // PWRGOOD pin (optional, can be -1)
} tps65185_gpio_config_t;

/*******************************************************************************
 * Handle Type
 ******************************************************************************/

typedef struct tps65185_dev *tps65185_handle_t;

/*******************************************************************************
 * Function Declarations
 ******************************************************************************/

/**
 * @brief Initialize TPS65185 and add to I2C bus
 * 
 * @param bus_handle I2C bus handle
 * @param gpio_cfg GPIO pin configuration (can have -1 for unused pins)
 * @param handle Pointer to store the device handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_init(i2c_master_bus_handle_t bus_handle, 
                        const tps65185_gpio_config_t *gpio_cfg,
                        tps65185_handle_t *handle);

/**
 * @brief Deinitialize TPS65185 and release resources
 * 
 * @param handle Device handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_deinit(tps65185_handle_t handle);

/**
 * @brief Get device revision ID
 * 
 * @param handle Device handle
 * @param rev_id Pointer to store revision ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_get_revid(tps65185_handle_t handle, uint8_t *rev_id);

/*******************************************************************************
 * Power Control Functions
 ******************************************************************************/

/**
 * @brief Wake up the TPS65185 from sleep/standby
 * 
 * @param handle Device handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_wakeup(tps65185_handle_t handle);

/**
 * @brief Put TPS65185 into standby mode (low power)
 * 
 * @param handle Device handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_standby(tps65185_handle_t handle);

/**
 * @brief Power up all rails for E-Paper display
 * 
 * @param handle Device handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_power_up(tps65185_handle_t handle);

/**
 * @brief Power down all rails
 * 
 * @param handle Device handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_power_down(tps65185_handle_t handle);

/**
 * @brief Check if all power rails are good
 * 
 * @param handle Device handle
 * @param power_good Pointer to store power good status
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_is_power_good(tps65185_handle_t handle, bool *power_good);

/**
 * @brief Get detailed power good status for each rail
 * 
 * @param handle Device handle
 * @param pg_status Pointer to store PG register value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_get_power_good_status(tps65185_handle_t handle, uint8_t *pg_status);

/**
 * @brief Set VPOS/VNEG voltage level
 * 
 * @param handle Device handle
 * @param vset Voltage setting (12V to 15V)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_set_vpos_vneg(tps65185_handle_t handle, tps65185_vset_t vset);

/*******************************************************************************
 * VCOM Control Functions
 ******************************************************************************/

/**
 * @brief Set VCOM voltage (negative voltage for E-Paper)
 * 
 * @param handle Device handle
 * @param vcom_mv VCOM voltage in millivolts (positive value, e.g., 2500 for -2.5V)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_set_vcom(tps65185_handle_t handle, uint16_t vcom_mv);

/**
 * @brief Get current VCOM voltage setting
 * 
 * @param handle Device handle
 * @param vcom_mv Pointer to store VCOM voltage in millivolts
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_get_vcom(tps65185_handle_t handle, uint16_t *vcom_mv);

/**
 * @brief Enable VCOM output
 * 
 * @param handle Device handle
 * @param enable true to enable, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_vcom_enable(tps65185_handle_t handle, bool enable);

/**
 * @brief Set VCOM to Hi-Z mode (high impedance)
 * 
 * @param handle Device handle
 * @param hiz true for Hi-Z, false for normal operation
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_vcom_hiz(tps65185_handle_t handle, bool hiz);

/**
 * @brief Program VCOM value to non-volatile memory
 * 
 * @param handle Device handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_vcom_program_nvm(tps65185_handle_t handle);

/*******************************************************************************
 * Temperature Sensing Functions
 ******************************************************************************/

/**
 * @brief Read thermistor temperature
 * 
 * @param handle Device handle
 * @param temperature Pointer to store temperature in degrees Celsius
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_read_temperature(tps65185_handle_t handle, int8_t *temperature);

/**
 * @brief Read raw thermistor ADC value
 * 
 * @param handle Device handle
 * @param raw_value Pointer to store raw ADC value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_read_thermistor_raw(tps65185_handle_t handle, uint8_t *raw_value);

/*******************************************************************************
 * Power Sequencing Functions
 ******************************************************************************/

/**
 * @brief Configure power-up sequence timing
 * 
 * @param handle Device handle
 * @param vddh_strobe VDDH strobe delay
 * @param vpos_strobe VPOS strobe delay
 * @param vee_strobe VEE strobe delay
 * @param vneg_strobe VNEG strobe delay
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_set_powerup_sequence(tps65185_handle_t handle,
                                         tps65185_strobe_t vddh_strobe,
                                         tps65185_strobe_t vpos_strobe,
                                         tps65185_strobe_t vee_strobe,
                                         tps65185_strobe_t vneg_strobe);

/**
 * @brief Configure power-down sequence timing
 * 
 * @param handle Device handle
 * @param vddh_strobe VDDH strobe delay
 * @param vpos_strobe VPOS strobe delay
 * @param vee_strobe VEE strobe delay
 * @param vneg_strobe VNEG strobe delay
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_set_powerdown_sequence(tps65185_handle_t handle,
                                           tps65185_strobe_t vddh_strobe,
                                           tps65185_strobe_t vpos_strobe,
                                           tps65185_strobe_t vee_strobe,
                                           tps65185_strobe_t vneg_strobe);

/*******************************************************************************
 * Interrupt Functions
 ******************************************************************************/

/**
 * @brief Enable interrupts
 * 
 * @param handle Device handle
 * @param int_en1 INT_EN1 register value
 * @param int_en2 INT_EN2 register value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_enable_interrupts(tps65185_handle_t handle, uint8_t int_en1, uint8_t int_en2);

/**
 * @brief Read and clear interrupt status
 * 
 * @param handle Device handle
 * @param int1 Pointer to store INT1 register (can be NULL)
 * @param int2 Pointer to store INT2 register (can be NULL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_read_interrupts(tps65185_handle_t handle, uint8_t *int1, uint8_t *int2);

/*******************************************************************************
 * Low-Level Register Access
 ******************************************************************************/

/**
 * @brief Read a single register
 * 
 * @param handle Device handle
 * @param reg Register address
 * @param value Pointer to store register value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_read_register(tps65185_handle_t handle, uint8_t reg, uint8_t *value);

/**
 * @brief Write a single register
 * 
 * @param handle Device handle
 * @param reg Register address
 * @param value Value to write
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_write_register(tps65185_handle_t handle, uint8_t reg, uint8_t value);

/**
 * @brief Modify specific bits in a register
 * 
 * @param handle Device handle
 * @param reg Register address
 * @param mask Bit mask
 * @param value Value to set for masked bits
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_modify_register(tps65185_handle_t handle, uint8_t reg, uint8_t mask, uint8_t value);

/**
 * @brief Dump all registers for debugging
 * 
 * @param handle Device handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_dump_registers(tps65185_handle_t handle);

/**
 * @brief Run VCOM diagnostic - check VCOM configuration and status
 * 
 * @param handle Device handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tps65185_vcom_diagnostic(tps65185_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // TPS65185_H
