/*
 * display.c — LCD display pipeline (PicoCalc)
 *
 * Video path (per scanline):
 *   InfoNES_PreDrawLine points the PPU at s_line_buffer[256].
 *   InfoNES_PostDrawLine packs that RGB565 line into the LCD DMA buffer.
 *   Completed strips are flushed to the active NES viewport.
 *
 * Normal view keeps the original 256×240 frame centered on the 320×320 LCD.
 * Stretch view expands 256×240 to 320×300 by repeating every fourth source
 * line and every fourth source pixel.
 *
 * Platform-specific LCD SPI/DMA functions are declared extern and must
 * be implemented in the driver layer (drivers/lcd_spi.c).
 *
 * Part of Picocalc_NESco
 * MIT License
 */

#include "display.h"

#include "InfoNES.h"
#include "InfoNES_System.h"
#include "../font/menu_font_pixelmplus.h"
#include "version.h"

#include <stdio.h>
#include <string.h>

#ifdef PICO_BUILD
#include "pico/time.h"
#endif

/* =====================================================================
 *  NES system palette source data
 *  Standard NTSC NES palette in the original RGB444-like encoding.
 * ===================================================================== */
bool micromenu = false;

const WORD NesPalette[64] = {
    0x77f7, 0x37f0, 0x28f2, 0x28f4, 0x17f6, 0x14f8, 0x11f8, 0x20f6,
    0x30f4, 0x40f1, 0x40f0, 0x41f0, 0x44f0, 0x00f0, 0x00f0, 0x00f0,
    0xbbfb, 0x7cf1, 0x6df4, 0x5df8, 0x4bfb, 0x47fc, 0x43fc, 0x50fa,
    0x60f7, 0x70f4, 0x81f1, 0x84f0, 0x88f0, 0x00f0, 0x00f0, 0x00f0,
    0xFFFF, 0xcff6, 0xbff9, 0x9ffd, 0x9fff, 0x9cff, 0x98ff, 0xa5ff,
    0xb3fc, 0xc3f9, 0xd6f6, 0xd9f4, 0xddf4, 0x55f5, 0x00f0, 0x00f0,
    0xFFFF, 0xeffc, 0xeffe, 0xefff, 0xefff, 0xdfff, 0xddff, 0xecff,
    0xebff, 0xfbfd, 0xfcfc, 0xfdfb, 0xfffb, 0xccfc, 0x00f0, 0x00f0,
};

/* =====================================================================
 *  RGB444 nibble → RGB565 component LUTs
 *  4-bit R/G/B nibbles are expanded independently, then ORed together.
 * ===================================================================== */
static WORD s_rgb4_to_rgb565_r[16];
static WORD s_rgb4_to_rgb565_g[16];
static WORD s_rgb4_to_rgb565_b[16];

static int s_strip_line = 0;   /* Source lines written into the current strip. */
static WORD s_line_buffer[256];
static uint64_t s_perf_lcd_wait_us = 0;
static uint64_t s_perf_lcd_flush_us = 0;

#ifdef PICO_BUILD
static uint64_t s_last_frame_us = 0;
static uint64_t s_next_frame_deadline_us = 0;
static WORD s_active_frame_skip = 0;
#endif

static display_mode_t s_display_mode = DISPLAY_MODE_NES_VIEW;
static nes_view_scale_mode_t s_nes_view_scale = NES_VIEW_SCALE_NORMAL;
static volatile display_lcd_worker_state_t s_lcd_worker_state = DISPLAY_LCD_WORKER_STOPPED;

/* Active LCD rectangle for NES output or full-screen UI operations. */
static int s_lcd_x0 = 32;
static int s_lcd_y0 = 24;
static int s_lcd_w  = 256;
static int s_lcd_h  = 240;

enum {
    NES_VIEW_NORMAL_X = 32,
    NES_VIEW_NORMAL_Y = 24,
    NES_VIEW_NORMAL_W = 256,
    NES_VIEW_NORMAL_H = 240,
    NES_VIEW_STRETCH_X = 0,
    NES_VIEW_STRETCH_Y = 10,
    NES_VIEW_STRETCH_W = 320,
    NES_VIEW_STRETCH_H = 300,
    NES_STRETCH_STRIP_HEIGHT = STRIP_HEIGHT + (STRIP_HEIGHT / 4),
};

