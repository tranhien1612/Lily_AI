#include "display.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "../../main/board_pins.h"

static const char *TAG = "DISPLAY";

static uint8_t s_buffer[OLED_WIDTH * (OLED_HEIGHT / 8)]; // 1024 bytes

// Standard 5x7 ASCII Font table (ASCII 32 to 126)
static const uint8_t FONT5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Space
    {0x00, 0x00, 0x5f, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7f, 0x14, 0x7f, 0x14}, // #
    {0x24, 0x2a, 0x7f, 0x2a, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1c, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1c, 0x00}, // )
    {0x14, 0x08, 0x3e, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3e, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, // 0
    {0x00, 0x42, 0x7f, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4b, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7f, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1e}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3e}, // @
    {0x7e, 0x11, 0x11, 0x11, 0x7e}, // A
    {0x7f, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3e, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7f, 0x41, 0x41, 0x22, 0x1c}, // D
    {0x7f, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7f, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3e, 0x41, 0x49, 0x49, 0x7a}, // G
    {0x7f, 0x08, 0x08, 0x08, 0x7f}, // H
    {0x00, 0x41, 0x7f, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3f, 0x01}, // J
    {0x7f, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7f, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7f, 0x02, 0x0c, 0x02, 0x7f}, // M
    {0x7f, 0x04, 0x08, 0x10, 0x7f}, // N
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, // O
    {0x7f, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3e, 0x41, 0x51, 0x21, 0x5e}, // Q
    {0x7f, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7f, 0x01, 0x01}, // T
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, // U
    {0x1f, 0x20, 0x40, 0x20, 0x1f}, // V
    {0x3f, 0x40, 0x38, 0x40, 0x3f}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x7f, 0x41, 0x41, 0x00}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x00, 0x41, 0x41, 0x7f, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a
    {0x7f, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7f}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7e, 0x09, 0x01, 0x02}, // f
    {0x0c, 0x52, 0x52, 0x52, 0x3e}, // g
    {0x7f, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7d, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3d, 0x00}, // j
    {0x7f, 0x10, 0x28, 0x44, 0x00}, // k
    {0x00, 0x41, 0x7f, 0x40, 0x00}, // l
    {0x7c, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7c, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7c, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7c}, // q
    {0x7c, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3f, 0x44, 0x40, 0x20}, // t
    {0x3c, 0x40, 0x40, 0x20, 0x7c}, // u
    {0x1c, 0x20, 0x40, 0x20, 0x1c}, // v
    {0x3c, 0x40, 0x30, 0x40, 0x3c}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0c, 0x50, 0x50, 0x50, 0x3c}, // y
    {0x44, 0x64, 0x54, 0x4c, 0x44}  // z
};

static esp_err_t ssd1306_send_cmd(uint8_t cmd)
{
    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (DISPLAY_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x00, true); // Control byte: Command
    i2c_master_write_byte(link, cmd, true);
    i2c_master_stop(link);
    esp_err_t ret = i2c_master_cmd_begin(DISPLAY_I2C_PORT, link, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(link);
    return ret;
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing SSD1306 OLED via I2C (SDA=%d, SCL=%d)...", DISPLAY_I2C_SDA, DISPLAY_I2C_SCL);

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = DISPLAY_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = DISPLAY_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(DISPLAY_I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(DISPLAY_I2C_PORT, conf.mode, 0, 0, 0));

    // SSD1306 Init Sequence
    const uint8_t init_cmds[] = {
        0xAE,       // Display OFF
        0xD5, 0x80, // Set Display Clock Divide Ratio / Oscillator Frequency
        0xA8, 0x3F, // Set Multiplex Ratio (64 lines)
        0xD3, 0x00, // Set Display Offset (0)
        0x40,       // Set Display Start Line (0)
        0x8D, 0x14, // Enable Charge Pump
        0x20, 0x00, // Horizontal Addressing Mode
        0xA1,       // Set Segment Re-map (A0 or A1)
        0xC8,       // Set COM Output Scan Direction
        0xDA, 0x12, // Set COM Pins Hardware Config
        0x81, 0xCF, // Set Contrast Control
        0xD9, 0xF1, // Set Pre-charge Period
        0xDB, 0x40, // Set VCOMH Deselect Level
        0xA4,       // Entire Display ON (Resume to RAM)
        0xA6,       // Normal Display (not inverted)
        0xAF        // Display ON
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        ssd1306_send_cmd(init_cmds[i]);
    }

    display_clear();
    display_flush();
    ESP_LOGI(TAG, "SSD1306 Display initialized successfully");
    return ESP_OK;
}

void display_clear(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
}

void display_flush(void)
{
    ssd1306_send_cmd(0x21); // Set column address
    ssd1306_send_cmd(0x00); // Column start
    ssd1306_send_cmd(OLED_WIDTH - 1); // Column end

    ssd1306_send_cmd(0x22); // Set page address
    ssd1306_send_cmd(0x00); // Page start
    ssd1306_send_cmd((OLED_HEIGHT / 8) - 1); // Page end

    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (DISPLAY_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x40, true); // Control byte: Data
    i2c_master_write(link, s_buffer, sizeof(s_buffer), true);
    i2c_master_stop(link);
    i2c_master_cmd_begin(DISPLAY_I2C_PORT, link, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(link);
}

void display_draw_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    int page = y / 8;
    int bit = y % 8;
    int index = page * OLED_WIDTH + x;
    if (on) {
        s_buffer[index] |= (1 << bit);
    } else {
        s_buffer[index] &= ~(1 << bit);
    }
}

void display_draw_line(int x0, int y0, int x1, int y1, bool on)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        display_draw_pixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void display_draw_rect(int x, int y, int w, int h, bool on)
{
    display_draw_line(x, y, x + w - 1, y, on);
    display_draw_line(x, y + h - 1, x + w - 1, y + h - 1, on);
    display_draw_line(x, y, x, y + h - 1, on);
    display_draw_line(x + w - 1, y, x + w - 1, y + h - 1, on);
}

void display_fill_rect(int x, int y, int w, int h, bool on)
{
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            display_draw_pixel(x + i, y + j, on);
        }
    }
}

