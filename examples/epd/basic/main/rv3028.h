#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RV-3028-C7 I2C Address */
#define RV3028_I2C_ADDRESS          0x52

/* ============================================================================
 * RV-3028-C7 Register Map
 * ============================================================================ */

/* Clock Registers (0x00-0x06) */
#define RV3028_REG_SECONDS          0x00    // Seconds (0-59)
#define RV3028_REG_MINUTES          0x01    // Minutes (0-59)
#define RV3028_REG_HOURS            0x02    // Hours (0-23)
#define RV3028_REG_WEEKDAY          0x03    // Weekday (0-6)
#define RV3028_REG_DATE             0x04    // Date (1-31)
#define RV3028_REG_MONTH            0x05    // Month (1-12)
#define RV3028_REG_YEAR             0x06    // Year (0-99)

/* Alarm Registers (0x07-0x0D) */
#define RV3028_REG_MINUTES_ALARM    0x07    // Minutes Alarm
#define RV3028_REG_HOURS_ALARM      0x08    // Hours Alarm
#define RV3028_REG_WEEKDAY_ALARM    0x09    // Weekday/Date Alarm

/* Timer Registers (0x0A-0x0C) */
#define RV3028_REG_TIMER_VALUE_0    0x0A    // Timer Value (Low)
#define RV3028_REG_TIMER_VALUE_1    0x0B    // Timer Value (High)
#define RV3028_REG_TIMER_STATUS     0x0C    // Timer Status

/* Status and Control Registers (0x0D-0x10) */
#define RV3028_REG_STATUS           0x0E    // Status Register
#define RV3028_REG_CONTROL1         0x0F    // Control 1 Register
#define RV3028_REG_CONTROL2         0x10    // Control 2 Register

/* GP Bits and INT Mask (0x11-0x12) */
#define RV3028_REG_GP_BITS          0x11    // General Purpose Bits
#define RV3028_REG_INT_MASK         0x12    // Interrupt Mask

/* Event Control (0x13) */
#define RV3028_REG_EVENT_CONTROL    0x13    // Event Control

/* Timestamp Registers (0x14-0x1A) */
#define RV3028_REG_TS_COUNT         0x14    // Timestamp Count
#define RV3028_REG_TS_SECONDS       0x15    // Timestamp Seconds
#define RV3028_REG_TS_MINUTES       0x16    // Timestamp Minutes
#define RV3028_REG_TS_HOURS         0x17    // Timestamp Hours
#define RV3028_REG_TS_DATE          0x18    // Timestamp Date
#define RV3028_REG_TS_MONTH         0x19    // Timestamp Month
#define RV3028_REG_TS_YEAR          0x1A    // Timestamp Year

/* Unix Time Registers (0x1B-0x1E) */
#define RV3028_REG_UNIX_TIME_0      0x1B    // Unix Time (Byte 0, LSB)
#define RV3028_REG_UNIX_TIME_1      0x1C    // Unix Time (Byte 1)
#define RV3028_REG_UNIX_TIME_2      0x1D    // Unix Time (Byte 2)
#define RV3028_REG_UNIX_TIME_3      0x1E    // Unix Time (Byte 3, MSB)

/* User RAM (0x1F-0x2B) */
#define RV3028_REG_USER_RAM1        0x1F    // User RAM 1
#define RV3028_REG_USER_RAM2        0x20    // User RAM 2

/* Password Registers (0x21-0x24) */
#define RV3028_REG_PASSWORD_0       0x21    // Password 0
#define RV3028_REG_PASSWORD_1       0x22    // Password 1
#define RV3028_REG_PASSWORD_2       0x23    // Password 2
#define RV3028_REG_PASSWORD_3       0x24    // Password 3

/* EEPROM Address and Data (0x25-0x26) */
#define RV3028_REG_EEPROM_ADDR      0x25    // EEPROM Address
#define RV3028_REG_EEPROM_DATA      0x26    // EEPROM Data

/* EEPROM Command (0x27) */
#define RV3028_REG_EEPROM_CMD       0x27    // EEPROM Command

/* Chip ID (0x28) */
#define RV3028_REG_ID               0x28    // Chip ID

