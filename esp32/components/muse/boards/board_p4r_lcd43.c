/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * P4_R 4.3": an ESP32-P4 (silicon v1.x, 16 MB flash, 32 MB PSRAM) board with a
 * 4.3" 480x800 ST7102 MIPI-DSI panel mounted sideways (800x480 here), an ST7123
 * touch controller, an ES8388 codec for the speaker, two PDM microphones, and
 * an ESP32-C6 for Wi-Fi and BLE over SDIO (ESP-Hosted). Its pin map, panel
 * init sequence and the traps below were measured on the boards with their
 * stock firmware. The hard button (GPIO20) is left unused: here everything is
 * on the touch screen, and the mic icon on it talks.
 *
 * - The I2C bus (GPIO7/8) is shared by the touch controller (0x55) and the
 *   codec (0x10).
 * - TP_RST (GPIO32) is wired but must not be driven: the ST7123 NACKs after a
 *   pulse on it, so the touch config leaves it unset.
 * - The panel gets its rail 50 ms before the reset, and the backlight (a boost
 *   enable on GPIO34, PWM) comes on only once there's a frame to show.
 * - Touch is polled: INT (GPIO31) isn't used.
 *
 * The panel's coordinates are portrait; LVGL draws 800x480 and the adapter
 * rotates it, which turns the touch points too.
 */
#include <math.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "muse_audio.h"
#include "muse_board.h"
#include "muse_mem.h"
#include "p4r/esp_lcd_st7102.h"
#include "p4r/esp_lcd_touch_st7123.h"

static const char *TAG = "board";

#define PANEL_W 480            /* the panel's own, portrait */
#define PANEL_H 800
#define LCD_RST GPIO_NUM_33
#define BL_EN GPIO_NUM_34      /* boost enable, active high; PWM dims it */
#define STRIP_ROWS 32          /* LVGL draws in strips of 800 x this many pixels, in internal RAM (three: one to rotate in) */
#define MIPI_PHY_LDO_CHAN 3
#define MIPI_PHY_LDO_MV 2500

#define I2C_SDA GPIO_NUM_7
#define I2C_SCL GPIO_NUM_8
#define CODEC_I2C_ADDR 0x10    /* ES8388, 7-bit */

#define I2S_MCLK GPIO_NUM_13   /* the speaker: ES8388 on I2S1 */
#define I2S_BCLK GPIO_NUM_12
#define I2S_WS GPIO_NUM_10
#define I2S_DOUT GPIO_NUM_11   /* to the codec's DSDIN, not GPIO9 */
#define PA_EN GPIO_NUM_6       /* two NS4150B amps, active high */
#define MIC_CLK GPIO_NUM_51    /* two PDM mics on I2S0: left and right on one data line */
#define MIC_DIN GPIO_NUM_52
#define MIC_GAIN_OFFSET_DB 6   /* the mics are quiet: settings' dB plus this */
#define VOL_WRITES 4           /* a volume write is repeated: it's often lost */

static i2c_master_bus_handle_t s_i2c;
static i2s_chan_handle_t s_spk_tx, s_mic_rx;
static bool s_spk_inited, s_spk_on, s_mic_on, s_mic_settled;
static int32_t s_mic_dc[2];
static int s_mic_gain_q8 = 256;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;

