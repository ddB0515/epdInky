#pragma once

#include "esp_err.h"
#include "web_server.h"  // for matrix_config_t

/**
 * Apply a matrix update to the FastEPD pipeline.
 * Expects row-major data in cfg->u8_graytable with dimensions cfg->width x cfg->height.
 */
esp_err_t epd_apply_matrix(const matrix_config_t *cfg);

/** One-time initialization for FastEPD pipeline. Safe to call multiple times. */
esp_err_t epd_init(void);

/**
 * Broadcast the currently loaded grayscale matrix to WebSocket clients using the editor format:
 * {"type":"matrix","width":W,"height":H,"bar_width":BW,"bar_height":BH,"data":[..]}.
 */
esp_err_t epd_send_matrix_state(void);