/* EEPROM Mirror Registers (0x30-0x37) */
#define RV3028_REG_EEPROM_PMU       0x30    // EEPROM Power Management Unit
#define RV3028_REG_EEPROM_OFFSET    0x31    // EEPROM Offset
#define RV3028_REG_EEPROM_CLKOUT    0x32    // EEPROM CLKOUT
#define RV3028_REG_EEPROM_BACKUP    0x33    // EEPROM Backup

/* User EEPROM (0x00-0x2A in EEPROM space) */
#define RV3028_EEPROM_USER_START    0x00    // User EEPROM Start
#define RV3028_EEPROM_USER_END      0x2A    // User EEPROM End

/* ============================================================================
 * Status Register Bits (0x0E)
 * ============================================================================ */
#define RV3028_STATUS_EEBUSY        (1 << 7)    // EEPROM Busy
#define RV3028_STATUS_CLKF          (1 << 6)    // Clock Output Interrupt Flag
#define RV3028_STATUS_BSF           (1 << 5)    // Backup Switchover Flag
#define RV3028_STATUS_UF            (1 << 4)    // Periodic Time Update Flag
#define RV3028_STATUS_TF            (1 << 3)    // Periodic Countdown Timer Flag
#define RV3028_STATUS_AF            (1 << 2)    // Alarm Flag
#define RV3028_STATUS_EVF           (1 << 1)    // External Event Flag
#define RV3028_STATUS_PORF          (1 << 0)    // Power On Reset Flag

/* ============================================================================
 * Control 1 Register Bits (0x0F)
 * ============================================================================ */
#define RV3028_CTRL1_TRPT           (1 << 7)    // Timer Repeat
#define RV3028_CTRL1_WADA           (1 << 5)    // Weekday/Date Alarm
#define RV3028_CTRL1_USEL           (1 << 4)    // Update Interrupt Select
#define RV3028_CTRL1_EERD           (1 << 3)    // EEPROM Memory Refresh Disable
#define RV3028_CTRL1_TE             (1 << 2)    // Periodic Countdown Timer Enable
#define RV3028_CTRL1_TD_MASK        0x03        // Timer Clock Frequency Mask

/* Timer Clock Frequency Values */
#define RV3028_TD_4096HZ            0x00        // 4096 Hz (244.14 µs)
#define RV3028_TD_64HZ              0x01        // 64 Hz (15.625 ms)
#define RV3028_TD_1HZ               0x02        // 1 Hz (1 s)
#define RV3028_TD_1_60HZ            0x03        // 1/60 Hz (60 s)

/* ============================================================================
 * Control 2 Register Bits (0x10)
 * ============================================================================ */
#define RV3028_CTRL2_TSE            (1 << 7)    // Timestamp Enable
#define RV3028_CTRL2_CLKIE          (1 << 6)    // Clock Output Interrupt Enable
#define RV3028_CTRL2_UIE            (1 << 5)    // Periodic Time Update Interrupt Enable
#define RV3028_CTRL2_TIE            (1 << 4)    // Periodic Countdown Timer Interrupt Enable
#define RV3028_CTRL2_AIE            (1 << 3)    // Alarm Interrupt Enable
#define RV3028_CTRL2_EIE            (1 << 2)    // External Event Interrupt Enable
#define RV3028_CTRL2_12_24          (1 << 1)    // 12/24 Hour Mode
#define RV3028_CTRL2_RESET          (1 << 0)    // Software Reset

/* ============================================================================
 * Event Control Register Bits (0x13)
 * ============================================================================ */
#define RV3028_EVENT_ET_MASK        0x30        // Event Filter Time Mask
#define RV3028_EVENT_TSR            (1 << 2)    // Timestamp Reset
#define RV3028_EVENT_TSOW           (1 << 1)    // Timestamp Overwrite
#define RV3028_EVENT_TSS            (1 << 0)    // Timestamp Source

/* ============================================================================
 * EEPROM Backup Register Bits (0x33)
 * ============================================================================ */