/* The panel's init sequence, from the board's stock firmware. */
static const st7102_lcd_init_cmd_t s_lcd_init[] = {
    {0x99, (uint8_t[]){0x71, 0x02, 0xa2}, 3, 0},
    {0x99, (uint8_t[]){0x71, 0x02, 0xa3}, 3, 0},
    {0x99, (uint8_t[]){0x71, 0x02, 0xa4}, 3, 0},
    {0xB0, (uint8_t[]){0x22, 0x57, 0x1E, 0x61, 0x2F, 0x57, 0x61}, 7, 0},
    {0xB7, (uint8_t[]){0x64, 0x64}, 2, 0},
    {0xBF, (uint8_t[]){0xB4, 0xB4}, 2, 0},
    {0xC8, (uint8_t[]){0x00, 0x00, 0x13, 0x24, 0x44, 0x00, 0x74, 0x03, 0xB8, 0x04,
                       0x11, 0x16, 0x08, 0x86, 0x04, 0x21, 0xD3, 0x02, 0x10, 0x0F,
                       0x22, 0x4D, 0x0E, 0x90, 0x09, 0x32, 0xF0, 0x0B, 0x40, 0x0E,
                       0xF3, 0x7D, 0x0E, 0xA9, 0xBF, 0x03, 0xC4}, 37, 0},
    {0xC9, (uint8_t[]){0x00, 0x00, 0x13, 0x24, 0x44, 0x00, 0x74, 0x03, 0xB8, 0x04,
                       0x11, 0x16, 0x08, 0x86, 0x04, 0x21, 0xD3, 0x02, 0x10, 0x0F,
                       0x22, 0x4D, 0x0E, 0x90, 0x09, 0x32, 0xF0, 0x0B, 0x40, 0x0E,
                       0xF3, 0x7D, 0x0E, 0xA9, 0xBF, 0x03, 0xC4}, 37, 0},
    {0xD7, (uint8_t[]){0x10, 0x0C, 0x36, 0x19, 0x90, 0x90}, 6, 0},
    {0xA3, (uint8_t[]){0x51, 0x03, 0x80, 0xCF, 0x44, 0x00, 0x00, 0x00, 0x00, 0x04,
                       0x78, 0x78, 0x00, 0x1A, 0x00, 0x45, 0x05, 0x00, 0x00, 0x00,
                       0x00, 0x46, 0x00, 0x00, 0x02, 0x20, 0x52, 0x00, 0x05, 0x00,
                       0x00, 0xFF}, 32, 0},
    {0xA6, (uint8_t[]){0x02, 0x00, 0x24, 0x55, 0x35, 0x00, 0x38, 0x00, 0x78, 0x78,
                       0x00, 0x24, 0x55, 0x36, 0x00, 0x37, 0x00, 0x78, 0x78, 0x02,
                       0xAC, 0x51, 0x3A, 0x00, 0x00, 0x00, 0x78, 0x78, 0x03, 0xAC,
                       0x21, 0x00, 0x04, 0x00, 0x00, 0x78, 0x78, 0x3e, 0x00, 0x06,
                       0x00, 0x00, 0x00, 0x00}, 44, 0},
    {0xA7, (uint8_t[]){0x19, 0x19, 0x00, 0x64, 0x40, 0x07, 0x16, 0x40, 0x00, 0x04,
                       0x03, 0x78, 0x78, 0x00, 0x64, 0x40, 0x25, 0x34, 0x00, 0x00,
                       0x02, 0x01, 0x78, 0x78, 0x00, 0x64, 0x40, 0x4B, 0x5A, 0x00,
                       0x00, 0x02, 0x01, 0x78, 0x78, 0x00, 0x24, 0x40, 0x69, 0x78,
                       0x00, 0x00, 0x00, 0x00, 0x78, 0x78, 0x00, 0x44}, 48, 0},
    {0xAC, (uint8_t[]){0x08, 0x0A, 0x11, 0x00, 0x13, 0x03, 0x1B, 0x18, 0x06, 0x1A,
                       0x19, 0x1B, 0x1B, 0x1B, 0x18, 0x1B, 0x09, 0x0B, 0x10, 0x02,
                       0x12, 0x01, 0x1B, 0x18, 0x06, 0x1A, 0x19, 0x1B, 0x1B, 0x1B,
                       0x18, 0x1B, 0xFF, 0x67, 0xFF, 0x67, 0x00}, 37, 0},
    {0xAD, (uint8_t[]){0xCC, 0x40, 0x46, 0x11, 0x04, 0x78, 0x78}, 7, 0},
    {0xE8, (uint8_t[]){0x30, 0x07, 0x00, 0x94, 0x94, 0x9C, 0x00, 0xE2, 0x04, 0x00,
                       0x00, 0x00, 0x00, 0xEF}, 14, 0},
    {0xE7, (uint8_t[]){0x8B, 0x3C, 0x00, 0x0C, 0xF0, 0x5D, 0x00, 0x5D, 0x00, 0x5D,
                       0x00, 0x5D, 0x00, 0xFF, 0x00, 0x08, 0x7B, 0x00, 0x00, 0xC8,
                       0x6A, 0x5A, 0x08, 0x1A, 0x3C, 0x00, 0x81, 0x01, 0xCC, 0x01,
                       0x7F, 0xF0, 0x22}, 33, 0},
    {0x11, (uint8_t[]){0x00}, 0, 600},   /* sleep out */
    {0x29, (uint8_t[]){0x00}, 0, 120},   /* display on */
};

