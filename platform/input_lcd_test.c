/*
 * input_lcd_test.c — PicoCalc LCD + keyboard smoke test
 *
 * This is a temporary hardware bring-up tool:
 *   - Uses full-screen 320x320 output
 *   - Reads raw keyboard events from the PicoCalc keyboard controller
 *   - Echoes printable characters to the screen
 *   - Shows the last raw key code and key state for non-printable events
 */

#include "input_lcd_test.h"

#include "display.h"
#include "common_types.h"
#include "version.h"

#include <stdio.h>

#ifdef PICO_BUILD
#  include "pico/stdlib.h"
#endif

extern BYTE i2c_kbd_read_key(void);
extern BYTE i2c_kbd_last_state(void);

extern void lcd_set_window(int x, int y, int w, int h);
extern void lcd_dma_write_rgb565_async(const WORD *buf, int n_pixels);
extern void lcd_dma_wait(void);
extern void lcd_fill_rect_color(int x, int y, int w, int h, WORD color);

enum {
    TEST_FG       = 0xFFFF,
    TEST_BG       = 0x0000,
    TEST_ACCENT   = 0x07E0,
    TEST_STATUS   = 0xFBE0,
    FONT_W        = 5,
    FONT_H        = 7,
    FONT_SCALE    = 2,
    CHAR_ADVANCE  = 12,
    LINE_ADVANCE  = 16,
    TEXT_COLS     = 26,
    TEXT_ROWS     = 14,
};

static char s_text[TEXT_ROWS][TEXT_COLS + 1];
static int s_cursor_row = 0;
static int s_cursor_col = 0;

static void test_log_line(const char *text) {
#ifdef PICO_BUILD
    printf("%s\r\n", text);
#else
    (void)text;
#endif
}

static void test_log_color_step(const char *name, WORD color) {
#ifdef PICO_BUILD
    printf("[LCDTEST] fill %s color=%04X\r\n", name, color);
    fflush(stdout);
#else
    (void)name;
    (void)color;
#endif
}