#define RV3028_BACKUP_EEOFFSET_MASK 0x80        // EEPROM Offset
#define RV3028_BACKUP_BSIE          (1 << 6)    // Backup Switchover Interrupt Enable
#define RV3028_BACKUP_TCE           (1 << 5)    // Trickle Charge Enable
#define RV3028_BACKUP_FEDE          (1 << 4)    // Fast Edge Detection Enable
#define RV3028_BACKUP_BSM_MASK      0x0C        // Backup Switchover Mode Mask
#define RV3028_BACKUP_TCR_MASK      0x03        // Trickle Charge Resistor Mask

/* Backup Switchover Mode Values */
#define RV3028_BSM_DISABLED         (0x00 << 2) // Switchover disabled
#define RV3028_BSM_DIRECT           (0x01 << 2) // Direct switching mode
#define RV3028_BSM_LEVEL            (0x02 << 2) // Level switching mode (typ. 2.0V)
#define RV3028_BSM_LEVEL_ALT        (0x03 << 2) // Level switching mode (typ. 1.4V)

/* Trickle Charge Resistor Values */
#define RV3028_TCR_3K               0x00        // 3 kOhm
#define RV3028_TCR_5K               0x01        // 5 kOhm
#define RV3028_TCR_9K               0x02        // 9 kOhm
#define RV3028_TCR_15K              0x03        // 15 kOhm

/* ============================================================================
 * EEPROM PMU Register Bits (0x30)
 * ============================================================================ */
#define RV3028_PMU_NERD             (1 << 2)    // No EEPROM Refresh Delay

/* ============================================================================
 * CLKOUT Register Bits (0x32)
 * ============================================================================ */
#define RV3028_CLKOUT_CLKOE         (1 << 7)    // CLKOUT Enable
#define RV3028_CLKOUT_CLKSY         (1 << 6)    // CLKOUT Synchronized
#define RV3028_CLKOUT_PORIE         (1 << 5)    // POR Interrupt Enable
#define RV3028_CLKOUT_FD_MASK       0x07        // Frequency Selection Mask

/* CLKOUT Frequency Values */
#define RV3028_CLKOUT_32768HZ       0x00        // 32.768 kHz
#define RV3028_CLKOUT_8192HZ        0x01        // 8192 Hz
#define RV3028_CLKOUT_1024HZ        0x02        // 1024 Hz
#define RV3028_CLKOUT_64HZ          0x03        // 64 Hz
#define RV3028_CLKOUT_32HZ          0x04        // 32 Hz
#define RV3028_CLKOUT_1HZ           0x05        // 1 Hz
#define RV3028_CLKOUT_LOW           0x06        // Static Low
#define RV3028_CLKOUT_HIGH          0x07        // Static High (TEST only)

/* ============================================================================
 * EEPROM Command Values (0x27)
 * ============================================================================ */
#define RV3028_EEPROM_CMD_UPDATE    0x11        // Update EEPROM (mirror -> EEPROM)
#define RV3028_EEPROM_CMD_REFRESH   0x12        // Refresh RAM (EEPROM -> mirror)
#define RV3028_EEPROM_CMD_READ      0x22        // Read single byte from EEPROM
#define RV3028_EEPROM_CMD_WRITE     0x21        // Write single byte to EEPROM

/* ============================================================================
 * Data Types
 * ============================================================================ */

/**
 * @brief RV-3028 device handle
 */
typedef struct rv3028_dev_t *rv3028_handle_t;

/**
 * @brief RV-3028 time structure (avoids time.h dependency)
 */
typedef struct {
    uint8_t seconds;    // 0-59
    uint8_t minutes;    // 0-59
    uint8_t hours;      // 0-23
    uint8_t weekday;    // 0-6 (0 = Sunday)
    uint8_t date;       // 1-31
    uint8_t month;      // 1-12
    uint16_t year;      // Full year (e.g., 2025)
} rv3028_time_t;

/**
 * @brief RV-3028 alarm structure
 */
typedef struct {
    uint8_t minutes;        // 0-59 (use 0x80 to disable)
    uint8_t hours;          // 0-23 (use 0x80 to disable)
    uint8_t weekday_date;   // Weekday (0-6) or Date (1-31) depending on mode
    bool use_date_mode;     // true = use date, false = use weekday
} rv3028_alarm_t;