static void display_draw_text_span_scaled(int x,
                                          int y,
                                          int span_w,
                                          const char *text,
                                          WORD fg,
                                          WORD bg,
                                          int font_scale,
                                          int char_advance);
static void display_draw_text_span_scaled_cropped(int x,
                                                  int y,
                                                  int span_w,
                                                  const char *text,
                                                  WORD fg,
                                                  WORD bg,
                                                  int font_scale,
                                                  int char_advance,
                                                  int crop_top_rows);
static int display_measure_text_width(const char *text, int char_advance, int glyph_w, int scale);
static void display_apply_nes_viewport(void);
extern "C" void lcd_dma_wait(void);

display_lcd_worker_state_t display_lcd_worker_get_state(void) {
    return s_lcd_worker_state;
}

bool display_lcd_worker_is_running(void) {
    return s_lcd_worker_state == DISPLAY_LCD_WORKER_RUNNING;
}

void display_lcd_worker_prepare_nes_view(void) {
    /* Phase 2 only defines ownership points. Actual core1 LCD transfer starts later. */
    s_lcd_worker_state = DISPLAY_LCD_WORKER_STOPPED;
}

void display_lcd_worker_stop_and_drain(void) {
    if (s_lcd_worker_state == DISPLAY_LCD_WORKER_RUNNING) {
        s_lcd_worker_state = DISPLAY_LCD_WORKER_DRAINING;
    }
    lcd_dma_wait();
    s_lcd_worker_state = DISPLAY_LCD_WORKER_STOPPED;
}

static void display_prepare_nes_view_surface(void) {
    static const WORD bg = 0x0000;
    static const WORD fg = 0x7BEF;
    static const char *hint_normal = "Shift+W Stretch Screen";
    static const char *hint_stretch = "Shift+W Normal Screen";
    int hint_w;

    display_set_viewport(0, 0, 320, 320);
    display_clear_rgb565(bg);

    if (s_nes_view_scale == NES_VIEW_SCALE_NORMAL) {
        hint_w = display_measure_text_width(hint_normal, 6, MENU_FONT_PIXELMPLUS_WIDTH, 1);
        display_draw_text_span_scaled(8, 309, hint_w, hint_normal, fg, bg, 1, 6);
    } else {
        hint_w = display_measure_text_width(hint_stretch, 6, MENU_FONT_PIXELMPLUS_WIDTH, 1);
        display_draw_text_span_scaled_cropped(8, 310, hint_w, hint_stretch, fg, bg, 1, 6, 1);
    }

    display_apply_nes_viewport();
}

/* =====================================================================
 *  Extern LCD driver calls (implement in drivers/lcd_spi.c)
 * ===================================================================== */
extern "C" void lcd_init(void);
extern "C" void lcd_set_window(int x, int y, int w, int h);
extern "C" void lcd_dma_write_rgb565_async(const WORD *buf, int n_pixels);
extern "C" BYTE *lcd_dma_acquire_buffer(void);
extern "C" void lcd_dma_write_bytes_async(const BYTE *buf, int n_bytes);
extern "C" void lcd_dma_wait(void);

static const BYTE *display_menu_glyph(char c) {
    static const BYTE unknown[MENU_FONT_PIXELMPLUS_HEIGHT] = {
        0x00, 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00
    };

    if ((unsigned char)c < MENU_FONT_PIXELMPLUS_FIRST ||
        (unsigned char)c > MENU_FONT_PIXELMPLUS_LAST) {
        return unknown;
    }
    return menu_font_pixelmplus[(unsigned char)c - MENU_FONT_PIXELMPLUS_FIRST];
}