void display_draw_circle(int x0, int y0, int r, bool on)
{
    int f = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x = 0;
    int y = r;

    display_draw_pixel(x0, y0 + r, on);
    display_draw_pixel(x0, y0 - r, on);
    display_draw_pixel(x0 + r, y0, on);
    display_draw_pixel(x0 - r, y0, on);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        display_draw_pixel(x0 + x, y0 + y, on);
        display_draw_pixel(x0 - x, y0 + y, on);
        display_draw_pixel(x0 + x, y0 - y, on);
        display_draw_pixel(x0 - x, y0 - y, on);
        display_draw_pixel(x0 + y, y0 + x, on);
        display_draw_pixel(x0 - y, y0 + x, on);
        display_draw_pixel(x0 + y, y0 - x, on);
        display_draw_pixel(x0 - y, y0 - x, on);
    }
}

void display_fill_circle(int x0, int y0, int r, bool on)
{
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                display_draw_pixel(x0 + x, y0 + y, on);
            }
        }
    }
}

void display_fill_round_rect(int x, int y, int w, int h, int r, bool on)
{
    if (r <= 0) {
        display_fill_rect(x, y, w, h, on);
        return;
    }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    display_fill_rect(x + r, y, w - 2 * r, h, on);
    display_fill_rect(x, y + r, r, h - 2 * r, on);
    display_fill_rect(x + w - r, y + r, r, h - 2 * r, on);

    for (int cy = 0; cy <= r; cy++) {
        for (int cx = 0; cx <= r; cx++) {
            if ((r - cx) * (r - cx) + (r - cy) * (r - cy) <= r * r) {
                display_draw_pixel(x + cx, y + cy, on);
                display_draw_pixel(x + w - 1 - cx, y + cy, on);
                display_draw_pixel(x + cx, y + h - 1 - cy, on);
                display_draw_pixel(x + w - 1 - cx, y + h - 1 - cy, on);
            }
        }
    }
}

void display_draw_round_rect(int x, int y, int w, int h, int r, bool on)
{
    if (r <= 0) {
        display_draw_rect(x, y, w, h, on);
        return;
    }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    display_draw_line(x + r, y, x + w - r - 1, y, on);
    display_draw_line(x + r, y + h - 1, x + w - r - 1, y + h - 1, on);
    display_draw_line(x, y + r, x, y + h - r - 1, on);
    display_draw_line(x + w - 1, y + r, x + w - 1, y + h - r - 1, on);

    for (int cy = 0; cy <= r; cy++) {
        for (int cx = 0; cx <= r; cx++) {
            int d = (r - cx) * (r - cx) + (r - cy) * (r - cy);
            if (d <= r * r && d > (r - 1.5) * (r - 1.5)) {
                display_draw_pixel(x + cx, y + cy, on);
                display_draw_pixel(x + w - 1 - cx, y + cy, on);
                display_draw_pixel(x + cx, y + h - 1 - cy, on);
                display_draw_pixel(x + w - 1 - cx, y + h - 1 - cy, on);
            }
        }
    }
}