/**
 * @brief RV-3028 timestamp structure
 */
typedef struct {
    uint8_t count;      // Event count
    uint8_t seconds;    // 0-59
    uint8_t minutes;    // 0-59
    uint8_t hours;      // 0-23
    uint8_t date;       // 1-31
    uint8_t month;      // 1-12
    uint16_t year;      // Full year
} rv3028_timestamp_t;

/**
 * @brief Backup switchover mode
 */
typedef enum {
    RV3028_BACKUP_DISABLED = 0,     // Switchover disabled
    RV3028_BACKUP_DIRECT = 1,       // Direct switching mode
    RV3028_BACKUP_LEVEL = 2,        // Level switching mode (typ. 2.0V)
    RV3028_BACKUP_LEVEL_ALT = 3,    // Level switching mode (typ. 1.4V)
} rv3028_backup_mode_t;

/**
 * @brief Trickle charge resistor selection
 */
typedef enum {
    RV3028_TRICKLE_3K = 0,          // 3 kOhm
    RV3028_TRICKLE_5K = 1,          // 5 kOhm
    RV3028_TRICKLE_9K = 2,          // 9 kOhm
    RV3028_TRICKLE_15K = 3,         // 15 kOhm
} rv3028_trickle_resistor_t;

/**
 * @brief CLKOUT frequency selection
 */
typedef enum {
    RV3028_CLKOUT_FREQ_32768HZ = 0, // 32.768 kHz
    RV3028_CLKOUT_FREQ_8192HZ = 1,  // 8192 Hz
    RV3028_CLKOUT_FREQ_1024HZ = 2,  // 1024 Hz
    RV3028_CLKOUT_FREQ_64HZ = 3,    // 64 Hz
    RV3028_CLKOUT_FREQ_32HZ = 4,    // 32 Hz
    RV3028_CLKOUT_FREQ_1HZ = 5,     // 1 Hz
    RV3028_CLKOUT_FREQ_LOW = 6,     // Static Low
} rv3028_clkout_freq_t;

/**
 * @brief Timer clock frequency selection
 */
typedef enum {
    RV3028_TIMER_4096HZ = 0,        // 4096 Hz (244.14 µs per tick)
    RV3028_TIMER_64HZ = 1,          // 64 Hz (15.625 ms per tick)
    RV3028_TIMER_1HZ = 2,           // 1 Hz (1 s per tick)
    RV3028_TIMER_1_60HZ = 3,        // 1/60 Hz (60 s per tick)
} rv3028_timer_freq_t;

/* ============================================================================
 * Core Functions
 * ============================================================================ */

/**
 * @brief Initialize RV-3028-C7 device
 * 
 * @param bus_handle I2C master bus handle
 * @param address I2C address of the device (typically 0x52)
 * @param ret_handle Pointer to return the device handle
 * @return esp_err_t 
 */
esp_err_t rv3028_init(i2c_master_bus_handle_t bus_handle, uint8_t address, rv3028_handle_t *ret_handle);

/**
 * @brief Deinitialize RV-3028-C7 device
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t rv3028_deinit(rv3028_handle_t handle);

/**
 * @brief Software reset the RV-3028-C7
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t rv3028_reset(rv3028_handle_t handle);

/**
 * @brief Read the chip ID
 * 
 * @param handle Device handle
 * @param id Pointer to store the chip ID
 * @return esp_err_t 
 */
esp_err_t rv3028_get_id(rv3028_handle_t handle, uint8_t *id);

/* ============================================================================
 * Time Functions
 * ============================================================================ */

/**
 * @brief Get time from RV-3028-C7
 * 
 * @param handle Device handle
 * @param time Pointer to rv3028_time_t structure to store the time
 * @return esp_err_t 
 */
esp_err_t rv3028_get_time(rv3028_handle_t handle, rv3028_time_t *time);

/**
 * @brief Set time to RV-3028-C7
 * 
 * @param handle Device handle
 * @param time Pointer to rv3028_time_t structure containing the time to set
 * @return esp_err_t 
 */