static void test_fill_rect(int x, int y, int w, int h, WORD color) {
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

static void test_clear_text_buffer(void) {
    for (int row = 0; row < TEXT_ROWS; row++) {
        for (int col = 0; col < TEXT_COLS; col++) {
            s_text[row][col] = ' ';
        }
        s_text[row][TEXT_COLS] = '\0';
    }
    s_cursor_row = 0;
    s_cursor_col = 0;
}

static const BYTE *glyph_for_char(char c) {
    static const BYTE blank[FONT_H] = { 0, 0, 0, 0, 0, 0, 0 };
    static const BYTE unknown[FONT_H] = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };
    static const BYTE g_space[FONT_H] = { 0, 0, 0, 0, 0, 0, 0 };
    static const BYTE g_excl[FONT_H]  = { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 };
    static const BYTE g_minus[FONT_H] = { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
    static const BYTE g_dot[FONT_H]   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06 };
    static const BYTE g_colon[FONT_H] = { 0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00 };
    static const BYTE g_slash[FONT_H] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00 };
    static const BYTE g_lbrk[FONT_H]  = { 0x06, 0x04, 0x04, 0x04, 0x04, 0x04, 0x06 };
    static const BYTE g_rbrk[FONT_H]  = { 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0C };

    static const BYTE g_0[FONT_H] = { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
    static const BYTE g_1[FONT_H] = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
    static const BYTE g_2[FONT_H] = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
    static const BYTE g_3[FONT_H] = { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E };
    static const BYTE g_4[FONT_H] = { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
    static const BYTE g_5[FONT_H] = { 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E };
    static const BYTE g_6[FONT_H] = { 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E };
    static const BYTE g_7[FONT_H] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
    static const BYTE g_8[FONT_H] = { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
    static const BYTE g_9[FONT_H] = { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E };

    static const BYTE g_A[FONT_H] = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
    static const BYTE g_B[FONT_H] = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
    static const BYTE g_C[FONT_H] = { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
    static const BYTE g_D[FONT_H] = { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C };
    static const BYTE g_E[FONT_H] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
    static const BYTE g_F[FONT_H] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
    static const BYTE g_G[FONT_H] = { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E };
    static const BYTE g_H[FONT_H] = { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
    static const BYTE g_I[FONT_H] = { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
    static const BYTE g_J[FONT_H] = { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E };
    static const BYTE g_K[FONT_H] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
    static const BYTE g_L[FONT_H] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
    static const BYTE g_M[FONT_H] = { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
    static const BYTE g_N[FONT_H] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
    static const BYTE g_O[FONT_H] = { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
    static const BYTE g_P[FONT_H] = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
    static const BYTE g_Q[FONT_H] = { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
    static const BYTE g_R[FONT_H] = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
    static const BYTE g_S[FONT_H] = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
    static const BYTE g_T[FONT_H] = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
    static const BYTE g_U[FONT_H] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
    static const BYTE g_V[FONT_H] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
    static const BYTE g_W[FONT_H] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A };
    static const BYTE g_X[FONT_H] = { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
    static const BYTE g_Y[FONT_H] = { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
    static const BYTE g_Z[FONT_H] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };

    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }

    switch (c) {
    case ' ': return g_space;
    case '!': return g_excl;
    case '-': return g_minus;
    case '.': return g_dot;
    case ':': return g_colon;
    case '/': return g_slash;
    case '[': return g_lbrk;
    case ']': return g_rbrk;
    case '0': return g_0;
    case '1': return g_1;
    case '2': return g_2;
    case '3': return g_3;
    case '4': return g_4;
    case '5': return g_5;
    case '6': return g_6;
    case '7': return g_7;
    case '8': return g_8;
    case '9': return g_9;
    case 'A': return g_A;
    case 'B': return g_B;
    case 'C': return g_C;
    case 'D': return g_D;
    case 'E': return g_E;
    case 'F': return g_F;
    case 'G': return g_G;
    case 'H': return g_H;
    case 'I': return g_I;
    case 'J': return g_J;
    case 'K': return g_K;
    case 'L': return g_L;
    case 'M': return g_M;
    case 'N': return g_N;
    case 'O': return g_O;
    case 'P': return g_P;
    case 'Q': return g_Q;
    case 'R': return g_R;
    case 'S': return g_S;
    case 'T': return g_T;
    case 'U': return g_U;
    case 'V': return g_V;
    case 'W': return g_W;
    case 'X': return g_X;
    case 'Y': return g_Y;
    case 'Z': return g_Z;
    case '\0': return blank;
    default: return unknown;
    }
}

static void test_draw_char(int x, int y, char c, WORD fg, WORD bg) {
    const BYTE *glyph = glyph_for_char(c);

    for (int row = 0; row < FONT_H; row++) {
        BYTE bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            WORD color = (bits & (1u << (FONT_W - 1 - col))) ? fg : bg;
            test_fill_rect(x + col * FONT_SCALE,
                           y + row * FONT_SCALE,
                           FONT_SCALE,
                           FONT_SCALE,
                           color);
        }
    }
}

static void test_draw_text(int x, int y, const char *text, WORD fg, WORD bg) {
    while (*text) {
        test_draw_char(x, y, *text, fg, bg);
        x += CHAR_ADVANCE;
        text++;
    }
}

static void test_format_hex2(char *out, BYTE value) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = hex[(value >> 4) & 0x0F];
    out[1] = hex[value & 0x0F];
    out[2] = '\0';
}

static const char *test_state_name(BYTE state) {
    switch (state) {
    case 1: return "PRESSED";
    case 2: return "HOLD";
    case 3: return "RELEASED";
    default: return "IDLE";
    }
}

static void test_render_frame(BYTE last_key, BYTE last_state) {
    char hex[3];

    display_set_mode(DISPLAY_MODE_FULLSCREEN);
    display_clear_rgb565(TEST_BG);
    test_fill_rect(0, 0, 320, 24, TEST_ACCENT);
    test_draw_text(8, 5, PICOCALC_NESCO_BANNER, TEST_BG, TEST_ACCENT);
    test_draw_text(8, 32, "TYPE ON KEYBOARD. ESC=CLEAR", TEST_FG, TEST_BG);
    test_draw_text(8, 48, "KEYS ALSO LOG TO SERIAL.", TEST_FG, TEST_BG);

    test_format_hex2(hex, last_key);
    test_draw_text(8, 72, "LAST KEY:", TEST_STATUS, TEST_BG);
    test_draw_text(112, 72, hex, TEST_STATUS, TEST_BG);
    test_draw_text(160, 72, test_state_name(last_state), TEST_STATUS, TEST_BG);

    test_fill_rect(6, 96, 308, 2, TEST_ACCENT);

    for (int row = 0; row < TEXT_ROWS; row++) {
        test_draw_text(8, 108 + row * LINE_ADVANCE, s_text[row], TEST_FG, TEST_BG);
    }
}

static void test_run_color_sequence(void) {
    static const struct {
        const char *name;
        WORD color;
    } steps[] = {
        { "RED",   0xF800u },
        { "GREEN", 0x07E0u },
        { "BLUE",  0x001Fu },
        { "WHITE", 0xFFFFu },
        { "BLACK", 0x0000u },
    };

    display_set_mode(DISPLAY_MODE_FULLSCREEN);

    for (unsigned int i = 0; i < ARRAY_SIZE(steps); i++) {
        test_log_color_step(steps[i].name, steps[i].color);
        lcd_fill_rect_color(0, 0, 320, 320, steps[i].color);
#ifdef PICO_BUILD
        sleep_ms(700);
#endif
    }
}

static void test_scroll_up(void) {
    for (int row = 1; row < TEXT_ROWS; row++) {
        for (int col = 0; col < TEXT_COLS; col++) {
            s_text[row - 1][col] = s_text[row][col];
        }
    }
    for (int col = 0; col < TEXT_COLS; col++) {
        s_text[TEXT_ROWS - 1][col] = ' ';
    }
    s_cursor_row = TEXT_ROWS - 1;
    s_cursor_col = 0;
}

static void test_newline(void) {
    s_cursor_col = 0;
    s_cursor_row++;
    if (s_cursor_row >= TEXT_ROWS) {
        test_scroll_up();
    }
}

static void test_append_char(char c) {
    if (c == '\n') {
        test_newline();
        return;
    }

    if (s_cursor_col >= TEXT_COLS) {
        test_newline();
    }

    s_text[s_cursor_row][s_cursor_col++] = c;
}

static void test_backspace(void) {
    if (s_cursor_col > 0) {
        s_cursor_col--;
        s_text[s_cursor_row][s_cursor_col] = ' ';
    }
}

static char test_key_to_char(BYTE key) {
    if (key >= 32 && key <= 126) {
        return (char)key;
    }
    if (key == 0x0A) {
        return '\n';
    }
    if (key == 0x08) {
        return '\b';
    }
    return '\0';
}

void picocalc_input_lcd_test(void) {
    BYTE last_key = 0;
    BYTE last_state = 0;
#ifdef PICO_BUILD
    absolute_time_t next_heartbeat = make_timeout_time_ms(1000);
#endif

    test_log_line(PICOCALC_NESCO_BANNER_FULL);
    display_set_mode(DISPLAY_MODE_FULLSCREEN);
    test_clear_text_buffer();

#ifdef PICO_BUILD
    test_run_color_sequence();
    lcd_fill_rect_color(0, 0, 320, 320, TEST_BG);
    sleep_ms(30);
    test_render_frame(last_key, last_state);
    test_render_frame(last_key, last_state);

    for (;;) {
        BYTE key = i2c_kbd_read_key();
        BYTE state = i2c_kbd_last_state();

        if (key != 0) {
            char ch = test_key_to_char(key);
            last_key = key;
            last_state = state;

            if (ch >= 32 && ch <= 126) {
                printf("key=%02X state=%u char=%c\r\n", key, state, ch);
            } else if (ch == '\n') {
                printf("key=%02X state=%u char=LF\r\n", key, state);
            } else if (ch == '\b') {
                printf("key=%02X state=%u char=BS\r\n", key, state);
            } else {
                printf("key=%02X state=%u\r\n", key, state);
            }
            fflush(stdout);

            if (state == 1 || state == 2) {
                if (key == 0xB1) {
                    test_clear_text_buffer();
                } else if (ch == '\b') {
                    test_backspace();
                } else if (ch != '\0') {
                    test_append_char(ch);
                }
            }

            test_render_frame(last_key, last_state);
        }

        if (absolute_time_diff_us(get_absolute_time(), next_heartbeat) <= 0) {
            printf("[TEST] input lcd alive last_key=%02X state=%u\r\n", last_key, last_state);
            fflush(stdout);
            next_heartbeat = make_timeout_time_ms(1000);
        }

        sleep_ms(10);
    }
#else
    test_run_color_sequence();
    test_render_frame(last_key, last_state);
#endif
}
