# BoardP4C6_VN (ESP32-P4 + ESP32-C6, 4.3" touch)

Muse on a 4.3" touch board with an ESP32-P4 and an ESP32-C6: a 480×800 ST7102 MIPI-DSI panel drawn as
800×480, an ST7123 touch controller, an ES8388 codec with a speaker amplifier, two PDM microphones, 16 MB of
flash and 32 MB of PSRAM. The C6 provides Wi-Fi and BLE over ESP-Hosted SDIO. The console and flashing go
through the P4's own USB Serial/JTAG (`303a:1001`). The firmware names itself `BoardP4C6_VN`.

Everything is on the touch screen: the microphone icon is push-to-talk, and the settings pages (Wi-Fi, Muse,
Bluetooth, sound, auto-sleep, battery) are swipes away. The board's hard button is not used. The overlay
turns on the Vietnamese screen text and captions (`CONFIG_MUSE_UI_LANG_VI`, `CONFIG_MUSE_LATIN_FONT`).

## Build

Activate ESP-IDF **v6.0.1** with the `esp32p4` tools installed, then set your SDK token in the build's
`sdkconfig` (see [`AGENTS.md`](../AGENTS.md#build)):

```sh
tools/muse/board.sh build p4r
```

Or directly:

```sh
idf.py -B build-muse-p4r-lcd43 -DIDF_TARGET=esp32p4 -DSDKCONFIG=build-muse-p4r-lcd43/sdkconfig -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-p4r-lcd43" build
```

To hear Vietnamese replies and have voice notes transcribed in Vietnamese, also enable
`CONFIG_MUSE_STT_AZURE` and `CONFIG_MUSE_TTS_AZURE` with your own Azure Speech key and region (`idf.py -B
build-muse-p4r-lcd43 menuconfig`, under "ESP32 Device SDK"). The key ships inside the firmware: keep it out of git.

The overlay builds for P4 silicon **v1.x** (`CONFIG_ESP32P4_SELECTS_REV_LESS_V3`, 360 MHz). A v3 image does
not boot on it.

## Slow answers

When Muse is still working 20 s after it took a request (a long answer, a document), the board says so
("Muse is working on it. I'll tell you when it's done.") and lets go, so you can ask something else. When the
answer comes, the screen lights up and the board shows and says it. Only a reply to that request is announced,
one request at a time, for up to 30 minutes, and only while the board stays connected. A reply that arrives
while you are in the middle of another turn is not announced; it is in the Muse app.

## C6 coprocessor

The host is pinned to ESP-Hosted 2.12.12, so the C6 must run the 2.12.12 `network_adapter` slave. Flashing
Muse does not touch the C6.

If the C6 hangs after a soft restart (pairing and a new SDK token both restart the board), ESP-Hosted logs
"Not able to connect with ESP-Hosted slave device" and restarts the P4 every ~14 s, with a blue screen between
boots. A reset through GPIO54 does not help; only cutting power does. The board therefore counts short boots
and deep-sleeps for 5 s on the second one (`early_init` in `boards/board_p4r_lcd43.c`), which brings it back
in about 20 s.

## Traps

- The I2C bus (GPIO7/8) is shared by the touch controller and the codec. Codec writes fail while the speaker's
  I2S runs, so the board pauses it around them and repeats them.
- Do not drive TP_RST (GPIO32): the ST7123 then stops answering.
- The panel draws through `esp_lvgl_port` 2.8, in strips in internal RAM on both cores. The `esp_lv_adapter`
  DSI path stops drawing after the first frame on this chip under ESP-IDF 6.0.1.
- The panel and touch drivers are Espressif code, kept in `components/muse/boards/p4r/` with the changes
  listed in its README.

Pin map, timings and the rest are in `components/muse/boards/board_p4r_lcd43.c`.
