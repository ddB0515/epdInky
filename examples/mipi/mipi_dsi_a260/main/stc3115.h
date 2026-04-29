#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

/* ---- STC3115 I2C Address (7-bit) ---- */
#define STC3115_I2C_ADDR                0x70    /* 8-bit: 0xE0 */

/* ---- STC3115 Register Map ---- */
#define STC3115_REG_MODE                0x00
#define STC3115_REG_CTRL                0x01
#define STC3115_REG_SOC                 0x02    /* 2 bytes */
#define STC3115_REG_COUNTER             0x04    /* 2 bytes */
#define STC3115_REG_CURRENT             0x06    /* 2 bytes */
#define STC3115_REG_VOLTAGE             0x08    /* 2 bytes */
#define STC3115_REG_TEMPERATURE         0x0A
#define STC3115_REG_CC_ADJ_HIGH         0x0B
#define STC3115_REG_VM_ADJ_HIGH         0x0C
#define STC3115_REG_OCV                 0x0D    /* 2 bytes */
#define STC3115_REG_CC_CNF              0x0F    /* 2 bytes */
#define STC3115_REG_VM_CNF              0x11    /* 2 bytes */
#define STC3115_REG_ALARM_SOC           0x13
#define STC3115_REG_ALARM_VOLTAGE       0x14
#define STC3115_REG_CURRENT_THRES       0x15
#define STC3115_REG_RELAX_COUNT         0x16
#define STC3115_REG_RELAX_MAX           0x17
#define STC3115_REG_ID                  0x18
#define STC3115_REG_CC_ADJ_LOW          0x19
#define STC3115_REG_VM_ADJ_LOW          0x1A
#define STC3115_REG_RAM                 0x20    /* 16 bytes */
#define STC3115_REG_OCVTAB              0x30    /* 16 bytes */

/* ---- REG_MODE bit masks ---- */
#define STC3115_VMODE                   0x01
#define STC3115_CLR_VM_ADJ              0x02
#define STC3115_CLR_CC_ADJ              0x04
#define STC3115_ALM_ENA                 0x08
#define STC3115_GG_RUN                  0x10
#define STC3115_FORCE_CC                0x20
#define STC3115_FORCE_VM                0x40
#define STC3115_REGMODE_DEFAULT_STANDBY 0x09

/* ---- REG_CTRL bit masks ---- */
#define STC3115_GG_RST                  0x02
#define STC3115_GG_VM                   0x04
#define STC3115_BATFAIL                 0x08
#define STC3115_PORDET                  0x10
#define STC3115_ALM_SOC_BIT             0x20
#define STC3115_ALM_VOLT_BIT            0x40

/* ---- General constants ---- */
#define STC3115_ID_VAL                  0x14
#define STC3115_RAM_SIZE                16
#define STC3115_OCVTAB_SIZE             16
#define STC3115_VCOUNT                  4
#define STC3115_MAX_HRSOC               51200   /* 100% in 1/512% */
#define STC3115_MAX_SOC                 1000    /* 100% in 0.1%   */
#define STC3115_OK                      0
#define STC3115_VOLTAGE_FACTOR          9011    /* LSB=2.20mV  */
#define STC3115_CURRENT_FACTOR          24084   /* LSB=5.88uV/R */
#define STC3115_VOLTAGE_SECURITY_RANGE  200
#define STC3115_RAM_TESTWORD            0x53A9

/* ---- State machine ---- */
#define STC3115_STATE_UNINIT            0
#define STC3115_STATE_INIT              'I'
#define STC3115_STATE_RUNNING           'R'
#define STC3115_STATE_POWERDN           'D'

/* ---- Operating modes ---- */
#define STC3115_VM_MODE                 1
#define STC3115_MIXED_MODE              0

/**
 * @brief Battery configuration (fill before calling stc3115_init)
 */
typedef struct {
    int capacity_mah;       /**< Battery nominal capacity in mAh */
    int internal_rint;      /**< Battery internal resistance in mOhms (0 = use default 200) */
    int rsense;             /**< Sense resistor in mOhms (0 = use default 10) */
    int vmode;              /**< 0=Mixed mode, 1=Voltage mode */
    int alm_soc;            /**< SOC alarm level in % (0 = disabled) */
    int alm_vbat;           /**< Voltage alarm level in mV (0 = disabled) */
    int eoc_current;        /**< End of charge current in mA */
    int cutoff_voltage;     /**< Application cutoff voltage in mV */
    int ocv_offset[16];     /**< OCV curve offset table */
} stc3115_battery_config_t;

/**
 * @brief Battery data returned by periodic task
 */
typedef struct {
    int status_word;        /**< STC3115 status registers */
    int hrsoc;              /**< SOC in 1/512% */
    int soc;                /**< SOC in 0.1% (0-1000 = 0.0%-100.0%) */
    int voltage;            /**< Battery voltage in mV */
    int current;            /**< Battery current in mA */
    int temperature;        /**< Temperature in 0.1°C */
    int conv_counter;       /**< Conversion counter */
    int ocv;                /**< Open-circuit voltage in mV */
    int presence;           /**< Battery presence (1=present) */
    int charge_value;       /**< Remaining capacity in mAh */
    int rem_time;           /**< Remaining time in minutes (-1=unavailable) */
} stc3115_battery_data_t;

typedef struct stc3115_dev_t *stc3115_handle_t;

/**
 * @brief Initialize STC3115 device (tca6408-style init)
 *
 * @param bus_handle I2C master bus handle
 * @param address    I2C 7-bit address (default 0x70)
 * @param config     Battery configuration
 * @param ret_handle Pointer to return the device handle
 * @return esp_err_t
 */
esp_err_t stc3115_init(i2c_master_bus_handle_t bus_handle, uint8_t address,
                       const stc3115_battery_config_t *config,
                       stc3115_handle_t *ret_handle);

/**
 * @brief Periodic gas gauge task — call every 1-60s (recommended 5s)
 *
 * @param handle Device handle
 * @param data   Pointer to battery data struct (filled on return)
 * @return 1 if all data available, 0 if partial, -1 on error
 */
int stc3115_task(stc3115_handle_t handle, stc3115_battery_data_t *data);

/**
 * @brief Stop the gas gauge and save context (call on shutdown)
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
 * @brief Check STC3115 chip ID over I2C
 *
 * @param handle Device handle
 * @return ESP_OK if ID matches, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t stc3115_check_id(stc3115_handle_t handle);

/**
 * @brief Get latest battery data without running full task
 *
 * @param handle Device handle
 * @param data   Pointer to battery data struct
 * @return esp_err_t
 */
esp_err_t stc3115_get_battery_data(stc3115_handle_t handle, stc3115_battery_data_t *data);
