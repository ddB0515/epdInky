#include <stdlib.h>
#include <string.h>
#include "STC3115.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "STC3115";

// Default OCV table (8-bit values, 16 entries)
static const uint8_t stc3115_default_ocv_table[16] = {0}; // STC3115_DEFAULT_OCV_TABLE;

// RAM structure for saving/restoring state
typedef struct {
    uint8_t  test_word;      // RAM[0]: Test word (0x53 = 'S')
    uint8_t  ctrl_reg;       // RAM[1]: Saved CTRL register
    uint16_t soc;            // RAM[2-3]: Saved SOC
    uint16_t ocv;            // RAM[4-5]: Saved OCV
    uint16_t cc_cnf;         // RAM[6-7]: CC configuration
    uint16_t vm_cnf;         // RAM[8-9]: VM configuration
    uint8_t  cc_adj_h;       // RAM[10]: CC adjustment high
    uint8_t  cc_adj_l;       // RAM[11]: CC adjustment low
    uint8_t  vm_adj_h;       // RAM[12]: VM adjustment high
    uint8_t  vm_adj_l;       // RAM[13]: VM adjustment low
    uint8_t  reserved;       // RAM[14]: Reserved
    uint8_t  crc;            // RAM[15]: CRC checksum
} __attribute__((packed)) stc3115_ram_data_t;

struct stc3115_dev_t {
    i2c_master_dev_handle_t i2c_dev;
    stc3115_config_t config;
    float current_factor;     // Calculated based on sense resistor
    uint16_t cc_cnf;          // Calculated CC configuration
    uint16_t vm_cnf;          // Calculated VM configuration
    bool running;             // Gas gauge running state
};

// ============================================================================
// Internal helper functions
// ============================================================================

static esp_err_t stc3115_write_reg(stc3115_handle_t handle, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(handle->i2c_dev, buf, sizeof(buf), -1);
}

static esp_err_t stc3115_read_reg(stc3115_handle_t handle, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(handle->i2c_dev, &reg, 1, val, 1, -1);
}

static esp_err_t stc3115_write_regs(stc3115_handle_t handle, uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t *buf = malloc(len + 1);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "No memory for write buffer");
    
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    esp_err_t ret = i2c_master_transmit(handle->i2c_dev, buf, len + 1, -1);
    free(buf);
    return ret;
}

static esp_err_t stc3115_read_regs(stc3115_handle_t handle, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(handle->i2c_dev, &reg, 1, data, len, -1);
}

static esp_err_t stc3115_write_reg16(stc3115_handle_t handle, uint8_t reg, uint16_t val)
{
    uint8_t buf[3] = {reg, val & 0xFF, (val >> 8) & 0xFF};
    return i2c_master_transmit(handle->i2c_dev, buf, sizeof(buf), -1);
}

static esp_err_t stc3115_read_reg16(stc3115_handle_t handle, uint8_t reg, uint16_t *val)
{
    uint8_t buf[2];
    esp_err_t ret = i2c_master_transmit_receive(handle->i2c_dev, &reg, 1, buf, 2, -1);
    if (ret == ESP_OK) {
        *val = buf[0] | (buf[1] << 8);
    }
    return ret;
}

/**
 * @brief Calculate CRC8 for RAM data
 */
static uint8_t stc3115_calc_crc(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief Read RAM data from device
 */
static esp_err_t stc3115_read_ram(stc3115_handle_t handle, stc3115_ram_data_t *ram_data)
{
    return stc3115_read_regs(handle, STC3115_REG_RAM0, (uint8_t *)ram_data, sizeof(stc3115_ram_data_t));
}

/**
 * @brief Write RAM data to device
 */
static esp_err_t stc3115_write_ram(stc3115_handle_t handle, const stc3115_ram_data_t *ram_data)
{
    return stc3115_write_regs(handle, STC3115_REG_RAM0, (const uint8_t *)ram_data, sizeof(stc3115_ram_data_t));
}

/**
 * @brief Check if RAM content is valid
 */
static bool stc3115_is_ram_valid(const stc3115_ram_data_t *ram_data)
{
    // Check test word
    if (ram_data->test_word != STC3115_RAM_TEST_WORD) {
        return false;
    }
    
    // Verify CRC (over first 15 bytes)
    uint8_t calc_crc = stc3115_calc_crc((const uint8_t *)ram_data, sizeof(stc3115_ram_data_t) - 1);
    if (calc_crc != ram_data->crc) {
        return false;
    }
    
    return true;
}

/**
 * @brief Write OCV table to device (write in small chunks to avoid I2C issues)
 */
static esp_err_t stc3115_write_ocv_table(stc3115_handle_t handle, const uint8_t *ocv_table)
{
    const uint8_t *table = ocv_table ? ocv_table : stc3115_default_ocv_table;
    esp_err_t ret = ESP_OK;
    
    // Write OCV table in chunks of 4 bytes to avoid I2C buffer issues
    for (int i = 0; i < 16; i += 4) {
        ret = stc3115_write_regs(handle, STC3115_REG_OCV_TAB0 + i, &table[i], 4);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write OCV table chunk %d", i / 4);
            return ret;
        }
    }
    
    return ESP_OK;
}

