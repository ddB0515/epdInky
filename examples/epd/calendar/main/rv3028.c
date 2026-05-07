#include "rv3028.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "RV3028";

struct rv3028_dev_t {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t address;
};

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static uint8_t bcd2bin(uint8_t val) {
    return (val & 0x0F) + (val >> 4) * 10;
}

static uint8_t bin2bcd(uint8_t val) {
    return ((val / 10) << 4) + (val % 10);
}

/* ============================================================================
 * Low-Level Register Access
 * ============================================================================ */

esp_err_t rv3028_read_reg(rv3028_handle_t handle, uint8_t reg, uint8_t *data, size_t len) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return i2c_master_transmit_receive(handle->i2c_dev, &reg, 1, data, len, -1);
}

esp_err_t rv3028_write_reg(rv3028_handle_t handle, uint8_t reg, const uint8_t *data, size_t len) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    
    uint8_t *buf = malloc(len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    
    esp_err_t ret = i2c_master_transmit(handle->i2c_dev, buf, len + 1, -1);
    free(buf);
    return ret;
}

static esp_err_t rv3028_write_reg_byte(rv3028_handle_t handle, uint8_t reg, uint8_t data) {
    return rv3028_write_reg(handle, reg, &data, 1);
}

static esp_err_t rv3028_read_reg_byte(rv3028_handle_t handle, uint8_t reg, uint8_t *data) {
    return rv3028_read_reg(handle, reg, data, 1);
}

static esp_err_t rv3028_update_reg(rv3028_handle_t handle, uint8_t reg, uint8_t mask, uint8_t val) {
    uint8_t data;
    esp_err_t ret = rv3028_read_reg_byte(handle, reg, &data);
    if (ret != ESP_OK) return ret;
    
    data = (data & ~mask) | (val & mask);
    return rv3028_write_reg_byte(handle, reg, data);
}

/* ============================================================================
 * Core Functions
 * ============================================================================ */

esp_err_t rv3028_init(i2c_master_bus_handle_t bus_handle, uint8_t address, rv3028_handle_t *ret_handle) {
    ESP_RETURN_ON_FALSE(bus_handle, ESP_ERR_INVALID_ARG, TAG, "Invalid bus handle");
    ESP_RETURN_ON_FALSE(ret_handle, ESP_ERR_INVALID_ARG, TAG, "Invalid return handle pointer");

    rv3028_handle_t dev = calloc(1, sizeof(struct rv3028_dev_t));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "No memory for device struct");

    dev->address = address;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev->i2c_dev);
    if (ret != ESP_OK) {
        free(dev);
        ESP_LOGE(TAG, "Failed to add device 0x%02x", address);
        return ret;
    }

    // Verify chip ID
    uint8_t id;
    ret = rv3028_get_id(dev, &id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read chip ID, device may not be connected");
    } else {
        ESP_LOGI(TAG, "RV-3028-C7 detected, ID: 0x%02x", id);
    }

    *ret_handle = dev;
    return ESP_OK;
}

esp_err_t rv3028_deinit(rv3028_handle_t handle) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    
    esp_err_t ret = i2c_master_bus_rm_device(handle->i2c_dev);
    free(handle);
    return ret;
}

esp_err_t rv3028_reset(rv3028_handle_t handle) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    
    esp_err_t ret = rv3028_update_reg(handle, RV3028_REG_CONTROL2, RV3028_CTRL2_RESET, RV3028_CTRL2_RESET);
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(10)); // Wait for reset to complete
    }
    return ret;
}

esp_err_t rv3028_get_id(rv3028_handle_t handle, uint8_t *id) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(id, ESP_ERR_INVALID_ARG, TAG, "Invalid ID pointer");
    
    return rv3028_read_reg_byte(handle, RV3028_REG_ID, id);
}

/* ============================================================================
 * Time Functions
 * ============================================================================ */

esp_err_t rv3028_get_time(rv3028_handle_t handle, rv3028_time_t *time) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(time, ESP_ERR_INVALID_ARG, TAG, "Invalid time pointer");

    uint8_t buf[7];
    esp_err_t ret = rv3028_read_reg(handle, RV3028_REG_SECONDS, buf, 7);
    if (ret != ESP_OK) return ret;

    time->seconds = bcd2bin(buf[0] & 0x7F);
    time->minutes = bcd2bin(buf[1] & 0x7F);
    time->hours = bcd2bin(buf[2] & 0x3F);
    time->weekday = bcd2bin(buf[3] & 0x07);
    time->date = bcd2bin(buf[4] & 0x3F);
    time->month = bcd2bin(buf[5] & 0x1F);
    time->year = 2000 + bcd2bin(buf[6]);

    return ESP_OK;
}