static void display_draw_text_span_scaled(int x,
                                          int y,
                                          int span_w,
                                          const char *text,
                                          WORD fg,
                                          WORD bg,
                                          int font_scale,
                                          int char_advance) {
    WORD line[320];
    int width = span_w;
    int height = MENU_FONT_PIXELMPLUS_HEIGHT * font_scale;

    if (width <= 0 || width > 320) {
        return;
    }

    lcd_set_window(x, y, width, height);
    for (int py = 0; py < height; py++) {
        for (int px = 0; px < width; px++) {
            line[px] = bg;
        }

        for (int idx = 0; text[idx] &&
                          idx * char_advance + MENU_FONT_PIXELMPLUS_WIDTH * font_scale <= width;
             idx++) {
            const BYTE *glyph = display_menu_glyph(text[idx]);
            int base_x = idx * char_advance;
            int glyph_row = py / font_scale;

            if (glyph_row < 0 || glyph_row >= MENU_FONT_PIXELMPLUS_HEIGHT) {
                continue;
            }

            BYTE bits = glyph[glyph_row];
            for (int col = 0; col < MENU_FONT_PIXELMPLUS_WIDTH; col++) {
                WORD color =
                    (bits & (1u << (MENU_FONT_PIXELMPLUS_WIDTH - 1 - col))) ? fg : bg;
                for (int sx = 0; sx < font_scale; sx++) {
                    int px = base_x + col * font_scale + sx;
                    if (px >= 0 && px < width) {
                        line[px] = color;
                    }
                }
            }
        }

        lcd_dma_write_rgb565_async(line, width);
        lcd_dma_wait();
    }
}

static void display_draw_text_span_scaled_cropped(int x,
                                                  int y,
                                                  int span_w,
                                                  const char *text,
                                                  WORD fg,
                                                  WORD bg,
                                                  int font_scale,
                                                  int char_advance,
                                                  int crop_top_rows) {
    WORD line[320];
    int width = span_w;
    int src_height = MENU_FONT_PIXELMPLUS_HEIGHT * font_scale;
    int height = src_height - crop_top_rows;

    if (width <= 0 || width > 320 || crop_top_rows < 0 || crop_top_rows >= src_height) {
        return;
    }

    lcd_set_window(x, y, width, height);
    for (int py = 0; py < height; py++) {
        int src_py = py + crop_top_rows;
        int glyph_row = src_py / font_scale;

        for (int px = 0; px < width; px++) {
            line[px] = bg;
        }

        if (glyph_row >= 0 && glyph_row < MENU_FONT_PIXELMPLUS_HEIGHT) {
            for (int idx = 0; text[idx] &&
                              idx * char_advance + MENU_FONT_PIXELMPLUS_WIDTH * font_scale <= width;
                 idx++) {
                const BYTE *glyph = display_menu_glyph(text[idx]);
                int base_x = idx * char_advance;
                BYTE bits = glyph[glyph_row];

                for (int col = 0; col < MENU_FONT_PIXELMPLUS_WIDTH; col++) {
                    WORD color =
                        (bits & (1u << (MENU_FONT_PIXELMPLUS_WIDTH - 1 - col))) ? fg : bg;
                    for (int sx = 0; sx < font_scale; sx++) {
                        int px = base_x + col * font_scale + sx;
                        if (px >= 0 && px < width) {
                            line[px] = color;
                        }
                    }
                }
            }
        }

        lcd_dma_write_rgb565_async(line, width);
        lcd_dma_wait();
    }
}

static int display_measure_text_width(const char *text, int char_advance, int glyph_w, int scale) {
    size_t len = strlen(text);

    if (len == 0) {
        return 0;
    }

    return (int)((len - 1) * char_advance + glyph_w * scale);
}

/* =====================================================================
 *  display_init
 * ===================================================================== */
void display_init(void) {
    /* Build RGB444 nibble → RGB565 component LUTs */
    for (int i = 0; i < 16; i++) {
        int r5 = (i << 1) | (i >> 3);
        int g6 = (i << 2) | (i >> 2);
        int b5 = (i << 1) | (i >> 3);
        s_rgb4_to_rgb565_r[i] = (WORD)(r5 << 11);
        s_rgb4_to_rgb565_g[i] = (WORD)(g6 << 5);
        s_rgb4_to_rgb565_b[i] = (WORD)b5;
    }

    lcd_init();
    display_set_mode(DISPLAY_MODE_NES_VIEW);

    s_strip_line = 0;
#ifdef PICO_BUILD
    s_last_frame_us = 0;
    s_next_frame_deadline_us = 0;
    s_active_frame_skip = 0;
#endif
}