/**
 * @brief Full initialization with default parameters (new battery)
 * 
 * Following official sequence:
 * 1. Read OCV register (first measurement)
 * 2. Set parameters with GG_RUN = 0
 * 3. Write back OCV to trigger SOC calculation
 */
static esp_err_t stc3115_full_init(stc3115_handle_t handle)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Performing full initialization");
    
    // Step 1: Stop gas gauge (ensure GG_RUN = 0)
    ret = stc3115_write_reg(handle, STC3115_REG_MODE, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop gas gauge");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Step 2: Read initial voltage measurement (use VOLTAGE register, not OCV, as OCV may be invalid)
    // The OCV register only has valid data after gas gauge runs
    uint16_t initial_voltage;
    ret = stc3115_read_reg16(handle, STC3115_REG_VOLTAGE_L, &initial_voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read initial voltage");
        return ret;
    }
    float voltage_mv = initial_voltage * STC3115_VOLTAGE_FACTOR;
    ESP_LOGI(TAG, "Initial voltage reading: 0x%04x (%.0f mV)", initial_voltage, voltage_mv);
    
    // Step 3: Configure CC_CNF and VM_CNF
    ret = stc3115_write_reg16(handle, STC3115_REG_CC_CNF_L, handle->cc_cnf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to write CC_CNF");
    }
    
    ret = stc3115_write_reg16(handle, STC3115_REG_VM_CNF_L, handle->vm_cnf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to write VM_CNF");
    }
    
    // Step 4: Write OCV table
    ret = stc3115_write_ocv_table(handle, handle->config.ocv_table);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to write OCV table");
    }
    
    // Step 5: Configure alarm thresholds
    if (handle->config.alarm_soc > 0) {
        uint8_t alarm_soc = handle->config.alarm_soc * 2;  // 0.5% per LSB
        stc3115_write_reg(handle, STC3115_REG_ALARM_SOC, alarm_soc);
    }
    
    if (handle->config.alarm_voltage_mv > 0) {
        uint8_t alarm_volt = (uint8_t)(handle->config.alarm_voltage_mv / 17.6f);
        stc3115_write_reg(handle, STC3115_REG_ALARM_VOLTAGE, alarm_volt);
    }
    
    // Step 6: Configure current threshold and relax max
    uint8_t current_thres = handle->config.current_thres ? handle->config.current_thres : STC3115_DEFAULT_CURRENT_THRES;
    uint8_t relax_max = handle->config.relax_max ? handle->config.relax_max : STC3115_DEFAULT_RELAX_MAX;
    stc3115_write_reg(handle, STC3115_REG_CURRENT_THRES, current_thres);
    stc3115_write_reg(handle, STC3115_REG_RELAX_MAX, relax_max);
    
    // Step 7: Clear adjustments
    ret = stc3115_write_reg(handle, STC3115_REG_MODE, STC3115_MODE_CLR_VM_ADJ | STC3115_MODE_CLR_CC_ADJ);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear adjustments");
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Step 8: Write voltage as OCV to set initial SOC
    // Using the voltage reading as initial OCV since gas gauge wasn't running
    ret = stc3115_write_reg16(handle, STC3115_REG_OCV_L, initial_voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write OCV");
        return ret;
    }
    ESP_LOGI(TAG, "Wrote initial OCV: 0x%04x", initial_voltage);
    
    // Step 9: Initialize RAM with valid data
    stc3115_ram_data_t ram_data = {0};
    ram_data.test_word = STC3115_RAM_TEST_WORD;
    ram_data.ocv = initial_voltage;
    ram_data.cc_cnf = handle->cc_cnf;
    ram_data.vm_cnf = handle->vm_cnf;
    ram_data.crc = stc3115_calc_crc((const uint8_t *)&ram_data, sizeof(stc3115_ram_data_t) - 1);
    ret = stc3115_write_ram(handle, &ram_data);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize RAM");
    }
    
    // Wait for SOC to be available (100ms as per datasheet)
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "Full initialization complete");
    return ESP_OK;
}

/**
 * @brief Restore state from RAM data (STC3115 restoration steps)
 * 
 * Following official sequence:
 * 1. Read SOC from RAM (already done by caller)
 * 2. Set parameters with GG_RUN = 0
 * 3. Write SOC register to restore battery state
 */
