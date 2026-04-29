#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/lock.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "driver/ppa.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "lvgl.h"
#include "main.h" 

static const char *TAG = "LCD-mipi";

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD Spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Refresh Rate = 33670000/(480+20+30+30)/(960+2+20+20) = 60.00Hz
#define EXAMPLE_MIPI_DSI_DPI_CLK_MHZ  60
#define EXAMPLE_MIPI_DSI_LCD_H_RES    540
#define EXAMPLE_MIPI_DSI_LCD_V_RES    960
#define EXAMPLE_MIPI_DSI_LCD_HSYNC    4
#define EXAMPLE_MIPI_DSI_LCD_HBP      40
#define EXAMPLE_MIPI_DSI_LCD_HFP      364
#define EXAMPLE_MIPI_DSI_LCD_VSYNC    2
#define EXAMPLE_MIPI_DSI_LCD_VBP      13
#define EXAMPLE_MIPI_DSI_LCD_VFP      244

// The "VDD_MIPI_DPHY" should be supplied with 2.5V, it can source from the internal LDO regulator or from external LDO chip
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN       3  // LDO_VO3 is connected to VDD_MIPI_DPHY
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL           true
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL          !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_BK_LIGHT                -1
#define EXAMPLE_PIN_NUM_LCD_RST                 -1


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your Application ///////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define EXAMPLE_LVGL_DRAW_BUF_LINES    (EXAMPLE_MIPI_DSI_LCD_V_RES / 10) // number of display lines in each draw buffer
#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (4 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     2

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

#define TPS65132_ADDR 0x3E
#define TPS65132_CLK_SPEED 400000

#define MOUNT_POINT "/sdcard"
static const char *file_hello = MOUNT_POINT"/stc3115.txt";

i2c_master_bus_handle_t i2c_bus_handle = NULL;
i2c_master_dev_handle_t tps65132_dev_handle = NULL;

tca6408_handle_t tca_board = NULL;
tca6408_handle_t tca_display = NULL;
stc3115_handle_t stc3115_dev = NULL;

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

// LVGL library is not thread-safe, this example will call LVGL APIs from different tasks, so use a mutex to protect it
static _lock_t lvgl_api_lock;

extern void example_lvgl_demo_ui(lv_display_t *disp);

static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // pass the draw buffer to the driver
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);

        // in case of task watch dog timeout, set the minimal delay to 10ms
        if (time_till_next_ms < 10) {
            time_till_next_ms = 10;
        }
        usleep(1000 * time_till_next_ms);
    }
}

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void example_bsp_enable_dsi_phy_power(void)
{
    // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to "Shutdown" state
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
#ifdef EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif
}

static void example_bsp_init_lcd_backlight(void) {
    // Enable backlight 1st
    tca6408_set_output_pin(tca_display, 3, 1); // Set pin 3 high // PWM_EN
    // Initialize the backlight driver
    sgm37604a_init();
}