void display_set_viewport(int x, int y, int w, int h) {
    s_lcd_x0 = x;
    s_lcd_y0 = y;
    s_lcd_w = w;
    s_lcd_h = h;
}

void display_get_viewport(int *x, int *y, int *w, int *h) {
    if (x) {
        *x = s_lcd_x0;
    }
    if (y) {
        *y = s_lcd_y0;
    }
    if (w) {
        *w = s_lcd_w;
    }
    if (h) {
        *h = s_lcd_h;
    }
}

static void display_apply_nes_viewport(void) {
    if (s_nes_view_scale == NES_VIEW_SCALE_STRETCH_320X300) {
        display_set_viewport(NES_VIEW_STRETCH_X,
                             NES_VIEW_STRETCH_Y,
                             NES_VIEW_STRETCH_W,
                             NES_VIEW_STRETCH_H);
    } else {
        display_set_viewport(NES_VIEW_NORMAL_X,
                             NES_VIEW_NORMAL_Y,
                             NES_VIEW_NORMAL_W,
                             NES_VIEW_NORMAL_H);
    }
}

void display_set_mode(display_mode_t mode) {
    if (mode == DISPLAY_MODE_FULLSCREEN) {
        display_lcd_worker_stop_and_drain();
    }

    s_display_mode = mode;
    if (mode == DISPLAY_MODE_FULLSCREEN) {
        display_set_viewport(0, 0, 320, 320);
    } else {
        display_prepare_nes_view_surface();
        display_lcd_worker_prepare_nes_view();
    }

    s_strip_line = 0;
#ifdef PICO_BUILD
    s_last_frame_us = 0;
    s_next_frame_deadline_us = 0;
    s_active_frame_skip = 0;
#endif
}

void display_toggle_nes_view_scale(void) {
    display_lcd_worker_stop_and_drain();

    s_nes_view_scale =
        (s_nes_view_scale == NES_VIEW_SCALE_NORMAL)
            ? NES_VIEW_SCALE_STRETCH_320X300
            : NES_VIEW_SCALE_NORMAL;

    if (s_display_mode == DISPLAY_MODE_NES_VIEW) {
        display_prepare_nes_view_surface();
    }

    s_strip_line = 0;
#ifdef PICO_BUILD
    s_last_frame_us = 0;
    s_next_frame_deadline_us = 0;
    s_active_frame_skip = 0;
#endif
}

nes_view_scale_mode_t display_get_nes_view_scale(void) {
    return s_nes_view_scale;
}

void display_perf_reset(void) {
    s_perf_lcd_wait_us = 0;
    s_perf_lcd_flush_us = 0;
}

void display_reset_frame_pacing(void) {
#ifdef PICO_BUILD
    s_last_frame_us = 0;
    s_next_frame_deadline_us = 0;
    s_active_frame_skip = 0;
#endif
    FrameSkip = 0;
}

void display_perf_snapshot(uint64_t *wait_us, uint64_t *flush_us) {
    if (wait_us) {
        *wait_us = s_perf_lcd_wait_us;
    }
    if (flush_us) {
        *flush_us = s_perf_lcd_flush_us;
    }
}

void display_clear_rgb565(WORD color) {
    WORD line[320];
    int width = s_lcd_w;
    int height = s_lcd_h;

    if (width <= 0 || height <= 0 || width > 320 || height > 320) {
        return;
    }

    for (int i = 0; i < width; i++) {
        line[i] = color;
    }

    lcd_dma_wait();
    lcd_set_window(s_lcd_x0, s_lcd_y0, width, height);
    for (int y = 0; y < height; y++) {
        lcd_dma_write_rgb565_async(line, width);
    }
    lcd_dma_wait();
}

