// FastJsonDL — BLE GATT server receive example
//
// Advertises as a BLE GATT server using the Nordic UART Service (NUS) UUIDs
// and receives a FastJsonDL JSON payload from a remote client such as the
// FastJsonRenderer web app (Chrome / Edge with Web Bluetooth enabled).
// Once all bytes are received the payload is passed to FastJsonDL for rendering.
//
// Protocol:
//   The client prepends an 8-byte header and sends the resulting frame as one
//   or more BLE write chunks.  Depending on the negotiated MTU the BLE stack
//   may deliver chunks via normal write-without-response (ATT_WRITE_CMD) or
//   via the prepared-write / long-write path (ATT_PREPARE_WRITE_REQ +
//   ATT_EXECUTE_WRITE_REQ).  Both paths are handled identically.
//
//   Header layout (8 bytes, little-endian):
//     Byte 0-1 : type   uint16 LE  0x0001 = JSON payload
//     Byte 2-7 : length uint48 LE  total JSON byte count (bytes 6-7 always 0
//                                  for payloads < 4 GB)
//
//   When the header is present the device renders as soon as the announced
//   number of bytes have been received.  If no valid header is detected the
//   device falls back to a 500 ms inactivity timer.
//
// BLE UUIDs (Nordic UART Service — NUS):
//   Service        : 6e400001-b5a3-f393-e0a9-e50e24dcca9e
//   Characteristic : 6e400002-b5a3-f393-e0a9-e50e24dcca9e  (write RX)
//
//   These are the default UUIDs used by the FastJsonRenderer web client.
//   The Web Bluetooth filter in the client uses the service UUID, so the
//   device MUST advertise exactly this UUID to appear in Chrome's picker.
//
// Target board: EPDInky-P4C6.r2 (or any ESP32 variant with BLE support).
//               Adjust BB_PANEL / setPanelSize() for your panel.
//
// The receive buffer is allocated from SPIRAM when available; it falls back
// to internal heap when SPIRAM is not present.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gatt_common_api.h"

#include "esp_hosted.h"
#include "esp_hosted_bt.h"
extern "C" {
#include "esp_hosted_bluedroid.h"
#include "esp_bluedroid_hci.h"
}

#include "miniz.h"
#include "FastEPD.h"
#include "FastJsonDL.h"
#include "ubuntu20.h"
#include "ubuntu30.h"
#include "ubuntu40.h"

// Font registry: maps JSON "font" field values to in-memory font data.
static const FastJsonDLFont fonts[] = {
    { "Ubuntu20", ubuntu20 },
    { "Ubuntu30", ubuntu30 },
    { "Ubuntu40", ubuntu40 },
};
// ---------------------------------------------------------------------------
// Logging tag
// ---------------------------------------------------------------------------
static const char *TAG = "BLE_FASTJSON";

// ---------------------------------------------------------------------------
// BLE identifiers — Nordic UART Service (NUS)
// ---------------------------------------------------------------------------
// These 128-bit UUIDs must match the service/characteristic UUIDs configured
// in the FastJsonRenderer web client (JsonFooter.jsx DEFAULT_SERVICE_UUID /
// DEFAULT_CHARACTERISTIC_UUID).  Chrome's Web Bluetooth requestDevice() uses
// a service-UUID filter, so the firmware MUST advertise this exact UUID.
//
// Byte arrays are stored LSB-first as required by the ESP-IDF BLE stack.

// 6e400001-b5a3-f393-e0a9-e50e24dcca9e  (NUS service)
static const uint8_t NUS_SERVICE_UUID[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e,
};

// 6e400002-b5a3-f393-e0a9-e50e24dcca9e  (NUS RX characteristic — client writes here)
static const uint8_t NUS_CHAR_UUID[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e,
};

#define DEVICE_NAME             "FastJsonDL"
// NUS service handle count: service + characteristic declaration + value + CCCD
#define GATTS_NUM_HANDLES       4
#define PREPARE_BUF_MAX_SIZE    1024

