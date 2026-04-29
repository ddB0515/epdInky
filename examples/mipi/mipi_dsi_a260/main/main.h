#ifndef MAIN_H
#define MAIN_H

#include "freertos/FreeRTOS.h"

#include "TD4101.h"
#include "SGM37604A.h"
#include "tca6408.h"
#include "stc3115.h"
#include "sd_card_functions.h"

#define delay(ms) vTaskDelay(pdMS_TO_TICKS(ms))

extern i2c_master_bus_handle_t i2c_bus_handle;
extern tca6408_handle_t tca_board;
extern tca6408_handle_t tca_display;
extern stc3115_handle_t stc3115_dev;

#endif // MAIN_H