static esp_err_t stc3115_restore_state(stc3115_handle_t handle, const stc3115_ram_data_t *ram_data)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Restoring state from RAM (SOC=0x%04x, OCV=0x%04x)", ram_data->soc, ram_data->ocv);
    
    // Step 1: Stop gas gauge first (ensure GG_RUN = 0)
    ret = stc3115_write_reg(handle, STC3115_REG_MODE, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop gas gauge");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Step 2: Set STC3115 parameters
    
    // 2a. Write OCV table
    ret = stc3115_write_ocv_table(handle, handle->config.ocv_table);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to write OCV table");
    }
    
    // 2b. Restore CC_CNF and VM_CNF from RAM (may have improved values from battery aging)
    ret = stc3115_write_reg16(handle, STC3115_REG_CC_CNF_L, ram_data->cc_cnf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to restore CC_CNF");
    }
    ESP_LOGI(TAG, "Restored CC_CNF = %d", ram_data->cc_cnf);
    
    ret = stc3115_write_reg16(handle, STC3115_REG_VM_CNF_L, ram_data->vm_cnf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to restore VM_CNF");
    }
    ESP_LOGI(TAG, "Restored VM_CNF = %d", ram_data->vm_cnf);
    
    // 2c. Restore CC and VM adjustment values
    stc3115_write_reg(handle, STC3115_REG_CC_ADJ_H, ram_data->cc_adj_h);
    stc3115_write_reg(handle, STC3115_REG_CC_ADJ_L, ram_data->cc_adj_l);
    stc3115_write_reg(handle, STC3115_REG_VM_ADJ_H, ram_data->vm_adj_h);
    stc3115_write_reg(handle, STC3115_REG_VM_ADJ_L, ram_data->vm_adj_l);
    
    // 2d. Configure alarm thresholds
    if (handle->config.alarm_soc > 0) {
        uint8_t alarm_soc = handle->config.alarm_soc * 2;  // 0.5% per LSB
        stc3115_write_reg(handle, STC3115_REG_ALARM_SOC, alarm_soc);
    }
    
    if (handle->config.alarm_voltage_mv > 0) {
        uint8_t alarm_volt = (uint8_t)(handle->config.alarm_voltage_mv / 17.6f);
        stc3115_write_reg(handle, STC3115_REG_ALARM_VOLTAGE, alarm_volt);
    }
    
    // 2e. Configure current threshold and relax max
    uint8_t current_thres = handle->config.current_thres ? handle->config.current_thres : STC3115_DEFAULT_CURRENT_THRES;
    uint8_t relax_max = handle->config.relax_max ? handle->config.relax_max : STC3115_DEFAULT_RELAX_MAX;
    stc3115_write_reg(handle, STC3115_REG_CURRENT_THRES, current_thres);
    stc3115_write_reg(handle, STC3115_REG_RELAX_MAX, relax_max);
    
    // Step 3: Write SOC register directly to restore battery state
    // This is the key difference from full init - we write SOC, not OCV
    ret = stc3115_write_reg16(handle, STC3115_REG_SOC_L, ram_data->soc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write SOC register");
        return ret;
    }
    ESP_LOGI(TAG, "Restored SOC register: 0x%04x (%d.%d%%)", 
             ram_data->soc, 
             (ram_data->soc * 10 / 512) / 10,
             (ram_data->soc * 10 / 512) % 10);
    
    // SOC is immediately available after writing REG_SOC (no delay needed per datasheet)
    
    ESP_LOGI(TAG, "State restoration complete");
    return ESP_OK;
}

// ============================================================================
// Public API functions
// ============================================================================

