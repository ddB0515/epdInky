#include <stdlib.h>
#include <string.h>

#include "kxtj3_1057.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "KXTJ3";

struct kxtj3_dev_t {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t address;
    kxtj3_range_t range;
    bool high_res;
};

static esp_err_t kxtj3_write_reg(kxtj3_handle_t handle, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(handle->i2c_dev, buf, sizeof(buf), -1);
}

static esp_err_t kxtj3_read_reg(kxtj3_handle_t handle, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(handle->i2c_dev, &reg, 1, val, 1, -1);
}

static esp_err_t kxtj3_read_regs(kxtj3_handle_t handle, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(handle->i2c_dev, &reg, 1, data, len, -1);
}

static uint8_t kxtj3_odr_to_data_ctrl(uint16_t odr_hz)
{
    // Typical KXTJ3 DATA_CTRL mapping:
    // 0: 12.5Hz, 1: 25Hz, 2: 50Hz, 3: 100Hz, 4: 200Hz, 5: 400Hz, 6: 800Hz
    // Choose closest supported rate (simple clamp).
    if (odr_hz >= 800) return 0x06;
    if (odr_hz >= 400) return 0x05;
    if (odr_hz >= 200) return 0x04;
    if (odr_hz >= 100) return 0x03;
    if (odr_hz >= 50)  return 0x02;
    if (odr_hz >= 25)  return 0x01;
    return 0x00;
}

static uint8_t kxtj3_range_to_gsel(kxtj3_range_t range)
{
    switch (range) {
        case KXTJ3_RANGE_2G: return KXTJ3_CTRL1_GSEL_2G;
        case KXTJ3_RANGE_4G: return KXTJ3_CTRL1_GSEL_4G;
        case KXTJ3_RANGE_8G: return KXTJ3_CTRL1_GSEL_8G;
        default: return KXTJ3_CTRL1_GSEL_2G;
    }
}

esp_err_t kxtj3_configure(kxtj3_handle_t handle, kxtj3_range_t range, uint16_t odr_hz, bool high_res, bool data_ready_int)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Null handle");

    // Put device in standby (PC1=0) before changing settings
    esp_err_t ret = kxtj3_write_reg(handle, KXTJ3_REG_CTRL_REG1, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enter standby: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    // Configure output data rate
    uint8_t data_ctrl = kxtj3_odr_to_data_ctrl(odr_hz);
    ret = kxtj3_write_reg(handle, KXTJ3_REG_DATA_CTRL, data_ctrl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set ODR: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure CTRL_REG1 and start measurements (PC1=1)
    uint8_t ctrl1 = 0;
    if (high_res) {
        ctrl1 |= KXTJ3_CTRL1_RES;
    }
    if (data_ready_int) {
        ctrl1 |= KXTJ3_CTRL1_DRDYE;
    }
    ctrl1 |= kxtj3_range_to_gsel(range);
    ctrl1 |= KXTJ3_CTRL1_PC1;

    ret = kxtj3_write_reg(handle, KXTJ3_REG_CTRL_REG1, ctrl1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start (CTRL_REG1): %s", esp_err_to_name(ret));
        return ret;
    }

    handle->range = range;
    handle->high_res = high_res;

    return ESP_OK;
}

esp_err_t kxtj3_init(i2c_master_bus_handle_t bus_handle, uint8_t address, kxtj3_handle_t *ret_handle)
{
    ESP_RETURN_ON_FALSE(bus_handle, ESP_ERR_INVALID_ARG, TAG, "Invalid bus handle");
    ESP_RETURN_ON_FALSE(ret_handle, ESP_ERR_INVALID_ARG, TAG, "Invalid return handle");

    // Quick presence check to provide a clearer error than a raw NACK later
    esp_err_t probe_ret = i2c_master_probe(bus_handle, address, 100);
    if (probe_ret != ESP_OK) {
        ESP_LOGW(TAG, "No ACK from device at 0x%02x (probe failed: %s)", address, esp_err_to_name(probe_ret));
        return probe_ret;
    }

    kxtj3_handle_t dev = calloc(1, sizeof(struct kxtj3_dev_t));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "No memory for device struct");

    dev->address = address;
    dev->range = KXTJ3_RANGE_2G;
    dev->high_res = true;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev->i2c_dev);
    if (ret != ESP_OK) {
        free(dev);
        ESP_LOGE(TAG, "Failed to add I2C device 0x%02x: %s", address, esp_err_to_name(ret));
        return ret;
    }

    uint8_t who = 0;
    ret = kxtj3_read_reg(dev, KXTJ3_REG_WHO_AM_I, &who);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WHO_AM_I read failed: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(dev->i2c_dev);
        free(dev);
        return ret;
    }
    ESP_LOGI(TAG, "WHO_AM_I=0x%02x (expected 0x%02x)", who, KXTJ3_WHO_AM_I_VALUE);

    // Start with sane defaults
    ret = kxtj3_configure(dev, KXTJ3_RANGE_2G, 100, true, false);
    if (ret != ESP_OK) {
        i2c_master_bus_rm_device(dev->i2c_dev);
        free(dev);
        return ret;
    }

    *ret_handle = dev;
    return ESP_OK;
}

