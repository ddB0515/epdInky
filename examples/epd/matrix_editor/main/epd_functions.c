#include "epd_functions.h"

#include "esp_log.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "FastEPD.h"
#include "cJSON.h"
#include "web_server.h"

#include "Roboto_Black_80.h"
#include "Roboto_Black_40.h"
#include "Courier_Prime_16.h"

static const char *TAG = "epd_functions";

FASTEPDSTATE bbep;
static bool s_initialized = false;
static uint8_t *s_matrix_buf = NULL;
static size_t s_matrix_len = 0;
static int s_matrix_width = 0;
static int s_matrix_height = 0;
static int s_bar_width = 0;
static int s_bar_height = 0;

uint8_t u8_graytable[] = {
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

esp_err_t epd_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "FastEPD init");
    int rc = bbepInitPanel(&bbep, BB_PANEL_EPDINKY_P4_16, 20000000); //BB_PANEL_EPDINKY_P4_16
    ESP_LOGI(TAG, "bbepInitPanel returned %d", rc);
    if (rc == BBEP_SUCCESS) {
      bbepSetPanelSize(&bbep, 2400, 1034, BB_PANEL_FLAG_MIRROR_Y, 0); // ED113TC1
      bbepSetCustomMatrix(&bbep, u8_graytable, sizeof(u8_graytable));
      bbepSetMode(&bbep, BB_MODE_4BPP);
      matrix_config_t cfg = {
          .width = 12,
          .height = 16,
          .bar_width = 48,
          .bar_height = 250,
          .u8_graytable = u8_graytable,
          .len = sizeof(u8_graytable),
      };
      epd_apply_matrix(&cfg);
    }
    s_initialized = true;
    return ESP_OK;
}

esp_err_t epd_apply_matrix(const matrix_config_t *cfg)
{
    if (!cfg || !cfg->u8_graytable) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        ESP_LOGW(TAG, "EPD not initialized; call epd_init() first");
    }

    const int expected = cfg->width * cfg->height;
    if (expected <= 0 || (int)cfg->len < expected) {
        ESP_LOGW(TAG, "Matrix dimensions/len mismatch: %dx%d len=%u", cfg->width, cfg->height, (unsigned)cfg->len);
    }
    s_matrix_width = cfg->width;
    s_matrix_height = cfg->height;
    // Keep our own copy because FastEPD stores the pointer; the request buffer is freed later.
    if (s_matrix_buf) {
        free(s_matrix_buf);
        s_matrix_buf = NULL;
        s_matrix_len = 0;
    }
    s_matrix_buf = malloc(cfg->len);
    if (!s_matrix_buf) {
        ESP_LOGE(TAG, "No mem for matrix copy (%u bytes)", (unsigned)cfg->len);
        return ESP_ERR_NO_MEM;
    }
    memcpy(s_matrix_buf, cfg->u8_graytable, cfg->len);
    s_matrix_len = cfg->len;

    int rc = bbepSetCustomMatrix(&bbep, s_matrix_buf, s_matrix_len);
    if (rc != BBEP_SUCCESS) {
        ESP_LOGE(TAG, "setCustomMatrix returned %d", rc);
        return ESP_FAIL;
    }
    ESP_LOGW(TAG, "bbepSetCustomMatrix OK");
    const int bars = 16;
    int bar_w = cfg->bar_width > 0 ? cfg->bar_width : 48;
    int bar_h = cfg->bar_height > 0 ? cfg->bar_height : 250;
    // Require bar width divisible by 16 for consistent layout; fallback to nearest lower multiple.
    if (bar_w % bars != 0) {
        bar_w = (bar_w / bars) * bars;
        if (bar_w <= 0)
            bar_w = bars;
    }
    s_bar_width = bar_w;
    s_bar_height = bar_h;
    bbepFillScreen(&bbep, 0xf);
    int finalWidth = bars * bar_w;
    for (int i = 0; i < finalWidth; i += bar_w) {
        uint8_t color = i / bar_w;
        bbepRectangle(&bbep, i, 0, i + bar_w - 1, bar_h - 1, color, 1);
    }
    for (uint8_t i = 0; i < bars; i++) {
        char buffer[4];
        snprintf(buffer, sizeof(buffer), "%d", i);
        bbepWriteStringCustom(&bbep, Courier_Prime_16, (i * bar_w) + (bar_w / 3), bar_h + (bar_w / 2), buffer, BBEP_BLACK);
    }
    bbepFullUpdate(&bbep, CLEAR_SLOW, 1, NULL);
    ESP_LOGW(TAG, "bbepFullUpdate OK");
    return ESP_OK;
}

esp_err_t epd_send_matrix_state(void)
{
    const uint8_t *matrix = s_matrix_buf ? s_matrix_buf : bbep.panelDef.pGrayMatrix;
    size_t matrix_len = s_matrix_buf ? s_matrix_len : bbep.panelDef.iMatrixSize;
    if (!matrix || matrix_len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    int height = s_matrix_height > 0 ? s_matrix_height : 16;
    if (height <= 0) height = 16;
    if (matrix_len % (size_t)height != 0) {
        ESP_LOGW(TAG, "Matrix size %zu not divisible by %d", matrix_len, height);
    }
    int width = s_matrix_width > 0 ? s_matrix_width : (int)(matrix_len / (size_t)height);
    if (width <= 0) width = (int)matrix_len;
    int bar_w = s_bar_width > 0 ? s_bar_width : 48;
    int bar_h = s_bar_height > 0 ? s_bar_height : 250;

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON_AddStringToObject(root, "type", "matrix");
    cJSON_AddNumberToObject(root, "width", width);
    cJSON_AddNumberToObject(root, "height", height);
    cJSON_AddNumberToObject(root, "bar_width", bar_w);
    cJSON_AddNumberToObject(root, "bar_height", bar_h);
    cJSON *arr = cJSON_AddArrayToObject(root, "data");
    if (!arr) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    uint8_t min_v = 255, max_v = 0;
    for (size_t i = 0; i < matrix_len; ++i) {
        uint8_t v = matrix[i];
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
        uint8_t clipped = v > 2 ? 2 : v; // keep UI 0-2 palette stable
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(clipped));
    }

    char *msg = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!msg) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Send matrix JSON (len=%zu width=%d height=%d min=%u max=%u src=%s): %s", matrix_len, width, height, min_v, max_v, s_matrix_buf ? "cached" : "bbep", msg);

    esp_err_t res = web_server_broadcast_text(msg);
    free(msg);
    return res;
}
