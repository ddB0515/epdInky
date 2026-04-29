#include "stc3115.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static const char *TAG = "STC3115";
#define DEBUG 1
/* ---- Internal RAM union (mirrors ST reference driver) ---- */
typedef union {
    unsigned char db[STC3115_RAM_SIZE];
    struct {
        short test_word;        /* 0-1  */
        short hrsoc;            /* 2-3  */
        short cc_cnf;           /* 4-5  */
        short vm_cnf;           /* 6-7  */
        char  soc;              /* 8    */
        char  state;            /* 9    */
        char  unused[5];        /* 10-14 */
        char  crc;              /* 15   */
    } reg;
} stc3115_ram_t;

/* ---- Internal config (computed from user config) ---- */
typedef struct {
    int vmode;
    int alm_soc;
    int alm_vbat;
    int cc_cnf;
    int vm_cnf;
    int cnom;
    int rsense;
    int relax_current;
    int eoc_current;
    int cutoff_voltage;
    unsigned char ocv_offset[16];
} stc3115_internal_config_t;

/* ---- Device struct ---- */
struct stc3115_dev_t {
    i2c_master_dev_handle_t     i2c_dev;
    uint8_t                     address;
    stc3115_internal_config_t   cfg;
    stc3115_ram_t               ram;
    stc3115_battery_data_t      last_data;
};

/* ===========================================================================
 *  Low-level I2C helpers (with retry for NAK during device-busy)
 * =========================================================================== */

#define STC3115_I2C_RETRIES     3
#define STC3115_I2C_RETRY_MS    5

static esp_err_t stc3115_write_byte(stc3115_handle_t h, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    esp_err_t ret;
    for (int i = 0; i < STC3115_I2C_RETRIES; i++) {
        ret = i2c_master_transmit(h->i2c_dev, buf, 2, -1);
        if (ret == ESP_OK) return ESP_OK;
        ESP_LOGW(TAG, "i2c write_byte reg=0x%02x retry %d/%d (%s)", reg, i+1, STC3115_I2C_RETRIES, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(STC3115_I2C_RETRY_MS));
    }
    ESP_LOGE(TAG, "i2c write_byte reg=0x%02x FAILED after %d retries", reg, STC3115_I2C_RETRIES);
    return ret;
}

static int stc3115_read_byte(stc3115_handle_t h, uint8_t reg)
{
    uint8_t val = 0;
    esp_err_t ret;
    for (int i = 0; i < STC3115_I2C_RETRIES; i++) {
        ret = i2c_master_transmit_receive(h->i2c_dev, &reg, 1, &val, 1, -1);
        if (ret == ESP_OK) return val;
        ESP_LOGW(TAG, "i2c read_byte reg=0x%02x retry %d/%d (%s)", reg, i+1, STC3115_I2C_RETRIES, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(STC3115_I2C_RETRY_MS));
    }
    ESP_LOGE(TAG, "i2c read_byte reg=0x%02x FAILED after %d retries", reg, STC3115_I2C_RETRIES);
    return -1;
}

static int stc3115_read_word(stc3115_handle_t h, uint8_t reg)
{
    uint8_t data[2] = {0};
    esp_err_t ret;
    for (int i = 0; i < STC3115_I2C_RETRIES; i++) {
        ret = i2c_master_transmit_receive(h->i2c_dev, &reg, 1, data, 2, -1);
        if (ret == ESP_OK) return (data[1] << 8) | data[0];
        ESP_LOGW(TAG, "i2c read_word reg=0x%02x retry %d/%d (%s)", reg, i+1, STC3115_I2C_RETRIES, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(STC3115_I2C_RETRY_MS));
    }
    ESP_LOGE(TAG, "i2c read_word reg=0x%02x FAILED after %d retries", reg, STC3115_I2C_RETRIES);
    return -1;
}

static esp_err_t stc3115_write_word(stc3115_handle_t h, uint8_t reg, int val)
{
    uint8_t buf[3] = {reg, val & 0xFF, (val >> 8) & 0xFF};
    esp_err_t ret;
    for (int i = 0; i < STC3115_I2C_RETRIES; i++) {
        ret = i2c_master_transmit(h->i2c_dev, buf, 3, -1);
        if (ret == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(STC3115_I2C_RETRY_MS));
    }
    return ret;
}