esp_err_t stc3115_init(i2c_master_bus_handle_t bus_handle, const stc3115_config_t *config, 
                       stc3115_handle_t *ret_handle, stc3115_init_status_t *init_status)
{
    ESP_RETURN_ON_FALSE(bus_handle, ESP_ERR_INVALID_ARG, TAG, "Invalid bus handle");
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "Invalid config pointer");
    ESP_RETURN_ON_FALSE(ret_handle, ESP_ERR_INVALID_ARG, TAG, "Invalid return handle pointer");
    ESP_RETURN_ON_FALSE(config->sense_resistor_mohm > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid sense resistor value");

    stc3115_init_status_t status = STC3115_INIT_FAILED;

    stc3115_handle_t dev = calloc(1, sizeof(struct stc3115_dev_t));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "No memory for device struct");

    memcpy(&dev->config, config, sizeof(stc3115_config_t));
    
    // Calculate current conversion factor based on sense resistor
    dev->current_factor = (STC3115_CURRENT_FACTOR * 10.0f) / (float)config->sense_resistor_mohm;
    
    // Calculate CC_CNF and VM_CNF based on battery capacity and sense resistor
    // CC_CNF = (Capacity * Rsense * 250) / 4096
    dev->cc_cnf = (uint16_t)(((uint32_t)config->battery_capacity_mah * config->sense_resistor_mohm * 250) / 4096);
    dev->vm_cnf = dev->cc_cnf;  // Same for VM mode
    
    ESP_LOGI(TAG, "CC_CNF = %d, VM_CNF = %d", dev->cc_cnf, dev->vm_cnf);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = STC3115_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev->i2c_dev);
    if (ret != ESP_OK) {
        free(dev);
        ESP_LOGE(TAG, "Failed to add device 0x%02x", STC3115_I2C_ADDRESS);
        return ret;
    }

    // ========================================================================
    // Step 1: Read and verify chip ID
    // ========================================================================
    uint8_t id;
    ret = stc3115_read_reg(dev, STC3115_REG_ID, &id);
    if (ret != ESP_OK) {
        i2c_master_bus_rm_device(dev->i2c_dev);
        free(dev);
        ESP_LOGE(TAG, "Failed to read device ID");
        return ret;
    }

    if (id != STC3115_ID) {
        i2c_master_bus_rm_device(dev->i2c_dev);
        free(dev);
        ESP_LOGE(TAG, "Invalid device ID: 0x%02x (expected 0x%02x)", id, STC3115_ID);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "STC3115 detected (ID: 0x%02x)", id);

    // ========================================================================
    // Step 2: Check RAM memory status
    // ========================================================================
    stc3115_ram_data_t ram_data;
    ret = stc3115_read_ram(dev, &ram_data);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read RAM, performing full init");
        ret = stc3115_full_init(dev);
        status = (ret == ESP_OK) ? STC3115_INIT_NEW : STC3115_INIT_FAILED;
        goto init_done;
    }

    bool ram_valid = stc3115_is_ram_valid(&ram_data);
    ESP_LOGI(TAG, "RAM validity: %s (test=0x%02x)", ram_valid ? "VALID" : "INVALID", ram_data.test_word);

    // ========================================================================
    // Step 3: If RAM invalid, perform full initialization
    // ========================================================================
    if (!ram_valid) {
        ESP_LOGI(TAG, "RAM invalid, performing full initialization");
        ret = stc3115_full_init(dev);
        status = (ret == ESP_OK) ? STC3115_INIT_NEW : STC3115_INIT_FAILED;
        goto init_done;
    }

    // ========================================================================
    // Step 4: Check PORDET and BATFAIL bits
    // ========================================================================
    uint8_t ctrl;
    ret = stc3115_read_reg(dev, STC3115_REG_CTRL, &ctrl);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read CTRL, performing full init");
        ret = stc3115_full_init(dev);
        status = (ret == ESP_OK) ? STC3115_INIT_NEW : STC3115_INIT_FAILED;
        goto init_done;
    }
    
    ESP_LOGI(TAG, "CTRL register: 0x%02x (PORDET=%d, BATFAIL=%d)", 
             ctrl, (ctrl & STC3115_CTRL_PORDET) ? 1 : 0, (ctrl & STC3115_CTRL_BATFAIL) ? 1 : 0);

    // ========================================================================
    // Step 5: If BATFAIL or PORDET, perform full initialization
    // ========================================================================
    if (ctrl & (STC3115_CTRL_BATFAIL | STC3115_CTRL_PORDET)) {
        if (ctrl & STC3115_CTRL_BATFAIL) {
            ESP_LOGW(TAG, "BATFAIL detected - battery was below 2.6V or removed");
        }
        if (ctrl & STC3115_CTRL_PORDET) {
            ESP_LOGW(TAG, "PORDET detected - battery was below 2V (POR)");
        }
        
        // Clear the flags
        stc3115_write_reg(dev, STC3115_REG_CTRL, 0x00);
        
        ret = stc3115_full_init(dev);
        status = (ret == ESP_OK) ? STC3115_INIT_NEW : STC3115_INIT_FAILED;
        goto init_done;
    }

    // ========================================================================
    // Step 6: RAM valid and no failures - restore from RAM
    // ========================================================================
    ESP_LOGI(TAG, "Battery not removed, restoring from RAM");
    ret = stc3115_restore_state(dev, &ram_data);
    status = (ret == ESP_OK) ? STC3115_INIT_RESTORED : STC3115_INIT_FAILED;

init_done:
    if (status == STC3115_INIT_FAILED) {
        i2c_master_bus_rm_device(dev->i2c_dev);
        free(dev);
        if (init_status) *init_status = status;
        return ESP_FAIL;
    }

    dev->running = false;
    *ret_handle = dev;
    if (init_status) *init_status = status;
    
    ESP_LOGI(TAG, "STC3115 initialization complete (status: %s)", 
             status == STC3115_INIT_NEW ? "NEW" : "RESTORED");
    
    return ESP_OK;
}

