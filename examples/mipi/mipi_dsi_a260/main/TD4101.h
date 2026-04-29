#pragma once

#include <stdint.h>
#include "soc/soc_caps.h"

#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_mipi_dsi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD panel initialization commands.
 *
 */
typedef struct {
    uint8_t cmd;            /*<! The specific LCD command */
    const void *data;       /*<! Buffer that holds the command specific data */
    uint8_t data_bytes;     /*<! Size of `data` in memory, in bytes */
    uint16_t delay_ms;      /*<! Delay in milliseconds after this command */
} td4101_lcd_init_cmd_t;

/**
 * @brief LCD panel vendor configuration.
 *
 * @note  This structure needs to be passed to the `vendor_config` field in `esp_lcd_panel_dev_config_t`.
 *
 */
typedef struct {
    const td4101_lcd_init_cmd_t *init_cmds;         /*!< Pointer to initialization commands array. Set to NULL if using default commands.
                                                     *   The array should be declared as `static const` and positioned outside the function.
                                                     *   Please refer to `vendor_specific_init_default` in source file.
                                                     */
    uint16_t init_cmds_size;                        /*<! Number of commands in above array */
    struct {
        esp_lcd_dsi_bus_handle_t dsi_bus;               /*!< MIPI-DSI bus configuration */
        const esp_lcd_dpi_panel_config_t *dpi_config;   /*!< MIPI-DPI panel configuration */
        uint8_t  lane_num;
    } mipi_config;
} td4101_vendor_config_t;

/**
 * @brief Create LCD panel for model TD4101
 *
 * @note  Vendor specific initialization can be different between manufacturers, should consult the LCD supplier for initialization sequence code.
 *
 * @param[in]  io LCD panel IO handle
 * @param[in]  panel_dev_config General panel device configuration
 * @param[out] ret_panel Returned LCD panel handle
 * @return
 *      - ESP_ERR_INVALID_ARG   if parameter is invalid
 *      - ESP_OK                on success
 *      - Otherwise             on fail
 */
esp_err_t esp_lcd_new_panel_td4101(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel);

/**
 * @brief MIPI DSI bus configuration structure
 *
 */
#define TD4101_PANEL_BUS_DSI_2CH_CONFIG()                 \
    {                                                     \
        .bus_id = 0,                                      \
        .num_data_lanes = 2,                              \
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,      \
        .lane_bit_rate_mbps = 900,                        \
    }

/**
 * @brief MIPI DBI panel IO configuration structure
 *
 */
#define TD4101_PANEL_IO_DBI_CONFIG()  \
    {                                 \
        .virtual_channel = 0,         \
        .lcd_cmd_bits = 8,            \
        .lcd_param_bits = 8,          \
    }

/**
 * @brief MIPI DPI configuration structure
 *
 * @note  refresh_rate = (dpi_clock_freq_mhz * 1000000) / (h_res + hsync_pulse_width + hsync_back_porch + hsync_front_porch)
 *                                                      / (v_res + vsync_pulse_width + vsync_back_porch + vsync_front_porch)
 * @note  refresh_rate = (45 * 1000000) / (540 + 40 + 4 + 364) / (960 + 13 + 2 + 244) = 38.94 Mhz
 */

#define TD4101_540_960_PANEL_DPI_CONFIG(px_format)             \
    {                                                           \
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,            \
        .dpi_clock_freq_mhz = 40,                               \
        .virtual_channel = 0,                                   \
        .in_color_format = px_format,                              \
        .num_fbs = 1,                                           \
        .video_timing = {                                       \
            .h_size = 540,                                      \
            .v_size = 960,                                      \
            .hsync_back_porch = 40,                             \
            .hsync_pulse_width = 4,                             \
            .hsync_front_porch = 364,                           \
            .vsync_back_porch = 13,                             \
            .vsync_pulse_width = 2,                             \
            .vsync_front_porch = 244,                           \
        },                                                      \
    }

#ifdef __cplusplus
}
#endif