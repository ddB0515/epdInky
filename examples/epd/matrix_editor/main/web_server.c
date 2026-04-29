#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"

#include "web_server.h"
#include "epd_functions.h"

#ifndef CONFIG_HTTPD_WS_SUPPORT
#error "Please enable HTTPD WebSocket support (CONFIG_HTTPD_WS_SUPPORT) in menuconfig."
#endif

#define WS_MAX_CLIENTS 4

static const char *TAG = "web_server";

static httpd_handle_t s_server = NULL;

typedef struct {
    int sock_fds[WS_MAX_CLIENTS];
    SemaphoreHandle_t lock;
} ws_clients_t;

static ws_clients_t s_clients = { .sock_fds = { -1, -1, -1, -1 }, .lock = NULL };

static esp_err_t parse_matrix_json(const char *json, matrix_config_t *out)
{
    if (!json || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "cJSON parse failed");
        return ESP_FAIL;
    }

    cJSON *width = cJSON_GetObjectItem(root, "width");
    cJSON *height = cJSON_GetObjectItem(root, "height");
    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *bar_w = cJSON_GetObjectItem(root, "bar_width");
    cJSON *bar_h = cJSON_GetObjectItem(root, "bar_height");

    if (!cJSON_IsNumber(width) || !cJSON_IsNumber(height) || !cJSON_IsArray(data)) {
        ESP_LOGW(TAG, "Invalid matrix JSON fields");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const int w = width->valuedouble;
    const int h = height->valuedouble;
    const int expected = w * h;
    const int n = cJSON_GetArraySize(data);

    if (w <= 0 || h <= 0 || n <= 0) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (expected > 0 && n != expected) {
        ESP_LOGW(TAG, "Matrix size mismatch: w=%d h=%d expect=%d got=%d", w, h, expected, n);
        // continue but report mismatch
    }

    uint8_t *buf = calloc(n, sizeof(uint8_t));
    if (!buf) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < n; ++i) {
        cJSON *el = cJSON_GetArrayItem(data, i);
        if (!cJSON_IsNumber(el)) {
            free(buf);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
        int v = (int)el->valuedouble;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        buf[i] = (uint8_t)v;
    }

    out->width = w;
    out->height = h;
    out->bar_width = (cJSON_IsNumber(bar_w) ? bar_w->valuedouble : 50);
    out->bar_height = (cJSON_IsNumber(bar_h) ? bar_h->valuedouble : 250);
    out->u8_graytable = buf;
    out->len = n;

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t matrix_config_parse(const char *json, matrix_config_t *out)
{
    return parse_matrix_json(json, out);
}

void matrix_config_free(matrix_config_t *cfg)
{
    if (!cfg) {
        return;
    }
    free(cfg->u8_graytable);
    cfg->u8_graytable = NULL;
    cfg->len = 0;
    cfg->width = 0;
    cfg->height = 0;
}

static void ws_clients_reset(void)
{
    if (!s_clients.lock) {
        s_clients.lock = xSemaphoreCreateMutex();
    }
    for (int i = 0; i < WS_MAX_CLIENTS; ++i) {
        s_clients.sock_fds[i] = -1;
    }
}

static void ws_clients_add(int sockfd)
{
    if (!s_clients.lock) {
        ws_clients_reset();
    }
    if (xSemaphoreTake(s_clients.lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to lock client list");
        return;
    }
    for (int i = 0; i < WS_MAX_CLIENTS; ++i) {
        if (s_clients.sock_fds[i] == -1) {
            s_clients.sock_fds[i] = sockfd;
            ESP_LOGI(TAG, "WS client added (slot %d, sock %d)", i, sockfd);
            break;
        }
    }
    xSemaphoreGive(s_clients.lock);
}

static void ws_clients_remove(int sockfd)
{
    if (!s_clients.lock) {
        return;
    }
    if (xSemaphoreTake(s_clients.lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to lock client list for remove");
        return;
    }
    for (int i = 0; i < WS_MAX_CLIENTS; ++i) {
        if (s_clients.sock_fds[i] == sockfd) {
            s_clients.sock_fds[i] = -1;
            ESP_LOGI(TAG, "WS client removed (slot %d, sock %d)", i, sockfd);
            break;
        }
    }
    xSemaphoreGive(s_clients.lock);
}

static esp_err_t ws_send_text_locked(const char *msg)
{
    if (!msg || !s_clients.lock) {
        return ESP_ERR_INVALID_ARG;
    }
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)msg,
        .len = strlen(msg),
    };

    esp_err_t last_err = ESP_OK;
    for (int i = 0; i < WS_MAX_CLIENTS; ++i) {
        if (s_clients.sock_fds[i] == -1) {
            continue;
        }
        int sockfd = s_clients.sock_fds[i];
        esp_err_t err = httpd_ws_send_frame_async(s_server, sockfd, &frame);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed sending to sock %d: %s", sockfd, esp_err_to_name(err));
            last_err = err;
        }
    }
    return last_err;
}