/*
 * The I2S lines held low, and the amp off. A soft reset (a crash, the watchdog)
 * leaves the last run's 4 MHz MCLK going, and it upsets the I2C lines the codec
 * and the touch controller share.
 */
static void quiet_audio_pins(void)
{
    const gpio_num_t pins[] = { I2S_MCLK, I2S_BCLK, I2S_WS, I2S_DOUT, PA_EN };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(pins[i], 0);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
}

static esp_err_t init(void)
{
    quiet_audio_pins();
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&i2c_cfg, &s_i2c);
}

/* Backlight: a boost enable dimmed by PWM at 200 Hz, 8 bits, off until a frame is up. */
static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 200,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel = {
        .gpio_num = BL_EN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    return ledc_channel_config(&channel);
}

/* Panel on MIPI-DSI, two lanes, powered from LDO 3. */
static esp_err_t panel_init(esp_lcd_panel_io_handle_t *io_out)
{
    esp_ldo_channel_handle_t ldo = NULL;
    const esp_ldo_channel_config_t ldo_cfg = { .chan_id = MIPI_PHY_LDO_CHAN, .voltage_mv = MIPI_PHY_LDO_MV };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo), TAG, "dsi phy ldo");

    esp_lcd_dsi_bus_handle_t bus;
    const esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 520,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &bus), TAG, "dsi bus");
    esp_lcd_dbi_io_config_t dbi_cfg = ST7102_MIPI_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, io_out), TAG, "dbi io");

    esp_lcd_dpi_panel_config_t dpi = ST7102_MIPI_480_800_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_FMT_RGB565);
    st7102_vendor_config_t vendor = {
        .init_cmds = s_lcd_init,
        .init_cmds_size = sizeof(s_lcd_init) / sizeof(s_lcd_init[0]),
        .flags.use_mipi_interface = 1,
        .mipi_config = { .dsi_bus = bus, .dpi_config = &dpi },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7102(*io_out, &panel_cfg, &s_panel), TAG, "panel");
    vTaskDelay(pdMS_TO_TICKS(50));   /* the panel's rail needs this before the reset */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_enable_dma2d(s_panel), TAG, "dma2d");   /* frames are copied by hardware */
    return esp_lcd_panel_disp_on_off(s_panel, true);
}

/* The raw ST7123 points are the panel's own (portrait); LVGL turns them with the display's rotation. */
static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x = 0, y = 0;
    uint8_t n = 0;
    if (!s_touch || esp_lcd_touch_read_data(s_touch) != ESP_OK
        || !esp_lcd_touch_get_coordinates(s_touch, &x, &y, NULL, &n, 1) || !n) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
}

