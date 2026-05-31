#ifndef SEWU_GFX_H
#define SEWU_GFX_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Color definitions (RGB565) */
#define GFX_BLACK    0x0000
#define GFX_NAVY     0x000F
#define GFX_DARKGREEN 0x03E0
#define GFX_DARKCYAN 0x03EF
#define GFX_MAROON   0x7800
#define GFX_PURPLE   0x780F
#define GFX_OLIVE    0x7BE0
#define GFX_LIGHTGREY 0xC618
#define GFX_DARKGREY 0x7BEF
#define GFX_BLUE     0x001F
#define GFX_GREEN    0x07E0
#define GFX_CYAN     0x07FF
#define GFX_RED      0xF800
#define GFX_MAGENTA  0xF81F
#define GFX_YELLOW   0xFFE0
#define GFX_WHITE    0xFFFF
#define GFX_ORANGE   0xFD20
#define GFX_GREENYELLOW 0xAFE5
#define GFX_PINK     0xF81F

/* LCD rotation */
typedef enum {
    GFX_ROT_0   = 0,  /* Portrait */
    GFX_ROT_90  = 1,  /* Landscape */
    GFX_ROT_180 = 2,  /* Portrait inverted */
    GFX_ROT_270 = 3,  /* Landscape inverted */
} gfx_rotation_t;

/* LCD dimensions (after rotation) */
extern uint16_t gfx_width;
extern uint16_t gfx_height;

/**
 * @brief Initialize GFX with an initialized esp_lcd panel handle
 * @param panel_handle Initialized esp_lcd_panel_handle_t
 * @param rotation Display rotation
 * @return ESP_OK on success
 */
esp_err_t gfx_init(void *panel_handle, gfx_rotation_t rotation);

/**
 * @brief Set the IO handle for direct register access (MADCTL, etc.)
 * @param io_handle esp_lcd_panel_io_handle_t
 */
void gfx_set_io_handle(void *io_handle);

/**
 * @brief Set rotation
 */
esp_err_t gfx_set_rotation(gfx_rotation_t rotation);

/**
 * @brief Fill entire screen with a color
 */
void gfx_fill_screen(uint16_t color);

/**
 * @brief Draw a pixel at (x,y)
 */
void gfx_draw_pixel(int16_t x, int16_t y, uint16_t color);

/**
 * @brief Draw a vertical line
 */
void gfx_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color);

/**
 * @brief Draw a horizontal line
 */
void gfx_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);

/**
 * @brief Draw a rectangle outline
 */
void gfx_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/**
 * @brief Fill a rectangle
 */
void gfx_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/**
 * @brief Draw a line from (x0,y0) to (x1,y1)
 */
void gfx_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

/**
 * @brief Draw a circle outline
 */
void gfx_draw_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

/**
 * @brief Fill a circle
 */
void gfx_fill_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

/**
 * @brief Draw a triangle outline
 */
void gfx_draw_triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);

/**
 * @brief Fill a triangle
 */
void gfx_fill_triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);

/**
 * @brief Draw a rounded rectangle outline
 */
void gfx_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);

/**
 * @brief Fill a rounded rectangle
 */
void gfx_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);

/**
 * @brief Draw a single character (5x7 font)
 * @param x X position
 * @param y Y position
 * @param c Character to draw
 * @param color Foreground color
 * @param bg Background color
 */
void gfx_draw_char(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg);

/**
 * @brief Draw a string at position (x,y)
 * @param x X position
 * @param y Y position
 * @param str Null-terminated string
 * @param color Foreground color
 * @param bg Background color
 */
void gfx_draw_text(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg);

/**
 * @brief Draw a single character (5x7 font scaled 2x)
 */
void gfx_draw_char2x(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg);

/**
 * @brief Draw a string (scaled 2x)
 */
void gfx_draw_text2x(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg);

/**
 * @brief Draw a formatted string (scaled 2x)
 */
void gfx_printf2x(int16_t x, int16_t y, uint16_t color, uint16_t bg, const char *fmt, ...);

/**
 * @brief Draw a formatted string
 */
void gfx_printf(int16_t x, int16_t y, uint16_t color, uint16_t bg, const char *fmt, ...);

/**
 * @brief Draw a bitmap (image) from buffer at (x,y)
 * @param x X position
 * @param y Y position
 * @param w Width in pixels
 * @param h Height in pixels
 * @param bitmap Pixel data (RGB565)
 */
void gfx_draw_bitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *bitmap);

/**
 * @brief Create a 16-bit RGB565 color
 */
static inline uint16_t gfx_color565(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t v = ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
    /* Byte-swap: ESP32 SPI DMA sends bytes in memory order (little-endian = low byte first).
     * ST7789 expects big-endian RGB565 (high byte first). Pre-swapping here fixes the
     * byte order, eliminating the purple/wrong-color tint on neutral greys.
     * Equivalent to esp_lcd io_cfg.flags.swap_color_bytes=1 but works on all IDF versions. */
    return (uint16_t)((v >> 8) | (v << 8));
}

#ifdef __cplusplus
}
#endif

#endif /* SEWU_GFX_H */