#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/lock.h>
#include <sys/select.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "main.h"
#include "STC3115.h" 
#include "kxtj3_1057.h"
#include "rv3028.h"
#include "TPS65185.h"

#include "FastEPD.h"
#include "Roboto_Black_80.h"
#include "Roboto_Black_40.h"
#include "Courier_Prime_16.h"
#include "calendar.h"

static const char *TAG = "eInky-P4";

////////////////////////////////////////////
//////////// I2C EXPANDER //////////////////
////////////////////////////////////////////
#define I2C_MASTER_SCL_IO           29      /*!< GPIO number for I2C master clock */
#define I2C_MASTER_SDA_IO           28      /*!< GPIO number for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0 /*!< I2C master i2c port number */

i2c_master_bus_handle_t i2c_bus_handle = NULL;
tca6408_handle_t tca_board = NULL;
tca6408_handle_t tca_display = NULL;
stc3115_handle_t stc3115_handle = NULL;
kxtj3_handle_t kxtj3_handle = NULL;
rv3028_handle_t rv3028_handle = NULL;
tps65185_handle_t epd_pmic_handle = NULL;

FASTEPDSTATE bbep;

static esp_err_t i2c_master_init(void) {
    // Configure the I2C bus
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,           // Your existing I2C_NUM_0
        .sda_io_num = I2C_MASTER_SDA_IO,      // Your existing SDA pin
        .scl_io_num = I2C_MASTER_SCL_IO,      // Your existing SCL pin
        .clk_source = I2C_CLK_SRC_DEFAULT,    // Use default clock source
        .glitch_ignore_cnt = 100,             // Filter out glitches < 350ns
        .flags.enable_internal_pullup = true, // Equivalent to your pullup enables
    };

    // Initialize the bus and get handle
    esp_err_t err = i2c_new_master_bus(&bus_config, &i2c_bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "i2c_master_init ESP_OK");
    return ESP_OK;
}

/**
 * @brief Scan for I2C devices
 */
void i2c_scan_devices(void)
{
    uint8_t address, devices_found;
    devices_found = 0;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            esp_err_t ret = i2c_master_probe(i2c_bus_handle, address, -1);
            if (ret == ESP_OK) {
                printf("%02x ", address);
                devices_found++;
            } else if (ret == ESP_ERR_TIMEOUT) {
                printf("UU ");
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }
}


void app_main(void)
{
    i2c_master_init();

    ESP_LOGI(TAG, "tca6408_init...");

    if (tca6408_init(i2c_bus_handle, 0x21, &tca_board) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TCA6408 at 0x20");
    }

    tca6408_set_config(tca_board, 0x00); // All pins as output
    tca6408_set_output_val(tca_board, 0x00); // Set all pins low

    i2c_scan_devices();

    esp_err_t rc = bbepInitPanel(&bbep, BB_PANEL_EPDINKY_P4_16, 20000000);
    if (rc != BBEP_SUCCESS) {
        ESP_LOGE(TAG, "bbepInitPanel failed: %d", rc);
        return;
    }

    // Change for your panel size and orientation here if needed
    bbepSetPanelSize(&bbep, 2400, 1034, BB_PANEL_FLAG_MIRROR_Y, -1000); // ED113TC1
    // bbepSetPanelSize(&bbep, 1264, 1680, BB_PANEL_FLAG_NONE, -1400); // ED070KH1
    // bbepSetPanelSize(&bbep, 1200, 825, BB_PANEL_FLAG_NONE, -1620); // ED097TC1/2

    // Rotate 90° for landscape (swap width/height: 1680×1264)
    // Uncomment out for portrait/landscape if needed
    // bbepSetRotation(&bbep, 90);

    /* Initial full clear */
    bbepFillScreen(&bbep, BBEP_WHITE);
    bbepFullUpdate(&bbep, CLEAR_SLOW, 0, NULL);

    /* Run calendar demo for given week/month */
    calendar_demo_month(&bbep);   /* Full-screen month grid – May 2026  */
    // calendar_demo_week(&bbep);  /* ISO week planner  – Week 19 / 2026 */

}
