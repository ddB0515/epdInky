#ifndef MAIN_H
#define MAIN_H

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "tca6408.h"
#include "sd_card_functions.h"
#include "esp_hosted_wifi.h"

#define EXAMPLE_ESP_WIFI_SSID      "WIFI_SSID"
#define EXAMPLE_ESP_WIFI_PASS      "WIFI_PASSWORD"

#define MOUNT_POINT "/sdcard"

extern i2c_master_bus_handle_t i2c_bus_handle;
extern tca6408_handle_t tca_board;
extern tca6408_handle_t tca_display;

/** Set to true to cut power to the WiFi module (GPIO 54 LOW), false to re-enable. */
void wifi_monitor_set_disabled(bool disabled);

#endif // MAIN_H