esp_err_t rv3028_set_time(rv3028_handle_t handle, const rv3028_time_t *time) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(time, ESP_ERR_INVALID_ARG, TAG, "Invalid time pointer");

    // Validate time values
    ESP_RETURN_ON_FALSE(time->seconds < 60, ESP_ERR_INVALID_ARG, TAG, "Invalid seconds");
    ESP_RETURN_ON_FALSE(time->minutes < 60, ESP_ERR_INVALID_ARG, TAG, "Invalid minutes");
    ESP_RETURN_ON_FALSE(time->hours < 24, ESP_ERR_INVALID_ARG, TAG, "Invalid hours");
    ESP_RETURN_ON_FALSE(time->weekday < 7, ESP_ERR_INVALID_ARG, TAG, "Invalid weekday");
    ESP_RETURN_ON_FALSE(time->date >= 1 && time->date <= 31, ESP_ERR_INVALID_ARG, TAG, "Invalid date");
    ESP_RETURN_ON_FALSE(time->month >= 1 && time->month <= 12, ESP_ERR_INVALID_ARG, TAG, "Invalid month");
    ESP_RETURN_ON_FALSE(time->year >= 2000 && time->year <= 2099, ESP_ERR_INVALID_ARG, TAG, "Invalid year");

    uint8_t buf[7];
    buf[0] = bin2bcd(time->seconds);
    buf[1] = bin2bcd(time->minutes);
    buf[2] = bin2bcd(time->hours);
    buf[3] = bin2bcd(time->weekday);
    buf[4] = bin2bcd(time->date);
    buf[5] = bin2bcd(time->month);
    buf[6] = bin2bcd(time->year - 2000);

    return rv3028_write_reg(handle, RV3028_REG_SECONDS, buf, 7);
}

esp_err_t rv3028_get_unix_time(rv3028_handle_t handle, uint32_t *unix_time) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(unix_time, ESP_ERR_INVALID_ARG, TAG, "Invalid unix_time pointer");

    uint8_t buf[4];
    esp_err_t ret = rv3028_read_reg(handle, RV3028_REG_UNIX_TIME_0, buf, 4);
    if (ret != ESP_OK) return ret;

    *unix_time = ((uint32_t)buf[3] << 24) | ((uint32_t)buf[2] << 16) | 
                 ((uint32_t)buf[1] << 8) | buf[0];

    return ESP_OK;
}

esp_err_t rv3028_set_unix_time(rv3028_handle_t handle, uint32_t unix_time) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    uint8_t buf[4];
    buf[0] = unix_time & 0xFF;
    buf[1] = (unix_time >> 8) & 0xFF;
    buf[2] = (unix_time >> 16) & 0xFF;
    buf[3] = (unix_time >> 24) & 0xFF;

    return rv3028_write_reg(handle, RV3028_REG_UNIX_TIME_0, buf, 4);
}

/* ============================================================================
 * Alarm Functions
 * ============================================================================ */

esp_err_t rv3028_set_alarm(rv3028_handle_t handle, const rv3028_alarm_t *alarm) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(alarm, ESP_ERR_INVALID_ARG, TAG, "Invalid alarm pointer");

    uint8_t buf[3];
    buf[0] = (alarm->minutes > 59) ? 0x80 : bin2bcd(alarm->minutes);
    buf[1] = (alarm->hours > 23) ? 0x80 : bin2bcd(alarm->hours);
    buf[2] = (alarm->weekday_date > 31) ? 0x80 : bin2bcd(alarm->weekday_date);

    // Set weekday/date alarm mode
    esp_err_t ret = rv3028_update_reg(handle, RV3028_REG_CONTROL1, RV3028_CTRL1_WADA, 
                                       alarm->use_date_mode ? RV3028_CTRL1_WADA : 0);
    if (ret != ESP_OK) return ret;

    return rv3028_write_reg(handle, RV3028_REG_MINUTES_ALARM, buf, 3);
}