esp_err_t stc3115_delete(stc3115_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    // Save state and stop the gas gauge
    stc3115_save_state(handle);
    stc3115_stop(handle);

    esp_err_t ret = i2c_master_bus_rm_device(handle->i2c_dev);
    free(handle);
    return ret;
}

esp_err_t stc3115_start(stc3115_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    // Read current state for diagnostics
    uint8_t ctrl_before, mode_before;
    stc3115_read_reg(handle, STC3115_REG_CTRL, &ctrl_before);
    stc3115_read_reg(handle, STC3115_REG_MODE, &mode_before);
    ESP_LOGI(TAG, "Before start: CTRL=0x%02x, MODE=0x%02x", ctrl_before, mode_before);
    
    // If BATFAIL is set, we need to do a soft reset and full re-init
    if (ctrl_before & STC3115_CTRL_BATFAIL) {
        ESP_LOGW(TAG, "BATFAIL detected - performing soft reset");
        
        // Step 1: Stop the gas gauge first
        stc3115_write_reg(handle, STC3115_REG_MODE, 0x00);
        vTaskDelay(pdMS_TO_TICKS(30));
        
        // Step 2: Set GG_RST bit to reset the gas gauge
        stc3115_write_reg(handle, STC3115_REG_CTRL, STC3115_CTRL_GG_RST);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Step 3: Clear GG_RST bit
        stc3115_write_reg(handle, STC3115_REG_CTRL, 0x00);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Step 4: Re-initialize configuration registers
        // Write CC_CNF and VM_CNF
        stc3115_write_reg16(handle, STC3115_REG_CC_CNF_L, handle->cc_cnf);
        stc3115_write_reg16(handle, STC3115_REG_VM_CNF_L, handle->vm_cnf);
        
        // Write OCV table
        stc3115_write_ocv_table(handle, handle->config.ocv_table);
        
        // Configure alarm thresholds
        if (handle->config.alarm_soc > 0) {
            uint8_t alarm_soc = handle->config.alarm_soc * 2;
            stc3115_write_reg(handle, STC3115_REG_ALARM_SOC, alarm_soc);
        }
        if (handle->config.alarm_voltage_mv > 0) {
            uint8_t alarm_volt = (uint8_t)(handle->config.alarm_voltage_mv / 17.6f);
            stc3115_write_reg(handle, STC3115_REG_ALARM_VOLTAGE, alarm_volt);
        }
        
        ESP_LOGI(TAG, "Soft reset complete, re-configured CC_CNF=%d VM_CNF=%d", 
                 handle->cc_cnf, handle->vm_cnf);
    }
    
    // Clear any remaining flags by writing 0 to CTRL
    stc3115_write_reg(handle, STC3115_REG_CTRL, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t mode = STC3115_MODE_GG_RUN | STC3115_MODE_ALM_ENA;
    
    if (handle->config.voltage_mode) {
        mode |= STC3115_MODE_VMODE;
    } else {
        // Force CC mode for current measurement
        mode |= STC3115_MODE_FORCE_CC;
    }

    esp_err_t ret = stc3115_write_reg(handle, STC3115_REG_MODE, mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write MODE register");
        return ret;
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));  // Wait for gas gauge to start
    
    // Read back to verify
    uint8_t mode_after, ctrl_after;
    stc3115_read_reg(handle, STC3115_REG_MODE, &mode_after);
    stc3115_read_reg(handle, STC3115_REG_CTRL, &ctrl_after);
    
    bool in_vm = (ctrl_after & STC3115_CTRL_GG_VM) != 0;
    ESP_LOGI(TAG, "After start: CTRL=0x%02x (VM=%d), MODE=0x%02x (expected: 0x%02x)", 
             ctrl_after, in_vm, mode_after, mode);
    
    // Check if BATFAIL is still set
    if (ctrl_after & STC3115_CTRL_BATFAIL) {
        ESP_LOGE(TAG, "BATFAIL still set after reset! Check battery/sense resistor connection");
    }
    
    if ((mode_after & STC3115_MODE_GG_RUN) == 0) {
        ESP_LOGE(TAG, "GG_RUN bit not set! Gas gauge failed to start");
        return ESP_FAIL;
    }
    
    handle->running = true;
    ESP_LOGI(TAG, "Gas gauge started successfully");
    return ESP_OK;
}

esp_err_t stc3115_stop(stc3115_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    esp_err_t ret = stc3115_write_reg(handle, STC3115_REG_MODE, 0x00);
    if (ret == ESP_OK) {
        handle->running = false;
        ESP_LOGI(TAG, "Gas gauge stopped");
    }
    return ret;
}