static lv_display_t *display_start(lv_indev_t **touch)
{
    lvgl_port_cfg_t cfg = ESP_LVGL_PORT_INIT_CONFIG();
    cfg.task_priority = MUSE_UI_PRIORITY;
    cfg.task_affinity = MUSE_UI_CORE;
    cfg.task_stack = 16 * 1024;
    esp_lcd_panel_io_handle_t io = NULL;
    if (lvgl_port_init(&cfg) != ESP_OK || backlight_init() != ESP_OK || panel_init(&io) != ESP_OK) {
        return NULL;
    }

    /*
     * Two full-size buffers in PSRAM, redrawn in parts and rotated by LVGL
     * into the panel's one frame buffer. Tearing avoidance, full refresh and
     * PPA each tore the picture on this board.
     */
    const lvgl_port_display_cfg_t dc = {
        .io_handle = io,
        .panel_handle = s_panel,
        .buffer_size = PANEL_H * STRIP_ROWS,
        .double_buffer = true,
        .hres = PANEL_W,
        .vres = PANEL_H,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = { .buff_dma = true, .buff_spiram = false, .sw_rotate = 1 },
    };
    const lvgl_port_display_dsi_cfg_t dsi = { .flags = { .avoid_tearing = false } };
    lv_display_t *disp = lvgl_port_add_disp_dsi(&dc, &dsi);
    if (!disp) {
        return NULL;
    }
    lvgl_port_lock(0);
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);

    /* Touch is optional: a missing panel mustn't stop the board booting. */
    esp_lcd_panel_io_handle_t tp_io = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG();
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = PANEL_W,
        .y_max = PANEL_H,
        .rst_gpio_num = GPIO_NUM_NC,   /* GPIO32 is wired, but driving it makes the ST7123 NACK */
        .int_gpio_num = GPIO_NUM_NC,   /* polled */
    };
    if (esp_lcd_new_panel_io_i2c(s_i2c, &tp_io_cfg, &tp_io) == ESP_OK
        && esp_lcd_touch_new_i2c_st7123(tp_io, &tp_cfg, &s_touch) == ESP_OK) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(indev, disp);
        lv_indev_set_read_cb(indev, touch_read);
        *touch = indev;
    } else {
        ESP_LOGW(TAG, "touch controller not found: running without touch");
    }
    lvgl_port_unlock();
    return disp;
}

static bool display_lock(int timeout_ms)
{
    return lvgl_port_lock(timeout_ms < 0 ? 0 : (uint32_t)timeout_ms);   /* 0 waits forever */
}

static void display_unlock(void)
{
    lvgl_port_unlock();
}

static void set_brightness(int pct)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, pct * 255 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void panel_sleep(bool sleep)
{
    esp_lcd_panel_disp_sleep(s_panel, sleep);
}

/* ---- The speaker: an ES8388, its I2S started on the first sample ---- */

static void spk_set(bool on)
{
    if (on == s_spk_on) {
        return;
    }
    if (on && !s_spk_inited) {
        const i2s_std_config_t cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = { .mclk = I2S_MCLK, .bclk = I2S_BCLK, .ws = I2S_WS, .dout = I2S_DOUT, .din = I2S_GPIO_UNUSED },
        };
        if (i2s_channel_init_std_mode(s_spk_tx, &cfg) != ESP_OK) {
            return;
        }
        s_spk_inited = true;
    }
    if ((on ? i2s_channel_enable(s_spk_tx) : i2s_channel_disable(s_spk_tx)) == ESP_OK) {
        s_spk_on = on;
    }
}

/*
 * esp_codec_dev turns the data path on before it writes the codec's registers.
 * Here that wait for the first sample: with I2S running, a write over the
 * shared I2C bus nearly always fails, and the driver doesn't say so.
 */
static int spk_enable(const audio_codec_data_if_t *h, esp_codec_dev_type_t type, bool on)
{
    (void)h;
    (void)type;
    if (!on) {
        spk_set(false);
    }
    return ESP_CODEC_DEV_OK;
}

