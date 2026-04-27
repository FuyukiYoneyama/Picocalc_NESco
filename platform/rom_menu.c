/*
 * rom_menu.c — PicoCalc fullscreen ROM selection UI
 *
 * Current scope:
 * - Small-font fullscreen menu
 * - One valid boot entry for built-in flash ROM
 * - Returns selected ROM path to the caller
 */

#include "rom_menu.h"

#include "display.h"
#include "InfoNES_Types.h"
#include "../font/menu_font_pixelmplus.h"
#include "audio.h"
#include "rom_image.h"
#include "screenshot.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PICO_BUILD
#  include "pico/stdlib.h"
#endif

#if !defined(NESCO_RUNTIME_LOGS)
#define printf(...) ((void)0)
#define fflush(...) ((void)0)
#endif

extern BYTE i2c_kbd_read_key(void);
extern BYTE i2c_kbd_last_state(void);
extern int i2c_kbd_read_battery(BYTE *value);

extern void lcd_set_window(int x, int y, int w, int h);
extern void lcd_dma_write_rgb565_async(const WORD *buf, int n_pixels);
extern void lcd_dma_wait(void);

enum {
    MENU_BG       = 0x0000,
    MENU_FG       = 0xFFFF,
    MENU_ACCENT   = 0x07E0,
    MENU_DIM      = 0x7BEF,
    MENU_STATUS   = 0xFBE0,
    MENU_HILITE   = 0x001F,
    FONT_W        = MENU_FONT_PIXELMPLUS_WIDTH,
    FONT_H        = MENU_FONT_PIXELMPLUS_HEIGHT,
    FONT_SCALE    = 1,
    CHAR_ADVANCE  = 6,
    LINE_ADVANCE  = 10,
    KEY_STATE_PRESSED  = 1,
    KEY_STATE_HOLD     = 2,
    KEY_LEFT     = 0xB4,
    KEY_UP       = 0xB5,
    KEY_DOWN     = 0xB6,
    KEY_RIGHT    = 0xB7,
    KEY_ENTER    = 0x0A,
    KEY_MINUS    = '-',
    KEY_B_LOWER  = 'b',
    KEY_B_UPPER  = 'B',
    KEY_H_LOWER  = 'h',
    KEY_H_UPPER  = 'H',
    KEY_QUESTION = '?',
    KEY_ESC      = 0xB1,
    KEY_F5       = 0x85,
    MENU_LIST_Y  = 68,
    MENU_ROW_H   = 24,
    MENU_LIST_MAX_VISIBLE = 8,
};

enum {
    MENU_MAX_ENTRIES = 64,
};

enum {
    MENU_TEXT_PIXEL_COUNT = 320 * FONT_H * 2,
};

enum {
    HELP_PAGE_HELP = 0,
    HELP_PAGE_VERSION = 1,
    HELP_PAGE_LICENSE_1 = 2,
    HELP_PAGE_LICENSE_2 = 3,
    HELP_PAGE_COUNT = 4,
};

static void menu_fill_rect(int x, int y, int w, int h, WORD color) {
    WORD line[320];

    if (w <= 0 || h <= 0 || w > 320) {
        return;
    }

    for (int i = 0; i < w; i++) {
        line[i] = color;
    }

    lcd_set_window(x, y, w, h);
    for (int row = 0; row < h; row++) {
        lcd_dma_write_rgb565_async(line, w);
    }
    lcd_dma_wait();
}

static WORD *s_menu_text_pixels = NULL;

static int menu_text_buffer_begin(void) {
    if (s_menu_text_pixels) {
        return 1;
    }

    s_menu_text_pixels = (WORD *)malloc(MENU_TEXT_PIXEL_COUNT * sizeof(WORD));
    return s_menu_text_pixels != NULL;
}

static void menu_text_buffer_end(void) {
    free(s_menu_text_pixels);
    s_menu_text_pixels = NULL;
}

