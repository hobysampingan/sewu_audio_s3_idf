/* sewu_gfx.c — Lightweight GFX library for ST7789 via esp_lcd
 * Rotation handled by caller using esp_lcd functions.
 * GFX only draws — does NOT write MADCTL registers.
 * Row-by-row fills for buffer safety.
 */

#include "sewu_gfx.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "esp_lcd_panel_ops.h"

static esp_lcd_panel_handle_t s_panel = NULL;

uint16_t gfx_width  = 240;
uint16_t gfx_height = 320;

/* DMA-safe buffer pool. ESP32 SPI DMA queues up to 10 transactions.
 * Using 12 buffers ensures a buffer is never overwritten while in flight. */
#define GFX_POOL_SIZE 12
#define GFX_POOL_BUF_LEN 1024
static uint16_t s_pool[GFX_POOL_SIZE][GFX_POOL_BUF_LEN];
static int s_pool_idx = 0;

static uint16_t* get_gfx_buf(void) {
    uint16_t* buf = s_pool[s_pool_idx];
    s_pool_idx = (s_pool_idx + 1) % GFX_POOL_SIZE;
    return buf;
}

esp_err_t gfx_init(void *panel_handle, gfx_rotation_t rotation) {
    s_panel = (esp_lcd_panel_handle_t)panel_handle;
    if (!s_panel) return ESP_ERR_INVALID_ARG;
    /* Rotation already set by caller via esp_lcd functions */
    if (rotation == 0 || rotation == 2) {
        gfx_width  = 240;
        gfx_height = 320;
    } else {
        gfx_width  = 320;
        gfx_height = 240;
    }
    return ESP_OK;
}

void gfx_set_io_handle(void *io_handle) {
    /* Not needed — MADCTL handled by caller */
    (void)io_handle;
}

esp_err_t gfx_set_rotation(gfx_rotation_t rotation) {
    if (rotation == 0 || rotation == 2) {
        gfx_width  = 240;
        gfx_height = 320;
    } else {
        gfx_width  = 320;
        gfx_height = 240;
    }
    return ESP_OK;
}

void gfx_fill_screen(uint16_t color) {
    gfx_fill_rect(0, 0, gfx_width, gfx_height, color);
}

void gfx_draw_pixel(int16_t x, int16_t y, uint16_t color) {
    if (!s_panel) return;
    if (x < 0 || x >= (int16_t)gfx_width || y < 0 || y >= (int16_t)gfx_height) return;
    uint16_t* buf = get_gfx_buf();
    buf[0] = color;
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x+1, y+1, buf);
}

void gfx_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (!s_panel || w <= 0) return;
    if (y < 0 || y >= (int16_t)gfx_height) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > (int16_t)gfx_width) w = gfx_width - x;
    if (w <= 0) return;
    
    int chunk = (w > GFX_POOL_BUF_LEN) ? GFX_POOL_BUF_LEN : w;
    uint16_t* buf = get_gfx_buf();
    for (int i = 0; i < chunk; i++) buf[i] = color;
    
    for (int i = 0; i < w; i += chunk) {
        int cw = (w - i > chunk) ? chunk : w - i;
        if (i > 0) {
            // Need a new buffer for subsequent chunks to avoid corrupting the in-flight one
            buf = get_gfx_buf();
            for (int k = 0; k < cw; k++) buf[k] = color;
        }
        esp_lcd_panel_draw_bitmap(s_panel, x+i, y, x+i+cw, y+1, buf);
    }
}

