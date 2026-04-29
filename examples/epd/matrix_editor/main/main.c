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
#include "esp_wifi.h"
#include "web_server.h"
#include "epd_functions.h"

static const char *TAG = "eInky-P4";

static void wifi_monitor_task(void *arg);
static volatile bool s_wifi_disabled = false;

////////////////////////////////////////////
//////////// I2C EXPANDER //////////////////
////////////////////////////////////////////
#define I2C_MASTER_SCL_IO           29      /*!< GPIO number for I2C master clock */
#define I2C_MASTER_SDA_IO           28      /*!< GPIO number for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0 /*!< I2C master i2c port number */
#define I2C_MASTER_FREQ_HZ          400000 /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0      /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0      /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000


i2c_master_bus_handle_t i2c_bus_handle = NULL;
tca6408_handle_t tca_board = NULL;
tca6408_handle_t tca_display = NULL;
stc3115_handle_t stc3115_handle = NULL;
kxtj3_handle_t kxtj3_handle = NULL;
rv3028_handle_t rv3028_handle = NULL;

tps65185_handle_t epd_pmic_handle = NULL;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

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

void gpio_init(void) {
    // 1. Configure GPIO 26 as Output
    gpio_config_t io_conf_output = {};
    io_conf_output.pin_bit_mask = (1ULL << GPIO_NUM_26); // Select GPIO 26
    io_conf_output.mode = GPIO_MODE_OUTPUT;              // Set as output
    io_conf_output.intr_type = GPIO_INTR_DISABLE;        // Disable interrupt
    io_conf_output.pull_down_en = 0;                     // Disable pull-down
    io_conf_output.pull_up_en = 0;                       // Disable pull-up
    gpio_config(&io_conf_output);

    gpio_config_t io_conf_output2 = {};
    io_conf_output2.pin_bit_mask = (1ULL << GPIO_NUM_37); // Select GPIO 37
    io_conf_output2.mode = GPIO_MODE_OUTPUT;              // Set as output
    io_conf_output2.intr_type = GPIO_INTR_DISABLE;        // Disable interrupt
    io_conf_output2.pull_down_en = 0;                     // Disable pull-down
    io_conf_output2.pull_up_en = 0;                       // Disable pull-up
    gpio_config(&io_conf_output2);

    gpio_config_t io_conf_output3 = {};
    io_conf_output3.pin_bit_mask = (1ULL << GPIO_NUM_49); // Select GPIO 49
    io_conf_output3.mode = GPIO_MODE_OUTPUT;              // Set as output
    io_conf_output3.intr_type = GPIO_INTR_DISABLE;        // Disable interrupt
    io_conf_output3.pull_down_en = 0;                     // Disable pull-down
    io_conf_output3.pull_up_en = 0;                       // Disable pull-up
    gpio_config(&io_conf_output3);

    gpio_config_t io_conf_output4 = {};
    io_conf_output4.pin_bit_mask = (1ULL << GPIO_NUM_54); // Select GPIO 54 C6 EN
    io_conf_output4.mode = GPIO_MODE_OUTPUT;              // Set as output
    io_conf_output4.intr_type = GPIO_INTR_DISABLE;        // Disable interrupt
    io_conf_output4.pull_down_en = 0;                     // Disable pull-down
    io_conf_output4.pull_up_en = 0;                       // Disable pull-up
    gpio_config(&io_conf_output4);

    // 2. Configure GPIO 27 as Input
    gpio_config_t io_conf_input = {};
    io_conf_input.pin_bit_mask = (1ULL << GPIO_NUM_27);  // Select GPIO 27
    io_conf_input.mode = GPIO_MODE_INPUT;                // Set as input
    io_conf_input.intr_type = GPIO_INTR_DISABLE;         // Disable interrupt
    // Choose pull mode (optional, but often needed for inputs):
    io_conf_input.pull_up_en = 1;    // Enable internal pull-up resistor
    io_conf_input.pull_down_en = 0;  // Disable pull-down
    gpio_config(&io_conf_input);
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
    gpio_init();
    gpio_set_level(GPIO_NUM_26, 1); // Set HIGH
    gpio_set_level(GPIO_NUM_37, 1); // Set HIGH
    gpio_set_level(GPIO_NUM_49, 1); // Set HIGH
    gpio_set_level(GPIO_NUM_54, 0); // Set LOW

    i2c_master_init();

    ESP_LOGI(TAG, "tca6408_init...");

    if (tca6408_init(i2c_bus_handle, 0x21, &tca_board) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TCA6408 at 0x20");
    }

    tca6408_set_config(tca_board, 0x00); // All pins as output
    tca6408_set_output_val(tca_board, 0x00); // Set all pins low

    i2c_scan_devices();
  
    if (epd_init() != ESP_OK) {
        ESP_LOGE(TAG, "EPD init failed");
    }

    // mount the sd card at the sdmmc slot and mount point
    ESP_ERROR_CHECK(sd_card_mount(CONFIG_EXAMPLE_SDMMC_SLOT, MOUNT_POINT));
    // First create a file.
    const char *file_hello = MOUNT_POINT"/hello.txt";
    char data[EXAMPLE_MAX_CHAR_SIZE];
    snprintf(data, EXAMPLE_MAX_CHAR_SIZE, "%s %s!\n", "Hello", sd_card_get_card_name());
    esp_err_t ret = sd_card_write_file(file_hello, data);
    if (ret != ESP_OK) {
        return;
    }

    init_wifi();
    ESP_LOGI(TAG, "Doing Wi-Fi Scan");
    do_wifi_scan();

    ESP_LOGI(TAG, "Connecting to WiFi");
    wifi_connect_sta(EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    
    // wifi_init_sntp();

    if (web_server_start() != ESP_OK) {
        ESP_LOGE(TAG, "Web server failed to start");
    }

    //xTaskCreate(wifi_monitor_task, "wifi_monitor", 2048, NULL, 1, NULL);
}

void wifi_monitor_set_disabled(bool disabled)
{
    s_wifi_disabled = disabled;
}

static void wifi_monitor_task(void *arg)
{
    bool last_disabled = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        if (s_wifi_disabled != last_disabled) {
            last_disabled = s_wifi_disabled;
            gpio_set_level(GPIO_NUM_54, s_wifi_disabled ? 0 : 1);
            ESP_LOGI(TAG, "WiFi module %s (GPIO54=%d)", s_wifi_disabled ? "disabled" : "enabled", s_wifi_disabled ? 0 : 1);
        }

        if (s_wifi_disabled) {
            ESP_LOGI(TAG, "WiFi disabled – skipping status check");
            continue;
        }

        wifi_ap_record_t ap_info;
        esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "WiFi connected: SSID=%s RSSI=%d", ap_info.ssid, ap_info.rssi);
            wifi_monitor_set_disabled(true); // Disable WiFi after successful connection for demonstration
        } else {
            ESP_LOGW(TAG, "WiFi not connected (%s)", esp_err_to_name(ret));
        }
    }
}