static const BYTE *menu_glyph(char c) {
    static const BYTE unknown[FONT_H] = {
        0x00, 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00
    };

    if ((unsigned char)c < MENU_FONT_PIXELMPLUS_FIRST ||
        (unsigned char)c > MENU_FONT_PIXELMPLUS_LAST) {
        return unknown;
    }
    return menu_font_pixelmplus[(unsigned char)c - MENU_FONT_PIXELMPLUS_FIRST];
}

static void menu_draw_text_span_scaled(int x,
                                       int y,
                                       int span_w,
                                       const char *text,
                                       WORD fg,
                                       WORD bg,
                                       int font_scale,
                                       int char_advance) {
    WORD *pixels = s_menu_text_pixels;
    int width = span_w;
    int height = FONT_H * font_scale;

    if (!pixels || width <= 0 || width > 320) {
        return;
    }

    for (int i = 0; i < width * height; i++) {
        pixels[i] = bg;
    }

    for (int idx = 0; text[idx] && idx * char_advance + FONT_W * font_scale <= width; idx++) {
        const BYTE *glyph = menu_glyph(text[idx]);
        int base_x = idx * char_advance;

        for (int row = 0; row < FONT_H; row++) {
            BYTE bits = glyph[row];
            for (int col = 0; col < FONT_W; col++) {
                WORD color = (bits & (1u << (FONT_W - 1 - col))) ? fg : bg;
                for (int sy = 0; sy < font_scale; sy++) {
                    for (int sx = 0; sx < font_scale; sx++) {
                        int px = base_x + col * font_scale + sx;
                        int py = row * font_scale + sy;
                        pixels[py * width + px] = color;
                    }
                }
            }
        }
    }

    lcd_set_window(x, y, width, height);
    lcd_dma_write_rgb565_async(pixels, width * height);
    lcd_dma_wait();
}

static void menu_draw_text_span(int x, int y, int span_w, const char *text, WORD fg, WORD bg) {
    menu_draw_text_span_scaled(x, y, span_w, text, fg, bg, FONT_SCALE, CHAR_ADVANCE);
}

static int menu_measure_text_width(const char *text, int char_advance, int glyph_w, int font_scale) {
    size_t len = strlen(text);

    if (len == 0) {
        return 0;
    }

    return (int)((len - 1) * char_advance + glyph_w * font_scale);
}

static void menu_draw_battery_icon(int x, int y, int percent, int charging, WORD fg, WORD bg) {
    int body_w = 12;
    int body_h = 8;
    int cap_w = 2;
    int fill_w;

    menu_fill_rect(x, y, body_w, body_h, fg);
    menu_fill_rect(x + 1, y + 1, body_w - 2, body_h - 2, bg);
    menu_fill_rect(x + body_w, y + 2, cap_w, body_h - 4, fg);

    if (percent < 0) {
        return;
    }

    fill_w = percent * (body_w - 4) / 100;
    if (fill_w < 1 && percent > 0) {
        fill_w = 1;
    }
    if (fill_w > body_w - 4) {
        fill_w = body_w - 4;
    }
    if (fill_w > 0) {
        menu_fill_rect(x + 2, y + 2, fill_w, body_h - 4, fg);
    }

    if (charging) {
        menu_fill_rect(x + 5, y + 1, 2, 6, fg);
        menu_fill_rect(x + 4, y + 3, 4, 2, fg);
    }
}

static void menu_draw_battery_header(WORD fg, WORD bg) {
    BYTE battery = 0;
    char battery_text[8];
    int rc = i2c_kbd_read_battery(&battery);
    int percent = -1;
    int charging = 0;
    int text_w;
    int icon_w = 14;
    int x;

    if (rc != 0) {
        strcpy(battery_text, "--%");
    } else if (battery & 0x80u) {
        percent = (int)(battery & 0x7Fu);
        charging = 1;
        snprintf(battery_text, sizeof(battery_text), "%u%%+",
                 (unsigned)(battery & 0x7Fu));
    } else {
        percent = (int)(battery & 0x7Fu);
        snprintf(battery_text, sizeof(battery_text), "%u%%",
                 (unsigned)(battery & 0x7Fu));
    }

    text_w = menu_measure_text_width(battery_text, CHAR_ADVANCE, FONT_W, FONT_SCALE);
    x = 314 - text_w - icon_w - 4;
    if (x < 200) {
        x = 200;
    }
    menu_draw_battery_icon(x, 6, percent, charging, fg, bg);
    menu_draw_text_span(x + icon_w + 4, 8, 314 - (x + icon_w + 4), battery_text, fg, bg);
}

