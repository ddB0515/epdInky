#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/lock.h>
#include <sys/select.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "main.h"
#include "STC3115.h" 
#include "kxtj3_1057.h"
#include "rv3028.h"
#include "TPS65185.h"

#include "FastEPD.h"
#include "Roboto_Black_80.h"
#include "Roboto_Black_40.h"
#include "Courier_Prime_16.h"

static const char *TAG = "eInky-P4";

////////////////////////////////////////////
//////////// I2C EXPANDER //////////////////
////////////////////////////////////////////
#define I2C_MASTER_SCL_IO           29      /*!< GPIO number for I2C master clock */
#define I2C_MASTER_SDA_IO           28      /*!< GPIO number for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0 /*!< I2C master i2c port number */

i2c_master_bus_handle_t i2c_bus_handle = NULL;
tca6408_handle_t tca_board = NULL;
tca6408_handle_t tca_display = NULL;
stc3115_handle_t stc3115_handle = NULL;
kxtj3_handle_t kxtj3_handle = NULL;
rv3028_handle_t rv3028_handle = NULL;
tps65185_handle_t epd_pmic_handle = NULL;

FASTEPDSTATE bbep;

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

/**
 * @brief Scan for I2C devices
 */
void i2c_scan_devices(void)
{
    uint8_t address, devices_found;
    devices_found = 0;
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


/* -----------------------------------------------------------------------
 * Helper: draw a centred title bar at the top of the screen
 * ----------------------------------------------------------------------- */
static void draw_title(const char *text, int w, int h)
{
    (void)w;
    int bar_h = h / 12;
    bbepRectangle(&bbep, 0, 0, bbep.width - 1, bar_h, BBEP_BLACK, 1);
    bbepWriteString(&bbep, 8, bar_h / 4, (char *)text, FONT_16x16, BBEP_WHITE);
}

/* -----------------------------------------------------------------------
 * DEMO 1 – Font showcase  (full update)
 * Shows every available font at the same left margin so differences
 * in size are immediately obvious.
 * ----------------------------------------------------------------------- */
static void demo_fonts(void)
{
    int w = bbep.width;
    int h = bbep.height;
    int margin = w / 40;           /* ~20 px on 800-wide */
    int y;

    bbepFillScreen(&bbep, BBEP_WHITE);

    draw_title("DEMO 1 : FONT SHOWCASE", w, h);

    /* Roboto Black 80 */
    y = h / 4;
    bbepWriteStringCustom(&bbep, Roboto_Black_80, margin, y,
                          "Roboto 80", BBEP_BLACK);

    /* Roboto Black 40 */
    y += h / 5;
    bbepWriteStringCustom(&bbep, Roboto_Black_40, margin, y,
                          "Roboto Black 40pt", BBEP_BLACK);

    /* Courier Prime 16 */
    y += h / 8;
    bbepWriteStringCustom(&bbep, Courier_Prime_16, margin, y,
                          "Courier Prime 16 - fixed-width font", BBEP_BLACK);

    /* Built-in 12x16 */
    y += h / 16;
    bbepWriteString(&bbep, margin, y,
                    "Built-in FONT_12x16 - medium size", FONT_12x16, BBEP_BLACK);

    /* Built-in 8x8 */
    y += h / 18;
    bbepWriteString(&bbep, margin, y,
                    "Built-in FONT_8x8  - smallest built-in", FONT_8x8, BBEP_BLACK);

    /* Separator + note at bottom */
    y = h - h / 14;
    bbepDrawLine(&bbep, 0, y, w - 1, y, BBEP_BLACK);
    y += 4;
    bbepWriteString(&bbep, margin, y,
                    "FastEPD font demo", FONT_8x8, BBEP_BLACK);

    bbepFullUpdate(&bbep, CLEAR_SLOW, 0, NULL);
    vTaskDelay(pdMS_TO_TICKS(5000));
}

/* -----------------------------------------------------------------------
 * DEMO 2 – Shapes showcase  (full update)
 * 3×2 tile grid: each tile has a black header label and a centred shape.
 * Tiles: Lines starburst | Nested rects | Nested round-rects
 *        Filled checkerboard | Bullseye circle | Concentric ellipses
 * ----------------------------------------------------------------------- */
static void demo_shapes(void)
{
    int w      = bbep.width;
    int h      = bbep.height;
    int margin = w / 80;
    int title_h = h / 12;

    bbepFillScreen(&bbep, BBEP_WHITE);
    draw_title("DEMO 2 : SHAPES", w, h);

    int work_y = title_h + margin;
    int work_h = h - work_y - margin;
    int col_w  = w / 3;
    int row_h  = work_h / 2;
    int hdr_h  = row_h / 6;   /* label bar height inside each tile */

    /* outer border */
    bbepRectangle(&bbep, 0, work_y, w - 1, work_y + work_h - 1, BBEP_BLACK, 0);
    /* vertical dividers */
    bbepDrawLine(&bbep, col_w,     work_y, col_w,     work_y + work_h, BBEP_BLACK);
    bbepDrawLine(&bbep, col_w * 2, work_y, col_w * 2, work_y + work_h, BBEP_BLACK);
    /* horizontal divider */
    bbepDrawLine(&bbep, 0, work_y + row_h, w - 1, work_y + row_h, BBEP_BLACK);

    /* ---- Tile header bars (filled black, white label) ---- */
    int lbl_x[3] = { margin, col_w + margin, col_w * 2 + margin };
    int lbl_y0   = work_y + hdr_h / 4;
    int lbl_y1   = work_y + row_h + hdr_h / 4;

    /* Row 0 headers */
    bbepRectangle(&bbep, 1,          work_y + 1, col_w - 1,    work_y + hdr_h, BBEP_BLACK, 1);
    bbepRectangle(&bbep, col_w + 1,  work_y + 1, col_w * 2 - 1, work_y + hdr_h, BBEP_BLACK, 1);
    bbepRectangle(&bbep, col_w*2 + 1, work_y + 1, w - 1,        work_y + hdr_h, BBEP_BLACK, 1);
    bbepWriteString(&bbep, lbl_x[0], lbl_y0, "LINES",      FONT_12x16, BBEP_WHITE);
    bbepWriteString(&bbep, lbl_x[1], lbl_y0, "RECTANGLE",  FONT_12x16, BBEP_WHITE);
    bbepWriteString(&bbep, lbl_x[2], lbl_y0, "ROUND RECT", FONT_12x16, BBEP_WHITE);

    /* Row 1 headers */
    int r1 = work_y + row_h;
    bbepRectangle(&bbep, 1,          r1 + 1, col_w - 1,    r1 + hdr_h, BBEP_BLACK, 1);
    bbepRectangle(&bbep, col_w + 1,  r1 + 1, col_w * 2 - 1, r1 + hdr_h, BBEP_BLACK, 1);
    bbepRectangle(&bbep, col_w*2 + 1, r1 + 1, w - 1,        r1 + hdr_h, BBEP_BLACK, 1);
    bbepWriteString(&bbep, lbl_x[0], lbl_y1, "FILLED RECT", FONT_12x16, BBEP_WHITE);
    bbepWriteString(&bbep, lbl_x[1], lbl_y1, "CIRCLE",      FONT_12x16, BBEP_WHITE);
    bbepWriteString(&bbep, lbl_x[2], lbl_y1, "ELLIPSE",     FONT_12x16, BBEP_WHITE);

    /* ---- Shape body layout ---- */
    int body0_top = work_y + hdr_h + 2;
    int body0_h   = row_h - hdr_h - 4;
    int body1_top = r1 + hdr_h + 2;
    int body1_h   = row_h - hdr_h - 4;

    /* Centre X of each column */
    int cx0 = col_w / 2;
    int cx1 = col_w + col_w / 2;
    int cx2 = col_w * 2 + col_w / 2;

    int cx, cy, sw, sh, r;

    /* ---- Row 0, Col 0: LINES – starburst of 8 spokes ---- */
    cx = cx0;
    cy = body0_top + body0_h / 2;
    r  = (body0_h < col_w ? body0_h : col_w) * 2 / 5;
    bbepDrawLine(&bbep, cx,       cy - r, cx,       cy + r, BBEP_BLACK); /* N-S  */
    bbepDrawLine(&bbep, cx - r,   cy,     cx + r,   cy,     BBEP_BLACK); /* W-E  */
    bbepDrawLine(&bbep, cx - r*7/10, cy - r*7/10, cx + r*7/10, cy + r*7/10, BBEP_BLACK); /* NW-SE */
    bbepDrawLine(&bbep, cx + r*7/10, cy - r*7/10, cx - r*7/10, cy + r*7/10, BBEP_BLACK); /* NE-SW */
    bbepDrawLine(&bbep, cx - r*3/10, cy - r, cx + r*3/10, cy + r, BBEP_BLACK); /* NNW-SSE */
    bbepDrawLine(&bbep, cx + r*3/10, cy - r, cx - r*3/10, cy + r, BBEP_BLACK); /* NNE-SSW */
    bbepDrawLine(&bbep, cx - r, cy - r*3/10, cx + r, cy + r*3/10, BBEP_BLACK); /* WNW-ESE */
    bbepDrawLine(&bbep, cx - r, cy + r*3/10, cx + r, cy - r*3/10, BBEP_BLACK); /* WSW-ENE */
    /* small centre dot */
    bbepEllipse(&bbep, cx, cy, r / 8, r / 8, 0xff, BBEP_BLACK, 1);

    /* ---- Row 0, Col 1: RECTANGLE – three nested outlines ---- */
    cx = cx1;
    cy = body0_top + body0_h / 2;
    sw = col_w * 6 / 10;
    sh = body0_h * 6 / 10;
    bbepRectangle(&bbep, cx - sw/2,       cy - sh/2,       cx + sw/2,       cy + sh/2,       BBEP_BLACK, 0);
    bbepRectangle(&bbep, cx - sw*3/8,     cy - sh*3/8,     cx + sw*3/8,     cy + sh*3/8,     BBEP_BLACK, 0);
    bbepRectangle(&bbep, cx - sw/4,       cy - sh/4,       cx + sw/4,       cy + sh/4,       BBEP_BLACK, 0);

    /* ---- Row 0, Col 2: ROUND RECT – two nested ---- */
    cx = cx2;
    cy = body0_top + body0_h / 2;
    sw = col_w * 6 / 10;
    sh = body0_h * 6 / 10;
    bbepRoundRect(&bbep, cx - sw/2,   cy - sh/2,   sw,     sh,     sh/5,  BBEP_BLACK, 0);
    bbepRoundRect(&bbep, cx - sw*3/8, cy - sh*3/8, sw*3/4, sh*3/4, sh/7,  BBEP_BLACK, 0);
    bbepRoundRect(&bbep, cx - sw/4,   cy - sh/4,   sw/2,   sh/2,   sh/10, BBEP_BLACK, 1);

    /* ---- Row 1, Col 0: FILLED RECT – 2×2 checkerboard ---- */
    cx = cx0;
    cy = body1_top + body1_h / 2;
    sw = col_w * 6 / 10;
    sh = body1_h * 6 / 10;
    bbepRectangle(&bbep, cx - sw/2, cy - sh/2, cx,       cy,       BBEP_BLACK, 1); /* TL */
    bbepRectangle(&bbep, cx,        cy - sh/2, cx + sw/2, cy,       BBEP_BLACK, 0); /* TR outline */
    bbepRectangle(&bbep, cx - sw/2, cy,        cx,        cy + sh/2, BBEP_BLACK, 0); /* BL outline */
    bbepRectangle(&bbep, cx,        cy,        cx + sw/2, cy + sh/2, BBEP_BLACK, 1); /* BR */
    /* outer border to tie it together */
    bbepRectangle(&bbep, cx - sw/2, cy - sh/2, cx + sw/2, cy + sh/2, BBEP_BLACK, 0);

    /* ---- Row 1, Col 1: CIRCLE – bullseye (outer outline + inner filled) ---- */
    cx = cx1;
    cy = body1_top + body1_h / 2;
    r  = (body1_h < col_w ? body1_h : col_w) * 2 / 5;
    bbepEllipse(&bbep, cx, cy, r,     r,     0xff, BBEP_BLACK, 0);
    bbepEllipse(&bbep, cx, cy, r*6/10, r*6/10, 0xff, BBEP_BLACK, 0);
    bbepEllipse(&bbep, cx, cy, r*3/10, r*3/10, 0xff, BBEP_BLACK, 1);

    /* ---- Row 1, Col 2: ELLIPSE – two concentric ---- */
    cx = cx2;
    cy = body1_top + body1_h / 2;
    int rx = col_w   * 4 / 10;
    int ry = body1_h * 3 / 10;
    bbepEllipse(&bbep, cx, cy, rx,     ry,     0xff, BBEP_BLACK, 0);
    bbepEllipse(&bbep, cx, cy, rx*6/10, ry*6/10, 0xff, BBEP_BLACK, 0);
    bbepEllipse(&bbep, cx, cy, rx*3/10, ry*3/10, 0xff, BBEP_BLACK, 1);

    bbepFullUpdate(&bbep, CLEAR_SLOW, 0, NULL);
    vTaskDelay(pdMS_TO_TICKS(5000));
}

/* -----------------------------------------------------------------------
 * DEMO 3 – Partial update
 * One full update establishes a 3-zone baseline. Then each zone is updated
 * individually (counter / progress bar / shape), 3 rounds. The viewer sees
 * only the active zone flicker while the other two stay completely still.
 * ----------------------------------------------------------------------- */
static void demo_partial_update(void)
{
    int w         = bbep.width;
    int h         = bbep.height;
    int margin    = w / 40;
    int title_h   = h / 12;
    int work_h    = h - title_h;

    /* Zone heights: counter=20%, progress=30%, shape=50% */
    int zh[3] = { work_h / 5, work_h * 3 / 10, work_h - work_h / 5 - work_h * 3 / 10 };

    int zy[3] = { title_h,
                  title_h + zh[0],
                  title_h + zh[0] + zh[1] };

    /* progress bar geometry (zone 1) */
    int bar_x  = margin * 3;
    int bar_y  = zy[1] + zh[1] * 5 / 8;
    int bar_w  = w - margin * 6;
    int bar_h  = zh[1] / 7;

    /* shape centre (zone 2) */
    int scx = w / 2;
    int scy = zy[2] + zh[2] * 6 / 10;
    int sr  = zh[2] * 9 / 40;   /* 3/10 * 3/4 = 1/4 smaller */

    char buf[32];

    /* ------------------------------------------------------------------ */
    /* Draw full baseline and do ONE full update                           */
    /* ------------------------------------------------------------------ */
    bbepFillScreen(&bbep, BBEP_WHITE);
    draw_title("DEMO 3 : PARTIAL UPDATE", w, h);

    /* Zone borders */
    for (int i = 0; i < 3; i++)
        bbepRectangle(&bbep, 0, zy[i], w - 1, zy[i] + zh[i] - 1, BBEP_BLACK, 0);

    /* Zone 0 – COUNTER */
    int digit_y = zy[0] + zh[0] - 35;   /* bottom-aligned; top gap separates from title */
    bbepWriteString(&bbep, margin, digit_y - 22,
                    "ZONE 1 - COUNTER", FONT_12x16, BBEP_BLACK);
    bbepWriteStringCustom(&bbep, Roboto_Black_80,
                          w / 2 - margin * 3, digit_y, "0", BBEP_BLACK);

    /* Zone 1 – PROGRESS BAR */
    bbepWriteString(&bbep, margin, zy[1] + margin,
                    "ZONE 2 - PROGRESS BAR", FONT_12x16, BBEP_BLACK);
    bbepRectangle(&bbep, bar_x, bar_y, bar_x + bar_w, bar_y + bar_h, BBEP_BLACK, 0);

    /* Zone 2 – SHAPE */
    bbepWriteString(&bbep, margin, zy[2] + margin,
                    "ZONE 3 - SHAPE", FONT_12x16, BBEP_BLACK);
    bbepEllipse(&bbep, scx, scy, sr, sr, 0xff, BBEP_BLACK, 0);

    bbepFullUpdate(&bbep, CLEAR_SLOW, 0, NULL);
    vTaskDelay(pdMS_TO_TICKS(1500));

    /* ------------------------------------------------------------------ */
    /* 3 rounds: update each zone in turn                                  */
    /* ------------------------------------------------------------------ */
    for (int step = 1; step <= 3; step++) {

        /* === Zone 0: counter increments === */
        bbepRectangle(&bbep, 1, zy[0] + 1,
                      w - 2, zy[0] + zh[0] - 2, BBEP_WHITE, 1);
        bbepRectangle(&bbep, 0, zy[0], w - 1, zy[0] + zh[0] - 1, BBEP_BLACK, 0);
        bbepWriteString(&bbep, margin, zy[0] + zh[0] - 57,
                        "ZONE 1 - COUNTER", FONT_12x16, BBEP_BLACK);
        snprintf(buf, sizeof(buf), "%d", step);
        bbepWriteStringCustom(&bbep, Roboto_Black_80,
                              w / 2 - margin * 3, zy[0] + zh[0] - 35, buf, BBEP_BLACK);
        bbepPartialUpdate(&bbep, 0, zy[0], zy[0] + zh[0] - 1);
        vTaskDelay(pdMS_TO_TICKS(1500));

        /* === Zone 1: progress bar fills up === */
        bbepRectangle(&bbep, 1, zy[1] + 1,
                      w - 2, zy[1] + zh[1] - 2, BBEP_WHITE, 1);
        bbepRectangle(&bbep, 0, zy[1], w - 1, zy[1] + zh[1] - 1, BBEP_BLACK, 0);
        bbepWriteString(&bbep, margin, zy[1] + margin,
                        "ZONE 2 - PROGRESS BAR", FONT_12x16, BBEP_BLACK);
        bbepRectangle(&bbep, bar_x, bar_y, bar_x + bar_w, bar_y + bar_h, BBEP_BLACK, 0);
        int fill = bar_w * step / 3;
        if (fill > 2)
            bbepRectangle(&bbep, bar_x + 1, bar_y + 1,
                          bar_x + fill, bar_y + bar_h - 1, BBEP_BLACK, 1);
        snprintf(buf, sizeof(buf), "%d%%  < refreshing", step * 33 + (step == 3 ? 1 : 0));
        bbepWriteString(&bbep, bar_x, bar_y - zh[1] / 10, buf, FONT_12x16, BBEP_BLACK);
        bbepPartialUpdate(&bbep, 0, zy[1], zy[1] + zh[1] - 1);
        vTaskDelay(pdMS_TO_TICKS(1500));

        /* === Zone 2: shape morphs each step === */
        bbepRectangle(&bbep, 1, zy[2] + 1,
                      w - 2, zy[2] + zh[2] - 2, BBEP_WHITE, 1);
        bbepRectangle(&bbep, 0, zy[2], w - 1, zy[2] + zh[2] - 1, BBEP_BLACK, 0);
        bbepWriteString(&bbep, margin, zy[2] + margin,
                        "ZONE 3 - SHAPE", FONT_12x16, BBEP_BLACK);
        if (step == 1) {
            /* outline circle */
            bbepEllipse(&bbep, scx, scy, sr, sr, 0xff, BBEP_BLACK, 0);
            bbepWriteString(&bbep, w * 7 / 10, scy - margin, "circle", FONT_12x16, BBEP_BLACK);
        } else if (step == 2) {
            /* filled circle + outer ring */
            bbepEllipse(&bbep, scx, scy, sr * 3 / 2, sr * 3 / 2, 0xff, BBEP_BLACK, 0);
            bbepEllipse(&bbep, scx, scy, sr, sr, 0xff, BBEP_BLACK, 1);
            bbepWriteString(&bbep, w * 7 / 10, scy - margin, "bullseye", FONT_12x16, BBEP_BLACK);
        } else {
            /* round rect */
            bbepRoundRect(&bbep, scx - sr, scy - sr * 2 / 3,
                          sr * 2, sr * 4 / 3, sr / 4, BBEP_BLACK, 0);
            bbepEllipse(&bbep, scx, scy, sr / 4, sr / 4, 0xff, BBEP_BLACK, 1);
            bbepWriteString(&bbep, w * 7 / 10, scy - margin, "round rect", FONT_12x16, BBEP_BLACK);
        }
        bbepPartialUpdate(&bbep, 0, zy[2], zy[2] + zh[2] - 1);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

/* -----------------------------------------------------------------------
 * DEMO 4 – Full update modes (CLEAR_FAST vs CLEAR_SLOW)
 * Draws a high-contrast checkerboard pattern, then clears it first with
 * CLEAR_FAST and then with CLEAR_SLOW so the quality difference is visible.
 * ----------------------------------------------------------------------- */
static void demo_full_update(void)
{
    int w      = bbep.width;
    int h      = bbep.height;
    int margin = w / 40;
    int cell   = w / 10;   /* ~80 px on 800-wide */

    /* --- draw dense checkerboard to stress the clearing --- */
    bbepFillScreen(&bbep, BBEP_WHITE);
    draw_title("DEMO 4 : FULL UPDATE MODES", w, h);
    int title_h = h / 12;
    for (int row = 0; row * cell < h - title_h; row++) {
        for (int col = 0; col * cell < w; col++) {
            if ((row + col) & 1) {
                bbepRectangle(&bbep,
                              col * cell, title_h + row * cell,
                              (col + 1) * cell - 1, title_h + (row + 1) * cell - 1,
                              BBEP_BLACK, 1);
            }
        }
    }
    /* label */
    bbepWriteString(&bbep, margin, h / 2 - h / 20,
                    "Checkerboard stress pattern", FONT_12x16, BBEP_WHITE);

    bbepFullUpdate(&bbep, CLEAR_SLOW, 0, NULL);
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* --- clear with CLEAR_FAST and show result --- */
    bbepFillScreen(&bbep, BBEP_WHITE);
    bbepWriteString(&bbep, margin, h / 4,
                    "CLEAR_FAST (8 passes)", FONT_16x16, BBEP_BLACK);
    bbepWriteString(&bbep, margin, h / 4 + h / 15,
                    "Faster but may leave slight ghosting", FONT_12x16, BBEP_BLACK);
    bbepRoundRect(&bbep, margin, h / 4 - h / 30,
                  w - margin * 2, h / 5,
                  w / 40, BBEP_BLACK, 0);

    bbepFullUpdate(&bbep, CLEAR_FAST, 0, NULL);
    vTaskDelay(pdMS_TO_TICKS(2500));

    /* --- clear with CLEAR_SLOW and show result --- */
    bbepFillScreen(&bbep, BBEP_WHITE);
    bbepWriteString(&bbep, margin, h / 2,
                    "CLEAR_SLOW (10 passes)", FONT_16x16, BBEP_BLACK);
    bbepWriteString(&bbep, margin, h / 2 + h / 15,
                    "Slower but cleaner result", FONT_12x16, BBEP_BLACK);
    bbepRoundRect(&bbep, margin, h / 2 - h / 30,
                  w - margin * 2, h / 5,
                  w / 40, BBEP_BLACK, 0);

    bbepFullUpdate(&bbep, CLEAR_SLOW, 0, NULL);
    vTaskDelay(pdMS_TO_TICKS(2500));
}

void app_main(void)
{
    i2c_master_init();

    ESP_LOGI(TAG, "tca6408_init...");

    if (tca6408_init(i2c_bus_handle, 0x21, &tca_board) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TCA6408 at 0x20");
    }

    tca6408_set_config(tca_board, 0x00); // All pins as output
    tca6408_set_output_val(tca_board, 0x00); // Set all pins low

    i2c_scan_devices();

    esp_err_t rc = bbepInitPanel(&bbep, BB_PANEL_EPDINKY_P4_16, 20000000);
    if (rc != BBEP_SUCCESS) {
        ESP_LOGE(TAG, "bbepInitPanel failed: %d", rc);
        return;
    }

    // Change for your panel size and orientation here if needed
    bbepSetPanelSize(&bbep, 2400, 1034, BB_PANEL_FLAG_MIRROR_Y, -1000);
    //bbepSetPanelSize(&bbep, 1264, 1680, BB_PANEL_FLAG_NONE, -1400);
    //bbepSetPanelSize(&bbep, 1200, 825, BB_PANEL_FLAG_NONE, -1620);

    /* Initial full clear */
    bbepFillScreen(&bbep, BBEP_WHITE);
    bbepFullUpdate(&bbep, CLEAR_SLOW, 0, NULL);

    /* Run the four demos back-to-back */
    demo_fonts();
    demo_shapes();
    demo_partial_update();
    demo_full_update();

    /* End screen */
    bbepFillScreen(&bbep, BBEP_WHITE);
    bbepWriteStringCustom(&bbep, Roboto_Black_40,
                          bbep.width / 20, bbep.height / 3,
                          "Demo complete.", BBEP_BLACK);
    bbepWriteString(&bbep, bbep.width / 20, bbep.height / 3 + bbep.height / 8,
                    "FastEPD on epdInky P4", FONT_16x16, BBEP_BLACK);
    bbepFullUpdate(&bbep, CLEAR_SLOW, 0, NULL);
}