void gfx_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color) {
    if (!s_panel || h <= 0) return;
    if (x < 0 || x >= (int16_t)gfx_width) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > (int16_t)gfx_height) h = gfx_height - y;
    if (h <= 0) return;
    
    int chunk = (h > GFX_POOL_BUF_LEN) ? GFX_POOL_BUF_LEN : h;
    uint16_t* buf = get_gfx_buf();
    for (int i = 0; i < chunk; i++) buf[i] = color;
    
    for (int i = 0; i < h; i += chunk) {
        int ch = (h - i > chunk) ? chunk : h - i;
        if (i > 0) {
            buf = get_gfx_buf();
            for (int k = 0; k < ch; k++) buf[k] = color;
        }
        esp_lcd_panel_draw_bitmap(s_panel, x, y+i, x+1, y+i+ch, buf);
    }
}

void gfx_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    gfx_draw_hline(x, y, w, color);
    gfx_draw_hline(x, y+h-1, w, color);
    gfx_draw_vline(x, y, h, color);
    gfx_draw_vline(x+w-1, y, h, color);
}

void gfx_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (!s_panel || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int16_t)gfx_width)  w = gfx_width - x;
    if (y + h > (int16_t)gfx_height) h = gfx_height - y;
    if (w <= 0 || h <= 0) return;
    
    int chunk = (w * h > GFX_POOL_BUF_LEN) ? GFX_POOL_BUF_LEN : (w * h);
    uint16_t* buf = get_gfx_buf();
    for (int i = 0; i < chunk; i++) buf[i] = color;
    
    // For large rects, we send chunks of GFX_POOL_BUF_LEN
    int total = w * h;
    int sent = 0;
    while (sent < total) {
        int c = (total - sent > chunk) ? chunk : total - sent;
        if (sent > 0) {
            buf = get_gfx_buf();
            for (int k = 0; k < c; k++) buf[k] = color;
        }
        // Calculate rectangle bounds for this chunk
        // For simplicity, esp_lcd_panel_draw_bitmap expects contiguous rectangles.
        // It's much easier to draw row by row.
        break; // Stop while loop and do row-by-row
    }
    
    // Proper row-by-row / chunk-by-chunk logic
    int row_chunk = (w > GFX_POOL_BUF_LEN) ? GFX_POOL_BUF_LEN : w;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col += row_chunk) {
            int cw = (w - col > row_chunk) ? row_chunk : w - col;
            buf = get_gfx_buf();
            for (int k = 0; k < cw; k++) buf[k] = color;
            esp_lcd_panel_draw_bitmap(s_panel, x+col, y+row, x+col+cw, y+row+1, buf);
        }
    }
}