static void menu_draw_title_bar(void) {
    menu_fill_rect(0, 0, 320, 20, MENU_ACCENT);
    menu_draw_text_span(6, 6, 220, PICOCALC_NESCO_BANNER, MENU_BG, MENU_ACCENT);
    menu_draw_battery_header(MENU_BG, MENU_ACCENT);
}

static const char *menu_default_status(const rom_menu_entry_info_t *entries,
                                       int entry_count,
                                       int selected) {
    if (entry_count <= 0 || selected < 0 || selected >= entry_count) {
        return NULL;
    }

    if (!entries[selected].enabled) {
        return "ENTRY NOT AVAILABLE YET";
    }

    if (entries[selected].kind == ROM_ENTRY_FILE) {
        return "PRESS ENTER/- TO START";
    }

    return "PRESS ENTER/- TO OPEN";
}

static char menu_state_code(BYTE last_state) {
    switch (last_state) {
    case 1:
        return 'P';
    case 2:
        return 'C';
    case 3:
        return 'R';
    default:
        return '-';
    }
}

static void menu_build_debug_code(char debug_code[4], BYTE last_key, BYTE last_state) {
    static const char hex[] = "0123456789ABCDEF";

    debug_code[0] = hex[(last_key >> 4) & 0x0F];
    debug_code[1] = hex[last_key & 0x0F];
    debug_code[2] = menu_state_code(last_state);
    debug_code[3] = '\0';
}

static void menu_draw_index(int entry_count, int selected) {
    char index_text[20];
    int index_w;
    int index_x;

    snprintf(index_text, sizeof(index_text), "%d/%d", selected + 1, entry_count);
    index_w = menu_measure_text_width(index_text, CHAR_ADVANCE, FONT_W, FONT_SCALE);
    index_x = 312 - index_w;
    if (index_x < 220) {
        index_x = 220;
    }
    menu_fill_rect(220, 28, 92, FONT_H, MENU_BG);
    menu_draw_text_span(index_x, 28, 312 - index_x, index_text, MENU_DIM, MENU_BG);
}

static void menu_draw_status_line(const rom_menu_entry_info_t *entries,
                                  int entry_count,
                                  int selected,
                                  const char *status_text) {
    const char *effective_status = (status_text && *status_text)
                                       ? status_text
                                       : menu_default_status(entries, entry_count, selected);

    menu_fill_rect(8, 42, 220, FONT_H, MENU_BG);
    if (effective_status && *effective_status) {
        menu_draw_text_span(8, 42, 220, effective_status, MENU_DIM, MENU_BG);
    }
}

static void menu_draw_debug_code(BYTE last_key, BYTE last_state) {
    char debug_code[4];

    menu_build_debug_code(debug_code, last_key, last_state);
    menu_draw_text_span(290, 304, 18, debug_code, MENU_STATUS, MENU_BG);
}

static int menu_row_visible_index(int index, int first_visible) {
    int row = index - first_visible;

    if (row < 0 || row >= MENU_LIST_MAX_VISIBLE) {
        return -1;
    }
    return row;
}

