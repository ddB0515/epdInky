#ifndef SGM37604A_H
#define SGM37604A_H

#include <stdbool.h>
#include "driver/i2c_master.h"

// Configuration defines - adjust these for your specific board
#define SGM37604A_CLK_SPEED       400000  // 400kHz
#define SGM37604A_SLAVE           0x36    // Example address, adjust as needed

/* SGM37604A Register Map */
#define MAX_BRIGHTNESS			                (4095)
#define MIN_BRIGHTNESS                          (16)

#define SGM37604A_CTL_BRIGHTNESS_LSB_REG		0x1A
#define SGM37604A_CTL_BRIGHTNESS_MSB_REG		0x19
#define SGM37604A_CTL_BACKLIGHT_MODE_REG        0x11
#define SGM37604A_CTL_BACKLIGHT_LED_REG         0x10
#define SGM37604A_CTL_BACKLIGHT_CURRENT_REG     0x1B
#define SGM37604A_FAULT_FLAGS                   0x1F

#define SGM37604A_MIN_VALUE_SETTINGS 16		/* value min leds_brightness_set */
#define SGM37604A_MAX_VALUE_SETTINGS 4095	/* value max leds_brightness_set */
#define MIN_MAX_SCALE(x) (((x) < SGM37604A_MIN_VALUE_SETTINGS) ? SGM37604A_MIN_VALUE_SETTINGS :\
(((x) > SGM37604A_MAX_VALUE_SETTINGS) ? SGM37604A_MAX_VALUE_SETTINGS:(x)))

// Function declarations
esp_err_t sgm37604a_i2c_init(i2c_master_bus_handle_t bus_handle);
void sgm37604a_init(void);
void sgm37604a_set_brightness_level(unsigned int level);
void sgm37604a_backlight(bool enabled);
void sgm37604a_deinit(i2c_master_bus_handle_t bus_handle);

#endif // SGM37604A_H