void gfx_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    int16_t dx = (x1 > x0) ? (x1-x0) : (x0-x1);
    int16_t dy = (y1 > y0) ? (y1-y0) : (y0-y1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;
    while (1) {
        gfx_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void gfx_draw_char(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg) {
    if (c < 32 || c > 126) c = '?';
    uint8_t idx = c - 32;
    /* 5x7 font data */
    static const uint8_t font[] = {
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x04,0x04,0x04,0x04,0x00,0x04,
        0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00,0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A,
        0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04,0x18,0x19,0x02,0x04,0x08,0x13,0x03,
        0x0C,0x12,0x14,0x08,0x15,0x12,0x0D,0x0C,0x04,0x08,0x00,0x00,0x00,0x00,
        0x02,0x04,0x08,0x08,0x08,0x04,0x02,0x08,0x04,0x02,0x02,0x02,0x04,0x08,
        0x00,0x04,0x15,0x0E,0x15,0x04,0x00,0x00,0x04,0x04,0x1F,0x04,0x04,0x00,
        0x00,0x00,0x00,0x00,0x0C,0x04,0x08,0x00,0x00,0x00,0x1F,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x01,0x02,0x02,0x04,0x08,0x08,0x10,
        0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,
        0x0E,0x11,0x01,0x02,0x04,0x08,0x1F,0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E,
        0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,
        0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,0x1F,0x01,0x02,0x04,0x08,0x08,0x08,
        0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,
        0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08,
        0x02,0x04,0x08,0x10,0x08,0x04,0x02,0x00,0x00,0x1F,0x00,0x1F,0x00,0x00,
        0x08,0x04,0x02,0x01,0x02,0x04,0x08,0x0E,0x11,0x01,0x02,0x04,0x00,0x04,
        0x0E,0x11,0x17,0x15,0x17,0x10,0x0F,0x0E,0x11,0x11,0x1F,0x11,0x11,0x11,
        0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,
        0x1C,0x12,0x11,0x11,0x11,0x12,0x1C,0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,
        0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,0x0E,0x11,0x10,0x17,0x11,0x11,0x0F,
        0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0x1F,0x04,0x04,0x04,0x04,0x04,0x1F,
        0x01,0x01,0x01,0x01,0x11,0x11,0x0E,0x11,0x12,0x14,0x18,0x14,0x12,0x11,
        0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x11,0x1B,0x15,0x15,0x11,0x11,0x11,
        0x11,0x19,0x15,0x13,0x11,0x11,0x11,0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,
        0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,
        0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E,
        0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0x11,0x11,0x11,0x11,0x11,0x11,0x0E,
        0x11,0x11,0x11,0x11,0x11,0x0A,0x04,0x11,0x11,0x11,0x15,0x15,0x15,0x0A,
        0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0x11,0x11,0x0A,0x04,0x04,0x04,0x04,
        0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,
    };

    uint16_t* buf = get_gfx_buf();
    for (int i = 0; i < 48; i++) buf[i] = bg;

    for (int ry = 0; ry < 8; ry++) {
        uint8_t row = (ry < 7) ? font[idx * 7 + ry] : 0;
        for (int rx = 0; rx < 6; rx++) {
            if (rx < 5 && ((row >> (4 - rx)) & 1)) {
                buf[ry * 6 + rx] = color;
            }
        }
    }
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + 6, y + 8, buf);
}

void gfx_draw_text(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg) {
    if (!str || !s_panel) return;
    int cx = x;
    while (*str) {
        if (cx + 6 > gfx_width) break;
        gfx_draw_char(cx, y, *str, color, bg);
        cx += 6;
        str++;
    }
}

void gfx_draw_char2x(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg) {
    if (c < 32 || c > 126) c = '?';
    uint8_t idx = c - 32;
    /* 5x7 font data (same as above) */
    static const uint8_t font[] = {
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x04,0x04,0x04,0x04,0x00,0x04,
        0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00,0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A,
        0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04,0x18,0x19,0x02,0x04,0x08,0x13,0x03,
        0x0C,0x12,0x14,0x08,0x15,0x12,0x0D,0x0C,0x04,0x08,0x00,0x00,0x00,0x00,
        0x02,0x04,0x08,0x08,0x08,0x04,0x02,0x08,0x04,0x02,0x02,0x02,0x04,0x08,
        0x00,0x04,0x15,0x0E,0x15,0x04,0x00,0x00,0x04,0x04,0x1F,0x04,0x04,0x00,
        0x00,0x00,0x00,0x00,0x0C,0x04,0x08,0x00,0x00,0x00,0x1F,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x01,0x02,0x02,0x04,0x08,0x08,0x10,
        0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,
        0x0E,0x11,0x01,0x02,0x04,0x08,0x1F,0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E,
        0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,
        0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,0x1F,0x01,0x02,0x04,0x08,0x08,0x08,
        0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,
        0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08,
        0x02,0x04,0x08,0x10,0x08,0x04,0x02,0x00,0x00,0x1F,0x00,0x1F,0x00,0x00,
        0x08,0x04,0x02,0x01,0x02,0x04,0x08,0x0E,0x11,0x01,0x02,0x04,0x00,0x04,
        0x0E,0x11,0x17,0x15,0x17,0x10,0x0F,0x0E,0x11,0x11,0x1F,0x11,0x11,0x11,
        0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,
        0x1C,0x12,0x11,0x11,0x11,0x12,0x1C,0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,
        0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,0x0E,0x11,0x10,0x17,0x11,0x11,0x0F,
        0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0x1F,0x04,0x04,0x04,0x04,0x04,0x1F,
        0x01,0x01,0x01,0x01,0x11,0x11,0x0E,0x11,0x12,0x14,0x18,0x14,0x12,0x11,
        0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x11,0x1B,0x15,0x15,0x11,0x11,0x11,
        0x11,0x19,0x15,0x13,0x11,0x11,0x11,0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,
        0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,
        0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E,
        0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0x11,0x11,0x11,0x11,0x11,0x11,0x0E,
        0x11,0x11,0x11,0x11,0x11,0x0A,0x04,0x11,0x11,0x11,0x15,0x15,0x15,0x0A,
        0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0x11,0x11,0x0A,0x04,0x04,0x04,0x04,
        0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,
    };

    uint16_t* buf = get_gfx_buf();
    for (int i = 0; i < 192; i++) buf[i] = bg;

    for (int ry = 0; ry < 8; ry++) {
        uint8_t row = (ry < 7) ? font[idx * 7 + ry] : 0;
        for (int rx = 0; rx < 6; rx++) {
            if (rx < 5 && ((row >> (4 - rx)) & 1)) {
                buf[(ry * 2) * 12 + (rx * 2)] = color;
                buf[(ry * 2) * 12 + (rx * 2) + 1] = color;
                buf[(ry * 2 + 1) * 12 + (rx * 2)] = color;
                buf[(ry * 2 + 1) * 12 + (rx * 2) + 1] = color;
            }
        }
    }
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + 12, y + 16, buf);
}