// ---------------------------------------------------------------------------
// Transfer protocol — 8-byte header
// ---------------------------------------------------------------------------
// The client MUST send an 8-byte header as the very first bytes of a transfer,
// immediately followed by (or in the same chunk as) the payload.
//
//   Byte 0-1 : type   uint16 little-endian
//                       0x0001 = plain JSON payload
//                       0x0002 = raw DEFLATE-compressed JSON (RFC 1951,
//                                no zlib/gzip wrapper)
//   Byte 2-7 : length uint48 little-endian  total payload byte count
//              (for 0x0001: uncompressed JSON size;
//               for 0x0002: compressed payload size — NOT the JSON size)
//
// JavaScript (FastJsonRenderer JsonFooter.jsx) example for plain JSON:
//
//   const data   = new TextEncoder().encode(json);
//   const header = new Uint8Array(8);
//   header[0] = 0x01; header[1] = 0x00;          // type = 0x0001 LE
//   header[2] =  data.length        & 0xFF;
//   header[3] = (data.length >>  8) & 0xFF;
//   header[4] = (data.length >> 16) & 0xFF;
//   header[5] = (data.length >> 24) & 0xFF;
//   header[6] = 0x00; header[7] = 0x00;           // upper 2 bytes (< 4 GB)
//   const frame = new Uint8Array(8 + data.length);
//   frame.set(header); frame.set(data, 8);
//   // then send frame in BLE_CHUNK_SIZE slices
//
// For compressed JSON (type 0x0002), replace header[0] with 0x02 and
// set length to the compressed (not original) byte count.  The payload is
// the raw DEFLATE output of deflateRaw() / pako.deflateRaw() / tinfl.
//
// If the first write chunk does NOT start with bytes {0x01, 0x00} or
// {0x02, 0x00} the firmware falls back to headerless mode (plain JSON) and
// uses the idle timer instead.

#define HEADER_SIZE             8       // total header length in bytes
#define HEADER_TYPE_JSON        0x0001  // uint16 LE — plain JSON
#define HEADER_TYPE_DEFLATE     0x0002  // uint16 LE — raw DEFLATE compressed JSON

// ---------------------------------------------------------------------------
// Inactivity render timer
// ---------------------------------------------------------------------------
// Used as a safety-net when no valid header was received (headerless mode).
// Also fires if a transfer stalls mid-way with a known length.
#define RENDER_IDLE_MS          500

static TimerHandle_t s_render_timer = nullptr;

// ---------------------------------------------------------------------------
// Receive buffer
// ---------------------------------------------------------------------------
// Maximum JSON payload the device will accept over BLE (bytes).
// Increase if you need larger layouts, but keep it within available RAM.
#define JSON_BUF_MAX_SIZE       (64 * 1024)

static uint8_t  *s_json_buf        = nullptr; // Heap-allocated receive buffer
static uint32_t  s_json_buf_pos    = 0;        // Write cursor into s_json_buf
static uint32_t  s_expected_len    = 0;        // Payload length from header (0 = unknown)
static uint16_t  s_payload_type    = 0;        // Header type: HEADER_TYPE_JSON / HEADER_TYPE_DEFLATE
static bool      s_header_received = false;    // True once first chunk processed
static uint64_t  s_start_time      = 0;        // Timestamp of first received chunk (µs)
static uint64_t  s_receive_end_time = 0;       // Timestamp when all expected bytes arrived (µs)
static uint32_t  s_chunk_count     = 0;        // BLE write chunks received this transfer
static uint32_t  s_last_log_pos    = 0;        // s_json_buf_pos at last progress log

// Log a receive-progress line every PROGRESS_LOG_INTERVAL bytes.
#define PROGRESS_LOG_INTERVAL   (4 * 1024)

// ---------------------------------------------------------------------------
// EPD / FastJsonDL
// ---------------------------------------------------------------------------
static FASTEPD    *s_epaper = nullptr;
static FastJsonDL *s_dl     = nullptr;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                         esp_gatt_if_t        gatts_if,
                                         esp_ble_gatts_cb_param_t *param);

// ---------------------------------------------------------------------------
// GAP advertising configuration
// ---------------------------------------------------------------------------
// Advertise the NUS service UUID so that the Web Bluetooth filter in
// FastJsonRenderer can discover this device.
static uint8_t s_adv_service_uuid128[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e,
};

