# Drivers for the P4_R board's panel and touch controller

Both are Espressif code (SPDX Apache-2.0, as in each file's header), with the
changes below. They live here rather than as managed components because the
panel driver isn't in the component registry, and the touch driver needed a fix.

- `esp_lcd_st7102*`, `st7102_interface.h`: an ST7102 MIPI-DSI panel driver, from
  Espressif's `esp_lcd_*` panel template. The RGB interface is left out
  (this board uses MIPI-DSI only). The init command table is the board's own,
  in `../board_p4r_lcd43.c`.
- `esp_lcd_touch_st7123*`: `espressif/esp_lcd_touch_st7123` 1.0.2 from
  [esp-iot-solution](https://github.com/espressif/esp-iot-solution), with a fix.
  `read_data()` read `max_touches` from the chip (register 0x0009), then
  `7 * max_touches` bytes into a 70-byte array on the stack without checking
  it is at most 10. A noisy I2C read that returned a large count overwrote the
  stack frame and the return address (`Instruction access fault`). It now
  refuses a count above 10 and returns `ESP_ERR_INVALID_RESPONSE`, counted in
  `esp_lcd_touch_st7123_bad_reads`. The registry's latest release still has the bug.