static esp_err_t stc3115_read_bytes(stc3115_handle_t h, uint8_t reg, uint8_t *data, int len)
{
    esp_err_t ret;
    for (int i = 0; i < STC3115_I2C_RETRIES; i++) {
        ret = i2c_master_transmit_receive(h->i2c_dev, &reg, 1, data, len, -1);
        if (ret == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(STC3115_I2C_RETRY_MS));
    }
    return ret;
}

static esp_err_t stc3115_write_bytes(stc3115_handle_t h, uint8_t reg, const uint8_t *data, int len)
{
    uint8_t *buf = malloc(len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    esp_err_t ret;
    for (int i = 0; i < STC3115_I2C_RETRIES; i++) {
        ret = i2c_master_transmit(h->i2c_dev, buf, len + 1, -1);
        if (ret == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(STC3115_I2C_RETRY_MS));
    }
    free(buf);
    return ret;
}

/* ===========================================================================
 *  RAM CRC helpers (from ST reference)
 * =========================================================================== */

static uint8_t stc3115_calc_ram_crc8(const uint8_t *data, int n)
{
    int crc = 0;
    for (int i = 0; i < n; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc <<= 1;
            if (crc & 0x100) crc ^= 7;
        }
    }
    return (uint8_t)(crc & 0xFF);
}

static void stc3115_update_ram_crc(stc3115_handle_t h)
{
    h->ram.db[STC3115_RAM_SIZE - 1] =
        stc3115_calc_ram_crc8(h->ram.db, STC3115_RAM_SIZE - 1);
}

static esp_err_t stc3115_read_ram(stc3115_handle_t h)
{
    return stc3115_read_bytes(h, STC3115_REG_RAM, h->ram.db, STC3115_RAM_SIZE);
}

static esp_err_t stc3115_write_ram(stc3115_handle_t h)
{
    return stc3115_write_bytes(h, STC3115_REG_RAM, h->ram.db, STC3115_RAM_SIZE);
}

/* ===========================================================================
 *  Conversion utility  (value * factor / 4096)
 * =========================================================================== */

static int stc3115_conv(short value, unsigned short factor)
{
    int v = ((long)value * factor) >> 11;
    v = (v + 1) / 2;
    return v;
}

/* ===========================================================================
 *  Status / ID
 * =========================================================================== */

static int stc3115_get_status_word(stc3115_handle_t h)
{
    int val = stc3115_read_word(h, STC3115_REG_MODE);
    if (val < 0) return -1;
    return val & 0x7FFF;
}

/* ===========================================================================
 *  SetParamAndRun  (from ST reference)
 * =========================================================================== */

static void stc3115_set_param_and_run(stc3115_handle_t h)
{
    int value;

    /* standby first */
    stc3115_write_byte(h, STC3115_REG_MODE, STC3115_REGMODE_DEFAULT_STANDBY);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* write OCV offset table */
    stc3115_write_bytes(h, STC3115_REG_OCVTAB, h->cfg.ocv_offset, STC3115_OCVTAB_SIZE);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* alarm levels */
    if (h->cfg.alm_soc != 0)
        stc3115_write_byte(h, STC3115_REG_ALARM_SOC, h->cfg.alm_soc * 2);
    if (h->cfg.alm_vbat != 0) {
        value = ((long)(h->cfg.alm_vbat << 9) / STC3115_VOLTAGE_FACTOR);
        stc3115_write_byte(h, STC3115_REG_ALARM_VOLTAGE, value);
    }

    /* relaxation current threshold */
    if (h->cfg.rsense != 0) {
        value = ((long)(h->cfg.relax_current << 9) /
                 (STC3115_CURRENT_FACTOR / h->cfg.rsense));
        stc3115_write_byte(h, STC3115_REG_CURRENT_THRES, value);
    }

    /* CC_CNF / VM_CNF */
    if (h->cfg.cc_cnf != 0)
        stc3115_write_word(h, STC3115_REG_CC_CNF, h->cfg.cc_cnf);
    else
        stc3115_write_word(h, STC3115_REG_CC_CNF, 395);

    if (h->cfg.vm_cnf != 0)
        stc3115_write_word(h, STC3115_REG_VM_CNF, h->cfg.vm_cnf);
    else
        stc3115_write_word(h, STC3115_REG_VM_CNF, 321);

    /* clear PORDET, BATFAIL, reset conv counter */
    stc3115_write_byte(h, STC3115_REG_CTRL, 0x03);

    /* start running */
    uint8_t mode = STC3115_GG_RUN | (STC3115_VMODE * h->cfg.vmode);
    if (h->cfg.alm_soc != 0 || h->cfg.alm_vbat != 0)
        mode |= STC3115_ALM_ENA;
    stc3115_write_byte(h, STC3115_REG_MODE, mode);
}