typedef struct {
    uint16_t cp;
    char base;
    uint8_t mod;  // 0: none, 1: circumflex(^), 2: breve(breve), 3: horn(rau), 4: bar(d/D)
    uint8_t tone; // 0: none, 1: acute(/), 2: grave(\), 3: hook(?), 4: tilde(~), 5: dot(.)
} vn_glyph_t;

static const vn_glyph_t VN_GLYPHS[134] = {
    {0x00C0, 'A', 0, 2},
    {0x00C1, 'A', 0, 1},
    {0x00C2, 'A', 1, 0},
    {0x00C3, 'A', 0, 4},
    {0x00C8, 'E', 0, 2},
    {0x00C9, 'E', 0, 1},
    {0x00CA, 'E', 1, 0},
    {0x00CC, 'I', 0, 2},
    {0x00CD, 'I', 0, 1},
    {0x00D2, 'O', 0, 2},
    {0x00D3, 'O', 0, 1},
    {0x00D4, 'O', 1, 0},
    {0x00D5, 'O', 0, 4},
    {0x00D9, 'U', 0, 2},
    {0x00DA, 'U', 0, 1},
    {0x00DD, 'Y', 0, 1},
    {0x00E0, 'a', 0, 2},
    {0x00E1, 'a', 0, 1},
    {0x00E2, 'a', 1, 0},
    {0x00E3, 'a', 0, 4},
    {0x00E8, 'e', 0, 2},
    {0x00E9, 'e', 0, 1},
    {0x00EA, 'e', 1, 0},
    {0x00EC, 'i', 0, 2},
    {0x00ED, 'i', 0, 1},
    {0x00F2, 'o', 0, 2},
    {0x00F3, 'o', 0, 1},
    {0x00F4, 'o', 1, 0},
    {0x00F5, 'o', 0, 4},
    {0x00F9, 'u', 0, 2},
    {0x00FA, 'u', 0, 1},
    {0x00FD, 'y', 0, 1},
    {0x0102, 'A', 2, 0},
    {0x0103, 'a', 2, 0},
    {0x0110, 'D', 4, 0},
    {0x0111, 'd', 4, 0},
    {0x0128, 'I', 0, 4},
    {0x0129, 'i', 0, 4},
    {0x0168, 'U', 0, 4},
    {0x0169, 'u', 0, 4},
    {0x01A0, 'O', 3, 0},
    {0x01A1, 'o', 3, 0},
    {0x01AF, 'U', 3, 0},
    {0x01B0, 'u', 3, 0},
    {0x1EA0, 'A', 0, 5},
    {0x1EA1, 'a', 0, 5},
    {0x1EA2, 'A', 0, 3},
    {0x1EA3, 'a', 0, 3},
    {0x1EA4, 'A', 1, 1},
    {0x1EA5, 'a', 1, 1},
    {0x1EA6, 'A', 1, 2},
    {0x1EA7, 'a', 1, 2},
    {0x1EA8, 'A', 1, 3},
    {0x1EA9, 'a', 1, 3},
    {0x1EAA, 'A', 1, 4},
    {0x1EAB, 'a', 1, 4},
    {0x1EAC, 'A', 1, 5},
    {0x1EAD, 'a', 1, 5},
    {0x1EAE, 'A', 2, 1},
    {0x1EAF, 'a', 2, 1},
    {0x1EB0, 'A', 2, 2},
    {0x1EB1, 'a', 2, 2},
    {0x1EB2, 'A', 2, 3},
    {0x1EB3, 'a', 2, 3},
    {0x1EB4, 'A', 2, 4},
    {0x1EB5, 'a', 2, 4},
    {0x1EB6, 'A', 2, 5},
    {0x1EB7, 'a', 2, 5},
    {0x1EB8, 'E', 0, 5},
    {0x1EB9, 'e', 0, 5},
    {0x1EBA, 'E', 0, 3},
    {0x1EBB, 'e', 0, 3},
    {0x1EBC, 'E', 0, 4},
    {0x1EBD, 'e', 0, 4},
    {0x1EBE, 'E', 1, 1},
    {0x1EBF, 'e', 1, 1},
    {0x1EC0, 'E', 1, 2},
    {0x1EC1, 'e', 1, 2},
    {0x1EC2, 'E', 1, 3},
    {0x1EC3, 'e', 1, 3},
    {0x1EC4, 'E', 1, 4},
    {0x1EC5, 'e', 1, 4},
    {0x1EC6, 'E', 1, 5},
    {0x1EC7, 'e', 1, 5},
    {0x1EC8, 'I', 0, 3},
    {0x1EC9, 'i', 0, 3},
    {0x1ECA, 'I', 0, 5},
    {0x1ECB, 'i', 0, 5},
    {0x1ECC, 'O', 0, 5},
    {0x1ECD, 'o', 0, 5},
    {0x1ECE, 'O', 0, 3},
    {0x1ECF, 'o', 0, 3},
    {0x1ED0, 'O', 1, 1},
    {0x1ED1, 'o', 1, 1},
    {0x1ED2, 'O', 1, 2},
    {0x1ED3, 'o', 1, 2},
    {0x1ED4, 'O', 1, 3},
    {0x1ED5, 'o', 1, 3},
    {0x1ED6, 'O', 1, 4},
    {0x1ED7, 'o', 1, 4},
    {0x1ED8, 'O', 1, 5},
    {0x1ED9, 'o', 1, 5},
    {0x1EDA, 'O', 3, 1},
    {0x1EDB, 'o', 3, 1},
    {0x1EDC, 'O', 3, 2},
    {0x1EDD, 'o', 3, 2},
    {0x1EDE, 'O', 3, 3},
    {0x1EDF, 'o', 3, 3},
    {0x1EE0, 'O', 3, 4},
    {0x1EE1, 'o', 3, 4},
    {0x1EE2, 'O', 3, 5},
    {0x1EE3, 'o', 3, 5},
    {0x1EE4, 'U', 0, 5},
    {0x1EE5, 'u', 0, 5},
    {0x1EE6, 'U', 0, 3},
    {0x1EE7, 'u', 0, 3},
    {0x1EE8, 'U', 3, 1},
    {0x1EE9, 'u', 3, 1},
    {0x1EEA, 'U', 3, 2},
    {0x1EEB, 'u', 3, 2},
    {0x1EEC, 'U', 3, 3},
    {0x1EED, 'u', 3, 3},
    {0x1EEE, 'U', 3, 4},
    {0x1EEF, 'u', 3, 4},
    {0x1EF0, 'U', 3, 5},
    {0x1EF1, 'u', 3, 5},
    {0x1EF2, 'Y', 0, 2},
    {0x1EF3, 'y', 0, 2},
    {0x1EF4, 'Y', 0, 5},
    {0x1EF5, 'y', 0, 5},
    {0x1EF6, 'Y', 0, 3},
    {0x1EF7, 'y', 0, 3},
    {0x1EF8, 'Y', 0, 4},
    {0x1EF9, 'y', 0, 4},
};