void display_show_opening_screen(void) {
    static const WORD bg = 0x0000;
    static const WORD accent = 0x07E0;
    static const WORD fg = 0xFFFF;
    static const char *title = "PicoCalc NESco";
    char version_line[32];
    WORD line[320];
    int title_w = display_measure_text_width(title, 10, MENU_FONT_PIXELMPLUS_WIDTH, 2);
    int version_w;

    snprintf(version_line, sizeof(version_line), "Ver. %s", PICOCALC_NESCO_VERSION);
    version_w = display_measure_text_width(version_line, 6, MENU_FONT_PIXELMPLUS_WIDTH, 1);
    for (int i = 0; i < 320; i++) {
        line[i] = accent;
    }

    display_set_mode(DISPLAY_MODE_FULLSCREEN);
    display_clear_rgb565(bg);
    lcd_set_window(0, 88, 320, 2);
    for (int row = 0; row < 2; row++) {
        lcd_dma_write_rgb565_async(line, 320);
    }
    lcd_set_window(0, 230, 320, 2);
    for (int row = 0; row < 2; row++) {
        lcd_dma_write_rgb565_async(line, 320);
    }
    lcd_dma_wait();

    display_draw_text_span_scaled((320 - title_w) / 2, 120, title_w, title, accent, bg, 2, 10);
    display_draw_text_span_scaled((320 - version_w) / 2,
                                  172,
                                  version_w,
                                  version_line,
                                  fg,
                                  bg,
                                  1,
                                  6);
}

void display_show_loading_screen(void) {
    static const WORD bg = 0x0000;
    static const WORD fg = 0xFFFF;
    static const char *loading = "Loading...";
    int loading_w = display_measure_text_width(loading, 6, MENU_FONT_PIXELMPLUS_WIDTH, 1);

    display_set_mode(DISPLAY_MODE_FULLSCREEN);
    display_clear_rgb565(bg);
    display_draw_text_span_scaled((320 - loading_w) / 2,
                                  156,
                                  loading_w,
                                  loading,
                                  fg,
                                  bg,
                                  1,
                                  6);
}

/* =====================================================================
 *  InfoNES_PreDrawLine — point infones at the active line buffer
 * ===================================================================== */
void InfoNES_PreDrawLine(int scanline) {
    (void)scanline;
    InfoNES_SetLineBuffer(s_line_buffer, 256);
}

static void display_pack_line_normal(BYTE *dst, const WORD *src) {
    for (int x = 0; x < 256; x += 4) {
        WORD px;

        px = src[x + 0];
        dst[(x + 0) * 2 + 0] = (BYTE)(px >> 8);
        dst[(x + 0) * 2 + 1] = (BYTE)(px & 0xFFu);

        px = src[x + 1];
        dst[(x + 1) * 2 + 0] = (BYTE)(px >> 8);
        dst[(x + 1) * 2 + 1] = (BYTE)(px & 0xFFu);

        px = src[x + 2];
        dst[(x + 2) * 2 + 0] = (BYTE)(px >> 8);
        dst[(x + 2) * 2 + 1] = (BYTE)(px & 0xFFu);

        px = src[x + 3];
        dst[(x + 3) * 2 + 0] = (BYTE)(px >> 8);
        dst[(x + 3) * 2 + 1] = (BYTE)(px & 0xFFu);
    }
}

static void display_pack_line_stretch_320(BYTE *dst, const WORD *src) {
    for (int block = 0; block < 64; ++block) {
        const WORD s0 = src[block * 4 + 0];
        const WORD s1 = src[block * 4 + 1];
        const WORD s2 = src[block * 4 + 2];
        const WORD s3 = src[block * 4 + 3];
        WORD out[5] = {s0, s1, s2, s3, s3};

        for (int i = 0; i < 5; ++i) {
            const int dst_x = block * 5 + i;
            const WORD px = out[i];
            dst[dst_x * 2 + 0] = (BYTE)(px >> 8);
            dst[dst_x * 2 + 1] = (BYTE)(px & 0xFFu);
        }
    }
}

/* =====================================================================
 *  InfoNES_PostDrawLine — pack RGB565 line buffer into LCD byte buffer
 * ===================================================================== */
