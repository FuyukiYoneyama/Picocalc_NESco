/*
 * display.h — LCD display pipeline interface (PicoCalc)
 *
 * This is the platform-side owner of the LCD drawing policy.
 *
 * The low-level driver in drivers/lcd_spi.c only knows how to select a
 * rectangular LCD address window and push RGB565 bytes to it. This layer
 * decides which rectangle is active, how NES scanlines are converted to
 * LCD pixels, and when fullscreen UI code may take direct ownership of the
 * panel.
 *
 * InfoNES integration:
 *   - InfoNES_PreDrawLine() gives the PPU a 256-pixel RGB565 line buffer.
 *   - InfoNES_PostDrawLine() is called after the PPU has filled that line.
 *   - Lines are batched into 8-source-line strips before being sent to LCD.
 *   - In normal view, 8 NES lines become 8 LCD lines at 256 pixels wide.
 *   - In stretch view, 8 NES lines become 10 LCD lines at 320 pixels wide.
 *
 * LCD ownership rule:
 *   - DISPLAY_MODE_NES_VIEW is for game rendering through this pipeline.
 *   - DISPLAY_MODE_FULLSCREEN is for menus, help, screenshot viewer, etc.
 *   - Before fullscreen UI draws, display_lcd_worker_stop_and_drain() must
 *     ensure the core1 LCD worker and the DMA engine are idle.
 *
 * LCD: 320×320 RGB565
 * NES normal view: 256×240 pixels at (32,24)
 * NES stretch view: 320×300 pixels at (0,10)
 *
 * Part of Picocalc_NESco
 * MIT License
 */
#pragma once
#include "InfoNES_Types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NES palette lookup source data (64 entries) */
extern const WORD NesPalette[64];

/*
 * 8 source NES lines are the batching unit.
 *
 * The LCD driver exposes a staging buffer large enough for either:
 *   - 8 lines x 256 pixels x 2 bytes in normal view, or
 *   - 10 lines x 320 pixels x 2 bytes in stretch view
 *     because every fourth NES line is repeated.
 */
#define STRIP_HEIGHT  8

typedef enum {
    DISPLAY_MODE_FULLSCREEN = 0,
    DISPLAY_MODE_NES_VIEW   = 1,
} display_mode_t;

typedef enum {
    NES_VIEW_SCALE_NORMAL = 0,
    NES_VIEW_SCALE_STRETCH_320X300 = 1,
} nes_view_scale_mode_t;

typedef enum {
    DISPLAY_LCD_WORKER_STOPPED = 0,
    DISPLAY_LCD_WORKER_RUNNING = 1,
    DISPLAY_LCD_WORKER_DRAINING = 2,
} display_lcd_worker_state_t;

/**
 * display_init() — Initialise LCD hardware and precompute LUT.
 * Called once from main() before emulation starts.
 */
void display_init(void);

/**
 * display_set_viewport(x, y, w, h) — Set the LCD blit rectangle for NES output.
 */
void display_set_viewport(int x, int y, int w, int h);
void display_get_viewport(int *x, int *y, int *w, int *h);

/**
 * display_set_mode(mode) — Switch between full-screen UI mode and NES viewport mode.
 */
void display_set_mode(display_mode_t mode);
void display_toggle_nes_view_scale(void);
nes_view_scale_mode_t display_get_nes_view_scale(void);
display_lcd_worker_state_t display_lcd_worker_get_state(void);
void display_lcd_worker_prepare_nes_view(void);
void display_lcd_worker_stop_and_drain(void);
bool display_lcd_worker_is_running(void);
bool display_lcd_worker_poll_once(void);

/**
 * display_clear_rgb565(color) — Fill the current active display mode region.
 */
void display_clear_rgb565(WORD color);

/**
 * display_show_opening_screen() — Show a simple opening screen before the ROM menu.
 */
void display_show_opening_screen(void);

/**
 * display_show_loading_screen() — Show a simple loading screen while a ROM loads.
 */
void display_show_loading_screen(void);

/**
 * display_perf_reset() — Reset LCD-side timing accumulators.
 */
void display_perf_reset(void);
void display_reset_frame_pacing(void);

/**
 * display_perf_snapshot(wait_us, flush_us, queue_wait_us, queue_wait_count,
 * frame_pacing_sleep_us, frame_pacing_sleep_count) — Read current LCD-side timing
 * accumulators without resetting them.
 */
void display_perf_snapshot(uint64_t *wait_us,
                           uint64_t *flush_us,
                           uint64_t *queue_wait_us,
                           uint32_t *queue_wait_count,
                           uint64_t *frame_pacing_sleep_us,
                           uint32_t *frame_pacing_sleep_count);

#ifdef __cplusplus
}
#endif
