#include "SGM37604A.h"
#include "esp_log.h"

static const char *TAG = "SGM37604A";
static i2c_master_dev_handle_t dev_handle = NULL;

// Default register values
unsigned char backlight_led_reg_en = 0x1f;
unsigned char backlight_mode_reg = 0x05; // 0x65;
unsigned char backlight_led_reg = 0x00; // 0x06;
unsigned char backlight_current_reg = 0x66; //0xeb;
unsigned char backlight_current_max = 0x01; // 0x00-25mA, 0x01-30mA, 0x02-35mA, 0x03-40mA

esp_err_t sgm37604a_i2c_init(i2c_master_bus_handle_t bus_handle) {
    
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SGM37604A_SLAVE,
        .scl_speed_hz = SGM37604A_CLK_SPEED,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

static esp_err_t sgm37604a_write_register(uint8_t reg, uint8_t value) {
    uint8_t write_buf[2] = {reg, value};
    return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), -1);
}

void sgm37604a_init(void) {
    sgm37604a_write_register(SGM37604A_CTL_BACKLIGHT_LED_REG, backlight_led_reg_en);    
    sgm37604a_write_register(SGM37604A_CTL_BACKLIGHT_MODE_REG, backlight_mode_reg);    
    sgm37604a_write_register(SGM37604A_CTL_BRIGHTNESS_LSB_REG, backlight_led_reg);    
    sgm37604a_write_register(SGM37604A_CTL_BRIGHTNESS_MSB_REG, backlight_current_reg);    
    sgm37604a_write_register(SGM37604A_CTL_BACKLIGHT_CURRENT_REG, backlight_current_max);    
}

void sgm37604a_set_brightness_level(unsigned int level) {
    unsigned int level_a = MIN_MAX_SCALE(level);
    uint8_t data0 = (level_a & 0x0F);
    uint8_t data1 = level_a;
    
    sgm37604a_write_register(SGM37604A_CTL_BRIGHTNESS_LSB_REG, data0);    
    sgm37604a_write_register(SGM37604A_CTL_BRIGHTNESS_MSB_REG, data1);  
}

void sgm37604a_backlight(bool enabled) {
    sgm37604a_write_register(SGM37604A_CTL_BACKLIGHT_LED_REG, 
                            enabled ? backlight_led_reg_en : 0); 
}

void sgm37604a_deinit(i2c_master_bus_handle_t bus_handle) {
    if (dev_handle) {
        i2c_master_bus_rm_device(dev_handle);
        dev_handle = NULL;
    }
    if (bus_handle) {
        i2c_del_master_bus(bus_handle);
    }
}