esp_err_t rv3028_get_alarm(rv3028_handle_t handle, rv3028_alarm_t *alarm) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(alarm, ESP_ERR_INVALID_ARG, TAG, "Invalid alarm pointer");

    uint8_t buf[3];
    esp_err_t ret = rv3028_read_reg(handle, RV3028_REG_MINUTES_ALARM, buf, 3);
    if (ret != ESP_OK) return ret;

    alarm->minutes = (buf[0] & 0x80) ? 0xFF : bcd2bin(buf[0] & 0x7F);
    alarm->hours = (buf[1] & 0x80) ? 0xFF : bcd2bin(buf[1] & 0x3F);
    alarm->weekday_date = (buf[2] & 0x80) ? 0xFF : bcd2bin(buf[2] & 0x3F);

    uint8_t ctrl1;
    ret = rv3028_read_reg_byte(handle, RV3028_REG_CONTROL1, &ctrl1);
    if (ret != ESP_OK) return ret;

    alarm->use_date_mode = (ctrl1 & RV3028_CTRL1_WADA) ? true : false;

    return ESP_OK;
}

esp_err_t rv3028_enable_alarm_interrupt(rv3028_handle_t handle, bool enable) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return rv3028_update_reg(handle, RV3028_REG_CONTROL2, RV3028_CTRL2_AIE, 
                             enable ? RV3028_CTRL2_AIE : 0);
}

esp_err_t rv3028_get_alarm_flag(rv3028_handle_t handle, bool *flag) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(flag, ESP_ERR_INVALID_ARG, TAG, "Invalid flag pointer");

    uint8_t status;
    esp_err_t ret = rv3028_read_reg_byte(handle, RV3028_REG_STATUS, &status);
    if (ret != ESP_OK) return ret;

    *flag = (status & RV3028_STATUS_AF) ? true : false;
    return ESP_OK;
}

esp_err_t rv3028_clear_alarm_flag(rv3028_handle_t handle) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return rv3028_update_reg(handle, RV3028_REG_STATUS, RV3028_STATUS_AF, 0);
}

/* ============================================================================
 * Timer Functions
 * ============================================================================ */

esp_err_t rv3028_set_timer(rv3028_handle_t handle, uint16_t value, rv3028_timer_freq_t freq, bool repeat) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(value <= 4095, ESP_ERR_INVALID_ARG, TAG, "Timer value out of range");

    // Disable timer first
    esp_err_t ret = rv3028_enable_timer(handle, false);
    if (ret != ESP_OK) return ret;

    // Set timer value (12-bit)
    uint8_t timer_buf[2];
    timer_buf[0] = value & 0xFF;
    timer_buf[1] = (value >> 8) & 0x0F;
    ret = rv3028_write_reg(handle, RV3028_REG_TIMER_VALUE_0, timer_buf, 2);
    if (ret != ESP_OK) return ret;

    // Configure timer frequency and repeat
    uint8_t ctrl1_val = (freq & RV3028_CTRL1_TD_MASK);
    if (repeat) ctrl1_val |= RV3028_CTRL1_TRPT;
    
    return rv3028_update_reg(handle, RV3028_REG_CONTROL1, 
                             RV3028_CTRL1_TRPT | RV3028_CTRL1_TD_MASK, ctrl1_val);
}

esp_err_t rv3028_enable_timer(rv3028_handle_t handle, bool enable) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return rv3028_update_reg(handle, RV3028_REG_CONTROL1, RV3028_CTRL1_TE, 
                             enable ? RV3028_CTRL1_TE : 0);
}

esp_err_t rv3028_enable_timer_interrupt(rv3028_handle_t handle, bool enable) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return rv3028_update_reg(handle, RV3028_REG_CONTROL2, RV3028_CTRL2_TIE, 
                             enable ? RV3028_CTRL2_TIE : 0);
}

esp_err_t rv3028_get_timer_flag(rv3028_handle_t handle, bool *flag) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(flag, ESP_ERR_INVALID_ARG, TAG, "Invalid flag pointer");

    uint8_t status;
    esp_err_t ret = rv3028_read_reg_byte(handle, RV3028_REG_STATUS, &status);
    if (ret != ESP_OK) return ret;

    *flag = (status & RV3028_STATUS_TF) ? true : false;
    return ESP_OK;
}

esp_err_t rv3028_clear_timer_flag(rv3028_handle_t handle) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return rv3028_update_reg(handle, RV3028_REG_STATUS, RV3028_STATUS_TF, 0);
}

/* ============================================================================
 * Timestamp Functions
 * ============================================================================ */

esp_err_t rv3028_enable_timestamp(rv3028_handle_t handle, bool enable) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return rv3028_update_reg(handle, RV3028_REG_CONTROL2, RV3028_CTRL2_TSE, 
                             enable ? RV3028_CTRL2_TSE : 0);
}