esp_err_t stc3115_reset(stc3115_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    esp_err_t ret = stc3115_write_reg(handle, STC3115_REG_CTRL, STC3115_CTRL_GG_RST);
    if (ret == ESP_OK) {
        handle->running = false;
        ESP_LOGI(TAG, "Gas gauge reset");
    }
    return ret;
}

esp_err_t stc3115_save_state(stc3115_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    // Read current state
    uint8_t ctrl;
    uint16_t soc, ocv;
    
    esp_err_t ret = stc3115_read_reg(handle, STC3115_REG_CTRL, &ctrl);
    if (ret != ESP_OK) return ret;
    
    ret = stc3115_read_reg16(handle, STC3115_REG_SOC_L, &soc);
    if (ret != ESP_OK) return ret;
    
    ret = stc3115_read_reg16(handle, STC3115_REG_OCV_L, &ocv);
    if (ret != ESP_OK) return ret;

    // Read adjustment values
    uint8_t cc_adj_h, cc_adj_l, vm_adj_h, vm_adj_l;
    stc3115_read_reg(handle, STC3115_REG_CC_ADJ_H, &cc_adj_h);
    stc3115_read_reg(handle, STC3115_REG_CC_ADJ_L, &cc_adj_l);
    stc3115_read_reg(handle, STC3115_REG_VM_ADJ_H, &vm_adj_h);
    stc3115_read_reg(handle, STC3115_REG_VM_ADJ_L, &vm_adj_l);

    // Prepare RAM data
    stc3115_ram_data_t ram_data = {
        .test_word = STC3115_RAM_TEST_WORD,
        .ctrl_reg = ctrl,
        .soc = soc,
        .ocv = ocv,
        .cc_cnf = handle->cc_cnf,
        .vm_cnf = handle->vm_cnf,
        .cc_adj_h = cc_adj_h,
        .cc_adj_l = cc_adj_l,
        .vm_adj_h = vm_adj_h,
        .vm_adj_l = vm_adj_l,
        .reserved = 0,
    };
    ram_data.crc = stc3115_calc_crc((const uint8_t *)&ram_data, sizeof(stc3115_ram_data_t) - 1);

    ret = stc3115_write_ram(handle, &ram_data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "State saved to RAM (SOC=0x%04x, OCV=0x%04x)", soc, ocv);
    }
    return ret;
}

