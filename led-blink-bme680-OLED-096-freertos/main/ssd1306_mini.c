#include <string.h>
#include "ssd1306_mini.h"

// Control bytes per the SSD1306 datasheet protocol: first byte after the
// I2C address is either 0x00 (command stream follows) or 0x40 (data/GDDRAM
// stream follows). i2cdev's i2c_dev_write(dev, reg, reg_size, data, size)
// sends reg immediately followed by data in a single I2C write, which is
// exactly this control-byte + payload framing.
// https://esp-idf-lib.readthedocs.io/en/latest/groups/i2cdev.html#_CPPv413i2c_dev_writePK9i2c_dev_tPKv6size_tPKv6size_t
static esp_err_t ssd1306_cmd(ssd1306_mini_t *dev, uint8_t cmd)
{
    uint8_t ctrl = 0x00;
    return i2c_dev_write(&dev->i2c_dev, &ctrl, 1, &cmd, 1);
}

static esp_err_t ssd1306_data(ssd1306_mini_t *dev, const uint8_t *buf, size_t len)
{
    uint8_t ctrl = 0x40;
    return i2c_dev_write(&dev->i2c_dev, &ctrl, 1, buf, len);
}

esp_err_t ssd1306_mini_init(ssd1306_mini_t *dev, i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint8_t addr)
{
    memset(dev, 0, sizeof(*dev));
    dev->i2c_dev.port = port;
    dev->i2c_dev.addr = addr;
    dev->i2c_dev.cfg.sda_io_num = sda;
    dev->i2c_dev.cfg.scl_io_num = scl;
    dev->i2c_dev.cfg.master.clk_speed = 400000;

    esp_err_t err = i2c_dev_create_mutex(&dev->i2c_dev);
    if (err != ESP_OK) return err;

    // Standard 128x64 SSD1306 power-on init sequence (page addressing mode,
    // the controller's default - no need to touch the addressing-mode
    // register). Values are the ones from the SSD1306 datasheet's
    // recommended init and are the same across essentially every driver.
    static const uint8_t init_cmds[] = {
        0xAE,             // display off
        0xD5, 0x80,       // clock divide ratio / osc frequency
        0xA8, 0x3F,       // multiplex ratio = 63 (64 rows)
        0xD3, 0x00,       // display offset = 0
        0x40,             // display start line = 0
        0x8D, 0x14,       // charge pump enable
        0xA1,             // segment remap (mirror horizontally)
        0xC8,             // COM output scan direction (mirror vertically)
        0xDA, 0x12,       // COM pins hardware config
        0x81, 0xCF,       // contrast
        0xD9, 0xF1,       // pre-charge period
        0xDB, 0x40,       // VCOMH deselect level
        0xA4,             // resume RAM content display (not "all pixels on")
        0xA6,             // normal display (not inverted)
        0xAF,             // display on
    };
    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        err = ssd1306_cmd(dev, init_cmds[i]);
        if (err != ESP_OK) return err;
    }

    ssd1306_mini_clear(dev);
    return ssd1306_mini_flush(dev);
}

void ssd1306_mini_clear(ssd1306_mini_t *dev)
{
    memset(dev->framebuf, 0, sizeof(dev->framebuf));
}

void ssd1306_mini_draw_bitmap(ssd1306_mini_t *dev, const uint8_t *bitmap)
{
    // The bitmap is already laid out exactly like framebuf (page-major,
    // 128 bytes per page, bit0 = top row of the page) so this is a
    // straight copy - no per-pixel work needed.
    memcpy(dev->framebuf, bitmap, sizeof(dev->framebuf));
}
esp_err_t ssd1306_mini_flush(ssd1306_mini_t *dev)
{
    for (uint8_t page = 0; page < SSD1306_PAGES; page++) {
        esp_err_t err;
        if ((err = ssd1306_cmd(dev, 0xB0 + page)) != ESP_OK) return err; // set page address
        if ((err = ssd1306_cmd(dev, 0x00)) != ESP_OK) return err;        // column low nibble = 0
        if ((err = ssd1306_cmd(dev, 0x10)) != ESP_OK) return err;        // column high nibble = 0
        if ((err = ssd1306_data(dev, &dev->framebuf[page * SSD1306_WIDTH], SSD1306_WIDTH)) != ESP_OK) return err;
    }
    return ESP_OK;
}

static inline void set_pixel(ssd1306_mini_t *dev, int x, int y)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) return;
    dev->framebuf[(y / 8) * SSD1306_WIDTH + x] |= (1 << (y % 8));
}

// Tiny 8x8 font, MSB = leftmost column. Only the glyphs needed for a
// "T:23.4C" / "H:45.6%" / "P:1013h" style readout. Add more cases if you
// need other characters.
static const uint8_t *glyph_for(char c)
{
    static const uint8_t G_0[8] = {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00};
    static const uint8_t G_1[8] = {0x18,0x38,0x18,0x18,0x18,0x18,0x3C,0x00};
    static const uint8_t G_2[8] = {0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00};
    static const uint8_t G_3[8] = {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00};
    static const uint8_t G_4[8] = {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00};
    static const uint8_t G_5[8] = {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00};
    static const uint8_t G_6[8] = {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00};
    static const uint8_t G_7[8] = {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00};
    static const uint8_t G_8[8] = {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00};
    static const uint8_t G_9[8] = {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00};
    static const uint8_t G_DOT[8]   = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00};
    static const uint8_t G_COLON[8] = {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00};
    static const uint8_t G_PCT[8]   = {0x62,0x64,0x08,0x10,0x26,0x46,0x00,0x00};
    static const uint8_t G_C[8] = {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00};
    static const uint8_t G_H[8] = {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00};
    static const uint8_t G_P[8] = {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00};
    static const uint8_t G_T[8] = {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00};
    static const uint8_t G_h[8] = {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00};
    static const uint8_t G_a[8] = {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00};
    static const uint8_t G_DASH[8] = {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00};
    static const uint8_t G_SPACE[8] = {0,0,0,0,0,0,0,0};

    switch (c) {
        case '0': return G_0;  case '1': return G_1;  case '2': return G_2;
        case '3': return G_3;  case '4': return G_4;  case '5': return G_5;
        case '6': return G_6;  case '7': return G_7;  case '8': return G_8;
        case '9': return G_9;  case '.': return G_DOT; case ':': return G_COLON;
        case '%': return G_PCT; case 'C': return G_C;  case 'H': return G_H;
        case 'P': return G_P;  case 'T': return G_T;  case 'h': return G_h;
        case 'a': return G_a;
        case '-': return G_DASH;
        default:  return G_SPACE;
    }
}

void ssd1306_mini_draw_text_2x(ssd1306_mini_t *dev, int x0, int y0, const char *text)
{
    for (int i = 0; text[i] != '\0'; i++) {
        const uint8_t *glyph = glyph_for(text[i]);
        int cx = x0 + i * 16; // each glyph is 8px wide, drawn at 2x -> 16px advance
        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    int px = cx + col * 2;
                    int py = y0 + row * 2;
                    set_pixel(dev, px, py);
                    set_pixel(dev, px + 1, py);
                    set_pixel(dev, px, py + 1);
                    set_pixel(dev, px + 1, py + 1);
                }
            }
        }
    }
}