static int spk_write(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    (void)h;
    spk_set(true);
    size_t wrote;
    return i2s_channel_write(s_spk_tx, data, size, &wrote, portMAX_DELAY) == ESP_OK ? ESP_CODEC_DEV_OK
                                                                                     : ESP_CODEC_DEV_WRITE_FAIL;
}

/* The ES8388 driver, with each register write made safe: I2S paused around it, and repeated. */
static const audio_codec_if_t *s_es;
static audio_codec_if_t s_codec;

static bool spk_pause(void)
{
    bool was = s_spk_on;
    spk_set(false);
    return was;
}

static int safe_enable(const audio_codec_if_t *h, bool on)
{
    (void)h;
    int r = ESP_CODEC_DEV_DRV_ERR;
    bool was = spk_pause();
    for (int i = 0; i < 5 && r != ESP_CODEC_DEV_OK; i++) {
        r = s_es->enable(s_es, on);
        if (r != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (was) {
        spk_set(true);
    }
    return r;
}

static int safe_set_fs(const audio_codec_if_t *h, esp_codec_dev_sample_info_t *fs)
{
    (void)h;
    return s_es->set_fs(s_es, fs);
}

static int safe_set_vol(const audio_codec_if_t *h, float db)
{
    (void)h;
    int r = ESP_CODEC_DEV_OK;
    bool was = spk_pause();
    for (int i = 0; i < VOL_WRITES; i++) {
        r = s_es->set_vol(s_es, db);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (was) {
        spk_set(true);
    }
    return r;
}

static int safe_mute(const audio_codec_if_t *h, bool mute)
{
    (void)h;
    int r = ESP_CODEC_DEV_OK;
    bool was = spk_pause();
    for (int i = 0; i < VOL_WRITES; i++) {
        r = s_es->mute(s_es, mute);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (was) {
        spk_set(true);
    }
    return r;
}

static int safe_set_reg(const audio_codec_if_t *h, int reg, int value)
{
    (void)h;
    return s_es->set_reg(s_es, reg, value);
}

static int safe_get_reg(const audio_codec_if_t *h, int reg, int *value)
{
    (void)h;
    return s_es->get_reg(s_es, reg, value);
}

/* The codec must be told to power down (CHIPPOWER) before it is set up, or a warm start leaves it deaf. */
static void codec_power_down(void)
{
    i2c_master_dev_handle_t dev;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = CODEC_I2C_ADDR, .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(s_i2c, &cfg, &dev) != ESP_OK) {
        return;
    }
    const uint8_t pd[2] = { 0x02, 0xFF };
    for (int i = 0; i < 5; i++) {
        if (i2c_master_transmit(dev, pd, sizeof(pd), 200) == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    i2c_master_bus_rm_device(dev);
}

/* ---- The mics: two PDM mics, left and right of one data line ---- */

static int mic_enable(const audio_codec_data_if_t *h, esp_codec_dev_type_t type, bool on)
{
    (void)h;
    (void)type;
    if (on != s_mic_on) {
        if ((on ? i2s_channel_enable(s_mic_rx) : i2s_channel_disable(s_mic_rx)) != ESP_OK) {
            return ESP_CODEC_DEV_DRV_ERR;
        }
        s_mic_on = on;
        s_mic_settled = false;
    }
    return ESP_CODEC_DEV_OK;
}

/* Both slots, with each one's zero level tracked and taken off before the gain (a 10 Hz high-pass). */
static int mic_read(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    (void)h;
    size_t got;
    if (i2s_channel_read(s_mic_rx, data, size, &got, pdMS_TO_TICKS(1000)) != ESP_OK || got != (size_t)size) {
        return ESP_CODEC_DEV_READ_FAIL;
    }
    int16_t *s = (int16_t *)data;
    int n = size / 2;
    if (!s_mic_settled) {
        int32_t sum[2] = { 0, 0 };
        for (int i = 0; i < n; i++) {
            sum[i & 1] += s[i];
        }
        s_mic_dc[0] = sum[0] / (n / 2) * 256;
        s_mic_dc[1] = sum[1] / (n / 2) * 256;
        s_mic_settled = true;
    }
    for (int i = 0; i < n; i++) {
        int32_t *dc = &s_mic_dc[i & 1];
        *dc += (s[i] * 256 - *dc) >> 8;
        int v = (s[i] - (*dc >> 8)) * s_mic_gain_q8 >> 8;
        s[i] = v > INT16_MAX ? INT16_MAX : v < INT16_MIN ? INT16_MIN : v;
    }
    return ESP_CODEC_DEV_OK;
}

static void set_mic_gain(esp_codec_dev_handle_t mic, int db)
{
    (void)mic;
    s_mic_gain_q8 = (int)(256.0f * powf(10.0f, (db + MIC_GAIN_OFFSET_DB) / 20.0f));
}

static esp_err_t audio_init(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic)
{
    codec_power_down();

    /* The speaker's channel is only allocated here: its clocks start with the first sample (spk_set). */
    i2s_chan_config_t spk_chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    spk_chan.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&spk_chan, &s_spk_tx, NULL), TAG, "speaker channel");

    audio_codec_i2c_cfg_t i2c_cfg = { .port = I2C_NUM_0, .addr = ES8388_CODEC_DEFAULT_ADDR, .bus_handle = s_i2c };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(ctrl && gpio_if, ESP_ERR_NO_MEM, TAG, "codec interfaces");
    es8388_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .master_mode = false,
        .pa_pin = PA_EN,
        .pa_reverted = false,
    };
    s_es = es8388_codec_new(&es_cfg);
    ESP_RETURN_ON_FALSE(s_es, ESP_FAIL, TAG, "ES8388 not responding");
    s_codec = (audio_codec_if_t){
        .enable = safe_enable, .set_fs = safe_set_fs, .mute = safe_mute, .set_vol = safe_set_vol,
        .set_reg = safe_set_reg, .get_reg = safe_get_reg,
    };

    /* PDM input is on I2S0 only. */
    i2s_chan_config_t mic_chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&mic_chan, NULL, &s_mic_rx), TAG, "mic channel");
    const i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .clk = MIC_CLK, .din = MIC_DIN },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_pdm_rx_mode(s_mic_rx, &pdm_cfg), TAG, "mic pdm");

    static const audio_codec_data_if_t spk_if = { .enable = spk_enable, .write = spk_write };
    static const audio_codec_data_if_t mic_if = { .enable = mic_enable, .read = mic_read };
    esp_codec_dev_cfg_t out_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = &s_codec, .data_if = &spk_if };
    esp_codec_dev_cfg_t in_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_IN, .data_if = &mic_if };
    *spk = esp_codec_dev_new(&out_cfg);
    *mic = esp_codec_dev_new(&in_cfg);
    return *spk && *mic ? ESP_OK : ESP_FAIL;
}

/* No physical buttons: the mic icon on the screen supplies the talk presses. */
static unsigned poll_buttons(void)
{
    return 0;
}

static const muse_board_t s_board = {
    .name = "P4_R 4.3-inch",
    .width = PANEL_H,
    .height = PANEL_W,
    .round = false,
    .touch = true,
    .touch_talk = true,
    .diagonal_in = 4.3f,
    .talk_button = "screen microphone",
    .talk_hint = { LV_ALIGN_RIGHT_MID, -24, 0 },
    .frame_ms = 40,
    .init = init,
    .display_start = display_start,
    .display_lock = display_lock,
    .display_unlock = display_unlock,
    .set_brightness = set_brightness,
    .panel_sleep = panel_sleep,
    .audio_init = audio_init,
    .mic_slot = -1,   /* both mics, mixed */
    .set_mic_gain = set_mic_gain,
    .poll_buttons = poll_buttons,
    /* No .read_power (wall powered) or .power_off: no software off. */
};

const muse_board_t *muse_board_get(void)
{
    return &s_board;
}