static esp_ble_adv_data_t s_adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = false,
    .min_interval        = 0x10,
    .max_interval        = 0x20,
    .appearance          = 0x00,
    .manufacturer_len    = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len    = 0,
    .p_service_data      = nullptr,
    .service_uuid_len    = sizeof(s_adv_service_uuid128),
    .p_service_uuid      = s_adv_service_uuid128,
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_data_t s_scan_rsp_data = {
    .set_scan_rsp        = true,
    .include_name        = true,
    .include_txpower     = true,
    .min_interval        = 0x20,
    .max_interval        = 0x40,
    .appearance          = 0x00,
    .manufacturer_len    = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len    = 0,
    .p_service_data      = nullptr,
    .service_uuid_len    = sizeof(s_adv_service_uuid128),
    .p_service_uuid      = s_adv_service_uuid128,
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min       = 0x20,
    .adv_int_max       = 0x40,
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr         = {0, 0, 0, 0, 0, 0},   // unused for undirected ADV_TYPE_IND
    .peer_addr_type    = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Track which adv config steps have completed before starting advertising.
static uint8_t s_adv_config_done = 0;
#define ADV_CONFIG_FLAG      (1 << 0)
#define SCAN_RSP_CONFIG_FLAG (1 << 1)

// ---------------------------------------------------------------------------
// GATTS profile table (single profile)
// ---------------------------------------------------------------------------
#define PROFILE_APP_ID 0

struct GattsProfileInst {
    esp_gatts_cb_t       gatts_cb       = nullptr;
    uint16_t             gatts_if       = 0;
    uint16_t             app_id         = 0;
    uint16_t             conn_id        = 0;
    uint16_t             service_handle = 0;
    esp_gatt_srvc_id_t   service_id     = {};
    uint16_t             char_handle    = 0;
    esp_bt_uuid_t        char_uuid      = {};
    esp_gatt_perm_t      perm           = 0;
    esp_gatt_char_prop_t property       = 0;
    uint16_t             descr_handle   = 0;
    esp_bt_uuid_t        descr_uuid     = {};
};

static GattsProfileInst s_profile_tab[1] = {
    [PROFILE_APP_ID] = {
        .gatts_cb  = gatts_profile_event_handler,
        .gatts_if  = ESP_GATT_IF_NONE,
    },
};

// ---------------------------------------------------------------------------
// Prepare-write environment (long-write support)
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t *prepare_buf;
    int      prepare_len;
} prepare_type_env_t;

static prepare_type_env_t s_prepare_write_env;

// ---------------------------------------------------------------------------
// Characteristic initial value
// ---------------------------------------------------------------------------
static uint8_t s_char_init_val[] = { 0x00, 0x00 };
static esp_gatt_char_prop_t s_char_property = 0;

static esp_attr_value_t s_gatts_char_val = {
    .attr_max_len = PREPARE_BUF_MAX_SIZE,
    .attr_len     = sizeof(s_char_init_val),
    .attr_value   = s_char_init_val,
};

// ---------------------------------------------------------------------------
// Helper: reset transfer state
// ---------------------------------------------------------------------------
static void reset_transfer_state(void)
{
    s_json_buf_pos     = 0;
    s_expected_len     = 0;
    s_payload_type     = 0;
    s_header_received  = false;
    s_start_time       = 0;
    s_receive_end_time = 0;
    s_chunk_count      = 0;
    s_last_log_pos     = 0;
}

// ---------------------------------------------------------------------------
// Helper: render accumulated JSON buffer and refresh the display
// ---------------------------------------------------------------------------
static void render_json_and_refresh(void)
{
    if (s_json_buf_pos == 0) {
        ESP_LOGW(TAG, "render_json_and_refresh: empty buffer, skipping");
        return;
    }

    // Compute receive duration.  s_receive_end_time is set at the moment the
    // last expected byte arrives; for the idle-timer (headerless) path it is
    // set just before this function is called.
    uint32_t ms_receive = (s_receive_end_time > s_start_time)
                          ? (uint32_t)((s_receive_end_time - s_start_time) / 1000)
                          : 0;

    bool ok = false;

    if (s_payload_type == HEADER_TYPE_DEFLATE) {
        ESP_LOGI(TAG, "Decompressing + rendering DEFLATE payload (%lu bytes compressed, %lu chunks)...",
                 (unsigned long)s_json_buf_pos, (unsigned long)s_chunk_count);
        ok = s_dl->renderDeflatedJson(s_json_buf, s_json_buf_pos);
    } else {
        ESP_LOGI(TAG, "Rendering JSON (%lu bytes, %lu chunks)...",
                 (unsigned long)s_json_buf_pos, (unsigned long)s_chunk_count);
        ok = s_dl->renderJson((const char *)s_json_buf, s_json_buf_pos);
    }

    if (!ok) {
        ESP_LOGE(TAG, "FastJsonDL error: %s", s_dl->getLastError());
        return;
    }

    uint64_t t_epd = esp_timer_get_time();
    s_epaper->fullUpdate();
    uint32_t ms_epd = (uint32_t)((esp_timer_get_time() - t_epd) / 1000);

    uint32_t ms_decomp = s_dl->getLastDecompMs();
    uint32_t ms_render = s_dl->getLastRenderMs();
    uint32_t ms_total  = ms_receive + ms_decomp + ms_render + ms_epd;

    // Throughput: received bytes / receive time → kbit/s
    uint32_t recv_kbps = (ms_receive > 0)
                         ? (uint32_t)((s_json_buf_pos * 8ULL) / ms_receive)
                         : 0;

    ESP_LOGI(TAG, "=== Transfer + render statistics ===");
    ESP_LOGI(TAG, "  BLE receive  : %4lu ms  (%lu bytes, %lu chunks, %lu kbit/s)",
             (unsigned long)ms_receive,
             (unsigned long)s_json_buf_pos,
             (unsigned long)s_chunk_count,
             (unsigned long)recv_kbps);
    if (s_payload_type == HEADER_TYPE_DEFLATE) {
        ESP_LOGI(TAG, "  Decompress   : %4lu ms", (unsigned long)ms_decomp);
    }
    ESP_LOGI(TAG, "  Render JSON  : %4lu ms", (unsigned long)ms_render);
    ESP_LOGI(TAG, "  EPD refresh  : %4lu ms", (unsigned long)ms_epd);
    ESP_LOGI(TAG, "  Total        : %4lu ms", (unsigned long)ms_total);
    ESP_LOGI(TAG, "====================================");
}

