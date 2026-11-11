#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_WIDTH  128
#define OLED_HEIGHT 64

esp_err_t display_init(void);
void display_clear(void);
void display_flush(void);
void display_draw_pixel(int x, int y, bool on);
void display_draw_line(int x0, int y0, int x1, int y1, bool on);
void display_draw_rect(int x, int y, int w, int h, bool on);
void display_fill_rect(int x, int y, int w, int h, bool on);
void display_draw_circle(int x0, int y0, int r, bool on);
void display_fill_circle(int x0, int y0, int r, bool on);
void display_draw_round_rect(int x, int y, int w, int h, int r, bool on);
void display_fill_round_rect(int x, int y, int w, int h, int r, bool on);
void display_draw_char(int x, int y, char c, bool on);
void display_draw_char_vn(int x, int y, uint32_t cp, bool on);
int display_utf8_strlen(const char *str);
void display_draw_string(int x, int y, const char *str, bool on);
void display_draw_string_marquee(int x, int y, const char *str, int offset, int max_w, bool on);
void display_draw_bitmap(int x, int y, const uint8_t *bitmap, int w, int h, bool on);

#ifdef __cplusplus
}
#endif

