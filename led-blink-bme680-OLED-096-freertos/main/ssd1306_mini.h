#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
// i2c_dev_t / i2cdev_init(): https://esp-idf-lib.readthedocs.io/en/latest/groups/i2cdev.html
#include "i2cdev.h"

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_PAGES   (SSD1306_HEIGHT / 8)

typedef struct {
    i2c_dev_t i2c_dev;
    uint8_t   framebuf[SSD1306_WIDTH * SSD1306_PAGES];
} ssd1306_mini_t;

// addr is normally 0x3C, some modules ship with 0x3D - check yours if the screen stays blank
esp_err_t ssd1306_mini_init(ssd1306_mini_t *dev, i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint8_t addr);

// Clears the in-RAM framebuffer only (call ssd1306_mini_flush to push it to the panel)
void ssd1306_mini_clear(ssd1306_mini_t *dev);

// Draws text at 2x scale (16x16 px per glyph) starting at pixel (x0, y0).
// Only covers the characters needed for T/H/P readout: 0-9 . : % C H P T h - and space.
void ssd1306_mini_draw_text_2x(ssd1306_mini_t *dev, int x0, int y0, const char *text);

// Copies a full-screen 128x64 1-bit bitmap (page-major, same layout as
// framebuf) directly into the framebuffer, replacing whatever was there.
void ssd1306_mini_draw_bitmap(ssd1306_mini_t *dev, const uint8_t *bitmap);

// Sends the whole framebuffer to the panel (page addressing mode, one write per page)
esp_err_t ssd1306_mini_flush(ssd1306_mini_t *dev);