// ---------------------------------------------------------------------------
// Inactivity timer callback — fired when writing has been idle for
// RENDER_IDLE_MS milliseconds, signalling end of transfer.
// ---------------------------------------------------------------------------
static void render_timer_cb(TimerHandle_t xTimer)
{
    if (s_json_buf_pos == 0) {
        return;
    }
    // Capture the "end of receive" timestamp just before rendering so that the
    // timing summary in render_json_and_refresh() reflects the true transfer
    // duration (not including the idle timeout period).
    s_receive_end_time = esp_timer_get_time() - (RENDER_IDLE_MS * 1000ULL);
    uint32_t ms_receive = (uint32_t)((s_receive_end_time - s_start_time) / 1000);
    ESP_LOGI(TAG, "Idle timeout — transfer done: %lu bytes in %lu ms",
             (unsigned long)s_json_buf_pos, (unsigned long)ms_receive);
    render_json_and_refresh();
    reset_transfer_state();
}

// ---------------------------------------------------------------------------
// Long-write helpers (taken from ESP-IDF GATTS demo)
// ---------------------------------------------------------------------------
static void write_event_env(esp_gatt_if_t             gatts_if,
                             prepare_type_env_t       *env,
                             esp_ble_gatts_cb_param_t *param)
{
    esp_gatt_status_t status = ESP_GATT_OK;

    if (!param->write.need_rsp) {
        return;
    }

    if (param->write.is_prep) {
        if (env->prepare_buf == nullptr) {
            env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE);
            env->prepare_len = 0;
            if (env->prepare_buf == nullptr) {
                ESP_LOGE(TAG, "Gatt_server prep: no memory");
                status = ESP_GATT_NO_RESOURCES;
            }
        }

        if (status == ESP_GATT_OK) {
            if (param->write.offset > PREPARE_BUF_MAX_SIZE) {
                status = ESP_GATT_INVALID_OFFSET;
            } else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE) {
                status = ESP_GATT_INVALID_ATTR_LEN;
            }
        }

        esp_gatt_rsp_t *rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
        if (rsp != nullptr) {
            rsp->attr_value.len    = param->write.len;
            rsp->attr_value.handle = param->write.handle;
            rsp->attr_value.offset = param->write.offset;
            rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
            memcpy(rsp->attr_value.value, param->write.value, param->write.len);
            esp_err_t err = esp_ble_gatts_send_response(gatts_if,
                                                         param->write.conn_id,
                                                         param->write.trans_id,
                                                         status, rsp);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "send_response error: %s", esp_err_to_name(err));
            }
            free(rsp);
        }

        if (status == ESP_GATT_OK && env->prepare_buf != nullptr) {
            memcpy(env->prepare_buf + param->write.offset,
                   param->write.value,
                   param->write.len);
            env->prepare_len += param->write.len;
        }
    } else {
        esp_ble_gatts_send_response(gatts_if,
                                     param->write.conn_id,
                                     param->write.trans_id,
                                     status, nullptr);
    }
}

static void exec_write_event_env(prepare_type_env_t       *env,
                                  esp_ble_gatts_cb_param_t *param)
{
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC) {
        // The client (e.g. Web Bluetooth) may call writeValue() per BLE chunk,
        // each of which generates its own ATT_PREPARE + ATT_EXECUTE sequence.
        // Only render once we have all expected bytes; until then restart the
        // idle timer so subsequent non-prep or prep chunks can accumulate.
        if (s_expected_len > 0 && s_json_buf_pos >= s_expected_len) {
            xTimerStop(s_render_timer, 0);
            s_receive_end_time = esp_timer_get_time();
            render_json_and_refresh();
            reset_transfer_state();
        } else {
            ESP_LOGD(TAG, "Prepared-write commit: %lu/%lu bytes so far — waiting for more",
                     (unsigned long)s_json_buf_pos, (unsigned long)s_expected_len);
            if (xTimerReset(s_render_timer, 0) != pdPASS) {
                ESP_LOGE(TAG, "xTimerReset failed");
            }
        }
    } else {
        ESP_LOGI(TAG, "ESP_GATT_PREP_WRITE_CANCEL");
        reset_transfer_state();
    }

    if (env->prepare_buf != nullptr) {
        free(env->prepare_buf);
        env->prepare_buf = nullptr;
    }
    env->prepare_len = 0;
}