/* ===========================================================================
 *  Startup / Restore
 * =========================================================================== */

static int stc3115_startup(stc3115_handle_t h)
{
    int res = stc3115_get_status_word(h);
    if (res < 0) return res;

    int ocv = stc3115_read_word(h, STC3115_REG_OCV);
    int ocv_min = 6000 + h->cfg.ocv_offset[0];

    if (ocv <= ocv_min) {
        stc3115_write_word(h, STC3115_REG_SOC, 0);
        stc3115_set_param_and_run(h);
    } else {
        stc3115_set_param_and_run(h);
        stc3115_write_word(h, STC3115_REG_OCV, ocv);
    }
    return 0;
}

static int stc3115_restore_from_ram(stc3115_handle_t h)
{
    int res = stc3115_get_status_word(h);
    if (res < 0) return res;

    stc3115_set_param_and_run(h);

    /* restore last SOC if available */
    if (h->ram.reg.soc != 0 && h->ram.reg.hrsoc != 0) {
        stc3115_write_word(h, STC3115_REG_SOC, h->ram.reg.hrsoc);
    }
    return 0;
}

/* ===========================================================================
 *  Init RAM
 * =========================================================================== */

static void stc3115_init_ram_data(stc3115_handle_t h)
{
    memset(h->ram.db, 0, STC3115_RAM_SIZE);
    h->ram.reg.test_word = STC3115_RAM_TESTWORD;
    h->ram.reg.cc_cnf    = h->cfg.cc_cnf;
    h->ram.reg.vm_cnf    = h->cfg.vm_cnf;
    stc3115_update_ram_crc(h);
}

/* ===========================================================================
 *  Read battery data from registers
 * =========================================================================== */

static int stc3115_read_battery_data(stc3115_handle_t h, stc3115_battery_data_t *bd)
{
    uint8_t data[16];
    esp_err_t ret = stc3115_read_bytes(h, 0x00, data, 15);
    if (ret != ESP_OK) return -1;

    int value;

    /* SOC */
    value = (data[3] << 8) | data[2];
    bd->hrsoc = value;
    bd->soc   = (value * 10 + 256) / 512;

    /* Conversion counter */
    value = (data[5] << 8) | data[4];
    bd->conv_counter = value;

    /* Current */
    value = (data[7] << 8) | data[6];
    value &= 0x3FFF;
    if (value >= 0x2000) value -= 0x4000;
    bd->current = stc3115_conv(value, STC3115_CURRENT_FACTOR / h->cfg.rsense);

    /* Voltage */
    value = (data[9] << 8) | data[8];
    value &= 0x0FFF;
    if (value >= 0x0800) value -= 0x1000;
    bd->voltage = stc3115_conv(value, STC3115_VOLTAGE_FACTOR);

    /* Temperature */
    value = data[10];
    if (value >= 0x80) value -= 0x100;
    bd->temperature = value * 10;

    /* OCV */
    value = (data[14] << 8) | data[13];
    value &= 0x3FFF;
    if (value >= 0x2000) value -= 0x4000;
    value = stc3115_conv(value, STC3115_VOLTAGE_FACTOR);
    value = (value + 2) / 4;
    bd->ocv = value;

    return STC3115_OK;
}

/* ===========================================================================
 *  Powerdown
 * =========================================================================== */

static int stc3115_powerdown(stc3115_handle_t h)
{
    stc3115_write_byte(h, STC3115_REG_CTRL, 0x01);
    esp_err_t ret = stc3115_write_byte(h, STC3115_REG_MODE, 0);
    return (ret == ESP_OK) ? STC3115_OK : -1;
}

/* ===========================================================================
 *  Public API
 * =========================================================================== */