esp_err_t stc3115_read_data(stc3115_handle_t handle, stc3115_data_t *data)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(data, ESP_ERR_INVALID_ARG, TAG, "Invalid data pointer");

    // Read registers from CTRL to AVG_CURRENT_H (12 bytes)
    uint8_t buf[12];
    esp_err_t ret = stc3115_read_regs(handle, STC3115_REG_CTRL, buf, 12);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Debug: log raw register values including MODE
    uint8_t mode_reg;
    stc3115_read_reg(handle, STC3115_REG_MODE, &mode_reg);
    
    static uint16_t last_counter = 0xFFFF;
    uint16_t counter_now = buf[3] | (buf[4] << 8);
    
    // Log counter changes (debug only)
    if (counter_now != last_counter) {
        ESP_LOGD(TAG, "CNT=%d CTRL=0x%02x MODE=0x%02x V=%dmV", 
                 counter_now, buf[0], mode_reg,
                 (int)((buf[7] | (buf[8] << 8)) * STC3115_VOLTAGE_FACTOR));
        last_counter = counter_now;
    }
    
    // If BATFAIL is set OR GG_RUN is cleared, restart the gas gauge
    // This is a workaround for hardware noise on the sense resistor
    bool batfail = (buf[0] & STC3115_CTRL_BATFAIL) != 0;
    bool gg_stopped = (mode_reg & STC3115_MODE_GG_RUN) == 0;
    
    if (batfail || gg_stopped) {
        // Clear flags first
        stc3115_write_reg(handle, STC3115_REG_CTRL, 0x00);
        
        // Stop gas gauge
        stc3115_write_reg(handle, STC3115_REG_MODE, 0x00);
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Restart gas gauge with FORCE_CC if not in voltage mode
        mode_reg = STC3115_MODE_GG_RUN | STC3115_MODE_ALM_ENA;
        if (handle->config.voltage_mode) {
            mode_reg |= STC3115_MODE_VMODE;
        } else {
            mode_reg |= STC3115_MODE_FORCE_CC;
        }
        stc3115_write_reg(handle, STC3115_REG_MODE, mode_reg);
        
        // Only log occasionally to reduce spam
        static int restart_count = 0;
        restart_count++;
        if ((restart_count % 10) == 1) {
            ESP_LOGD(TAG, "GG restart #%d (BATFAIL=%d)", restart_count, batfail);
        }
    }

    // Parse CTRL register (offset 0)
    uint8_t ctrl = buf[0];
    data->alarm_soc = (ctrl & STC3115_CTRL_ALM_SOC) ? true : false;
    data->alarm_voltage = (ctrl & STC3115_CTRL_ALM_VOLT) ? true : false;
    data->battery_fail = (ctrl & STC3115_CTRL_BATFAIL) ? true : false;
    data->por_detect = (ctrl & STC3115_CTRL_PORDET) ? true : false;

    // Parse SOC (offset 1-2) - in 1/512% units, convert to 0.1% (permille)
    uint16_t soc_raw = buf[1] | (buf[2] << 8);
    data->soc_permille = (soc_raw * 10) / 512;
    if (data->soc_permille > 1000) {
        data->soc_permille = 1000;
    }

    // Parse counter (offset 3-4)
    data->counter = buf[3] | (buf[4] << 8);

    // Parse current (offset 5-6) - signed 16-bit value
    int16_t current_raw = (int16_t)(buf[5] | (buf[6] << 8));
    data->current_ua = (int32_t)(current_raw * handle->current_factor);
    
    // Debug: always show raw current for troubleshooting
    ESP_LOGI(TAG, "Current: raw=0x%04X (%d), avg_raw=0x%04X, factor=%.3f => %ld uA",
             (uint16_t)current_raw, current_raw,
             (uint16_t)(buf[10] | (buf[11] << 8)),
             handle->current_factor, (long)data->current_ua);

    // Parse voltage (offset 7-8)
    uint16_t voltage_raw = buf[7] | (buf[8] << 8);
    data->voltage_mv = (uint16_t)(voltage_raw * STC3115_VOLTAGE_FACTOR);

    // Parse temperature (offset 9)
    data->temperature_c = (int8_t)(buf[9] - STC3115_TEMP_OFFSET);

    // Parse average current (offset 10-11) - signed 16-bit value
    int16_t avg_current_raw = (int16_t)(buf[10] | (buf[11] << 8));
    data->avg_current_ua = (int32_t)(avg_current_raw * handle->current_factor);

    // Read OCV separately
    uint16_t ocv_raw;
    ret = stc3115_read_reg16(handle, STC3115_REG_OCV_L, &ocv_raw);
    if (ret == ESP_OK) {
        data->ocv_mv = (uint16_t)(ocv_raw * STC3115_VOLTAGE_FACTOR);
    } else {
        data->ocv_mv = 0;
    }

    // Software SOC calculation based on voltage
    // The STC3115's OCV table can only represent up to ~3900mV, so for higher
    // voltages we need to calculate SOC in software using linear interpolation
    // This provides a more accurate SOC for Li-ion batteries in the 3.0V-4.2V range
    if (data->voltage_mv >= STC3115_BATT_VOLTAGE_FULL) {
        data->soc_permille = 1000;  // 100%
    } else if (data->voltage_mv <= STC3115_BATT_VOLTAGE_EMPTY) {
        data->soc_permille = 0;     // 0%
    } else {
        // Linear interpolation between empty and full voltage
        // SOC% = (Vmeasured - Vempty) / (Vfull - Vempty) * 100
        uint32_t voltage_range = STC3115_BATT_VOLTAGE_FULL - STC3115_BATT_VOLTAGE_EMPTY;
        uint32_t voltage_above_empty = data->voltage_mv - STC3115_BATT_VOLTAGE_EMPTY;
        data->soc_permille = (uint16_t)((voltage_above_empty * 1000) / voltage_range);
    }

    // Charging detection based on voltage trend and current
    // Track voltage history to detect if battery is charging or discharging
    static uint16_t voltage_history[4] = {0};
    static int history_idx = 0;
    static bool history_valid = false;
    
    // Store current voltage in history
    voltage_history[history_idx] = data->voltage_mv;
    history_idx = (history_idx + 1) % 4;
    
    // Check if we have enough history
    static int sample_count = 0;
    if (sample_count < 4) {
        sample_count++;
        history_valid = (sample_count >= 4);
    }
    
    if (history_valid) {
        // Calculate average trend (compare oldest to newest)
        int oldest_idx = history_idx;  // Next to be overwritten = oldest
        int newest_idx = (history_idx + 3) % 4;
        int16_t trend = (int16_t)voltage_history[newest_idx] - (int16_t)voltage_history[oldest_idx];
        data->voltage_trend = trend;
        
        // Determine charging status
        // Keep track of last known state for when trend is stable
        static stc3115_charge_status_t last_known_status = STC3115_UNKNOWN;
        
        if (data->current_ua > 50) {
            // Positive current = charging (if current measurement works)
            data->charge_status = STC3115_CHARGING;
            last_known_status = STC3115_CHARGING;
        } else if (data->current_ua < -50) {
            // Negative current = discharging
            data->charge_status = STC3115_NOT_CHARGING;
            last_known_status = STC3115_NOT_CHARGING;
        } else if (data->voltage_mv >= 4200) {
            // At full charge voltage = fully charged
            data->charge_status = STC3115_FULLY_CHARGED;
            last_known_status = STC3115_FULLY_CHARGED;
        } else if (trend > 2) {
            // Voltage rising more than 2mV over last 4 samples = charging
            data->charge_status = STC3115_CHARGING;
            last_known_status = STC3115_CHARGING;
        } else if (trend < -1) {
            // Voltage falling = discharging
            data->charge_status = STC3115_NOT_CHARGING;
            last_known_status = STC3115_NOT_CHARGING;
        } else {
            // Voltage stable (trend between -1 and +2 mV)
            // If voltage is high (>4100mV) and stable, likely in CV charging phase or full
            if (data->voltage_mv >= 4100) {
                data->charge_status = STC3115_CHARGING;  // CV phase
            } else {
                // Use last known status
                data->charge_status = last_known_status;
            }
        }
    } else {
        data->voltage_trend = 0;
        data->charge_status = STC3115_UNKNOWN;
    }

    return ESP_OK;
}