esp_err_t rv3028_set_time(rv3028_handle_t handle, const rv3028_time_t *time);

/**
 * @brief Get Unix timestamp from RV-3028-C7
 * 
 * @param handle Device handle
 * @param unix_time Pointer to store the Unix timestamp
 * @return esp_err_t 
 */
esp_err_t rv3028_get_unix_time(rv3028_handle_t handle, uint32_t *unix_time);

/**
 * @brief Set Unix timestamp to RV-3028-C7
 * 
 * @param handle Device handle
 * @param unix_time Unix timestamp to set
 * @return esp_err_t 
 */
esp_err_t rv3028_set_unix_time(rv3028_handle_t handle, uint32_t unix_time);

/* ============================================================================
 * Alarm Functions
 * ============================================================================ */

/**
 * @brief Set alarm
 * 
 * @param handle Device handle
 * @param alarm Pointer to alarm configuration
 * @return esp_err_t 
 */
esp_err_t rv3028_set_alarm(rv3028_handle_t handle, const rv3028_alarm_t *alarm);

/**
 * @brief Get alarm configuration
 * 
 * @param handle Device handle
 * @param alarm Pointer to store alarm configuration
 * @return esp_err_t 
 */
esp_err_t rv3028_get_alarm(rv3028_handle_t handle, rv3028_alarm_t *alarm);

/**
 * @brief Enable alarm interrupt
 * 
 * @param handle Device handle
 * @param enable true to enable, false to disable
 * @return esp_err_t 
 */
esp_err_t rv3028_enable_alarm_interrupt(rv3028_handle_t handle, bool enable);

/**
 * @brief Check if alarm flag is set
 * 
 * @param handle Device handle
 * @param flag Pointer to store the flag state
 * @return esp_err_t 
 */
esp_err_t rv3028_get_alarm_flag(rv3028_handle_t handle, bool *flag);

/**
 * @brief Clear alarm flag
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t rv3028_clear_alarm_flag(rv3028_handle_t handle);

/* ============================================================================
 * Timer Functions
 * ============================================================================ */

/**
 * @brief Configure and start the countdown timer
 * 
 * @param handle Device handle
 * @param value Timer value (12-bit, 0-4095)
 * @param freq Timer clock frequency
 * @param repeat true for auto-repeat, false for single shot
 * @return esp_err_t 
 */
esp_err_t rv3028_set_timer(rv3028_handle_t handle, uint16_t value, rv3028_timer_freq_t freq, bool repeat);

/**
 * @brief Enable/disable the countdown timer
 * 
 * @param handle Device handle
 * @param enable true to enable, false to disable
 * @return esp_err_t 
 */
esp_err_t rv3028_enable_timer(rv3028_handle_t handle, bool enable);

/**
 * @brief Enable timer interrupt
 * 
 * @param handle Device handle
 * @param enable true to enable, false to disable
 * @return esp_err_t 
 */
esp_err_t rv3028_enable_timer_interrupt(rv3028_handle_t handle, bool enable);

/**
 * @brief Get timer flag
 * 
 * @param handle Device handle
 * @param flag Pointer to store the flag state
 * @return esp_err_t 
 */
esp_err_t rv3028_get_timer_flag(rv3028_handle_t handle, bool *flag);

/**
 * @brief Clear timer flag
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t rv3028_clear_timer_flag(rv3028_handle_t handle);

/* ============================================================================
 * Timestamp Functions
 * ============================================================================ */

/**
 * @brief Enable timestamp recording
 * 
 * @param handle Device handle
 * @param enable true to enable, false to disable
 * @return esp_err_t 
 */
esp_err_t rv3028_enable_timestamp(rv3028_handle_t handle, bool enable);

/**
 * @brief Get the last recorded timestamp
 * 
 * @param handle Device handle
 * @param ts Pointer to store the timestamp
 * @return esp_err_t 
 */
esp_err_t rv3028_get_timestamp(rv3028_handle_t handle, rv3028_timestamp_t *ts);

/**
 * @brief Reset timestamp
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t rv3028_reset_timestamp(rv3028_handle_t handle);

/* ============================================================================
 * Backup/Trickle Charge Functions
 * ============================================================================ */