esp_err_t rv3028_get_timestamp(rv3028_handle_t handle, rv3028_timestamp_t *ts) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(ts, ESP_ERR_INVALID_ARG, TAG, "Invalid timestamp pointer");

    uint8_t buf[7];
    esp_err_t ret = rv3028_read_reg(handle, RV3028_REG_TS_COUNT, buf, 7);
    if (ret != ESP_OK) return ret;

    ts->count = buf[0];
    ts->seconds = bcd2bin(buf[1] & 0x7F);
    ts->minutes = bcd2bin(buf[2] & 0x7F);
    ts->hours = bcd2bin(buf[3] & 0x3F);
    ts->date = bcd2bin(buf[4] & 0x3F);
    ts->month = bcd2bin(buf[5] & 0x1F);
    ts->year = 2000 + bcd2bin(buf[6]);

    return ESP_OK;
}

esp_err_t rv3028_reset_timestamp(rv3028_handle_t handle) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return rv3028_update_reg(handle, RV3028_REG_EVENT_CONTROL, RV3028_EVENT_TSR, RV3028_EVENT_TSR);
}

/* ============================================================================
 * Backup/Trickle Charge Functions
 * ============================================================================ */

esp_err_t rv3028_eeprom_wait_busy(rv3028_handle_t handle, uint32_t timeout_ms) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    
    uint32_t start = xTaskGetTickCount();
    uint8_t status;
    
    do {
        esp_err_t ret = rv3028_read_reg_byte(handle, RV3028_REG_STATUS, &status);
        if (ret != ESP_OK) return ret;
        
        if (!(status & RV3028_STATUS_EEBUSY)) {
            return ESP_OK;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    } while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms));
    
    return ESP_ERR_TIMEOUT;
}

static esp_err_t rv3028_eeprom_command(rv3028_handle_t handle, uint8_t cmd) {
    // Disable auto-refresh during EEPROM operation
    esp_err_t ret = rv3028_update_reg(handle, RV3028_REG_CONTROL1, RV3028_CTRL1_EERD, RV3028_CTRL1_EERD);
    if (ret != ESP_OK) return ret;

    // Wait for any previous operation
    ret = rv3028_eeprom_wait_busy(handle, 100);
    if (ret != ESP_OK) {
        rv3028_update_reg(handle, RV3028_REG_CONTROL1, RV3028_CTRL1_EERD, 0);
        return ret;
    }

    // Send command
    ret = rv3028_write_reg_byte(handle, RV3028_REG_EEPROM_CMD, cmd);
    if (ret != ESP_OK) {
        rv3028_update_reg(handle, RV3028_REG_CONTROL1, RV3028_CTRL1_EERD, 0);
        return ret;
    }

    // Wait for completion
    ret = rv3028_eeprom_wait_busy(handle, 100);
    
    // Re-enable auto-refresh
    rv3028_update_reg(handle, RV3028_REG_CONTROL1, RV3028_CTRL1_EERD, 0);
    
    return ret;
}

esp_err_t rv3028_set_backup_mode(rv3028_handle_t handle, rv3028_backup_mode_t mode) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    
    // Read current EEPROM backup register (mirror at 0x33)
    uint8_t backup;
    esp_err_t ret = rv3028_read_reg_byte(handle, RV3028_REG_EEPROM_BACKUP, &backup);
    if (ret != ESP_OK) return ret;

    // Update backup switchover mode
    backup = (backup & ~RV3028_BACKUP_BSM_MASK) | ((mode & 0x03) << 2);
    
    // Write to RAM mirror
    ret = rv3028_write_reg_byte(handle, RV3028_REG_EEPROM_BACKUP, backup);
    if (ret != ESP_OK) return ret;

    // Update EEPROM
    return rv3028_eeprom_command(handle, RV3028_EEPROM_CMD_UPDATE);
}

esp_err_t rv3028_set_trickle_charge(rv3028_handle_t handle, bool enable, rv3028_trickle_resistor_t resistor) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    
    // Read current EEPROM backup register (mirror at 0x33)
    uint8_t backup;
    esp_err_t ret = rv3028_read_reg_byte(handle, RV3028_REG_EEPROM_BACKUP, &backup);
    if (ret != ESP_OK) return ret;

    // Update trickle charge settings
    backup &= ~(RV3028_BACKUP_TCE | RV3028_BACKUP_TCR_MASK);
    if (enable) {
        backup |= RV3028_BACKUP_TCE | (resistor & RV3028_BACKUP_TCR_MASK);
    }
    
    // Write to RAM mirror
    ret = rv3028_write_reg_byte(handle, RV3028_REG_EEPROM_BACKUP, backup);
    if (ret != ESP_OK) return ret;

    // Update EEPROM
    return rv3028_eeprom_command(handle, RV3028_EEPROM_CMD_UPDATE);
}