static void example_bsp_set_lcd_backlight(uint32_t level) {
    sgm37604a_set_brightness_level(64);
    sgm37604a_backlight(true);
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

    gpio_config_t io_conf_input_tp_int = {};
    io_conf_input_tp_int.pin_bit_mask = (1ULL << GPIO_NUM_30); // Select GPIO 26
    io_conf_input_tp_int.mode = GPIO_MODE_OUTPUT;              // Set as output
    io_conf_input_tp_int.intr_type = GPIO_INTR_DISABLE;        // Disable interrupt
    io_conf_input_tp_int.pull_down_en = 0;                     // Disable pull-down
    io_conf_input_tp_int.pull_up_en = 0;                       // Disable pull-up
    gpio_config(&io_conf_input_tp_int);

    gpio_config_t io_conf_output_tp_res = {};
    io_conf_output_tp_res.pin_bit_mask = (1ULL << GPIO_NUM_31); // Select GPIO 26
    io_conf_output_tp_res.mode = GPIO_MODE_OUTPUT;              // Set as output
    io_conf_output_tp_res.intr_type = GPIO_INTR_DISABLE;        // Disable interrupt
    io_conf_output_tp_res.pull_down_en = 0;                     // Disable pull-down
    io_conf_output_tp_res.pull_up_en = 0;                       // Disable pull-up
    gpio_config(&io_conf_output_tp_res);

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

esp_err_t tps65132_i2c_init(i2c_master_bus_handle_t bus_handle) {
    
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TPS65132_ADDR,
        .scl_speed_hz = TPS65132_CLK_SPEED,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &tps65132_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

static esp_err_t tps65132_write_register(uint8_t reg, uint8_t value) {
    uint8_t write_buf[2] = {reg, value};
    return i2c_master_transmit(tps65132_dev_handle, write_buf, sizeof(write_buf), -1);
}


// Task function
void brightness_task(void *pvParameters) {
    int value = 10;
    while (1) {
        printf("Brightness set to: %d\n", value);
        sgm37604a_set_brightness_level(value);

        value+=10;
        if (value > 255) {
            value = 0; // loop back to 0
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // wait 
    }
}

void app_main(void)
{
    gpio_init();
    gpio_set_level(GPIO_NUM_26, 1); // Set HIGH
    gpio_set_level(GPIO_NUM_37, 1); // Set HIGH
    gpio_set_level(GPIO_NUM_49, 1); // Set HIGH
    gpio_set_level(GPIO_NUM_31, 1); // Set HIGH (LCD_RES)


    i2c_master_init();

    ESP_LOGI(TAG, "tca6408_init...");

    if (tca6408_init(i2c_bus_handle, 0x21, &tca_board) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TCA6408 at 0x20");
    }
    tca6408_set_config(tca_board, 0x00); // All pins as output
    tca6408_set_output_val(tca_board, 0x00); // Set all pins low
    tca6408_set_output_pin(tca_board, 7, 0); // Set pin SDCARD low to enable

    if (tca6408_init(i2c_bus_handle, 0x20, &tca_display) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TCA6408 at 0x21");
    }

    tca6408_set_config(tca_display, 0x00); // All pins as output
    tca6408_set_output_val(tca_display, 0x00); // Set all pins low

    tca6408_set_output_pin(tca_display, 0, 1); // Set pin 0 high // GPIO_EN
    tca6408_set_output_pin(tca_display, 1, 1); // Set pin 2 high // LCD BIAS EN
    tca6408_set_output_pin(tca_display, 2, 1); // Set pin 2 high // BL_EN


    if (tps65132_i2c_init(i2c_bus_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize tps65132");
    }
    // This needs to be run only once (didn't implement check if is already initialized)
    // // Set +5.9V
    // tps65132_write_register(0x00, 0x13);
    // // Set -5.9V
    // tps65132_write_register(0x01, 0x13);
    // vTaskDelay(pdMS_TO_TICKS(5));
    // tps65132_write_register(0xff, 0x80);
    // vTaskDelay(pdMS_TO_TICKS(5));
    // // Enable AVDD and AVEE
    // tps65132_write_register(0x03, 0x03);
    // vTaskDelay(pdMS_TO_TICKS(10));

    delay(10);   

    ESP_LOGI(TAG, "sgm37604a_init...");
    if (sgm37604a_i2c_init(i2c_bus_handle) != ESP_OK) {
        ESP_LOGE("MAIN", "Failed to initialize I2C");
        return;
    }

    delay(250);

    ESP_LOGI(TAG, "MIPI DSI LCD initialization");
    example_bsp_enable_dsi_phy_power();
    example_bsp_init_lcd_backlight();
    example_bsp_set_lcd_backlight(EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);

    i2c_scan_devices();

    // create MIPI DSI bus first, it will initialize the DSI PHY as well
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = TD4101_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    ESP_LOGI(TAG, "Install MIPI DSI LCD control IO");
    esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
    // we use DBI interface to send LCD commands and parameters
    esp_lcd_dbi_io_config_t dbi_config = TD4101_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));

    ESP_LOGI(TAG, "Install MIPI DSI LCD data panel");
    esp_lcd_panel_handle_t mipi_dpi_panel = NULL;
    esp_lcd_dpi_panel_config_t dpi_config = TD4101_540_960_PANEL_DPI_CONFIG(LCD_COLOR_FMT_RGB888); //LCD_COLOR_PIXEL_FORMAT_RGB565 LCD_COLOR_PIXEL_FORMAT_RGB888

    td4101_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = 2,
        },
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 24,
        .vendor_config = &vendor_config,
    };
    ESP_LOGI(TAG, "esp_lcd_new_panel_td4101 panel");
    ESP_ERROR_CHECK(esp_lcd_new_panel_td4101(mipi_dbi_io, &panel_config, &mipi_dpi_panel));
    ESP_LOGI(TAG, "esp_lcd_panel_reset panel");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(mipi_dpi_panel));
    ESP_LOGI(TAG, "esp_lcd_panel_init panel");
    ESP_ERROR_CHECK(esp_lcd_panel_init(mipi_dpi_panel));
    // turn on display
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(mipi_dpi_panel, true));
    // turn on backlight
    example_bsp_set_lcd_backlight(EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

    // DEBUG: Uncomment to test if MIPI DSI bus is working - shows color bars
    ESP_LOGI(TAG, "Setting test pattern - if you see color bars, MIPI DSI is working");
    esp_lcd_dpi_panel_set_pattern(mipi_dpi_panel, MIPI_DSI_PATTERN_BAR_VERTICAL);
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_lcd_dpi_panel_set_pattern(mipi_dpi_panel, MIPI_DSI_PATTERN_NONE); // Disable pattern for LVGL;

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // create a lvgl display
    lv_display_t *display = lv_display_create(EXAMPLE_MIPI_DSI_LCD_H_RES, EXAMPLE_MIPI_DSI_LCD_V_RES);
    // associate the mipi panel handle to the display
    lv_display_set_user_data(display, mipi_dpi_panel);
    // set color depth
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    // create draw buffer
    void *buf1 = NULL;
    void *buf2 = NULL;
    ESP_LOGI(TAG, "Allocate separate LVGL draw buffers");
    // Note:
    // Keep the display buffer in **internal** RAM can speed up the UI because LVGL uses it a lot and it should have a fast access time
    // This example allocate the buffer from PSRAM mainly because we want to save the internal RAM
    size_t draw_buffer_sz = EXAMPLE_MIPI_DSI_LCD_H_RES * EXAMPLE_LVGL_DRAW_BUF_LINES * sizeof(lv_color_t);
    buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM);
    assert(buf1);
    buf2 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM);
    assert(buf2);
    // initialize LVGL draw buffers
    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    // set the callback which can copy the rendered image to an area of the display
    lv_display_set_flush_cb(display, example_lvgl_flush_cb);

    ESP_LOGI(TAG, "Register DPI panel event callback for LVGL flush ready notification");
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = example_notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(mipi_dpi_panel, &cbs, display));

    ESP_LOGI(TAG, "Use esp_timer as LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Create LVGL task");
    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "Display LVGL Meter Widget");
    _lock_acquire(&lvgl_api_lock);
    example_lvgl_demo_ui(display);
    _lock_release(&lvgl_api_lock);
}
