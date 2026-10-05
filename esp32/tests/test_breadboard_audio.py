# Copyright (c) Meta Platforms, Inc. and affiliates.
# SPDX-License-Identifier: Apache-2.0

"""Exercise the breadboard INMP441/MAX98357A driver without I2S hardware."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

FAKE = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
typedef int esp_err_t;
typedef void *i2s_chan_handle_t;
typedef struct { int id, role, dma_frame_num; bool auto_clear; } i2s_chan_config_t;
typedef struct {
    int clk_cfg, slot_cfg;
    struct { int mclk, bclk, ws, dout, din; } gpio_cfg;
} i2s_std_config_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERR_TIMEOUT 5
#define I2S_NUM_0 0
#define I2S_NUM_1 1
#define I2S_ROLE_MASTER 0
#define I2S_GPIO_UNUSED -1
#define I2S_DATA_BIT_WIDTH_32BIT 32
#define I2S_SLOT_MODE_MONO 1
#define I2S_SLOT_MODE_STEREO 2
#define I2S_CHANNEL_DEFAULT_CONFIG(num, r) ((i2s_chan_config_t){.id = (num), .role = (r)})
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) (rate)
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, mode) ((bits) == 32 ? (mode) : -1)
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
const char *esp_err_to_name(esp_err_t);
esp_err_t i2s_new_channel(const i2s_chan_config_t *, i2s_chan_handle_t *, i2s_chan_handle_t *);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t, const i2s_std_config_t *);
esp_err_t i2s_channel_enable(i2s_chan_handle_t);
esp_err_t i2s_channel_disable(i2s_chan_handle_t);
esp_err_t i2s_channel_read(i2s_chan_handle_t, void *, size_t, size_t *, int);
esp_err_t i2s_channel_write(i2s_chan_handle_t, const void *, size_t, size_t *, int);
'''

HARNESS = r'''
#include "fake.h"
#include <string.h>
#include "voice_board.h"
static int rx, tx, rx_on, write_calls, write_error;
static bool short_write;
static int32_t raw = 0;
static int32_t written[256];
static size_t written_samples;
const char *esp_err_to_name(esp_err_t err) { (void)err; return "fake"; }
esp_err_t i2s_new_channel(const i2s_chan_config_t *cfg, void **out, void **in) {
    assert(cfg->role == I2S_ROLE_MASTER);
    if (cfg->id == I2S_NUM_0) { assert(!out && in && cfg->dma_frame_num == 320); *in = &rx; }
    else { assert(cfg->id == I2S_NUM_1 && out && !in && cfg->auto_clear); *out = &tx; }
    return 0;
}
esp_err_t i2s_channel_init_std_mode(void *h, const i2s_std_config_t *cfg) {
    assert(cfg->gpio_cfg.mclk == I2S_GPIO_UNUSED);
    if (h == &rx) {   /* INMP441 */
        assert(cfg->clk_cfg == 16000 && cfg->slot_cfg == I2S_SLOT_MODE_MONO);
        assert(cfg->gpio_cfg.ws == 4 && cfg->gpio_cfg.bclk == 5 && cfg->gpio_cfg.din == 6);
        assert(cfg->gpio_cfg.dout == I2S_GPIO_UNUSED);
    } else {          /* MAX98357A */
        assert(h == &tx && cfg->clk_cfg == 48000 && cfg->slot_cfg == I2S_SLOT_MODE_STEREO);
        assert(cfg->gpio_cfg.dout == 7 && cfg->gpio_cfg.bclk == 15 && cfg->gpio_cfg.ws == 16);
        assert(cfg->gpio_cfg.din == I2S_GPIO_UNUSED);
    }
    return 0;
}
esp_err_t i2s_channel_enable(void *h) { if (h == &rx) rx_on = 1; else assert(h == &tx); return 0; }
esp_err_t i2s_channel_disable(void *h) { assert(h == &rx); rx_on = 0; return 0; }
esp_err_t i2s_channel_read(void *h, void *out, size_t n, size_t *got, int timeout) {
    (void)timeout; assert(h == &rx && rx_on && n <= 320 * 4);
    for (size_t i = 0; i < n / 4; i++) ((int32_t *)out)[i] = raw;
    *got = n; return 0;
}
esp_err_t i2s_channel_write(void *h, const void *pcm, size_t n, size_t *got, int timeout) {
    (void)timeout; assert(h == &tx && n <= 32 * 8);
    assert(written_samples + n / 4 <= 256);
    memcpy(written + written_samples, pcm, n); written_samples += n / 4;
    write_calls++; *got = short_write ? n - 4 : n; return write_error;
}
int main(int argc, char **argv) {
    assert(argc == 2 && voice_board_init() == ESP_OK);
    assert(!voice_board_muted() && voice_board_dial_steps() == 0);
    if (!strcmp(argv[1], "microphone")) {
        int16_t pcm[324]; int peak;
        for (int i = 0; i < 324; i++) pcm[i] = 42;
        assert(voice_board_mic_read(pcm, 4, &peak) == 0 && peak == 0);
        assert(voice_board_mic_start() == ESP_OK);
        /* Full scale clips rather than wraps, and the read stops at a chunk. */
        raw = INT32_MAX;
        assert(voice_board_mic_read(pcm, 324, &peak) == 320 && peak == 32767 && pcm[0] == 32767);
        for (int i = 320; i < 324; i++) assert(pcm[i] == 42);
        raw = INT32_MIN;
        assert(voice_board_mic_read(pcm, 4, &peak) == 4 && peak == 32768 && pcm[3] == -32768);
        voice_board_mic_stop(); assert(!rx_on && voice_board_mic_read(pcm, 4, &peak) == 0);
        /* A steady offset fades out: 24 dB of gain, then the zero level is taken off. */
        assert(voice_board_mic_start() == ESP_OK);
        raw = 1000 << 12;
        for (int i = 0; i < 50; i++) voice_board_mic_read(pcm, 320, &peak);
        assert(pcm[319] >= 0 && pcm[319] <= 2 && peak <= 2);
        raw = 3000 << 12;   /* a step up shows at its full size first */
        assert(voice_board_mic_read(pcm, 1, &peak) == 1 && pcm[0] >= 1990 && pcm[0] <= 2002);
    } else if (!strcmp(argv[1], "speaker")) {
        int32_t pcm[99 * 2];
        for (int i = 0; i < 99; i++) { pcm[2*i] = INT32_MAX; pcm[2*i+1] = INT32_MIN; }
        voice_board_set_volume(110);
        assert(voice_board_speaker_write(pcm, 99) == ESP_OK);
        assert(write_calls == 4 && written_samples == 198);
        for (int i = 0; i < 99; i++) { assert(written[2*i] == INT32_MAX); assert(written[2*i+1] == INT32_MIN); }
        int32_t half[] = {1 << 20, -(1 << 20)}; written_samples = 0;
        voice_board_set_volume(50);
        assert(voice_board_speaker_write(half, 1) == ESP_OK);
        assert(written[0] == 1 << 18 && written[1] == -(1 << 18));
        voice_board_set_volume(-10); written_samples = 0;
        assert(voice_board_speaker_write(half, 1) == ESP_OK && written[0] == 0 && written[1] == 0);
        short_write = true; assert(voice_board_speaker_write(half, 1) == ESP_ERR_TIMEOUT);
        short_write = false; write_error = ESP_ERR_INVALID_STATE;
        assert(voice_board_speaker_write(half, 1) == ESP_ERR_INVALID_STATE);
    } else { assert(false); }
    return 0;
}
'''


class BreadboardAudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        tmp = Path(cls.temp.name)
        (tmp / "fake.h").write_text(FAKE)
        for name in ("esp_err.h", "esp_log.h", "driver/i2s_std.h"):
            path = tmp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "fake.h"\n')
        (tmp / "test.c").write_text(HARNESS)
        cls.exe = tmp / "test"
        result = subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(tmp),
            "-I", str(ROOT / "main"), str(ROOT / "main/voice_board_breadboard.c"),
            str(tmp / "test.c"), "-o", str(cls.exe)
        ], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.exe), scenario], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_microphone_gain_offset_clipping_and_bounds(self):
        self.run_scenario("microphone")

    def test_speaker_volume_chunks_and_write_failures(self):
        self.run_scenario("speaker")