void gfx_printf(int16_t x, int16_t y, uint16_t color, uint16_t bg, const char *fmt, ...) {
    char buf[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gfx_draw_text(x, y, buf, color, bg);
}

void gfx_draw_text2x(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg) {
    if (!str || !s_panel) return;
    int cx = x;
    while (*str) {
        if (cx + 12 > gfx_width) break;
        gfx_draw_char2x(cx, y, *str, color, bg);
        cx += 12;
        str++;
    }
}

void gfx_printf2x(int16_t x, int16_t y, uint16_t color, uint16_t bg, const char *fmt, ...) {
    char buf[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gfx_draw_text2x(x, y, buf, color, bg);
}

void gfx_draw_bitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *bitmap) {
    if (!s_panel || !bitmap || w <= 0 || h <= 0) return;
    if (x + w <= 0 || x >= (int16_t)gfx_width) return;
    if (y + h <= 0 || y >= (int16_t)gfx_height) return;
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x+w, y+h, bitmap);
}

void gfx_draw_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    int16_t x = 0, y = r, err = 1 - r;
    while (x <= y) {
        gfx_draw_pixel(x0+x, y0+y, color); gfx_draw_pixel(x0-x, y0+y, color);
        gfx_draw_pixel(x0+x, y0-y, color); gfx_draw_pixel(x0-x, y0-y, color);
        gfx_draw_pixel(x0+y, y0+x, color); gfx_draw_pixel(x0-y, y0+x, color);
        gfx_draw_pixel(x0+y, y0-x, color); gfx_draw_pixel(x0-y, y0-x, color);
        x++;
        if (err < 0) err += 2*x+1;
        else { y--; err += 2*(x-y)+1; }
    }
}

void gfx_fill_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    int16_t x = 0, y = r, err = 1 - r;
    while (x <= y) {
        gfx_draw_hline(x0 - x, y0 + y, 2 * x + 1, color);
        gfx_draw_hline(x0 - x, y0 - y, 2 * x + 1, color);
        gfx_draw_hline(x0 - y, y0 + x, 2 * y + 1, color);
        gfx_draw_hline(x0 - y, y0 - x, 2 * y + 1, color);
        x++;
        if (err < 0) err += 2 * x + 1;
        else { y--; err += 2 * (x - y) + 1; }
    }
}