esp_err_t kxtj3_delete(kxtj3_handle_t handle, i2c_master_bus_handle_t bus_handle)
{
    if (!handle) {
        return ESP_OK;
    }

    // Best-effort standby
    (void)kxtj3_write_reg(handle, KXTJ3_REG_CTRL_REG1, 0x00);

    if (handle->i2c_dev) {
        i2c_master_bus_rm_device(handle->i2c_dev);
        handle->i2c_dev = NULL;
    }

    if (bus_handle) {
        // bus is owned by app; do not delete it here
    }

    free(handle);
    return ESP_OK;
}

esp_err_t kxtj3_read_raw(kxtj3_handle_t handle, int16_t *x, int16_t *y, int16_t *z)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Null handle");
    ESP_RETURN_ON_FALSE(x && y && z, ESP_ERR_INVALID_ARG, TAG, "Null output");

    uint8_t buf[6] = {0};
    esp_err_t ret = kxtj3_read_regs(handle, KXTJ3_REG_XOUT_L, buf, sizeof(buf));
    if (ret != ESP_OK) {
        return ret;
    }

    int16_t raw_x = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t raw_y = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t raw_z = (int16_t)((buf[5] << 8) | buf[4]);

    // KXTJ3 data is left-justified; right-shift depending on resolution.
    // High-res (14-bit): valid bits [15:2]
    // Low-res  (12-bit): valid bits [15:4]
    if (handle->high_res) {
        raw_x >>= 2;
        raw_y >>= 2;
        raw_z >>= 2;
    } else {
        raw_x >>= 4;
        raw_y >>= 4;
        raw_z >>= 4;
    }

    *x = raw_x;
    *y = raw_y;
    *z = raw_z;
    return ESP_OK;
}

esp_err_t kxtj3_raw_to_mg(kxtj3_handle_t handle, int16_t x, int16_t y, int16_t z, float *x_mg, float *y_mg, float *z_mg)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Null handle");
    ESP_RETURN_ON_FALSE(x_mg && y_mg && z_mg, ESP_ERR_INVALID_ARG, TAG, "Null output");

    // Typical sensitivities (counts per g) for KXTJ3 family:
    // 14-bit mode: 2g=16384, 4g=8192, 8g=4096 counts/g
    // 12-bit mode: 2g=4096,  4g=2048, 8g=1024 counts/g
    float counts_per_g = 0.0f;
    if (handle->high_res) {
        switch (handle->range) {
            case KXTJ3_RANGE_2G: counts_per_g = 16384.0f; break;
            case KXTJ3_RANGE_4G: counts_per_g = 8192.0f; break;
            case KXTJ3_RANGE_8G: counts_per_g = 4096.0f; break;
            default: counts_per_g = 16384.0f; break;
        }
    } else {
        switch (handle->range) {
            case KXTJ3_RANGE_2G: counts_per_g = 4096.0f; break;
            case KXTJ3_RANGE_4G: counts_per_g = 2048.0f; break;
            case KXTJ3_RANGE_8G: counts_per_g = 1024.0f; break;
            default: counts_per_g = 4096.0f; break;
        }
    }

    *x_mg = ((float)x / counts_per_g) * 1000.0f;
    *y_mg = ((float)y / counts_per_g) * 1000.0f;
    *z_mg = ((float)z / counts_per_g) * 1000.0f;

    return ESP_OK;
}