static void menu_draw_entry_row(const rom_menu_entry_info_t *entries,
                                int entry_count,
                                int index,
                                int first_visible,
                                int selected) {
    int row = menu_row_visible_index(index, first_visible);
    int y;
    WORD row_bg;
    WORD row_fg;

    if (row < 0 || index < 0 || index >= entry_count) {
        return;
    }

    y = MENU_LIST_Y + row * MENU_ROW_H;
    row_bg = (index == selected) ? MENU_HILITE : MENU_BG;
    row_fg = entries[index].enabled ? MENU_FG : MENU_DIM;

    menu_fill_rect(8, y - 2, 304, 25, row_bg);
    if (index == selected) {
        menu_draw_text_span(12, y + 2, 12, ">", MENU_FG, row_bg);
    }
    menu_draw_text_span(24, y + 2, 280, entries[index].label, row_fg, row_bg);
    menu_draw_text_span(24, y + 12, 280, entries[index].detail, MENU_DIM, row_bg);
}

static void menu_update_selection_rows(const rom_menu_entry_info_t *entries,
                                       int entry_count,
                                       int old_selected,
                                       int selected,
                                       int first_visible,
                                       BYTE last_key,
                                       BYTE last_state,
                                       const char *status_text) {
    if (old_selected != selected) {
        menu_draw_entry_row(entries, entry_count, old_selected, first_visible, selected);
        menu_draw_entry_row(entries, entry_count, selected, first_visible, selected);
    }
    menu_draw_index(entry_count, selected);
    menu_draw_status_line(entries, entry_count, selected, status_text);
    menu_draw_debug_code(last_key, last_state);
}

static void menu_render(const rom_menu_entry_info_t *entries,
                        int entry_count,
                        int selected,
                        int first_visible,
                        BYTE last_key,
                        BYTE last_state,
                        const char *status_text) {
    display_set_mode(DISPLAY_MODE_FULLSCREEN);
    menu_fill_rect(0, 0, 320, 320, MENU_BG);
    menu_draw_title_bar();

    menu_draw_text_span(8, 28, 120, "FILE MENU", MENU_DIM, MENU_BG);
    menu_draw_index(entry_count, selected);
    menu_draw_status_line(entries, entry_count, selected, status_text);
    menu_fill_rect(8, 56, 304, 2, MENU_ACCENT);

    for (int row = 0; row < MENU_LIST_MAX_VISIBLE; row++) {
        int i = first_visible + row;

        if (i >= entry_count) {
            break;
        }
        menu_draw_entry_row(entries, entry_count, i, first_visible, selected);
    }

    menu_fill_rect(8, 280, 304, 2, MENU_ACCENT);
    menu_draw_text_span(8, 304, 280, "UP/DOWN MOVE  ENTER/- OPEN  H/? HELP", MENU_DIM, MENU_BG);
    menu_draw_debug_code(last_key, last_state);
}