/**
 * @brief Configure backup switchover mode
 * 
 * @param handle Device handle
 * @param mode Backup switchover mode
 * @return esp_err_t 
 */
esp_err_t rv3028_set_backup_mode(rv3028_handle_t handle, rv3028_backup_mode_t mode);

/**
 * @brief Configure trickle charger
 * 
 * @param handle Device handle
 * @param enable true to enable, false to disable
 * @param resistor Trickle charge resistor selection
 * @return esp_err_t 
 */
esp_err_t rv3028_set_trickle_charge(rv3028_handle_t handle, bool enable, rv3028_trickle_resistor_t resistor);

/**
 * @brief Check if backup switchover occurred
 * 
 * @param handle Device handle
 * @param flag Pointer to store the flag state
 * @return esp_err_t 
 */
esp_err_t rv3028_get_backup_flag(rv3028_handle_t handle, bool *flag);

/**
 * @brief Clear backup switchover flag
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t rv3028_clear_backup_flag(rv3028_handle_t handle);

/* ============================================================================
 * CLKOUT Functions
 * ============================================================================ */

/**
 * @brief Configure CLKOUT pin
 * 
 * @param handle Device handle
 * @param enable true to enable output, false to disable
 * @param freq Output frequency selection
 * @return esp_err_t 
 */
esp_err_t rv3028_set_clkout(rv3028_handle_t handle, bool enable, rv3028_clkout_freq_t freq);

/* ============================================================================
 * EEPROM Functions
 * ============================================================================ */

/**
 * @brief Read a byte from user EEPROM
 * 
 * @param handle Device handle
 * @param addr EEPROM address (0x00-0x2A)
 * @param data Pointer to store the data
 * @return esp_err_t 
 */
esp_err_t rv3028_eeprom_read(rv3028_handle_t handle, uint8_t addr, uint8_t *data);

/**
 * @brief Write a byte to user EEPROM
 * 
 * @param handle Device handle
 * @param addr EEPROM address (0x00-0x2A)
 * @param data Data to write
 * @return esp_err_t 
 */
esp_err_t rv3028_eeprom_write(rv3028_handle_t handle, uint8_t addr, uint8_t data);

/**
 * @brief Wait for EEPROM operation to complete
 * 
 * @param handle Device handle
 * @param timeout_ms Timeout in milliseconds
 * @return esp_err_t 
 */
esp_err_t rv3028_eeprom_wait_busy(rv3028_handle_t handle, uint32_t timeout_ms);

/* ============================================================================
 * Status Functions
 * ============================================================================ */

/**
 * @brief Get the full status register value
 * 
 * @param handle Device handle
 * @param status Pointer to store the status
 * @return esp_err_t 
 */
esp_err_t rv3028_get_status(rv3028_handle_t handle, uint8_t *status);

/**
 * @brief Check if power-on reset occurred
 * 
 * @param handle Device handle
 * @param flag Pointer to store the flag state
 * @return esp_err_t 
 */
esp_err_t rv3028_get_por_flag(rv3028_handle_t handle, bool *flag);

/**
 * @brief Clear power-on reset flag
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t rv3028_clear_por_flag(rv3028_handle_t handle);

/**
 * @brief Clear all status flags
 * 
 * @param handle Device handle
 * @return esp_err_t 
 */
esp_err_t rv3028_clear_all_flags(rv3028_handle_t handle);

/* ============================================================================
 * Low-Level Register Access
 * ============================================================================ */

/**
 * @brief Read register(s) from the RV-3028
 * 
 * @param handle Device handle
 * @param reg Register address
 * @param data Buffer to store data
 * @param len Number of bytes to read
 * @return esp_err_t 
 */
esp_err_t rv3028_read_reg(rv3028_handle_t handle, uint8_t reg, uint8_t *data, size_t len);

/**
 * @brief Write register(s) to the RV-3028
 * 
 * @param handle Device handle
 * @param reg Register address
 * @param data Data to write
 * @param len Number of bytes to write
 * @return esp_err_t 
 */
esp_err_t rv3028_write_reg(rv3028_handle_t handle, uint8_t reg, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
