#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef struct {
	int width;
	int height;
	int bar_width;   // width of each of the 16 bars (pixels)
	int bar_height;  // height of the bars (pixels)
	uint8_t *u8_graytable; // allocated buffer holding row-major values
	size_t len;            // number of entries in u8_graytable
} matrix_config_t;

/**
 * Start the HTTP server that serves index.html at "/" and a WebSocket at "/ws".
 * Safe to call multiple times; does nothing if already running.
 */
esp_err_t web_server_start(void);

/**
 * Stop the HTTP/WebSocket server if running.
 */
void web_server_stop(void);

/**
 * Broadcast a text message to all connected WebSocket clients.
 * Returns ESP_ERR_INVALID_STATE if the server is not running.
 */
esp_err_t web_server_broadcast_text(const char *msg);

/** Parse JSON payload of the form {"type":"matrix","width":W,"height":H,"data":[..]} into matrix_config_t.
 *  Allocates u8_graytable; caller must free via matrix_config_free.
 */
esp_err_t matrix_config_parse(const char *json, matrix_config_t *out);

/** Free owned memory in matrix_config_t. Safe to call with NULL. */
void matrix_config_free(matrix_config_t *cfg);