static void menu_render_help(int help_page, BYTE last_key, BYTE last_state, const char *status_text) {
    char debug_code[4];
    static const char hex[] = "0123456789ABCDEF";
    char page_title[20];
    char version_line[32];

    debug_code[0] = hex[(last_key >> 4) & 0x0F];
    debug_code[1] = hex[last_key & 0x0F];
    debug_code[2] = menu_state_code(last_state);
    debug_code[3] = '\0';

    display_set_mode(DISPLAY_MODE_FULLSCREEN);
    menu_fill_rect(0, 0, 320, 320, MENU_BG);
    menu_draw_title_bar();

    switch (help_page) {
    case HELP_PAGE_VERSION:
        strcpy(page_title, "VERSION");
        break;
    case HELP_PAGE_LICENSE_1:
        strcpy(page_title, "LICENSE 1/2");
        break;
    case HELP_PAGE_LICENSE_2:
        strcpy(page_title, "LICENSE 2/2");
        break;
    case HELP_PAGE_HELP:
    default:
        strcpy(page_title, "HELP");
        break;
    }

    menu_draw_text_span(8, 28, 180, page_title, MENU_DIM, MENU_BG);
    menu_fill_rect(8, 56, 304, 2, MENU_ACCENT);

    if (help_page == HELP_PAGE_HELP) {
        menu_draw_text_span(12, 72, 296, "ROM MENU KEYS", MENU_FG, MENU_BG);
        menu_draw_text_span(12, 92, 296, "UP/DOWN : MOVE CURSOR", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 106, 296, "ENTER/- : OPEN OR START", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 120, 296, "F5 : SAVE SCREENSHOT", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 134, 296, "LEFT/RIGHT : CHANGE PAGE", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 148, 296, "ESC OR H/? : CLOSE HELP", MENU_DIM, MENU_BG);

        menu_draw_text_span(12, 162, 296, "IN GAME", MENU_FG, MENU_BG);
        menu_draw_text_span(12, 182, 296, "ESC : RETURN TO ROM MENU", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 196, 296, "F1 : RESET   F5 : SCREENSHOT", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 210, 296, "` : SELECT   - : START", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 224, 296, "[ : B BUTTON   ] : A BUTTON", MENU_DIM, MENU_BG);
    } else if (help_page == HELP_PAGE_VERSION) {
        snprintf(version_line, sizeof(version_line), "Ver. %s", PICOCALC_NESCO_VERSION);
        menu_draw_text_span_scaled(32, 90, 256, "PicoCalc NESco", MENU_ACCENT, MENU_BG, 2, 10);
        menu_draw_text_span(84, 154, 160, version_line, MENU_FG, MENU_BG);
        menu_draw_text_span(20, 194, 280, "This page mirrors the opening screen.", MENU_DIM, MENU_BG);
        menu_draw_text_span(20, 208, 280, "Opening now shows title + version only.", MENU_DIM, MENU_BG);
        menu_draw_text_span(20, 222, 280, "ROM launch shows Loading... separately.", MENU_DIM, MENU_BG);
    } else if (help_page == HELP_PAGE_LICENSE_1) {
        menu_draw_text_span(12, 72, 296, "COMPONENT LICENSES", MENU_FG, MENU_BG);
        menu_draw_text_span(12, 92, 296, "infones : GNU GPL v2", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 106, 296, "see infones/doc/GPL2", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 126, 296, "fatfs : FatFs open-source license", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 140, 296, "BSD-style / 1-clause BSD equivalent", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 154, 296, "see fatfs/LICENSE.txt", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 174, 296, "font : M+ / PixelMplus derived data", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 188, 296, "free software; use/copy/distribute", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 202, 296, "see font/LICENSE.txt", MENU_DIM, MENU_BG);
    } else {
        menu_draw_text_span(12, 72, 296, "PROJECT LICENSES", MENU_FG, MENU_BG);
        menu_draw_text_span(12, 92, 296, "project root LICENSE : MIT", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 106, 296, "platform / drivers : MIT headers", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 126, 296, "core : source headers say MIT", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 140, 296, "project note: status not unified yet", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 160, 296, "Map30 / mapper work follows current tree.", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 180, 296, "License screen is a summary only.", MENU_DIM, MENU_BG);
        menu_draw_text_span(12, 194, 296, "Check tree files for exact texts.", MENU_DIM, MENU_BG);
    }

    menu_fill_rect(8, 280, 304, 2, MENU_ACCENT);
    if (status_text && *status_text) {
        menu_draw_text_span(8, 304, 270, status_text, MENU_STATUS, MENU_BG);
    } else {
        menu_draw_text_span(8, 304, 270, "LEFT/RIGHT PAGE  ESC/ENTER/H/? CLOSE", MENU_DIM, MENU_BG);
    }
    menu_draw_text_span(290, 304, 18, debug_code, MENU_STATUS, MENU_BG);
}

static int menu_clamp_first_visible(int entry_count, int selected, int first_visible) {
    int max_first = entry_count - MENU_LIST_MAX_VISIBLE;

    if (max_first < 0) {
        max_first = 0;
    }
    if (selected < first_visible) {
        first_visible = selected;
    }
    if (selected >= first_visible + MENU_LIST_MAX_VISIBLE) {
        first_visible = selected - MENU_LIST_MAX_VISIBLE + 1;
    }
    if (first_visible < 0) {
        first_visible = 0;
    }
    if (first_visible > max_first) {
        first_visible = max_first;
    }
    return first_visible;
}