void InfoNES_PostDrawLine(int scanline, bool frommenu) {
    const WORD *src = s_line_buffer;
    BYTE *strip = lcd_dma_acquire_buffer();

    if (s_nes_view_scale == NES_VIEW_SCALE_STRETCH_320X300) {
        const int src_strip_line = s_strip_line;
        const int dst_strip_line = src_strip_line + (src_strip_line / 4);
        const int repeat_count = ((src_strip_line & 3) == 3) ? 2 : 1;

        for (int rep = 0; rep < repeat_count; ++rep) {
            BYTE *dst = strip + (dst_strip_line + rep) * NES_VIEW_STRETCH_W * 2;
            display_pack_line_stretch_320(dst, src);
        }
    } else {
        BYTE *dst = strip + s_strip_line * NES_VIEW_NORMAL_W * 2;
        display_pack_line_normal(dst, src);
    }

    s_strip_line++;

    if ((s_nes_view_scale == NES_VIEW_SCALE_STRETCH_320X300 && s_strip_line >= STRIP_HEIGHT) ||
        (s_nes_view_scale == NES_VIEW_SCALE_NORMAL && s_strip_line >= STRIP_HEIGHT)) {
        /* Source strip begins at scanline - (STRIP_HEIGHT - 1). */
        int strip_y = scanline - (STRIP_HEIGHT - 1);
#ifdef PICO_BUILD
        uint64_t wait_start_us = time_us_64();
#endif

        /* Wait for the previous asynchronous transfer before reusing the DMA buffer. */
        lcd_dma_wait();
#ifdef PICO_BUILD
        s_perf_lcd_wait_us += time_us_64() - wait_start_us;
        uint64_t flush_start_us = time_us_64();
#endif

        /* Set LCD window for this completed strip in the active viewport. */
        if (s_nes_view_scale == NES_VIEW_SCALE_STRETCH_320X300) {
            const int stretch_y = strip_y + (strip_y / 4);
            lcd_set_window(s_lcd_x0, s_lcd_y0 + stretch_y, NES_VIEW_STRETCH_W, NES_STRETCH_STRIP_HEIGHT);

            /* Send 8 source lines as 10 stretched LCD lines. */
            lcd_dma_write_bytes_async(strip,
                                      NES_VIEW_STRETCH_W * NES_STRETCH_STRIP_HEIGHT * 2);
        } else {
            lcd_set_window(s_lcd_x0, s_lcd_y0 + strip_y, NES_VIEW_NORMAL_W, STRIP_HEIGHT);

            /* Send 8 normal LCD lines. */
            lcd_dma_write_bytes_async(strip,
                                      NES_VIEW_NORMAL_W * STRIP_HEIGHT * 2);
        }
#ifdef PICO_BUILD
        s_perf_lcd_flush_us += time_us_64() - flush_start_us;
#endif
        s_strip_line = 0;
    }

    (void)scanline;
    (void)frommenu;
    (void)s_display_mode;
}

/* =====================================================================
 *  InfoNES_LoadFrame — frame skip calculation
 *  Returns 0 to render all frames (platform can implement FPS control).
 * ===================================================================== */
int InfoNES_LoadFrame(void) {
#ifdef PICO_BUILD
    const uint64_t now_us = time_us_64();
    const uint64_t frame_target_us = 16667u;
    const uint64_t frame_span_us = frame_target_us * (uint64_t)(s_active_frame_skip + 1);
    uint64_t late_us = 0;

    if (s_next_frame_deadline_us == 0) {
        s_next_frame_deadline_us = now_us;
    } else {
        s_next_frame_deadline_us += frame_span_us;
    }

    if (now_us < s_next_frame_deadline_us) {
        sleep_us((uint32_t)(s_next_frame_deadline_us - now_us));
    } else {
        late_us = now_us - s_next_frame_deadline_us;
    }

    if (late_us > 20000u) {
        FrameSkip = 2;
    } else if (late_us > 6000u) {
        FrameSkip = 1;
    } else {
        FrameSkip = 0;
    }
    s_active_frame_skip = FrameSkip;
    s_last_frame_us = time_us_64();
#else
    FrameSkip = 0;
#endif

    return 0;
}

void RomSelect_PreDrawLine(int line) {
    (void)line;
}
