#include "tca6408.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "TCA6408";

struct tca6408_dev_t {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t address;
    uint8_t output_val;
};

static esp_err_t tca6408_write_reg(tca6408_handle_t handle, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(handle->i2c_dev, buf, sizeof(buf), -1);
}

static esp_err_t tca6408_read_reg(tca6408_handle_t handle, uint8_t reg, uint8_t *val) {
    return i2c_master_transmit_receive(handle->i2c_dev, &reg, 1, val, 1, -1);
}

esp_err_t tca6408_init(i2c_master_bus_handle_t bus_handle, uint8_t address, tca6408_handle_t *ret_handle) {
    ESP_RETURN_ON_FALSE(bus_handle, ESP_ERR_INVALID_ARG, TAG, "Invalid bus handle");
    ESP_RETURN_ON_FALSE(ret_handle, ESP_ERR_INVALID_ARG, TAG, "Invalid return handle pointer");

    tca6408_handle_t dev = calloc(1, sizeof(struct tca6408_dev_t));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "No memory for device struct");

    dev->address = address;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000, // Standard speed
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev->i2c_dev);
    if (ret != ESP_OK) {
        free(dev);
        ESP_LOGE(TAG, "Failed to add device 0x%02x", address);
        return ret;
    }

    // Read initial output state
    tca6408_read_reg(dev, TCA6408_OUTPUT_REG, &dev->output_val);

    *ret_handle = dev;
    return ESP_OK;
}

esp_err_t tca6408_set_output_val(tca6408_handle_t handle, uint8_t val) {
    handle->output_val = val;
    return tca6408_write_reg(handle, TCA6408_OUTPUT_REG, val);
}

esp_err_t tca6408_set_output_pin(tca6408_handle_t handle, int pin, int level) {
    if (pin < 0 || pin > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    if (level) {
        handle->output_val |= (1 << pin);
    } else {
        handle->output_val &= ~(1 << pin);
    }
    return tca6408_write_reg(handle, TCA6408_OUTPUT_REG, handle->output_val);
}

esp_err_t tca6408_get_input_val(tca6408_handle_t handle, uint8_t *val) {
    return tca6408_read_reg(handle, TCA6408_INPUT_REG, val);
}

esp_err_t tca6408_set_config(tca6408_handle_t handle, uint8_t val) {
    return tca6408_write_reg(handle, TCA6408_CONFIG_REG, val);
}

esp_err_t tca6408_set_polarity(tca6408_handle_t handle, uint8_t val) {
    return tca6408_write_reg(handle, TCA6408_POLARITY_REG, val);
}