esp_err_t stc3115_read_voltage(stc3115_handle_t handle, uint16_t *voltage_mv)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(voltage_mv, ESP_ERR_INVALID_ARG, TAG, "Invalid voltage pointer");

    uint16_t raw;
    esp_err_t ret = stc3115_read_reg16(handle, STC3115_REG_VOLTAGE_L, &raw);
    if (ret == ESP_OK) {
        *voltage_mv = (uint16_t)(raw * STC3115_VOLTAGE_FACTOR);
    }
    return ret;
}

esp_err_t stc3115_read_current(stc3115_handle_t handle, int16_t *current_ua)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(current_ua, ESP_ERR_INVALID_ARG, TAG, "Invalid current pointer");

    uint16_t raw;
    esp_err_t ret = stc3115_read_reg16(handle, STC3115_REG_CURRENT_L, &raw);
    if (ret == ESP_OK) {
        *current_ua = (int16_t)((int16_t)raw * handle->current_factor);
    }
    return ret;
}

esp_err_t stc3115_read_soc(stc3115_handle_t handle, uint16_t *soc_permille)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(soc_permille, ESP_ERR_INVALID_ARG, TAG, "Invalid SOC pointer");

    uint16_t raw;
    esp_err_t ret = stc3115_read_reg16(handle, STC3115_REG_SOC_L, &raw);
    if (ret == ESP_OK) {
        *soc_permille = (raw * 10) / 512;
        if (*soc_permille > 1000) {
            *soc_permille = 1000;
        }
    }
    return ret;
}

esp_err_t stc3115_read_temperature(stc3115_handle_t handle, int8_t *temp_c)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(temp_c, ESP_ERR_INVALID_ARG, TAG, "Invalid temperature pointer");

    uint8_t raw;
    esp_err_t ret = stc3115_read_reg(handle, STC3115_REG_TEMPERATURE, &raw);
    if (ret == ESP_OK) {
        *temp_c = (int8_t)(raw - STC3115_TEMP_OFFSET);
    }
    return ret;
}

esp_err_t stc3115_set_alarm_soc(stc3115_handle_t handle, uint8_t soc_percent)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(soc_percent <= 100, ESP_ERR_INVALID_ARG, TAG, "Invalid SOC percent");

    uint8_t alarm_val = soc_percent * 2;
    return stc3115_write_reg(handle, STC3115_REG_ALARM_SOC, alarm_val);
}

esp_err_t stc3115_set_alarm_voltage(stc3115_handle_t handle, uint16_t voltage_mv)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    uint8_t alarm_val = (uint8_t)(voltage_mv / 17.6f);
    return stc3115_write_reg(handle, STC3115_REG_ALARM_VOLTAGE, alarm_val);
}

esp_err_t stc3115_clear_alarms(stc3115_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return stc3115_write_reg(handle, STC3115_REG_CTRL, 0x00);
}

esp_err_t stc3115_read_id(stc3115_handle_t handle, uint8_t *id)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(id, ESP_ERR_INVALID_ARG, TAG, "Invalid ID pointer");

    return stc3115_read_reg(handle, STC3115_REG_ID, id);
}

esp_err_t stc3115_probe(stc3115_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    uint8_t id;
    esp_err_t ret = stc3115_read_reg(handle, STC3115_REG_ID, &id);
    if (ret != ESP_OK) {
        return ret;
    }

    if (id != STC3115_ID) {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

