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
 *   - InfoNES_PreDrawLine() gives the PPU a 256-pixel palette-index line buffer.
 *   - InfoNES_PostDrawLine() is called after the PPU has filled that line.
 *   - Lines are batched into STRIP_HEIGHT source-line strips before being sent
 *     to the LCD. The standard burst is 8 lines; the experimental arbitration
 *     candidate uses 4 lines.
 *   - In normal view, STRIP_HEIGHT NES lines become STRIP_HEIGHT LCD lines at
 *     256 pixels wide.
 *   - In stretch view, STRIP_HEIGHT NES lines become the corresponding scaled
 *     number of LCD lines at 320 pixels wide.
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

/*
 * Source NES lines are the batching unit.
 *
 * The LCD driver exposes a staging buffer large enough for either:
 *   - STRIP_HEIGHT lines x 256 pixels x 2 bytes in normal view, or
 *   - the scaled STRIP_HEIGHT line count x 320 pixels x 2 bytes in stretch view
 *     because every fourth NES line is repeated.
 */
#ifdef NESCO_LCD_DMA_STRIP4
#define STRIP_HEIGHT  4
#else
#define STRIP_HEIGHT  8
#endif

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
void display_toggle_stretch_frame_policy(void);
nes_view_scale_mode_t display_get_nes_view_scale(void);
display_lcd_worker_state_t display_lcd_worker_get_state(void);
void display_lcd_worker_prepare_nes_view(void);
void display_lcd_worker_stop_and_drain(void);
bool display_lcd_worker_is_running(void);
bool display_lcd_worker_poll_once(void);
void display_lcd_worker_palette_mark_dirty(void);
void display_lcd_worker_palette_force_snapshot(void);

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

typedef struct {
    uint64_t lcd_wait_us;
    uint64_t lcd_flush_us;
    uint64_t lcd_queue_wait_us;
    uint32_t lcd_queue_wait_count;
    uint32_t lcd_queue_wait_episodes;
    uint64_t frame_pacing_sleep_us;
    uint32_t frame_pacing_sleep_count;
    uint32_t palette_line_items;
    uint32_t palette_snapshots;
    uint32_t palette_forced;
    uint32_t palette_applied;
    uint32_t palette_protocol_faults;
    uint32_t lcd_empty_polls;
    uint16_t palette_version;
    uint64_t lcd_dma_wait_us;
    uint32_t lcd_dma_wait_count;
    uint64_t lcd_window_set_us;
    uint32_t lcd_window_set_count;
} display_perf_window_t;

/**
 * display_perf_take_window(window) — Copy and reset one logging window.
 * Core1-published fields are taken under the LCD worker queue lock; core0-only
 * fields need no lock.
 */
void display_perf_take_window(display_perf_window_t *window);

#ifdef __cplusplus
}
#endif