static const vn_glyph_t *find_vn_glyph(uint16_t cp)
{
    int left = 0, right = 133;
    while (left <= right) {
        int mid = (left + right) / 2;
        if (VN_GLYPHS[mid].cp == cp) return &VN_GLYPHS[mid];
        if (VN_GLYPHS[mid].cp < cp) left = mid + 1;
        else right = mid - 1;
    }
    return NULL;
}

static uint32_t utf8_decode(const char **pstr)
{
    const uint8_t *s = (const uint8_t *)*pstr;
    if (!*s) return 0;

    if (s[0] < 0x80) {
        (*pstr)++;
        return s[0];
    } else if ((s[0] & 0xE0) == 0xC0) {
        if (!s[1] || (s[1] & 0xC0) != 0x80) { (*pstr)++; return '?'; }
        uint32_t cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *pstr += 2;
        return cp;
    } else if ((s[0] & 0xF0) == 0xE0) {
        if (!s[1] || !s[2] || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) { (*pstr)++; return '?'; }
        uint32_t cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *pstr += 3;
        return cp;
    } else if ((s[0] & 0xF8) == 0xF0) {
        *pstr += 4;
        return '?';
    }
    (*pstr)++;
    return '?';
}

void display_draw_char(int x, int y, char c, bool on)
{
    if (c < 32 || c > 126) c = ' ';
    int font_idx = c - 32;
    for (int col = 0; col < 5; col++) {
        uint8_t line = FONT5x7[font_idx][col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                display_draw_pixel(x + col, y + row, on);
            }
        }
    }
}