esp_err_t stc3115_init(i2c_master_bus_handle_t bus_handle, uint8_t address,
                       const stc3115_battery_config_t *config,
                       stc3115_handle_t *ret_handle)
{
    ESP_RETURN_ON_FALSE(bus_handle, ESP_ERR_INVALID_ARG, TAG, "Invalid bus handle");
    ESP_RETURN_ON_FALSE(config,     ESP_ERR_INVALID_ARG, TAG, "Invalid config pointer");
    ESP_RETURN_ON_FALSE(ret_handle, ESP_ERR_INVALID_ARG, TAG, "Invalid return handle pointer");

    stc3115_handle_t dev = calloc(1, sizeof(struct stc3115_dev_t));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "No memory for device struct");

    dev->address = address;

    /* Add I2C device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = address,
        .scl_speed_hz    = 400000,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev->i2c_dev);
    if (ret != ESP_OK) {
        free(dev);
        ESP_LOGE(TAG, "Failed to add device 0x%02x: %s", address, esp_err_to_name(ret));
        return ret;
    }

    /* Verify chip ID */
    int id = stc3115_read_byte(dev, STC3115_REG_ID);
    if (id != STC3115_ID_VAL) {
        ESP_LOGE(TAG, "Bad chip ID: 0x%02x (expected 0x%02x)", id, STC3115_ID_VAL);
        free(dev);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "STC3115 detected (ID=0x%02x) at address 0x%02x", id, address);

    /* Build internal config from user config */
    int rsense = (config->rsense != 0) ? config->rsense : 10;
    int rint   = (config->internal_rint != 0) ? config->internal_rint : 200;

    dev->cfg.vmode         = config->vmode;
    dev->cfg.rsense        = rsense;
    dev->cfg.cnom          = config->capacity_mah;
    dev->cfg.alm_soc       = config->alm_soc;
    dev->cfg.alm_vbat      = config->alm_vbat;
    dev->cfg.eoc_current   = config->eoc_current  ? config->eoc_current  : 75;
    dev->cfg.cutoff_voltage = config->cutoff_voltage ? config->cutoff_voltage : 3000;
    dev->cfg.relax_current = config->capacity_mah / 20;

    dev->cfg.cc_cnf = (config->capacity_mah * rsense * 250 + 6194) / 12389;
    dev->cfg.vm_cnf = (config->capacity_mah * rint * 50 + 24444) / 48889;

    for (int i = 0; i < 16; i++) {
        int ov = config->ocv_offset[i];
        if (ov > 127) ov = 127;
        if (ov < -127) ov = -127;
        dev->cfg.ocv_offset[i] = (unsigned char)ov;
    }

    /* --- Gas gauge initialization (from ST reference) --- */
    dev->last_data.presence = 1;

    stc3115_read_ram(dev);

    if (dev->ram.reg.test_word != STC3115_RAM_TESTWORD ||
        stc3115_calc_ram_crc8(dev->ram.db, STC3115_RAM_SIZE) != 0) {
        /* RAM invalid — full init */
        ESP_LOGI(TAG, "RAM invalid, performing full initialization");
        stc3115_init_ram_data(dev);
        int res = stc3115_startup(dev);
        if (res < 0) {
            ESP_LOGE(TAG, "Startup failed");
            free(dev);
            return ESP_FAIL;
        }
    } else {
        /* RAM valid — check for POR or BATFAIL */
        int ctrl = stc3115_read_byte(dev, STC3115_REG_CTRL);
        ESP_LOGI(TAG, "RAM valid, checking for POR or BATFAIL(CTRL=0x%02x)", ctrl);
        if ((ctrl & (STC3115_BATFAIL | STC3115_PORDET)) != 0) {
            ESP_LOGI(TAG, "POR/BATFAIL detected, re-initializing");
            stc3115_reset(dev);
            int res = stc3115_startup(dev);
            if (res < 0) {
                free(dev);
                return ESP_FAIL;
            }
        } else {
            ESP_LOGI(TAG, "Restoring from RAM backup");
            int res = stc3115_restore_from_ram(dev);
            if (res < 0) {
                free(dev);
                return ESP_FAIL;
            }
        }
    }

    /* Mark INIT state in RAM */
    dev->ram.reg.state = STC3115_STATE_INIT;
    stc3115_update_ram_crc(dev);
    stc3115_write_ram(dev);

    /*
     * Stabilization loop: wait for gas gauge to start converting.
     * BATFAIL from floating BATD pin is expected and harmless — the GG engine
     * keeps running with GG_RUN=1 despite BATFAIL.  Restarting on BATFAIL is
     * counter-productive because it kills the running engine and creates an
     * infinite restart cycle.  Only restart if GG_RUN is genuinely lost.
     */
    ESP_LOGI(TAG, "Waiting for gas gauge to stabilize...");
    int stable_cnt = 0;
    for (int attempt = 0; attempt < 20; attempt++) {  /* 20 * 500ms = 10s max */
        vTaskDelay(pdMS_TO_TICKS(500));

        int ctrl = stc3115_read_byte(dev, STC3115_REG_CTRL);
        int mode = stc3115_read_byte(dev, STC3115_REG_MODE);
        int cnt  = stc3115_read_word(dev, STC3115_REG_COUNTER);

        ESP_LOGI(TAG, "  stabilize[%d]: MODE=0x%02x CTRL=0x%02x cnt=%d", attempt, mode, ctrl, cnt);

        /* BATFAIL from floating BATD pin — harmless, just log and ignore */
        if (ctrl >= 0 && (ctrl & STC3115_BATFAIL)) {
            ESP_LOGW(TAG, "  BATFAIL (floating BATD) — ignoring (GG_RUN=%d)",
                     (mode >> 4) & 1);
        }

        /* Restart only if GG_RUN is genuinely lost */
        if (mode >= 0 && !(mode & STC3115_GG_RUN)) {
            ESP_LOGW(TAG, "  GG_RUN lost, performing full restart");
            stc3115_set_param_and_run(dev);
            stable_cnt = 0;
            continue;
        }

        /* Check if counter is moving */
        if (cnt > 0) {
            stable_cnt++;
            if (stable_cnt >= 2) {
                ESP_LOGI(TAG, "Gas gauge stable (cnt=%d after %d attempts)", cnt, attempt + 1);
                break;
            }
        }
    }

    *ret_handle = dev;
    ESP_LOGI(TAG, "STC3115 initialized (capacity=%dmAh, Rsense=%dmΩ, mode=%s)",
             config->capacity_mah, rsense,
             config->vmode ? "Voltage" : "Mixed");
    return ESP_OK;
}