static int menu_keep_visible_or_clamp(int entry_count, int selected, int first_visible) {
    if (selected >= first_visible && selected < first_visible + MENU_LIST_MAX_VISIBLE) {
        return first_visible;
    }
    return menu_clamp_first_visible(entry_count, selected, first_visible);
}

static int menu_page_first_for_index(int index) {
    if (index < 0) {
        return 0;
    }
    return (index / MENU_LIST_MAX_VISIBLE) * MENU_LIST_MAX_VISIBLE;
}

static void menu_discard_pending_keys(void) {
#ifdef PICO_BUILD
    absolute_time_t until = make_timeout_time_ms(120);
    while (absolute_time_diff_us(get_absolute_time(), until) > 0) {
        while (i2c_kbd_read_key() != 0) {
            (void)i2c_kbd_last_state();
        }
        sleep_ms(5);
    }
#endif
}

static void menu_screenshot_start_sound(void) {
    audio_play_ui_tone(1760u, 55u, 42u);
    audio_play_ui_silence(25u);
}

static void menu_screenshot_done_sound(nesco_screenshot_result_t result) {
    if (result == NESCO_SCREENSHOT_OK) {
        audio_play_ui_tone(660u, 80u, 38u);
    } else {
        audio_play_ui_tone(220u, 120u, 46u);
    }
    audio_play_ui_silence(30u);
}

static void menu_log_key(BYTE key, BYTE state) {
    if (key >= 32 && key <= 126) {
        printf("[MENU] key=%02X state=%u char=%c\r\n", key, state, (char)key);
    } else if (key == KEY_ENTER) {
        printf("[MENU] key=%02X state=%u char=LF\r\n", key, state);
    } else {
        printf("[MENU] key=%02X state=%u\r\n", key, state);
    }
    fflush(stdout);
}