esp_err_t web_server_broadcast_text(const char *msg)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!msg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_clients.lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t res = ws_send_text_locked(msg);
    xSemaphoreGive(s_clients.lock);
    return res;
}

static void on_close(httpd_handle_t hd, int sockfd)
{
    (void)hd;
    ws_clients_remove(sockfd);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    int sockfd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        ws_clients_add(sockfd);
        return ESP_OK; // handshake done by core
    }

    httpd_ws_frame_t frame = {
        .payload = NULL,
        .len = 0,
    };

    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ws recv frame failed: %s", esp_err_to_name(err));
        return err;
    }

    if (frame.len) {
        frame.payload = calloc(1, frame.len + 1);
        if (!frame.payload) {
            ESP_LOGE(TAG, "No memory for WS payload");
            return ESP_ERR_NO_MEM;
        }
        err = httpd_ws_recv_frame(req, &frame, frame.len);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ws recv payload failed: %s", esp_err_to_name(err));
            free(frame.payload);
            return err;
        }
        ((char *)frame.payload)[frame.len] = '\0';
    }

    if (frame.type == HTTPD_WS_TYPE_TEXT && frame.payload) {
        const char *payload = (const char *)frame.payload;
        ESP_LOGI(TAG, "WS recv (sock %d): %s", sockfd, payload);

        bool handled = false;

        if (payload[0] == '{') {
            cJSON *root = cJSON_Parse(payload);
            if (root) {
                cJSON *type = cJSON_GetObjectItem(root, "type");
                if (cJSON_IsString(type) && type->valuestring) {
                    if (strcmp(type->valuestring, "matrix") == 0) {
                        matrix_config_t cfg = {0};
                        if (matrix_config_parse(payload, &cfg) == ESP_OK) {
                            ESP_LOGI(TAG, "Parsed matrix: %dx%d len=%u first=%u", cfg.width, cfg.height, (unsigned)cfg.len,
                                     cfg.len > 0 ? cfg.u8_graytable[0] : 0);
                            epd_apply_matrix(&cfg);
                        }
                        matrix_config_free(&cfg);
                        handled = true;
                    } else if (strcmp(type->valuestring, "get_matrix") == 0) {
                        ESP_LOGI(TAG, "WS request: get_matrix");
                        epd_send_matrix_state();
                        handled = true;
                    }
                }
                cJSON_Delete(root);
            }
        }

        if (!handled) {
            // Echo to all connected clients
            web_server_broadcast_text(payload);
        }
    }

    free(frame.payload);
    return ESP_OK;
}

static const httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .is_websocket = true,
};

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t matrix_editor_html_start[] asm("_binary_matrix_editor_html_start");
extern const uint8_t matrix_editor_html_end[] asm("_binary_matrix_editor_html_end");

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const size_t index_html_size = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_size);
}

static esp_err_t matrix_get_handler(httpd_req_t *req)
{
    const size_t html_size = matrix_editor_html_end - matrix_editor_html_start;
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)matrix_editor_html_start, html_size);
}

static const httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
};

static const httpd_uri_t matrix_uri = {
    .uri = "/matrix",
    .method = HTTP_GET,
    .handler = matrix_get_handler,
};

esp_err_t web_server_start(void)
{
    if (s_server) {
        return ESP_OK;
    }

    ws_clients_reset();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.close_fn = on_close;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &root_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &matrix_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &ws_uri));

    ESP_LOGI(TAG, "HTTP/WebSocket server started on port %d", config.server_port);
    return ESP_OK;
}

void web_server_stop(void)
{
    if (!s_server) {
        return;
    }
    httpd_stop(s_server);
    s_server = NULL;
    ws_clients_reset();
    ESP_LOGI(TAG, "HTTP/WebSocket server stopped");
}
