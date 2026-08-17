# Third-party notices

This project compiles two components directly from the clean Waveshare reference
checkout at commit `eb1f63427d735a22b9c30e22fa63ebddae1834d3`:

- `u8g2`: U8g2 monochrome graphics library. The code is BSD-2-Clause; individual
  fonts have their own notices in the upstream `LICENSE` file.
- `u8g2_st7305`: Waveshare's ESP-IDF adapter for the board's ST7305 reflective LCD.

The referenced files remain in an external dependency checkout selected by
`RLCD_DEPS_DIR` (the current workspace uses `third_party/sources/` outside this
Git repository). They are not copied into this repository, so their original
notices remain intact.