int stc3115_task(stc3115_handle_t handle, stc3115_battery_data_t *data)
{
    if (!handle || !data) return -1;

    int res;

    /* Assume battery present until proven otherwise */
    data->presence = 1;

    /* Read status */
    res = stc3115_get_status_word(handle);
    if (res < 0) {
        ESP_LOGW(TAG, "task: get_status_word failed (I2C error)");
        return -1;
    }
    data->status_word = res;

    /* Decode status word: low byte = REG_MODE, high byte = REG_CTRL */
    uint8_t reg_mode = res & 0xFF;
    uint8_t reg_ctrl = (res >> 8) & 0xFF;
    ESP_LOGD(TAG, "task: status=0x%04x MODE=0x%02x CTRL=0x%02x", res, reg_mode, reg_ctrl);

    /* Check RAM integrity */
    esp_err_t ram_ret = stc3115_read_ram(handle);
    if (ram_ret != ESP_OK) {
        ESP_LOGW(TAG, "task: read_ram failed (%s)", esp_err_to_name(ram_ret));
    }
    if (handle->ram.reg.test_word != STC3115_RAM_TESTWORD ||
        stc3115_calc_ram_crc8(handle->ram.db, STC3115_RAM_SIZE) != 0) {
        ESP_LOGW(TAG, "task: RAM invalid (test_word=0x%04x, crc_ok=%d), reinitializing",
                 handle->ram.reg.test_word,
                 stc3115_calc_ram_crc8(handle->ram.db, STC3115_RAM_SIZE) == 0);
        stc3115_init_ram_data(handle);
        handle->ram.reg.state = STC3115_STATE_INIT;
    } else {
        ESP_LOGD(TAG, "task: RAM ok (state=%c, soc=%d, hrsoc=%d)",
                 handle->ram.reg.state, handle->ram.reg.soc, handle->ram.reg.hrsoc);
    }

    /*
     * Floating BATD pin workaround:
     * BATFAIL fires every ~500ms and fully halts the conversion engine.
     * GG_RST alone doesn't recover from this — the engine stays halted.
     * A full standby→clear→GG_RUN cycle is needed to restart conversions.
     *
     * This approach produces fresh voltage/SOC readings each cycle (confirmed
     * with voltage tracking: 3788→3786→3780mV in earlier tests).
     *
     * Current will be 0 if the system runs from USB and no current flows
     * through the sense resistor. Temperature reads 0 without an NTC.
     * Both are genuine hardware readings, not driver bugs.
     *
     * Current sign convention: negative = discharge, positive = charge.
     */
    {
        /* Quick voltage sanity check — detect real battery removal */
        int vraw = stc3115_read_word(handle, STC3115_REG_VOLTAGE);
        if (vraw >= 0) {
            int v = vraw & 0x0FFF;
            if (v >= 0x0800) v -= 0x1000;
            int voltage = stc3115_conv(v, STC3115_VOLTAGE_FACTOR);
            if (voltage <= handle->cfg.cutoff_voltage && voltage > 0) {
                ESP_LOGW(TAG, "task: battery voltage %dmV <= cutoff %dmV, resetting",
                         voltage, handle->cfg.cutoff_voltage);
                data->presence = 0;
                stc3115_reset(handle);
                return -1;
            }
        }

        /* Full restart: standby → clear flags → GG_RUN → wait for conversion */
        stc3115_write_byte(handle, STC3115_REG_MODE, STC3115_REGMODE_DEFAULT_STANDBY);
        vTaskDelay(pdMS_TO_TICKS(10));
        stc3115_write_byte(handle, STC3115_REG_CTRL, 0x03);  /* clear PORDET + BATFAIL + GG_RST */
        uint8_t mode = STC3115_GG_RUN | (STC3115_VMODE * handle->cfg.vmode);
        if (handle->cfg.alm_soc != 0 || handle->cfg.alm_vbat != 0)
            mode |= STC3115_ALM_ENA;
        stc3115_write_byte(handle, STC3115_REG_MODE, mode);
        vTaskDelay(pdMS_TO_TICKS(600));  /* wait for one fresh conversion */
    }

    /* Read battery data (fresh from the conversion above) */
    res = stc3115_read_battery_data(handle, data);
    if (res != 0) {
        ESP_LOGW(TAG, "task: read_battery_data failed (I2C read of regs 0x00-0x0E)");
        return -1;
    }

    /* Dump raw registers 0x00-0x1E with decoded explanations */
    {
        if (DEBUG)
        {
            uint8_t raw[24];  /* 0x00 through 0x1E inclusive */
            if (stc3115_read_bytes(handle, 0x00, raw, 24) == ESP_OK) {
                ESP_LOGI(TAG, "raw task1: \nMODE=0x%02x CTRL=0x%02x SOC=0x%02x%02x CNT=0x%02x%02x CUR=0x%02x%02x VOLT=0x%02x%02x TEMP=0x%02x CC_ADJ_H=0x%02x VM_ADJ_H=0x%02x OCV=0x%02x%02x",
                        raw[0x00],                /* 0x00 MODE */
                        raw[0x01],                /* 0x01 CTRL */
                        raw[0x03], raw[0x02],     /* 0x02-03 SOC (high, low) */
                        raw[0x05], raw[0x04],     /* 0x04-05 COUNTER (high, low) */
                        raw[0x07], raw[0x06],     /* 0x06-07 CURRENT (high, low) */
                        raw[0x09], raw[0x08],     /* 0x08-09 VOLTAGE (high, low) */
                        raw[0x0A],                /* 0x0A TEMPERATURE */
                        raw[0x0B],                /* 0x0B CC_ADJ_HIGH */
                        raw[0x0C],                /* 0x0C VM_ADJ_HIGH */
                        raw[0x0E], raw[0x0D]      /* 0x0D-0E OCV (high, low) */
                        );
                ESP_LOGI(TAG, "raw task2: \nCC_CNF=0x%02x%02x VM_CNF=0x%02x%02x ALM_SOC=0x%02x ALM_VOLT=0x%02x CUR_THRES=0x%02x RELAX_CNT=0x%02x RELAX_MAX=0x%02x ID=0x%02x",
                        raw[0x10], raw[0x0F],     /* 0x0F-10 CC_CNF (high, low) */
                        raw[0x12], raw[0x11],     /* 0x11-12 VM_CNF (high, low) */
                        raw[0x13],                /* 0x13 ALARM_SOC */
                        raw[0x14],                /* 0x14 ALARM_VOLTAGE */
                        raw[0x15],                /* 0x15 CURRENT_THRES */
                        raw[0x16],                /* 0x16 RELAX_COUNT */
                        raw[0x17],                /* 0x17 RELAX_MAX */
                        raw[0x18]                /* 0x18 ID */
                        );
            }
        }
        
        ESP_LOGI(TAG, "task: SOC=%d.%d%% V=%dmV I=%dmA T=%d.%dC OCV=%dmV cnt=%d",
                 data->soc / 10, data->soc % 10,
                 data->voltage, data->current,
                 data->temperature / 10, abs(data->temperature % 10),
                 data->ocv, data->conv_counter);
    }

    /* Skip if counter is 0 (conversion didn't complete in time) */
    if (data->conv_counter == 0) {
        ESP_LOGD(TAG, "task: cnt=0, skipping this cycle");
        return 0;
    }

    /* Process state — accept cnt>=1 since BATFAIL (floating BATD) limits counter to 1 */
    if (handle->ram.reg.state == STC3115_STATE_INIT) {
        if (data->conv_counter >= 1) {
            handle->ram.reg.state = STC3115_STATE_RUNNING;
            data->presence = 1;
        }
    }

    if (handle->ram.reg.state != STC3115_STATE_RUNNING) {
        data->charge_value = handle->cfg.cnom * data->soc / STC3115_MAX_SOC;
        data->temperature  = 250;  /* 25.0°C default */
        data->rem_time     = -1;
    } else {
        /* Default temperature when raw reads 0 (no NTC / single-conversion) */
        if (data->temperature == 0)
            data->temperature = 250;  /* 25.0°C */

        /* Early empty compensation */
        if (data->voltage < handle->cfg.cutoff_voltage) {
            data->soc = 0;
        } else if (data->voltage < (handle->cfg.cutoff_voltage + STC3115_VOLTAGE_SECURITY_RANGE)) {
            data->soc = data->soc * (data->voltage - handle->cfg.cutoff_voltage) /
                        STC3115_VOLTAGE_SECURITY_RANGE;
        }

        data->charge_value = handle->cfg.cnom * data->soc / STC3115_MAX_SOC;

        if ((data->status_word & STC3115_VMODE) == 0) {
            /* Mixed mode — end-of-charge & remaining time */
            if (data->current > handle->cfg.eoc_current && data->soc > 990) {
                data->soc = 990;
                stc3115_write_word(handle, STC3115_REG_SOC, 99 * 512);
            }
            if (data->current < 0) {
                data->rem_time = data->charge_value / (-data->current) * 60;
                if (data->rem_time < 0) data->rem_time = -1;
            } else {
                data->rem_time = -1;
            }
        } else {
            /* Voltage-only mode — no current measurement */
            data->current  = 0;
            data->rem_time = -1;
        }

        /* SOC clamping */
        if (data->soc > 1000) data->soc = STC3115_MAX_SOC;
        if (data->soc < 0)    data->soc = 0;
    }

    /* Save SOC backup to RAM */
    handle->ram.reg.hrsoc = data->hrsoc;
    handle->ram.reg.soc   = (data->soc + 5) / 10;
    stc3115_update_ram_crc(handle);
    stc3115_write_ram(handle);

    /* Allow STC3115 to finish processing the RAM write before next I2C access */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Cache latest data */
    memcpy(&handle->last_data, data, sizeof(stc3115_battery_data_t));

    if (handle->ram.reg.state == STC3115_STATE_RUNNING)
        return 1;
    else
        return 0;
}

