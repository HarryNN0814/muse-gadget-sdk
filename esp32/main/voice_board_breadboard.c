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


/* ESP32-S3 breadboard: an INMP441 I2S MEMS mic and a MAX98357A I2S amp, wired
 * as xiaozhi-esp32's bread-compact-wifi board, the usual DIY voice build
 * (https://github.com/78/xiaozhi-esp32, main/boards/bread-compact-wifi/config.h):
 *   INMP441    WS GPIO4, SCK GPIO5, SD GPIO6, L/R to GND, VDD to 3V3
 *   MAX98357A  DIN GPIO7, BCLK GPIO15, LRC GPIO16, VIN to 5V
 * Each has its own I2S port, with the S3 as the clock master. There is no mute
 * switch, and the amp has no enable pin or volume of its own: the volume
 * scales the samples.
 */
#include "voice_board.h"

#include "driver/i2s_std.h"
#include "esp_log.h"

#define MIC_WS    4
#define MIC_SCK   5
#define MIC_SD    6
#define AMP_DIN   7
#define AMP_BCLK  15
#define AMP_LRC   16

#define CHUNK 320
/* The INMP441's 24 bits sit at the top of each 32-bit slot, and speech at arm's
 * length is around -60 dBFS. Keeping bits 31 to 12 is 24 dB of gain, as in
 * xiaozhi-esp32. ponytail: fixed gain; 11 doubles it, 13 halves it. */
#define MIC_SHIFT 12

static const char *TAG = "link.breadboard";
static i2s_chan_handle_t s_rx, s_tx;
static int32_t s_raw[CHUNK];
static int32_t s_dc;               /* the mic's zero level, x256 */
static bool s_mic_on;
static volatile int32_t s_gain = 65536;   /* Q16 */

void voice_board_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    // Squared, so the steps sound about even; 100 is unity.
    s_gain = percent * percent * 65536 / 10000;
}

bool voice_board_muted(void) { return false; }
int voice_board_dial_steps(void) { return 0; }
void voice_board_amp(bool on) { (void)on; }

esp_err_t voice_board_mic_start(void) {
    if (!s_rx) return ESP_ERR_INVALID_STATE;
    esp_err_t err = i2s_channel_enable(s_rx);
    if (err == ESP_OK) s_mic_on = true;
    return err;
}
void voice_board_mic_stop(void) {
    if (s_mic_on) { i2s_channel_disable(s_rx); s_mic_on = false; }
}
size_t voice_board_mic_read(int16_t *pcm, size_t frames, int *peak) {
    *peak = 0;
    if (!s_mic_on) return 0;
    if (frames > CHUNK) frames = CHUNK;
    size_t bytes = 0;
    if (i2s_channel_read(s_rx, s_raw, frames * sizeof(int32_t), &bytes, 500) != ESP_OK) return 0;
    size_t count = bytes / sizeof(int32_t);
    for (size_t i = 0; i < count; i++) {
        // The mic's small DC offset would eat the headroom: track it and take it off.
        int32_t v = s_raw[i] >> MIC_SHIFT;
        s_dc += (v * 256 - s_dc) >> 8;
        v -= s_dc >> 8;
        pcm[i] = v > INT16_MAX ? INT16_MAX : v < INT16_MIN ? INT16_MIN : (int16_t)v;
        int magnitude = pcm[i] < 0 ? -(int)pcm[i] : pcm[i];
        if (magnitude > *peak) *peak = magnitude;
    }
    return count;
}

esp_err_t voice_board_speaker_write(const int32_t *frames, size_t count) {
    if (!s_tx) return ESP_ERR_INVALID_STATE;
    // Keep the player's 3 KB task stack free for I2S and logging.
    int32_t out[32 * 2];
    int32_t gain = s_gain;
    while (count) {
        size_t n = count > 32 ? 32 : count;
        for (size_t i = 0; i < 2 * n; i++) out[i] = (int32_t)((int64_t)frames[i] * gain >> 16);
        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx, out, n * 2 * sizeof(int32_t), &written, 1000);
        if (err != ESP_OK) return err;
        if (written != n * 2 * sizeof(int32_t)) return ESP_ERR_TIMEOUT;
        frames += 2 * n;
        count -= n;
    }
    return ESP_OK;
}

esp_err_t voice_board_init(void) {
    i2s_chan_config_t mic_chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    mic_chan.dma_frame_num = CHUNK;
    esp_err_t err = i2s_new_channel(&mic_chan, NULL, &s_rx);
    // Mono takes the left slot, which an INMP441 with L/R to GND sends.
    i2s_std_config_t mic = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(VOICE_MIC_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = MIC_SCK, .ws = MIC_WS,
                     .dout = I2S_GPIO_UNUSED, .din = MIC_SD},
    };
    if (err == ESP_OK) err = i2s_channel_init_std_mode(s_rx, &mic);

    i2s_chan_config_t amp_chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    amp_chan.auto_clear = true;   // silence when the player runs dry, not old samples
    if (err == ESP_OK) err = i2s_new_channel(&amp_chan, &s_tx, NULL);
    i2s_std_config_t amp = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(VOICE_SPEAKER_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = AMP_BCLK, .ws = AMP_LRC,
                     .dout = AMP_DIN, .din = I2S_GPIO_UNUSED},
    };
    if (err == ESP_OK) err = i2s_channel_init_std_mode(s_tx, &amp);
    if (err == ESP_OK) err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S setup failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "breadboard audio ready: INMP441 and MAX98357A; BOOT is push-to-talk");
    return ESP_OK;
}