void display_draw_char_vn(int x, int y, uint32_t cp, bool on)
{
    if (cp < 128) {
        display_draw_char(x, y, (char)cp, on);
        return;
    }

    const vn_glyph_t *g = find_vn_glyph((uint16_t)cp);
    if (!g) {
        display_draw_char(x, y, ' ', on);
        return;
    }

    // 1. Draw base character
    display_draw_char(x, y, g->base, on);

    // 2. Clear dot on 'i' if a tone mark is placed above it
    if ((g->base == 'i' || g->base == 'j') && (g->tone > 0 || g->mod > 0)) {
        display_draw_pixel(x + 2, y + 2, !on);
    }

    // 3. Draw modifications (circumflex, breve, horn, bar for d/D)
    if (g->mod == 4) { // Bar on d/D
        if (g->base == 'd') {
            display_draw_line(x + 1, y + 2, x + 4, y + 2, on);
        } else {
            display_draw_line(x, y + 3, x + 3, y + 3, on);
        }
    } else if (g->mod == 1) { // Circumflex (^)
        display_draw_pixel(x + 2, y + 0, on);
        display_draw_pixel(x + 1, y + 1, on);
        display_draw_pixel(x + 3, y + 1, on);
    } else if (g->mod == 2) { // Breve (˘)
        display_draw_pixel(x + 1, y + 0, on);
        display_draw_pixel(x + 2, y + 1, on);
        display_draw_pixel(x + 3, y + 0, on);
    } else if (g->mod == 3) { // Horn (ơ, ư)
        display_draw_pixel(x + 4, y + 0, on);
        display_draw_pixel(x + 4, y + 1, on);
    }

    // 4. Draw Tone mark
    if (g->tone == 1) { // Sắc (acute /)
        if (g->mod != 0) {
            display_draw_pixel(x + 3, y + 0, on);
            display_draw_pixel(x + 4, y + 0, on);
        } else {
            display_draw_pixel(x + 3, y + 0, on);
            display_draw_pixel(x + 2, y + 1, on);
        }
    } else if (g->tone == 2) { // Huyền (grave \)
        if (g->mod != 0) {
            display_draw_pixel(x + 0, y + 0, on);
            display_draw_pixel(x + 1, y + 0, on);
        } else {
            display_draw_pixel(x + 1, y + 0, on);
            display_draw_pixel(x + 2, y + 1, on);
        }
    } else if (g->tone == 3) { // Hỏi (?)
        display_draw_pixel(x + 2, y + 0, on);
        display_draw_pixel(x + 3, y + 0, on);
        display_draw_pixel(x + 3, y + 1, on);
    } else if (g->tone == 4) { // Ngã (~)
        display_draw_pixel(x + 1, y + 0, on);
        display_draw_pixel(x + 2, y + 0, on);
        display_draw_pixel(x + 3, y + 1, on);
        display_draw_pixel(x + 4, y + 1, on);
    } else if (g->tone == 5) { // Nặng (.)
        display_draw_pixel(x + 2, y + 7, on);
        display_draw_pixel(x + 3, y + 7, on);
    }
}

int display_utf8_strlen(const char *str)
{
    if (!str) return 0;
    int len = 0;
    const char *p = str;
    while (*p) {
        utf8_decode(&p);
        len++;
    }
    return len;
}

void display_draw_string(int x, int y, const char *str, bool on)
{
    if (!str) return;
    int cur_x = x;
    const char *p = str;
    while (*p) {
        if (cur_x + 6 > OLED_WIDTH) break;
        uint32_t cp = utf8_decode(&p);
        display_draw_char_vn(cur_x, y, cp, on);
        cur_x += 6;
    }
}

void display_draw_string_marquee(int x, int y, const char *str, int offset, int max_w, bool on)
{
    if (!str || max_w <= 0) return;
    int len = display_utf8_strlen(str);
    int total_text_w = len * 6;
    if (total_text_w <= max_w) {
        display_draw_string(x, y, str, on);
        return;
    }

    int shift = offset % (total_text_w + 30);
    int start_col = -shift;

    const char *p = str;
    for (int i = 0; i < len; i++) {
        uint32_t cp = utf8_decode(&p);
        int char_x = x + start_col + (i * 6);
        if (char_x >= x && (char_x + 6) <= (x + max_w)) {
            display_draw_char_vn(char_x, y, cp, on);
        }
    }
}

void display_draw_bitmap(int x, int y, const uint8_t *bitmap, int w, int h, bool on)
{
    if (!bitmap) return;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int byte_idx = (j * ((w + 7) / 8)) + (i / 8);
            int bit_idx = 7 - (i % 8);
            if (bitmap[byte_idx] & (1 << bit_idx)) {
                display_draw_pixel(x + i, y + j, on);
            }
        }
    }
}