void gfx_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (r <= 0) {
        gfx_draw_rect(x, y, w, h, color);
        return;
    }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    
    gfx_draw_hline(x + r, y, w - 2 * r, color);
    gfx_draw_hline(x + r, y + h - 1, w - 2 * r, color);
    gfx_draw_vline(x, y + r, h - 2 * r, color);
    gfx_draw_vline(x + w - 1, y + r, h - 2 * r, color);
    
    int16_t cx = 0, cy = r, err = 1 - r;
    while (cx <= cy) {
        gfx_draw_pixel(x + r - cx, y + r - cy, color);
        gfx_draw_pixel(x + r - cy, y + r - cx, color);
        gfx_draw_pixel(x + w - 1 - r + cx, y + r - cy, color);
        gfx_draw_pixel(x + w - 1 - r + cy, y + r - cx, color);
        gfx_draw_pixel(x + r - cx, y + h - 1 - r + cy, color);
        gfx_draw_pixel(x + r - cy, y + h - 1 - r + cx, color);
        gfx_draw_pixel(x + w - 1 - r + cx, y + h - 1 - r + cy, color);
        gfx_draw_pixel(x + w - 1 - r + cy, y + h - 1 - r + cx, color);
        
        cx++;
        if (err < 0) err += 2 * cx + 1;
        else { cy--; err += 2 * (cx - cy) + 1; }
    }
}

void gfx_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (r <= 0) {
        gfx_fill_rect(x, y, w, h, color);
        return;
    }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    
    gfx_fill_rect(x, y + r, w, h - 2 * r, color);
    
    int16_t cx = 0, cy = r, err = 1 - r;
    while (cx <= cy) {
        gfx_draw_hline(x + r - cx, y + r - cy, w - 2 * r + 2 * cx, color);
        gfx_draw_hline(x + r - cy, y + r - cx, w - 2 * r + 2 * cy, color);
        gfx_draw_hline(x + r - cx, y + h - 1 - r + cy, w - 2 * r + 2 * cx, color);
        gfx_draw_hline(x + r - cy, y + h - 1 - r + cx, w - 2 * r + 2 * cy, color);
        
        cx++;
        if (err < 0) err += 2 * cx + 1;
        else { cy--; err += 2 * (cx - cy) + 1; }
    }
}

void gfx_draw_triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    gfx_draw_line(x0, y0, x1, y1, color);
    gfx_draw_line(x1, y1, x2, y2, color);
    gfx_draw_line(x2, y2, x0, y0, color);
}

void gfx_fill_triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { int16_t t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

    if (y0 == y2) {
        int16_t a = x0, b = x0;
        if (x1 < a)      a = x1;
        else if (x1 > b) b = x1;
        if (x2 < a)      a = x2;
        else if (x2 > b) b = x2;
        gfx_draw_hline(a, y0, b - a + 1, color);
        return;
    }

    int16_t dx01 = x1 - x0, dy01 = y1 - y0,
            dx02 = x2 - x0, dy02 = y2 - y0,
            dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t sa = 0, sb = 0;

    int16_t last = (y1 == y2) ? y1 : y1 - 1;

    int16_t y;
    for (y = y0; y <= last; y++) {
        int16_t a = x0 + sa / dy01;
        int16_t b = x0 + sb / dy02;
        sa += dx01;
        sb += dx02;
        if (a > b) { int16_t t = a; a = b; b = t; }
        gfx_draw_hline(a, y, b - a + 1, color);
    }

    sa = (int32_t)dx12 * (y - y1);
    sb = (int32_t)dx02 * (y - y0);
    for (; y <= y2; y++) {
        int16_t a = x1 + sa / dy12;
        int16_t b = x0 + sb / dy02;
        sa += dx12;
        sb += dx02;
        if (a > b) { int16_t t = a; a = b; b = t; }
        gfx_draw_hline(a, y, b - a + 1, color);
    }
}