// ---------------------------------------------------------------------------
// GAP event handler
// ---------------------------------------------------------------------------
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        s_adv_config_done &= ~ADV_CONFIG_FLAG;
        if (s_adv_config_done == 0) {
            esp_ble_gap_start_advertising(&s_adv_params);
        }
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        s_adv_config_done &= ~SCAN_RSP_CONFIG_FLAG;
        if (s_adv_config_done == 0) {
            esp_ble_gap_start_advertising(&s_adv_params);
        }
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Advertising start failed");
        } else {
            ESP_LOGI(TAG, "Advertising started");
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Advertising stop failed");
        } else {
            ESP_LOGI(TAG, "Advertising stopped");
        }
        break;

    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG, "Connection params updated: status=%d min=%d max=%d "
                 "interval=%d latency=%d timeout=%d",
                 param->update_conn_params.status,
                 param->update_conn_params.min_int,
                 param->update_conn_params.max_int,
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// GATTS profile event handler
// ---------------------------------------------------------------------------
static void gatts_profile_event_handler(esp_gatts_cb_event_t     event,
                                          esp_gatt_if_t            gatts_if,
                                          esp_ble_gatts_cb_param_t *param)
{
    switch (event) {

    case ESP_GATTS_REG_EVT: {
        ESP_LOGI(TAG, "REGISTER_APP_EVT status=%d app_id=%d",
                 param->reg.status, param->reg.app_id);

        s_profile_tab[PROFILE_APP_ID].service_id.is_primary          = true;
        s_profile_tab[PROFILE_APP_ID].service_id.id.inst_id          = 0x00;
        s_profile_tab[PROFILE_APP_ID].service_id.id.uuid.len         = ESP_UUID_LEN_128;
        memcpy(s_profile_tab[PROFILE_APP_ID].service_id.id.uuid.uuid.uuid128,
               NUS_SERVICE_UUID, ESP_UUID_LEN_128);

        esp_err_t err = esp_ble_gap_set_device_name(DEVICE_NAME);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "set_device_name failed: %s", esp_err_to_name(err));
        }

        err = esp_ble_gap_config_adv_data(&s_adv_data);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "config_adv_data failed: %s", esp_err_to_name(err));
        }
        s_adv_config_done |= ADV_CONFIG_FLAG;

        err = esp_ble_gap_config_adv_data(&s_scan_rsp_data);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "config_scan_rsp failed: %s", esp_err_to_name(err));
        }
        s_adv_config_done |= SCAN_RSP_CONFIG_FLAG;

        esp_ble_gatts_create_service(gatts_if,
                                      &s_profile_tab[PROFILE_APP_ID].service_id,
                                      GATTS_NUM_HANDLES);
        break;
    }

    case ESP_GATTS_READ_EVT: {
        // Respond with a dummy 4-byte value — no data to expose from this server.
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(rsp));
        rsp.attr_value.handle    = param->read.handle;
        rsp.attr_value.len       = 4;
        rsp.attr_value.value[0]  = 0x00;
        rsp.attr_value.value[1]  = 0x01;
        rsp.attr_value.value[2]  = 0x02;
        rsp.attr_value.value[3]  = 0x03;
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                     param->read.trans_id, ESP_GATT_OK, &rsp);
        break;
    }

    case ESP_GATTS_WRITE_EVT: {
        // Empty write: nothing to accumulate; just send ACK if required.
        if (param->write.len == 0) {
            write_event_env(gatts_if, &s_prepare_write_env, param);
            break;
        }

        // ── First chunk of a new transfer (prep or non-prep) ───────────────
        if (!s_header_received) {
            s_header_received = true;
            s_start_time      = esp_timer_get_time();
            s_chunk_count     = 1;

            // Decode the 8-byte header.
            // Supported type values:
            //   0x0001 — plain JSON
            //   0x0002 — raw DEFLATE-compressed JSON
            uint16_t hdr_type = 0;
            if (param->write.len >= HEADER_SIZE) {
                hdr_type = (uint16_t)param->write.value[0]
                           | ((uint16_t)param->write.value[1] << 8);
            }

            if (hdr_type == HEADER_TYPE_JSON || hdr_type == HEADER_TYPE_DEFLATE) {
                s_payload_type = hdr_type;

                // Extract 6-byte little-endian length (bytes 2-7).
                s_expected_len =
                    (uint32_t)param->write.value[2]
                    | ((uint32_t)param->write.value[3] <<  8)
                    | ((uint32_t)param->write.value[4] << 16)
                    | ((uint32_t)param->write.value[5] << 24);

                ESP_LOGI(TAG, "Header OK: type=0x%04x expected=%lu bytes",
                         hdr_type, (unsigned long)s_expected_len);

                // Validate announced length.
                if (s_expected_len == 0 || s_expected_len > JSON_BUF_MAX_SIZE) {
                    ESP_LOGE(TAG, "Invalid header length (%lu), max=%d — dropping",
                             (unsigned long)s_expected_len, JSON_BUF_MAX_SIZE);
                    reset_transfer_state();
                    write_event_env(gatts_if, &s_prepare_write_env, param);
                    break;
                }

                // Copy the payload bytes that arrived in the same chunk as the header.
                uint32_t payload_len = param->write.len - HEADER_SIZE;
                if (payload_len > 0) {
                    memcpy(s_json_buf, param->write.value + HEADER_SIZE, payload_len);
                    s_json_buf_pos = payload_len;
                }
            } else {
                // No valid header — headerless transfer (backward compat, plain JSON).
                ESP_LOGW(TAG, "No header detected — headerless mode (idle-timer fallback)");
                uint32_t copy_len = (param->write.len <= JSON_BUF_MAX_SIZE)
                                     ? param->write.len : JSON_BUF_MAX_SIZE;
                memcpy(s_json_buf, param->write.value, copy_len);
                s_json_buf_pos = copy_len;
            }

        // ── Subsequent chunks (prep or non-prep) ───────────────────────────
        } else {
            uint32_t remaining = JSON_BUF_MAX_SIZE - s_json_buf_pos;
            uint32_t copy_len  = param->write.len;

            if (copy_len > remaining) {
                ESP_LOGW(TAG, "Buffer full: truncating %lu bytes to %lu",
                         (unsigned long)copy_len, (unsigned long)remaining);
                copy_len = remaining;
            }

            memcpy(s_json_buf + s_json_buf_pos, param->write.value, copy_len);
            s_json_buf_pos += copy_len;
            s_chunk_count++;

            ESP_LOGD(TAG, "chunk=%u total=%lu/%lu",
                     copy_len,
                     (unsigned long)s_json_buf_pos,
                     (unsigned long)s_expected_len);

            // Periodic progress log — fires every PROGRESS_LOG_INTERVAL bytes.
            if (s_expected_len > 0
                && (s_json_buf_pos - s_last_log_pos) >= PROGRESS_LOG_INTERVAL)
            {
                s_last_log_pos = s_json_buf_pos;
                uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - s_start_time) / 1000);
                uint32_t speed_bps  = (elapsed_ms > 0)
                                      ? (uint32_t)((s_json_buf_pos * 8ULL) / elapsed_ms)
                                      : 0;
                ESP_LOGI(TAG, "Progress: %lu/%lu bytes (%.1f%%) in %lu ms — %lu kbit/s",
                         (unsigned long)s_json_buf_pos,
                         (unsigned long)s_expected_len,
                         100.0f * s_json_buf_pos / s_expected_len,
                         (unsigned long)elapsed_ms,
                         (unsigned long)speed_bps);
            }
        }

        // ── Non-prep writes: render immediately if transfer is complete ────
        // Prepared-write transfers defer rendering to EXEC_WRITE_EVT.
        if (!param->write.is_prep
            && s_expected_len > 0
            && s_json_buf_pos >= s_expected_len)
        {
            xTimerStop(s_render_timer, 0);
            s_receive_end_time = esp_timer_get_time();
            render_json_and_refresh();
            reset_transfer_state();
            break;  // write-without-response: no ATT ACK needed
        }

        // (Re)start the idle timer on every received chunk — prep or non-prep.
        // Previously the reset only happened for non-prep writes and EXEC_WRITE.
        // When a large transfer spans multiple logical writeValue() calls each
        // of which uses ATT_PREPARE_WRITE internally, the ATT_PREPARE segments
        // between two EXEC_WRITE events never reset the timer, causing it to
        // fire mid-transfer.
        if (xTimerReset(s_render_timer, 0) != pdPASS) {
            ESP_LOGE(TAG, "xTimerReset failed");
        }

        write_event_env(gatts_if, &s_prepare_write_env, param);
        break;
    }

    case ESP_GATTS_EXEC_WRITE_EVT:
        // Send ATT_EXECUTE_WRITE_RSP first so the client's writeValue() Promise
        // resolves and it can proceed to send any remaining chunks.
        esp_ble_gatts_send_response(gatts_if,
                                     param->exec_write.conn_id,
                                     param->exec_write.trans_id,
                                     ESP_GATT_OK, nullptr);
        exec_write_event_env(&s_prepare_write_env, param);
        break;

    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(TAG, "MTU negotiated: %d bytes", param->mtu.mtu);
        break;

    case ESP_GATTS_CREATE_EVT: {
        ESP_LOGI(TAG, "CREATE_SERVICE_EVT status=%d handle=%d",
                 param->create.status, param->create.service_handle);

        s_profile_tab[PROFILE_APP_ID].service_handle = param->create.service_handle;
        s_profile_tab[PROFILE_APP_ID].char_uuid.len = ESP_UUID_LEN_128;
        memcpy(s_profile_tab[PROFILE_APP_ID].char_uuid.uuid.uuid128,
               NUS_CHAR_UUID, ESP_UUID_LEN_128);

        esp_ble_gatts_start_service(s_profile_tab[PROFILE_APP_ID].service_handle);

        s_char_property = ESP_GATT_CHAR_PROP_BIT_READ
                        | ESP_GATT_CHAR_PROP_BIT_WRITE
                        | ESP_GATT_CHAR_PROP_BIT_WRITE_NR
                        | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

        esp_err_t err = esp_ble_gatts_add_char(
            s_profile_tab[PROFILE_APP_ID].service_handle,
            &s_profile_tab[PROFILE_APP_ID].char_uuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            s_char_property,
            &s_gatts_char_val,
            nullptr);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "add_char failed: %s", esp_err_to_name(err));
        }
        break;
    }

    case ESP_GATTS_ADD_CHAR_EVT: {
        ESP_LOGI(TAG, "ADD_CHAR_EVT status=%d attr_handle=%d svc_handle=%d",
                 param->add_char.status,
                 param->add_char.attr_handle,
                 param->add_char.service_handle);

        s_profile_tab[PROFILE_APP_ID].char_handle             = param->add_char.attr_handle;
        s_profile_tab[PROFILE_APP_ID].descr_uuid.len          = ESP_UUID_LEN_16;
        s_profile_tab[PROFILE_APP_ID].descr_uuid.uuid.uuid16  = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

        esp_err_t err = esp_ble_gatts_add_char_descr(
            s_profile_tab[PROFILE_APP_ID].service_handle,
            &s_profile_tab[PROFILE_APP_ID].descr_uuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            nullptr, nullptr);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "add_char_descr failed: %s", esp_err_to_name(err));
        }
        break;
    }

    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        s_profile_tab[PROFILE_APP_ID].descr_handle = param->add_char_descr.attr_handle;
        ESP_LOGI(TAG, "ADD_DESCR_EVT status=%d attr_handle=%d svc_handle=%d",
                 param->add_char_descr.status,
                 param->add_char_descr.attr_handle,
                 param->add_char_descr.service_handle);
        break;

    case ESP_GATTS_START_EVT:
        ESP_LOGI(TAG, "SERVICE_START_EVT status=%d handle=%d",
                 param->start.status, param->start.service_handle);
        break;

    case ESP_GATTS_CONNECT_EVT: {
        ESP_LOGI(TAG, "CONNECT_EVT conn_id=%d remote=%02x:%02x:%02x:%02x:%02x:%02x",
                 param->connect.conn_id,
                 param->connect.remote_bda[0], param->connect.remote_bda[1],
                 param->connect.remote_bda[2], param->connect.remote_bda[3],
                 param->connect.remote_bda[4], param->connect.remote_bda[5]);

        esp_ble_gap_set_pkt_data_len(param->connect.remote_bda, 251); // max PDU bytes
        s_profile_tab[PROFILE_APP_ID].conn_id = param->connect.conn_id;

        // Request faster connection parameters to improve throughput.
        esp_ble_conn_update_params_t conn_params = {};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.latency = 0;
        conn_params.max_int = 12;  // before 0x20 40 ms
        conn_params.min_int = 6;   // before 0x10 20 ms
        conn_params.timeout = 400; // before 10 s supervision timeout
        esp_ble_gap_update_conn_params(&conn_params);
        break;
    }

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "DISCONNECT_EVT reason=0x%x", param->disconnect.reason);
        reset_transfer_state();
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    case ESP_GATTS_CONF_EVT:
        if (param->conf.status != ESP_GATT_OK) {
            ESP_LOG_BUFFER_HEX(TAG, param->conf.value, param->conf.len);
        }
        break;

    case ESP_GATTS_UNREG_EVT:
    case ESP_GATTS_DELETE_EVT:
    case ESP_GATTS_STOP_EVT:
    case ESP_GATTS_ADD_INCL_SRVC_EVT:
    case ESP_GATTS_OPEN_EVT:
    case ESP_GATTS_CANCEL_OPEN_EVT:
    case ESP_GATTS_CLOSE_EVT:
    case ESP_GATTS_LISTEN_EVT:
    case ESP_GATTS_CONGEST_EVT:
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Top-level GATTS dispatcher — routes events to the correct profile handler
// ---------------------------------------------------------------------------
static void gatts_event_handler(esp_gatts_cb_event_t     event,
                                  esp_gatt_if_t            gatts_if,
                                  esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            s_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        } else {
            ESP_LOGE(TAG, "Reg app failed app_id=%04x status=%d",
                     param->reg.app_id, param->reg.status);
            return;
        }
    }

    for (int idx = 0; idx < 1; idx++) {
        if (gatts_if == ESP_GATT_IF_NONE
            || gatts_if == s_profile_tab[idx].gatts_if)
        {
            if (s_profile_tab[idx].gatts_cb) {
                s_profile_tab[idx].gatts_cb(event, gatts_if, param);
            }
        }
    }
}


// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------
extern "C" void app_main(void)
{
    /* Initialize NVS — it is used to store PHY calibration data */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    // initialise connection to co-processor
    esp_hosted_connect_to_slave();

    // init bt controller
    if (ESP_OK != esp_hosted_bt_controller_init()) {
        ESP_LOGE("INFO", "failed to init bt controller");
    }

    // enable bt controller
    if (ESP_OK != esp_hosted_bt_controller_enable()) {
        ESP_LOGE("INFO", "failed to enable bt controller");
    }

    hosted_hci_bluedroid_open();

    /* get HCI driver operations */
    esp_bluedroid_hci_driver_operations_t operations = {
        .send = hosted_hci_bluedroid_send,
        .check_send_available = hosted_hci_bluedroid_check_send_available,
        .register_host_callback = hosted_hci_bluedroid_register_host_callback,
    };
    
    esp_bluedroid_attach_hci_driver(&operations);

    if ((ret = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(TAG, "%s initialize bluedroid failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "enable bluedroid failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // -----------------------------------------------------------------------
    // Initialise EPD and FastJsonDL.
    // -----------------------------------------------------------------------
    s_epaper = new FASTEPD();

    // Replace BB_PANEL_SENSORIA_C5 with the constant that matches your panel
    // (see the BB_PANEL_* enum in FastEPD.h).
    int rc = s_epaper->initPanel(BB_PANEL_EPDINKY_P4_16);
    if (rc != BBEP_SUCCESS) {
        ESP_LOGE(TAG, "EPD init failed (%d)", rc);
        return;
    }

    // Adjust width, height and flags for your specific panel.
    s_epaper->setPanelSize(1264, 1680, BB_PANEL_FLAG_NONE, -1400);
    s_epaper->clearWhite();
    s_epaper->drawString("Waiting for BLE JSON payload", 100, 100);
    s_epaper->fullUpdate();

    s_dl = new FastJsonDL(*s_epaper);
    // Register fonts here if your JSON layouts reference them, e.g.:
    //   static const FastJsonDLFont fonts[] = { { "Ubuntu40", ubuntu40 } };
    s_dl->setFontRegistry(fonts, 3);

    // -----------------------------------------------------------------------
    // Allocate receive buffer.
    // The client sends raw JSON without a length hint, so we pre-allocate the
    // full JSON_BUF_MAX_SIZE buffer once at boot.
    // -----------------------------------------------------------------------
#if CONFIG_SPIRAM
    s_json_buf = (uint8_t *)heap_caps_malloc(JSON_BUF_MAX_SIZE, MALLOC_CAP_SPIRAM);
#else
    s_json_buf = (uint8_t *)malloc(JSON_BUF_MAX_SIZE);
#endif
    if (s_json_buf == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate receive buffer (%d bytes)", JSON_BUF_MAX_SIZE);
        return;
    }

    // -----------------------------------------------------------------------
    // Create the inactivity render timer (one-shot, auto-reload = false).
    // -----------------------------------------------------------------------
    s_render_timer = xTimerCreate("ble_render",
                                   pdMS_TO_TICKS(RENDER_IDLE_MS),
                                   pdFALSE,
                                   nullptr,
                                   render_timer_cb);
    if (s_render_timer == nullptr) {
        ESP_LOGE(TAG, "Failed to create render timer");
        return;
    }

    ESP_LOGI(TAG, "EPD initialised, waiting for BLE connection");

    // -----------------------------------------------------------------------
    // Initialise and start the BT / BLE stack.
    // -----------------------------------------------------------------------

    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(PROFILE_APP_ID));
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(500));
  

    // The BLE stack is event-driven; no polling loop is required here.
    // Advertising will start automatically once the GATTS_REG_EVT has been
    // handled and the adv-data configuration completes.
    ESP_LOGI(TAG, "BLE GATT server started — device name: \"%s\"", DEVICE_NAME);
}