esp_err_t stc3115_stop(stc3115_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    stc3115_read_ram(handle);
    handle->ram.reg.state = STC3115_STATE_POWERDN;
    stc3115_update_ram_crc(handle);
    stc3115_write_ram(handle);

    int res = stc3115_powerdown(handle);
    return (res == STC3115_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t stc3115_reset(stc3115_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    handle->ram.reg.test_word = 0;
    handle->ram.reg.state     = STC3115_STATE_UNINIT;
    esp_err_t ret = stc3115_write_ram(handle);
    if (ret != ESP_OK) return ret;

    return stc3115_write_byte(handle, STC3115_REG_CTRL, STC3115_BATFAIL | STC3115_PORDET);
}

esp_err_t stc3115_check_id(stc3115_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    int id = stc3115_read_byte(handle, STC3115_REG_ID);
    if (id == STC3115_ID_VAL) {
        ESP_LOGI(TAG, "STC3115 ID verified: 0x%02x", id);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "STC3115 ID mismatch: got 0x%02x, expected 0x%02x", id, STC3115_ID_VAL);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t stc3115_get_battery_data(stc3115_handle_t handle, stc3115_battery_data_t *data)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(data, ESP_ERR_INVALID_ARG, TAG, "Invalid data pointer");
    memcpy(data, &handle->last_data, sizeof(stc3115_battery_data_t));
    return ESP_OK;
}