esp_err_t rv3028_get_backup_flag(rv3028_handle_t handle, bool *flag) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(flag, ESP_ERR_INVALID_ARG, TAG, "Invalid flag pointer");

    uint8_t status;
    esp_err_t ret = rv3028_read_reg_byte(handle, RV3028_REG_STATUS, &status);
    if (ret != ESP_OK) return ret;

    *flag = (status & RV3028_STATUS_BSF) ? true : false;
    return ESP_OK;
}

esp_err_t rv3028_clear_backup_flag(rv3028_handle_t handle) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return rv3028_update_reg(handle, RV3028_REG_STATUS, RV3028_STATUS_BSF, 0);
}

/* ============================================================================
 * CLKOUT Functions
 * ============================================================================ */

esp_err_t rv3028_set_clkout(rv3028_handle_t handle, bool enable, rv3028_clkout_freq_t freq) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    
    // Read current CLKOUT register (mirror at 0x32)
    uint8_t clkout;
    esp_err_t ret = rv3028_read_reg_byte(handle, RV3028_REG_EEPROM_CLKOUT, &clkout);
    if (ret != ESP_OK) return ret;

    // Update settings
    clkout &= ~(RV3028_CLKOUT_CLKOE | RV3028_CLKOUT_FD_MASK);
    if (enable) {
        clkout |= RV3028_CLKOUT_CLKOE;
    }
    clkout |= (freq & RV3028_CLKOUT_FD_MASK);
    
    // Write to RAM mirror
    ret = rv3028_write_reg_byte(handle, RV3028_REG_EEPROM_CLKOUT, clkout);
    if (ret != ESP_OK) return ret;

    // Update EEPROM
    return rv3028_eeprom_command(handle, RV3028_EEPROM_CMD_UPDATE);
}

/* ============================================================================
 * EEPROM Functions
 * ============================================================================ */

esp_err_t rv3028_eeprom_read(rv3028_handle_t handle, uint8_t addr, uint8_t *data) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(data, ESP_ERR_INVALID_ARG, TAG, "Invalid data pointer");
    ESP_RETURN_ON_FALSE(addr <= RV3028_EEPROM_USER_END, ESP_ERR_INVALID_ARG, TAG, "Invalid EEPROM address");

    // Set EEPROM address
    esp_err_t ret = rv3028_write_reg_byte(handle, RV3028_REG_EEPROM_ADDR, addr);
    if (ret != ESP_OK) return ret;

    // Execute read command
    ret = rv3028_eeprom_command(handle, RV3028_EEPROM_CMD_READ);
    if (ret != ESP_OK) return ret;

    // Read data
    return rv3028_read_reg_byte(handle, RV3028_REG_EEPROM_DATA, data);
}

esp_err_t rv3028_eeprom_write(rv3028_handle_t handle, uint8_t addr, uint8_t data) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(addr <= RV3028_EEPROM_USER_END, ESP_ERR_INVALID_ARG, TAG, "Invalid EEPROM address");

    // Set EEPROM address
    esp_err_t ret = rv3028_write_reg_byte(handle, RV3028_REG_EEPROM_ADDR, addr);
    if (ret != ESP_OK) return ret;

    // Set data to write
    ret = rv3028_write_reg_byte(handle, RV3028_REG_EEPROM_DATA, data);
    if (ret != ESP_OK) return ret;

    // Execute write command
    return rv3028_eeprom_command(handle, RV3028_EEPROM_CMD_WRITE);
}

/* ============================================================================
 * Status Functions
 * ============================================================================ */

esp_err_t rv3028_get_status(rv3028_handle_t handle, uint8_t *status) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(status, ESP_ERR_INVALID_ARG, TAG, "Invalid status pointer");
    
    return rv3028_read_reg_byte(handle, RV3028_REG_STATUS, status);
}

esp_err_t rv3028_get_por_flag(rv3028_handle_t handle, bool *flag) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(flag, ESP_ERR_INVALID_ARG, TAG, "Invalid flag pointer");

    uint8_t status;
    esp_err_t ret = rv3028_read_reg_byte(handle, RV3028_REG_STATUS, &status);
    if (ret != ESP_OK) return ret;

    *flag = (status & RV3028_STATUS_PORF) ? true : false;
    return ESP_OK;
}

esp_err_t rv3028_clear_por_flag(rv3028_handle_t handle) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return rv3028_update_reg(handle, RV3028_REG_STATUS, RV3028_STATUS_PORF, 0);
}

esp_err_t rv3028_clear_all_flags(rv3028_handle_t handle) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return rv3028_write_reg_byte(handle, RV3028_REG_STATUS, 0x00);
}