const char *picocalc_rom_menu(void) {
    rom_menu_entry_info_t entries[MENU_MAX_ENTRIES];
    char status_buf[64];
    int entry_count = 0;
    int selected = 0;
    int first_visible = 0;
    BYTE last_key = 0;
    BYTE last_state = 0;
    int show_help = 0;
    int help_page = HELP_PAGE_HELP;
    const char *status_text = NULL;

    printf("[MENU] enter\r\n");
    fflush(stdout);
    if (!menu_text_buffer_begin()) {
        printf("[MENU] text buffer alloc failed\r\n");
        fflush(stdout);
#ifdef PICO_BUILD
        display_set_mode(DISPLAY_MODE_FULLSCREEN);
        menu_fill_rect(0, 0, 320, 320, MENU_BG);
        for (;;) {
            sleep_ms(50);
        }
#else
        return NULL;
#endif
    }
    menu_discard_pending_keys();
    if (!rom_image_menu_begin()) {
        rom_image_menu_end();
        entries[0].label = "MENU INIT FAILED";
        entries[0].path = NULL;
        entries[0].detail = rom_image_last_status();
        entries[0].kind = ROM_ENTRY_FILE;
        entries[0].storage = ROM_STORAGE_UNKNOWN;
        entries[0].enabled = 0;
        entry_count = 1;
        status_text = rom_image_last_status();
        menu_render(entries, entry_count, selected, first_visible, last_key, last_state, status_text);
#ifdef PICO_BUILD
        for (;;) {
            sleep_ms(50);
        }
#else
        return NULL;
#endif
    }

    entry_count = rom_image_menu_entries(entries, MENU_MAX_ENTRIES);
    status_text = rom_image_last_status();
    if (entry_count <= 0) {
        entry_count = 1;
        entries[0].label = "NO ROM FILES";
        entries[0].path = NULL;
        entries[0].detail = "NO *.NES IN CURRENT DIRECTORY";
        entries[0].kind = ROM_ENTRY_FILE;
        entries[0].storage = ROM_STORAGE_UNKNOWN;
        entries[0].enabled = 0;
        status_text = rom_image_last_status();
    }
    first_visible = menu_clamp_first_visible(entry_count, selected, first_visible);
    menu_render(entries, entry_count, selected, first_visible, last_key, last_state, status_text);

#ifdef PICO_BUILD
    for (;;) {
        BYTE key = i2c_kbd_read_key();
        BYTE state = i2c_kbd_last_state();

        if (key == 0) {
            sleep_ms(10);
            continue;
        }

        last_key = key;
        last_state = state;
        menu_log_key(key, state);

        if (state == KEY_STATE_PRESSED) {
            if (key == KEY_F5) {
                nesco_screenshot_result_t ss_result;

                menu_discard_pending_keys();
                menu_screenshot_start_sound();
                sleep_ms(80);
                audio_start_ui_busy_indicator();
                ss_result = nesco_take_screenshot_now_with_stem("NESCO");
                audio_stop_ui_busy_indicator();
                menu_screenshot_done_sound(ss_result);
                menu_discard_pending_keys();
                snprintf(status_buf,
                         sizeof(status_buf),
                         "%s",
                         (ss_result == NESCO_SCREENSHOT_OK) ? "SCREENSHOT SAVED" : "SCREENSHOT FAILED");
                status_text = status_buf;
            } else if (key == KEY_H_LOWER || key == KEY_H_UPPER || key == KEY_QUESTION) {
                show_help = !show_help;
                status_text = NULL;
                if (show_help) {
                    help_page = HELP_PAGE_HELP;
                }
            } else if (key == KEY_B_LOWER || key == KEY_B_UPPER) {
                BYTE battery = 0;
                int rc = i2c_kbd_read_battery(&battery);
                if (rc == 0) {
                    snprintf(status_buf,
                             sizeof(status_buf),
                             "BAT %u%% %s",
                             (unsigned)(battery & 0x7Fu),
                             (battery & 0x80u) ? "CHG" : "IDLE");
                    status_text = status_buf;
                    printf("[MENU] battery raw=0x%02X percent=%u charging=%u\r\n",
                           battery,
                           (unsigned)(battery & 0x7Fu),
                           (unsigned)((battery & 0x80u) ? 1u : 0u));
                } else {
                    snprintf(status_buf, sizeof(status_buf), "BAT READ FAILED");
                    status_text = status_buf;
                    printf("[MENU] battery read failed rc=%d\r\n", rc);
                }
                fflush(stdout);
            } else if (show_help) {
                if (key == KEY_ESC || key == KEY_ENTER || key == KEY_MINUS) {
                    show_help = 0;
                    status_text = NULL;
                } else if (key == KEY_LEFT) {
                    help_page--;
                    if (help_page < 0) {
                        help_page = HELP_PAGE_COUNT - 1;
                    }
                    status_text = NULL;
                } else if (key == KEY_RIGHT) {
                    help_page++;
                    if (help_page >= HELP_PAGE_COUNT) {
                        help_page = 0;
                    }
                    status_text = NULL;
                }
            } else if (key == KEY_UP) {
                int old_selected = selected;
                int old_first_visible = first_visible;
                int old_row = selected - first_visible;
                int page_scroll_up = (old_row == 0 && selected > 0);
                int wrap_up = 0;

                selected--;
                if (selected < 0) {
                    selected = entry_count - 1;
                    first_visible = menu_page_first_for_index(selected);
                    wrap_up = 1;
                } else if (page_scroll_up) {
                    first_visible = menu_page_first_for_index(selected);
                } else {
                    first_visible = menu_keep_visible_or_clamp(entry_count, selected, first_visible);
                }
                status_text = menu_default_status(entries, entry_count, selected);
                if (page_scroll_up || wrap_up) {
                    menu_render(entries, entry_count, selected, first_visible, last_key, last_state, status_text);
                    continue;
                }
                if (first_visible == old_first_visible) {
                    menu_update_selection_rows(entries,
                                               entry_count,
                                               old_selected,
                                               selected,
                                               first_visible,
                                               last_key,
                                               last_state,
                                               status_text);
                    continue;
                }
            } else if (key == KEY_DOWN) {
                int old_selected = selected;
                int old_first_visible = first_visible;
                int old_row = selected - first_visible;
                int old_visible_count = entry_count - first_visible;
                int page_scroll_down;
                int wrap_down = 0;

                if (old_visible_count > MENU_LIST_MAX_VISIBLE) {
                    old_visible_count = MENU_LIST_MAX_VISIBLE;
                }
                page_scroll_down = (old_row == old_visible_count - 1 && selected + 1 < entry_count);

                selected++;
                if (selected >= entry_count) {
                    selected = 0;
                    first_visible = 0;
                    wrap_down = 1;
                } else if (page_scroll_down) {
                    first_visible = menu_page_first_for_index(selected);
                } else {
                    first_visible = menu_keep_visible_or_clamp(entry_count, selected, first_visible);
                }
                status_text = menu_default_status(entries, entry_count, selected);
                if (page_scroll_down || wrap_down) {
                    menu_render(entries, entry_count, selected, first_visible, last_key, last_state, status_text);
                    continue;
                }
                if (first_visible == old_first_visible) {
                    menu_update_selection_rows(entries,
                                               entry_count,
                                               old_selected,
                                               selected,
                                               first_visible,
                                               last_key,
                                               last_state,
                                               status_text);
                    continue;
                }
            } else if (key == KEY_ENTER || key == KEY_MINUS) {
                if (entries[selected].enabled) {
                    if (entries[selected].kind == ROM_ENTRY_DIRECTORY ||
                        entries[selected].kind == ROM_ENTRY_PARENT) {
                        if (rom_image_enter_directory(entries[selected].path)) {
                            entry_count = rom_image_menu_entries(entries, MENU_MAX_ENTRIES);
                            selected = 0;
                            first_visible = 0;
                            status_text = rom_image_last_status();
                            if (entry_count <= 0) {
                                entry_count = 1;
                                entries[0].label = "NO ROM FILES";
                                entries[0].path = NULL;
                                entries[0].detail = "NO *.NES IN THIS DIRECTORY";
                                entries[0].kind = ROM_ENTRY_FILE;
                                entries[0].storage = ROM_STORAGE_UNKNOWN;
                                entries[0].enabled = 0;
                            }
                        } else {
                            status_text = "DIRECTORY CHANGE FAILED";
                        }
                    } else {
                        printf("[MENU] launch %s\r\n", entries[selected].path);
                        fflush(stdout);
                        rom_image_set_selected_path(entries[selected].path);
                        {
                            const char *launch_path = rom_image_get_selected_path();
                            rom_image_menu_end();
                            menu_text_buffer_end();
                            return launch_path;
                        }
                    }
                    first_visible = menu_clamp_first_visible(entry_count, selected, first_visible);
                    menu_render(entries, entry_count, selected, first_visible, last_key, last_state, status_text);
                    continue;
                }
                printf("[MENU] unavailable %s\r\n", entries[selected].path ? entries[selected].path : "(null)");
                fflush(stdout);
                status_text = "SELECTABLE *.NES FILE NOT AVAILABLE";
            }
        } else {
            if (!show_help) {
                menu_draw_debug_code(last_key, last_state);
            }
            continue;
        }

        first_visible = menu_clamp_first_visible(entry_count, selected, first_visible);
        if (show_help) {
            menu_render_help(help_page, last_key, last_state, status_text);
        } else {
            menu_render(entries, entry_count, selected, first_visible, last_key, last_state, status_text);
        }
    }
#else
    rom_image_set_selected_path(entries[selected].path);
    {
        const char *launch_path = rom_image_get_selected_path();
        rom_image_menu_end();
        menu_text_buffer_end();
        return launch_path;
    }
#endif
}
