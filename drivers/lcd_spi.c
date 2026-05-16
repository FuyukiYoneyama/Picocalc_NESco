/*
 * lcd_spi.c — PicoCalc LCD panel driver
 *
 * Hardware view:
 *   - The LCD panel is driven like an SPI display.
 *   - PIO generates the SCK/MOSI write stream.
 *   - DMA feeds bytes into the PIO TX FIFO for large pixel transfers.
 *   - A slower bit-bang path exists only for LCD readback / diagnostics.
 *
 * Public drawing contract:
 *   1. Call lcd_set_window(x, y, w, h).
 *      This sends column address set (0x2A), row address set (0x2B),
 *      then memory write (0x2C). After that, pixel bytes stream into the
 *      selected rectangle from left-to-right, top-to-bottom.
 *   2. Call lcd_dma_write_rgb565_async() or lcd_dma_write_bytes_async().
 *      These start an asynchronous DMA transfer.
 *   3. Call lcd_dma_wait() before changing the LCD window, reusing a buffer,
 *      switching to readback, or letting another subsystem draw.
 *
 * Byte order:
 *   Pixel data is RGB565, high byte first, low byte second.
 */

#include "common_types.h"
#include "display.h"
#include "runtime_log.h"

#ifdef PICO_BUILD
#  include "hardware/dma.h"
#  include "hardware/gpio.h"
#  include "hardware/pio.h"
#  include "pico/stdlib.h"
#  include <stdio.h>
#  include <string.h>
#  include "lcd_spi.pio.h"
#endif

enum {
    LCD_PIN_SCK    = 10,
    LCD_PIN_MOSI   = 11,
    LCD_PIN_MISO   = 12,
    LCD_PIN_CS     = 13,
    LCD_PIN_DC     = 14,
    LCD_PIN_RST    = 15,
    LCD_PIN_RAM_CS = 21,
    LCD_WIDTH      = 320,
    LCD_HEIGHT     = 320,
    LCD_DMA_MAX_WIDTH = 320,
    LCD_DMA_MAX_LINES = STRIP_HEIGHT + (STRIP_HEIGHT / 4),
    LCD_DMA_MAX_PIXELS = LCD_DMA_MAX_WIDTH * LCD_DMA_MAX_LINES,
    LCD_READ_DEFAULT_HALF_CYCLE_US = 1,
    LCD_READ_DUMMY_BYTES = 1,
};

typedef struct lcd_read_mode_t {
    int dummy_bytes;
    int bytes_per_pixel;
    bool swap_bytes;
} lcd_read_mode_t;

typedef enum lcd_bus_seq_t {
    LCD_BUS_SEQ_SPLIT_CS = 0,
    LCD_BUS_SEQ_HOLD_CS = 1,
} lcd_bus_seq_t;

typedef enum lcd_read_edge_t {
    LCD_READ_EDGE_RISING = 0,
    LCD_READ_EDGE_FALLING = 1,
} lcd_read_edge_t;

/*
 * The two DMA buffers are driver-owned staging memory.
 *
 * Callers may either provide WORD pixels and let lcd_dma_write_rgb565_async()
 * convert to high-byte/low-byte order, or ask for lcd_dma_acquire_buffer()
 * and fill a byte buffer themselves. In both cases the buffer selected for
 * DMA must not be overwritten until lcd_dma_wait() completes.
 */
#ifdef PICO_BUILD
static const float LCD_PIO_CLKDIV = 2.0f; /* 250 MHz sysclk -> 62.5 MHz SPI-equivalent */
static BYTE s_lcd_dma_buf[2][LCD_DMA_MAX_PIXELS * 2];
#endif

static int s_window_pixels = 0;
static int s_lcd_dma_buf_idx = 0;
static lcd_read_mode_t s_lcd_read_mode = { 1, 2, false };
static bool s_lcd_read_mode_valid = true;
static int s_lcd_read_half_cycle_us = LCD_READ_DEFAULT_HALF_CYCLE_US;
static lcd_bus_seq_t s_lcd_read_seq = LCD_BUS_SEQ_HOLD_CS;
static lcd_read_edge_t s_lcd_read_edge = LCD_READ_EDGE_FALLING;
static int s_lcd_read_bit_shift = 0;

void lcd_set_window(int x, int y, int w, int h);
void lcd_dma_write_rgb565_async(const WORD *buf, int n_pixels);
BYTE *lcd_dma_acquire_buffer(void);
void lcd_dma_write_bytes_async(const BYTE *buf, int n_bytes);
void lcd_dma_wait(void);
bool lcd_readback_rect_rgb565(int x, int y, int w, int h, WORD *dst_pixels);
void lcd_log_readback_diagnostics(void);
void lcd_fill_rect_color(int x, int y, int w, int h, WORD color);

#ifdef PICO_BUILD
static PIO s_lcd_pio = pio0;
static uint s_lcd_sm = 0;
static uint s_lcd_pio_offset = 0;
static bool s_lcd_pio_ready = false;
static int s_lcd_dma_chan = -1;
static bool s_lcd_dma_active = false;
static dma_channel_config s_lcd_dma_cfg;
static bool s_lcd_dma_cfg_ready = false;

static void lcd_read_io_delay(void) {
    busy_wait_us_32((uint32_t)s_lcd_read_half_cycle_us);
}

static void lcd_select(void) {
    gpio_put(LCD_PIN_CS, 0);
}

static void lcd_deselect(void) {
    gpio_put(LCD_PIN_CS, 1);
}

static void lcd_set_dc(int is_data) {
    gpio_put(LCD_PIN_DC, is_data ? 1 : 0);
}

static void lcd_wait_idle(void) {
    if (!s_lcd_pio_ready) {
        return;
    }
    lcd_spi_min_wait_idle(s_lcd_pio, s_lcd_sm);
}

void lcd_dma_wait(void) {
    /*
     * DMA completion is not enough by itself: wait until PIO has shifted the
     * final byte out, then release CS. This makes the next command/window
     * change safe.
     */
    if (!s_lcd_dma_active) {
        return;
    }

    dma_channel_wait_for_finish_blocking((uint)s_lcd_dma_chan);
    lcd_wait_idle();
    lcd_deselect();
    s_lcd_dma_active = false;
}

static void lcd_write_bytes(const BYTE *buf, size_t len) {
    if (!s_lcd_pio_ready) {
        return;
    }

    while (len > 0) {
        lcd_spi_min_put(s_lcd_pio, s_lcd_sm, *buf);
        buf++;
        len--;
    }
}

static void lcd_write_command(BYTE cmd) {
    /* Commands are serialized with any pending pixel DMA. */
    lcd_dma_wait();
    lcd_select();
    lcd_set_dc(0);
    lcd_write_bytes(&cmd, 1);
    lcd_wait_idle();
    lcd_deselect();
}

static void lcd_write_data(const BYTE *buf, size_t len) {
    if (len == 0) {
        return;
    }

    /* Small setup payloads are written synchronously through PIO. */
    lcd_dma_wait();
    lcd_select();
    lcd_set_dc(1);
    lcd_write_bytes(buf, len);
    lcd_wait_idle();
    lcd_deselect();
}

static void lcd_write_command1(BYTE cmd, BYTE data0) {
    lcd_write_command(cmd);
    lcd_write_data(&data0, 1);
}

static void lcd_write_commandn(BYTE cmd, const BYTE *data, size_t len) {
    lcd_write_command(cmd);
    lcd_write_data(data, len);
}

static void lcd_reset_panel(void) {
    gpio_put(LCD_PIN_RST, 1);
    sleep_ms(1);
    gpio_put(LCD_PIN_RST, 0);
    sleep_ms(10);
    gpio_put(LCD_PIN_RST, 1);
    sleep_ms(10);
}

static void lcd_fill_black(void) {
    lcd_fill_rect_color(0, 0, LCD_WIDTH, LCD_HEIGHT, 0x0000u);
}

static void lcd_set_bitbang_mode(bool enabled) {
    /*
     * LCD readback cannot use the write-only PIO program. Temporarily stop
     * the PIO state machine and switch the pins to SIO bit-bang mode.
     * Normal display writes switch back to PIO mode.
     */
    if (!s_lcd_pio_ready) {
        return;
    }

    if (enabled) {
        pio_sm_set_enabled(s_lcd_pio, s_lcd_sm, false);
        gpio_set_function(LCD_PIN_SCK, GPIO_FUNC_SIO);
        gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SIO);
        gpio_set_function(LCD_PIN_MISO, GPIO_FUNC_SIO);
        gpio_set_dir(LCD_PIN_SCK, GPIO_OUT);
        gpio_set_dir(LCD_PIN_MOSI, GPIO_OUT);
        gpio_set_dir(LCD_PIN_MISO, GPIO_IN);
        gpio_disable_pulls(LCD_PIN_MISO);
        gpio_put(LCD_PIN_SCK, 0);
        gpio_put(LCD_PIN_MOSI, 0);
        return;
    }

    gpio_set_function(LCD_PIN_SCK, GPIO_FUNC_PIO0);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_PIO0);
    gpio_set_function(LCD_PIN_MISO, GPIO_FUNC_SIO);
    gpio_set_dir(LCD_PIN_MISO, GPIO_IN);
    gpio_disable_pulls(LCD_PIN_MISO);
    pio_sm_set_enabled(s_lcd_pio, s_lcd_sm, true);
}

static void lcd_bitbang_write_byte(BYTE value) {
    for (int bit = 7; bit >= 0; --bit) {
        gpio_put(LCD_PIN_SCK, 0);
        gpio_put(LCD_PIN_MOSI, (value >> bit) & 1u);
        lcd_read_io_delay();
        gpio_put(LCD_PIN_SCK, 1);
        lcd_read_io_delay();
    }
    gpio_put(LCD_PIN_SCK, 0);
}

static BYTE lcd_bitbang_read_byte_edge(lcd_read_edge_t edge) {
    BYTE value = 0;

    for (int bit = 7; bit >= 0; --bit) {
        gpio_put(LCD_PIN_SCK, 0);
        lcd_read_io_delay();
        if (edge == LCD_READ_EDGE_FALLING) {
            if (gpio_get(LCD_PIN_MISO)) {
                value |= (BYTE)(1u << bit);
            }
            gpio_put(LCD_PIN_SCK, 1);
            lcd_read_io_delay();
        } else {
            gpio_put(LCD_PIN_SCK, 1);
            lcd_read_io_delay();
            if (gpio_get(LCD_PIN_MISO)) {
                value |= (BYTE)(1u << bit);
            }
        }
    }
    gpio_put(LCD_PIN_SCK, 0);
    return value;
}

static BYTE lcd_bitbang_read_byte(void) {
    return lcd_bitbang_read_byte_edge(s_lcd_read_edge);
}

static void lcd_bitbang_write_command(BYTE cmd) {
    lcd_select();
    lcd_set_dc(0);
    lcd_bitbang_write_byte(cmd);
    lcd_deselect();
}

static void lcd_bitbang_write_data(const BYTE *buf, size_t len) {
    if (!buf || len == 0u) {
        return;
    }

    lcd_select();
    lcd_set_dc(1);
    while (len-- > 0u) {
        lcd_bitbang_write_byte(*buf++);
    }
    lcd_deselect();
}

static void lcd_bitbang_write_commandn(BYTE cmd, const BYTE *data, size_t len) {
    lcd_bitbang_write_command(cmd);
    lcd_bitbang_write_data(data, len);
}

static void lcd_bitbang_write_commandn_held(BYTE cmd, const BYTE *data, size_t len) {
    lcd_set_dc(0);
    lcd_bitbang_write_byte(cmd);
    if (data && len > 0u) {
        lcd_set_dc(1);
        while (len-- > 0u) {
            lcd_bitbang_write_byte(*data++);
        }
    }
}

static void lcd_set_read_address_window(int x, int y, int w, int h) {
    BYTE col[4];
    BYTE row[4];
    int x0 = x;
    int y0 = y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
    if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;

    col[0] = (BYTE)((x0 >> 8) & 0xFF);
    col[1] = (BYTE)(x0 & 0xFF);
    col[2] = (BYTE)((x1 >> 8) & 0xFF);
    col[3] = (BYTE)(x1 & 0xFF);
    row[0] = (BYTE)((y0 >> 8) & 0xFF);
    row[1] = (BYTE)(y0 & 0xFF);
    row[2] = (BYTE)((y1 >> 8) & 0xFF);
    row[3] = (BYTE)(y1 & 0xFF);

    lcd_bitbang_write_commandn(0x2Au, col, ARRAY_SIZE(col));
    lcd_bitbang_write_commandn(0x2Bu, row, ARRAY_SIZE(row));
}

static void lcd_set_read_address_window_held(int x, int y, int w, int h) {
    BYTE col[4];
    BYTE row[4];
    int x0 = x;
    int y0 = y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
    if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;

    col[0] = (BYTE)((x0 >> 8) & 0xFF);
    col[1] = (BYTE)(x0 & 0xFF);
    col[2] = (BYTE)((x1 >> 8) & 0xFF);
    col[3] = (BYTE)(x1 & 0xFF);
    row[0] = (BYTE)((y0 >> 8) & 0xFF);
    row[1] = (BYTE)(y0 & 0xFF);
    row[2] = (BYTE)((y1 >> 8) & 0xFF);
    row[3] = (BYTE)(y1 & 0xFF);

    lcd_bitbang_write_commandn_held(0x2Au, col, ARRAY_SIZE(col));
    lcd_bitbang_write_commandn_held(0x2Bu, row, ARRAY_SIZE(row));
}

static __attribute__((unused)) void lcd_bitbang_read_command_bytes(BYTE cmd, int dummy_bytes, BYTE *dst, int n_bytes) {
    int i;

    if (!dst || n_bytes <= 0) {
        return;
    }

    lcd_select();
    lcd_set_dc(0);
    lcd_bitbang_write_byte(cmd);
    lcd_set_dc(1);

    for (i = 0; i < dummy_bytes; ++i) {
        (void)lcd_bitbang_read_byte();
    }
    for (i = 0; i < n_bytes; ++i) {
        dst[i] = lcd_bitbang_read_byte();
    }

    lcd_deselect();
}

static __attribute__((unused)) void lcd_bitbang_log_ramrd_probe(int x, int y, int dummy_bytes, int bytes_per_pixel) {
    BYTE raw[16] = { 0 };
    int total_bytes = bytes_per_pixel * 4;
    int i;

    if (total_bytes > (int)sizeof(raw)) {
        total_bytes = (int)sizeof(raw);
    }

    lcd_set_read_address_window(x, y, 2, 2);
    lcd_select();
    lcd_set_dc(0);
    lcd_bitbang_write_byte(0x2Eu);
    lcd_set_dc(1);

    for (i = 0; i < dummy_bytes; ++i) {
        (void)lcd_bitbang_read_byte();
    }
    for (i = 0; i < total_bytes; ++i) {
        raw[i] = lcd_bitbang_read_byte();
    }

    lcd_deselect();
    NESCO_LOGF("[LCDRD] probe x=%d y=%d dummy=%d bpp=%d raw=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
               x, y, dummy_bytes, bytes_per_pixel,
               raw[0], raw[1], raw[2], raw[3], raw[4], raw[5],
               raw[6], raw[7], raw[8], raw[9], raw[10], raw[11]);
}

static WORD lcd_decode_read_pixel(const BYTE *raw, const lcd_read_mode_t *mode) {
    if (!raw || !mode) {
        return 0u;
    }

    if (mode->bytes_per_pixel == 2) {
        BYTE hi = mode->swap_bytes ? raw[1] : raw[0];
        BYTE lo = mode->swap_bytes ? raw[0] : raw[1];
        return (WORD)(((WORD)hi << 8) | (WORD)lo);
    }

    if (mode->bytes_per_pixel == 3) {
        BYTE red = raw[0];
        BYTE green = raw[1];
        BYTE blue = raw[2];
        return (WORD)(((red & 0xF8u) << 8)
                    | ((green & 0xFCu) << 3)
                    | ((blue & 0xF8u) >> 3));
    }

    return 0u;
}

static BYTE lcd_extract_shifted_byte(const BYTE *src, int src_len, int bit_shift, int byte_index) {
    int bit_pos;
    int src_byte;
    int next_byte;
    BYTE cur;
    BYTE nxt;

    if (!src || src_len <= 0 || bit_shift < 0 || bit_shift > 7 || byte_index < 0) {
        return 0u;
    }

    bit_pos = bit_shift + (byte_index * 8);
    src_byte = bit_pos / 8;
    next_byte = src_byte + 1;
    cur = (src_byte < src_len) ? src[src_byte] : 0u;
    nxt = (next_byte < src_len) ? src[next_byte] : 0u;

    if ((bit_pos & 7) == 0) {
        return cur;
    }

    return (BYTE)((BYTE)(cur << (bit_pos & 7)) | (BYTE)(nxt >> (8 - (bit_pos & 7))));
}

static WORD lcd_decode_shifted_pixel(const BYTE *src,
                                     int src_len,
                                     int pixel_index,
                                     const lcd_read_mode_t *mode,
                                     int bit_shift) {
    BYTE shifted[3] = { 0, 0, 0 };
    int i;

    if (!src || !mode || pixel_index < 0) {
        return 0u;
    }

    for (i = 0; i < mode->bytes_per_pixel; ++i) {
        shifted[i] = lcd_extract_shifted_byte(src,
                                              src_len,
                                              bit_shift,
                                              pixel_index * mode->bytes_per_pixel + i);
    }

    return lcd_decode_read_pixel(shifted, mode);
}

static bool lcd_readback_rect_mode(int x,
                                   int y,
                                   int w,
                                   int h,
                                   const lcd_read_mode_t *mode,
                                   lcd_bus_seq_t seq,
                                   lcd_read_edge_t edge,
                                   int bit_shift,
                                   WORD *dst_pixels,
                                   BYTE *raw_sample,
                                   int raw_sample_size) {
    int pixel_count = w * h;
    int sample_bytes = 0;
    int capture_bytes = 0;
    int i;

    if (!mode || !dst_pixels || w <= 0 || h <= 0 || bit_shift < 0 || bit_shift > 7) {
        return false;
    }

    if (raw_sample && raw_sample_size > 0) {
        memset(raw_sample, 0, (size_t)raw_sample_size);
    }

    if (seq == LCD_BUS_SEQ_HOLD_CS) {
        lcd_select();
        lcd_set_read_address_window_held(x, y, w, h);
        lcd_set_dc(0);
        lcd_bitbang_write_byte(0x2Eu);
        lcd_set_dc(1);
    } else {
        lcd_set_read_address_window(x, y, w, h);
        lcd_select();
        lcd_set_dc(0);
        lcd_bitbang_write_byte(0x2Eu);
        lcd_set_dc(1);
    }

    for (i = 0; i < mode->dummy_bytes; ++i) {
        (void)lcd_bitbang_read_byte();
    }

    sample_bytes = mode->bytes_per_pixel * 4;
    if (sample_bytes > raw_sample_size) {
        sample_bytes = raw_sample_size;
    }
    capture_bytes = (pixel_count * mode->bytes_per_pixel) + 1;

    {
        BYTE raw_bytes[10];
        int raw_len = capture_bytes;

        if (raw_len > (int)sizeof(raw_bytes)) {
            raw_len = (int)sizeof(raw_bytes);
        }

        memset(raw_bytes, 0, sizeof(raw_bytes));
        for (i = 0; i < raw_len; ++i) {
            raw_bytes[i] = lcd_bitbang_read_byte_edge(edge);
            if (raw_sample && i < sample_bytes) {
                raw_sample[i] = raw_bytes[i];
            }
        }
        for (i = 0; i < pixel_count; ++i) {
            dst_pixels[i] = lcd_decode_shifted_pixel(raw_bytes, raw_len, i, mode, bit_shift);
        }
    }

    lcd_deselect();
    return true;
}

static bool lcd_readback_rect_full(int x,
                                   int y,
                                   int w,
                                   int h,
                                   const lcd_read_mode_t *mode,
                                   lcd_bus_seq_t seq,
                                   lcd_read_edge_t edge,
                                   int bit_shift,
                                   WORD *dst_pixels) {
    int pixel_count = w * h;
    int i;
    BYTE carry = 0u;
    BYTE current[3];
    int total_bytes;

    if (!mode || !dst_pixels || w <= 0 || h <= 0 || bit_shift < 0 || bit_shift > 7) {
        return false;
    }

    if (seq == LCD_BUS_SEQ_HOLD_CS) {
        lcd_select();
        lcd_set_read_address_window_held(x, y, w, h);
        lcd_set_dc(0);
        lcd_bitbang_write_byte(0x2Eu);
        lcd_set_dc(1);
    } else {
        lcd_set_read_address_window(x, y, w, h);
        lcd_select();
        lcd_set_dc(0);
        lcd_bitbang_write_byte(0x2Eu);
        lcd_set_dc(1);
    }

    for (i = 0; i < mode->dummy_bytes; ++i) {
        (void)lcd_bitbang_read_byte_edge(edge);
    }

    total_bytes = pixel_count * mode->bytes_per_pixel;
    if (bit_shift != 0) {
        carry = lcd_bitbang_read_byte_edge(edge);
        total_bytes += 1;
    }

    for (i = 0; i < pixel_count; ++i) {
        int byte_idx;
        for (byte_idx = 0; byte_idx < mode->bytes_per_pixel; ++byte_idx) {
            BYTE next = lcd_bitbang_read_byte_edge(edge);
            if (bit_shift == 0) {
                current[byte_idx] = next;
            } else {
                current[byte_idx] = (BYTE)((BYTE)(carry << bit_shift) | (BYTE)(next >> (8 - bit_shift)));
                carry = next;
            }
        }
        dst_pixels[i] = lcd_decode_read_pixel(current, mode);
    }

    if (bit_shift != 0 && total_bytes > 0) {
        (void)total_bytes;
    }

    lcd_deselect();
    return true;
}

static void lcd_bitbang_fill_rect_color(int x, int y, int w, int h, WORD color) {
    BYTE hi = (BYTE)(color >> 8);
    BYTE lo = (BYTE)(color & 0xFFu);
    int pixel_count = w * h;
    int i;

    if (w <= 0 || h <= 0) {
        return;
    }

    lcd_set_read_address_window(x, y, w, h);
    lcd_select();
    lcd_set_dc(0);
    lcd_bitbang_write_byte(0x2Cu);
    lcd_set_dc(1);

    for (i = 0; i < pixel_count; ++i) {
        lcd_bitbang_write_byte(hi);
        lcd_bitbang_write_byte(lo);
    }

    lcd_deselect();
}

static void lcd_bitbang_fill_rect_color_seq(int x, int y, int w, int h, WORD color, lcd_bus_seq_t seq) {
    BYTE hi = (BYTE)(color >> 8);
    BYTE lo = (BYTE)(color & 0xFFu);
    int pixel_count = w * h;
    int i;

    if (w <= 0 || h <= 0) {
        return;
    }

    if (seq == LCD_BUS_SEQ_HOLD_CS) {
        lcd_select();
        lcd_set_read_address_window_held(x, y, w, h);
        lcd_set_dc(0);
        lcd_bitbang_write_byte(0x2Cu);
        lcd_set_dc(1);
        for (i = 0; i < pixel_count; ++i) {
            lcd_bitbang_write_byte(hi);
            lcd_bitbang_write_byte(lo);
        }
        lcd_deselect();
        return;
    }

    lcd_bitbang_fill_rect_color(x, y, w, h, color);
}

static __attribute__((unused)) void lcd_probe_readback_mode(void) {
    static const WORD test_pixels[4] = { 0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu };
    static const lcd_read_mode_t candidate_modes[] = {
        { 1, 2, false },
        { 2, 2, false },
        { 3, 2, false },
        { 1, 2, true  },
        { 2, 2, true  },
        { 3, 2, true  },
        { 1, 3, false },
        { 2, 3, false },
        { 3, 3, false },
    };
    static const int candidate_half_cycles_us[] = { 1 };
    static const lcd_bus_seq_t candidate_seqs[] = {
        LCD_BUS_SEQ_SPLIT_CS,
        LCD_BUS_SEQ_HOLD_CS,
    };
    static const lcd_read_edge_t candidate_edges[] = {
        LCD_READ_EDGE_RISING,
        LCD_READ_EDGE_FALLING,
    };
    enum {
        TEST_X = 0,
        TEST_Y = 0,
        TEST_HALF = 32,
        TEST_SIZE = TEST_HALF * 2,
    };
    const int sample_points[4][2] = {
        { TEST_X + (TEST_HALF / 2),             TEST_Y + (TEST_HALF / 2) },
        { TEST_X + TEST_HALF + (TEST_HALF / 2), TEST_Y + (TEST_HALF / 2) },
        { TEST_X + (TEST_HALF / 2),             TEST_Y + TEST_HALF + (TEST_HALF / 2) },
        { TEST_X + TEST_HALF + (TEST_HALF / 2), TEST_Y + TEST_HALF + (TEST_HALF / 2) },
    };
    WORD read_pixels[4];
    BYTE raw_sample[12];
    int best_score = -1;
    int best_index = -1;
    int best_half_cycle_us = LCD_READ_DEFAULT_HALF_CYCLE_US;
    lcd_bus_seq_t best_seq = LCD_BUS_SEQ_SPLIT_CS;
    lcd_read_edge_t best_edge = LCD_READ_EDGE_RISING;
    int best_bit_shift = 0;
    int mode_idx;

    for (int write_mode = 0; write_mode < 3; ++write_mode) {
        best_score = -1;
        best_index = -1;

        if (write_mode == 0) {
            lcd_fill_rect_color(TEST_X, TEST_Y, TEST_HALF, TEST_HALF, test_pixels[0]);
            lcd_fill_rect_color(TEST_X + TEST_HALF, TEST_Y, TEST_HALF, TEST_HALF, test_pixels[1]);
            lcd_fill_rect_color(TEST_X, TEST_Y + TEST_HALF, TEST_HALF, TEST_HALF, test_pixels[2]);
            lcd_fill_rect_color(TEST_X + TEST_HALF, TEST_Y + TEST_HALF, TEST_HALF, TEST_HALF, test_pixels[3]);
            NESCO_LOGF("[LCDRD_INIT] write=pio\r\n");
        } else if (write_mode == 1) {
            lcd_set_bitbang_mode(true);
            lcd_bitbang_fill_rect_color_seq(TEST_X, TEST_Y, TEST_HALF, TEST_HALF, test_pixels[0], LCD_BUS_SEQ_SPLIT_CS);
            lcd_bitbang_fill_rect_color_seq(TEST_X + TEST_HALF, TEST_Y, TEST_HALF, TEST_HALF, test_pixels[1], LCD_BUS_SEQ_SPLIT_CS);
            lcd_bitbang_fill_rect_color_seq(TEST_X, TEST_Y + TEST_HALF, TEST_HALF, TEST_HALF, test_pixels[2], LCD_BUS_SEQ_SPLIT_CS);
            lcd_bitbang_fill_rect_color_seq(TEST_X + TEST_HALF, TEST_Y + TEST_HALF, TEST_HALF, TEST_HALF, test_pixels[3], LCD_BUS_SEQ_SPLIT_CS);
            lcd_set_bitbang_mode(false);
            NESCO_LOGF("[LCDRD_INIT] write=bitbang_split\r\n");
        } else {
            lcd_set_bitbang_mode(true);
            lcd_bitbang_fill_rect_color_seq(TEST_X, TEST_Y, TEST_HALF, TEST_HALF, test_pixels[0], LCD_BUS_SEQ_HOLD_CS);
            lcd_bitbang_fill_rect_color_seq(TEST_X + TEST_HALF, TEST_Y, TEST_HALF, TEST_HALF, test_pixels[1], LCD_BUS_SEQ_HOLD_CS);
            lcd_bitbang_fill_rect_color_seq(TEST_X, TEST_Y + TEST_HALF, TEST_HALF, TEST_HALF, test_pixels[2], LCD_BUS_SEQ_HOLD_CS);
            lcd_bitbang_fill_rect_color_seq(TEST_X + TEST_HALF, TEST_Y + TEST_HALF, TEST_HALF, TEST_HALF, test_pixels[3], LCD_BUS_SEQ_HOLD_CS);
            lcd_set_bitbang_mode(false);
            NESCO_LOGF("[LCDRD_INIT] write=bitbang_hold\r\n");
        }
        sleep_ms(150);

        lcd_set_bitbang_mode(true);
        for (int delay_idx = 0; delay_idx < (int)ARRAY_SIZE(candidate_half_cycles_us); ++delay_idx) {
            s_lcd_read_half_cycle_us = candidate_half_cycles_us[delay_idx];
            for (int seq_idx = 0; seq_idx < (int)ARRAY_SIZE(candidate_seqs); ++seq_idx) {
                for (int edge_idx = 0; edge_idx < (int)ARRAY_SIZE(candidate_edges); ++edge_idx) {
                    for (int bit_shift = 0; bit_shift < 8; ++bit_shift) {
                        for (mode_idx = 0; mode_idx < (int)ARRAY_SIZE(candidate_modes); ++mode_idx) {
                            int score = 0;
#if defined(NESCO_RUNTIME_LOGS)
                            uint32_t start_us = time_us_32();
                            uint32_t elapsed_us;
#endif

                            for (int i = 0; i < 4; ++i) {
                                lcd_readback_rect_mode(sample_points[i][0],
                                                       sample_points[i][1],
                                                       1,
                                                       1,
                                                       &candidate_modes[mode_idx],
                                                       candidate_seqs[seq_idx],
                                                       candidate_edges[edge_idx],
                                                       bit_shift,
                                                       &read_pixels[i],
                                                       i == 0 ? raw_sample : NULL,
                                                       i == 0 ? (int)sizeof(raw_sample) : 0);
                            }
#if defined(NESCO_RUNTIME_LOGS)
                            elapsed_us = time_us_32() - start_us;
#endif

                            for (int i = 0; i < 4; ++i) {
                                if (read_pixels[i] == test_pixels[i]) {
                                    score++;
                                }
                            }
#if defined(NESCO_RUNTIME_LOGS)
                            NESCO_LOGF("[LCDRD_INIT] cand=%d seq=%s edge=%s shift=%d delay=%dus dummy=%d bpp=%d swap=%d score=%d t=%luus raw=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X px=%04X %04X %04X %04X\r\n",
                                       mode_idx,
                                       candidate_seqs[seq_idx] == LCD_BUS_SEQ_HOLD_CS ? "hold" : "split",
                                       candidate_edges[edge_idx] == LCD_READ_EDGE_FALLING ? "fall" : "rise",
                                       bit_shift,
                                       s_lcd_read_half_cycle_us,
                                       candidate_modes[mode_idx].dummy_bytes,
                                       candidate_modes[mode_idx].bytes_per_pixel,
                                       candidate_modes[mode_idx].swap_bytes ? 1 : 0,
                                       score,
                                       (unsigned long)elapsed_us,
                                       raw_sample[0], raw_sample[1], raw_sample[2], raw_sample[3],
                                       raw_sample[4], raw_sample[5], raw_sample[6], raw_sample[7],
                                       raw_sample[8], raw_sample[9], raw_sample[10], raw_sample[11],
                                       read_pixels[0], read_pixels[1], read_pixels[2], read_pixels[3]);
#endif
                            if (score > best_score) {
                                best_score = score;
                                best_index = mode_idx;
                                best_half_cycle_us = s_lcd_read_half_cycle_us;
                                best_seq = candidate_seqs[seq_idx];
                                best_edge = candidate_edges[edge_idx];
                                best_bit_shift = bit_shift;
                            }
                        }
                    }
                }
            }
        }
        lcd_set_bitbang_mode(false);

        if (write_mode == 0 && best_index >= 0 && best_score == 4) {
            s_lcd_read_mode = candidate_modes[best_index];
            s_lcd_read_half_cycle_us = best_half_cycle_us;
            s_lcd_read_seq = best_seq;
            s_lcd_read_edge = best_edge;
            s_lcd_read_bit_shift = best_bit_shift;
            s_lcd_read_mode_valid = true;
            NESCO_LOGF("[LCDRD_INIT] selected cand=%d seq=%s edge=%s shift=%d delay=%dus dummy=%d bpp=%d swap=%d\r\n",
                       best_index,
                       best_seq == LCD_BUS_SEQ_HOLD_CS ? "hold" : "split",
                       best_edge == LCD_READ_EDGE_FALLING ? "fall" : "rise",
                       best_bit_shift,
                       s_lcd_read_half_cycle_us,
                       s_lcd_read_mode.dummy_bytes,
                       s_lcd_read_mode.bytes_per_pixel,
                       s_lcd_read_mode.swap_bytes ? 1 : 0);
        } else {
            NESCO_LOGF("[LCDRD_INIT] result write=%s best_cand=%d best_seq=%s best_edge=%s best_shift=%d best_delay=%dus best_score=%d\r\n",
                       write_mode == 0 ? "pio" : (write_mode == 1 ? "bitbang_split" : "bitbang_hold"),
                       best_index,
                       best_seq == LCD_BUS_SEQ_HOLD_CS ? "hold" : "split",
                       best_edge == LCD_READ_EDGE_FALLING ? "fall" : "rise",
                       best_bit_shift,
                       best_half_cycle_us,
                       best_score);
        }
    }

    lcd_fill_rect_color(TEST_X, TEST_Y, TEST_SIZE, TEST_SIZE, 0x0000u);
    if (!s_lcd_read_mode_valid) {
        NESCO_LOGF("[LCDRD_INIT] no exact PIO-write readback mode found\r\n");
    }
}
#endif

void lcd_init(void) {
#ifdef PICO_BUILD
    /*
     * Init order matters:
     *   1. GPIO idle levels
     *   2. PIO program for write transfers
     *   3. DMA channel configured to feed PIO TX FIFO
     *   4. panel reset and controller-specific command sequence
     */
    gpio_init(LCD_PIN_CS);
    gpio_init(LCD_PIN_DC);
    gpio_init(LCD_PIN_RST);
    gpio_init(LCD_PIN_RAM_CS);

    gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
    gpio_set_dir(LCD_PIN_DC, GPIO_OUT);
    gpio_set_dir(LCD_PIN_RST, GPIO_OUT);
    gpio_set_dir(LCD_PIN_RAM_CS, GPIO_OUT);
    gpio_init(LCD_PIN_MISO);
    gpio_set_dir(LCD_PIN_MISO, GPIO_IN);
    gpio_disable_pulls(LCD_PIN_MISO);

    gpio_put(LCD_PIN_CS, 1);
    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_RST, 1);
    gpio_put(LCD_PIN_RAM_CS, 1);

    s_lcd_pio_offset = pio_add_program(s_lcd_pio, &lcd_spi_min_program);
    lcd_spi_min_program_init(s_lcd_pio,
                             s_lcd_sm,
                             s_lcd_pio_offset,
                             LCD_PIN_MOSI,
                             LCD_PIN_SCK,
                             LCD_PIO_CLKDIV);
    s_lcd_pio_ready = true;
    s_lcd_dma_chan = dma_claim_unused_channel(true);
    s_lcd_dma_cfg = dma_channel_get_default_config((uint)s_lcd_dma_chan);
    channel_config_set_transfer_data_size(&s_lcd_dma_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&s_lcd_dma_cfg, true);
    channel_config_set_write_increment(&s_lcd_dma_cfg, false);
    channel_config_set_dreq(&s_lcd_dma_cfg, pio_get_dreq(s_lcd_pio, s_lcd_sm, true));
    s_lcd_dma_cfg_ready = true;

    lcd_reset_panel();

    {
        static const BYTE b9[] = { 0x02u, 0xE0u };
        static const BYTE c0[] = { 0x80u, 0x06u };
        static const BYTE e8[] = { 0x40u, 0x8Au, 0x00u, 0x00u, 0x29u, 0x19u, 0xAAu, 0x33u };
        static const BYTE e0[] = { 0xF0u, 0x06u, 0x0Fu, 0x05u, 0x04u, 0x20u, 0x37u, 0x33u,
                                   0x4Cu, 0x37u, 0x13u, 0x14u, 0x2Bu, 0x31u };
        static const BYTE e1[] = { 0xF0u, 0x11u, 0x1Bu, 0x11u, 0x0Fu, 0x0Au, 0x37u, 0x43u,
                                   0x4Cu, 0x37u, 0x13u, 0x13u, 0x2Cu, 0x32u };

        lcd_write_command1(0xF0u, 0xC3u);
        lcd_write_command1(0xF0u, 0x96u);
        lcd_write_command1(0x36u, 0x48u);
        lcd_write_command1(0x3Au, 0x65u);
        lcd_write_command1(0xB1u, 0xA0u);
        lcd_write_command1(0xB4u, 0x00u);
        lcd_write_command1(0xB7u, 0xC6u);
        lcd_write_commandn(0xB9u, b9, ARRAY_SIZE(b9));
        lcd_write_commandn(0xC0u, c0, ARRAY_SIZE(c0));
        lcd_write_command1(0xC1u, 0x15u);
        lcd_write_command1(0xC2u, 0xA7u);
        lcd_write_command1(0xC5u, 0x04u);
        lcd_write_commandn(0xE8u, e8, ARRAY_SIZE(e8));
        lcd_write_commandn(0xE0u, e0, ARRAY_SIZE(e0));
        lcd_write_commandn(0xE1u, e1, ARRAY_SIZE(e1));
        lcd_write_command1(0xF0u, 0x3Cu);
        lcd_write_command1(0xF0u, 0x69u);
        lcd_write_command1(0x35u, 0x00u);
    }

    NESCO_LOGF("[LCD] init sequence sent (62.5MHz, RGB565 spec)\r\n");

    lcd_write_command(0x11u);
    sleep_ms(120);
    lcd_write_command(0x21u);
    lcd_fill_black();
    lcd_write_command(0x29u);
    sleep_ms(120);
    lcd_set_window(0, 0, LCD_WIDTH, LCD_HEIGHT);
#endif
}

void lcd_set_window(int x, int y, int w, int h) {
    /*
     * Select the target rectangle for the following pixel stream.
     *
     * The LCD controller auto-increments its GRAM address inside this window,
     * so callers do not send per-pixel coordinates. s_window_pixels tracks
     * the remaining capacity and prevents accidental overrun.
     */
    if (w <= 0 || h <= 0) {
        s_window_pixels = 0;
        return;
    }

    int x0 = x;
    int y0 = y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= LCD_WIDTH)  x1 = LCD_WIDTH - 1;
    if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;
    if (x1 < x0 || y1 < y0) {
        s_window_pixels = 0;
        return;
    }

    s_window_pixels = (x1 - x0 + 1) * (y1 - y0 + 1);

#ifdef PICO_BUILD
    BYTE col[4] = {
        (BYTE)((x0 >> 8) & 0xFF),
        (BYTE)(x0 & 0xFF),
        (BYTE)((x1 >> 8) & 0xFF),
        (BYTE)(x1 & 0xFF),
    };
    BYTE row[4] = {
        (BYTE)((y0 >> 8) & 0xFF),
        (BYTE)(y0 & 0xFF),
        (BYTE)((y1 >> 8) & 0xFF),
        (BYTE)(y1 & 0xFF),
    };

    lcd_write_commandn(0x2Au, col, ARRAY_SIZE(col)); /* CASET: column range */
    lcd_write_commandn(0x2Bu, row, ARRAY_SIZE(row)); /* RASET: row range */
    lcd_write_command(0x2Cu);                        /* RAMWR: pixel data follows */
#endif
}

void lcd_dma_write_rgb565_async(const WORD *buf, int n_pixels) {
    /*
     * Convenience path for callers with WORD RGB565 pixels.
     * The LCD bus wants bytes, so this routine converts into the driver DMA
     * buffer and then starts lcd_dma_write_bytes_async().
     */
    if (!buf || n_pixels <= 0 || s_window_pixels <= 0) {
        return;
    }

    if (n_pixels > s_window_pixels) {
        n_pixels = s_window_pixels;
    }

#ifdef PICO_BUILD
    int remaining = n_pixels;
    const WORD *src = buf;

    while (remaining > 0) {
        int chunk_pixels = remaining;
        BYTE *dst = s_lcd_dma_buf[s_lcd_dma_buf_idx];

        if (chunk_pixels > LCD_DMA_MAX_PIXELS) {
            chunk_pixels = LCD_DMA_MAX_PIXELS;
        }

        lcd_dma_wait();

        for (int i = 0; i < chunk_pixels; i++) {
            WORD px = src[i];
            dst[i * 2 + 0] = (BYTE)(px >> 8);
            dst[i * 2 + 1] = (BYTE)(px & 0xFFu);
        }

        lcd_dma_write_bytes_async(dst, chunk_pixels * 2);
        src += chunk_pixels;
        remaining -= chunk_pixels;
    }
#else
    UNUSED(buf);
    UNUSED(n_pixels);
#endif
}

BYTE *lcd_dma_acquire_buffer(void) {
    /*
     * Return the currently free driver DMA buffer for callers that already
     * pack RGB565 as high-byte/low-byte pairs. The returned pointer is valid
     * until the next lcd_dma_write_*_async() call.
     */
#ifdef PICO_BUILD
    return s_lcd_dma_buf[s_lcd_dma_buf_idx];
#else
    return NULL;
#endif
}

void lcd_dma_write_bytes_async(const BYTE *buf, int n_bytes) {
    /*
     * Start an asynchronous byte transfer to the active LCD window.
     * This function waits for the previous transfer first, then keeps CS
     * asserted until lcd_dma_wait() observes completion.
     */
    if (!buf || n_bytes <= 0 || s_window_pixels <= 0) {
        return;
    }

    if (n_bytes > s_window_pixels * 2) {
        n_bytes = s_window_pixels * 2;
    }

#ifdef PICO_BUILD
    lcd_dma_wait();

    lcd_select();
    lcd_set_dc(1);

    dma_channel_configure((uint)s_lcd_dma_chan,
                          s_lcd_dma_cfg_ready ? &s_lcd_dma_cfg : NULL,
                          &s_lcd_pio->txf[s_lcd_sm],
                          buf,
                          (size_t)n_bytes,
                          true);

    s_lcd_dma_active = true;
    s_window_pixels -= n_bytes / 2;
    s_lcd_dma_buf_idx = 1 - s_lcd_dma_buf_idx;
#else
    UNUSED(buf);
    UNUSED(n_bytes);
#endif
}

void lcd_fill_rect_color(int x, int y, int w, int h, WORD color) {
#ifdef PICO_BUILD
    BYTE hi = (BYTE)(color >> 8);
    BYTE lo = (BYTE)(color & 0xFFu);

    if (w <= 0 || h <= 0) {
        return;
    }

    lcd_set_window(x, y, w, h);
    lcd_select();
    lcd_set_dc(1);
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            BYTE px[2] = { hi, lo };
            lcd_write_bytes(px, 2);
        }
    }
    lcd_wait_idle();
    lcd_deselect();
#else
    UNUSED(x);
    UNUSED(y);
    UNUSED(w);
    UNUSED(h);
    UNUSED(color);
#endif
}

bool lcd_readback_rect_rgb565(int x, int y, int w, int h, WORD *dst_pixels) {
#ifdef PICO_BUILD
    if (!dst_pixels || w <= 0 || h <= 0) {
        return false;
    }
    if (!s_lcd_read_mode_valid) {
        return false;
    }

    lcd_dma_wait();
    lcd_set_bitbang_mode(true);
    lcd_readback_rect_full(x,
                           y,
                           w,
                           h,
                           &s_lcd_read_mode,
                           s_lcd_read_seq,
                           s_lcd_read_edge,
                           s_lcd_read_bit_shift,
                           dst_pixels);
    lcd_set_bitbang_mode(false);
    return true;
#else
    UNUSED(x);
    UNUSED(y);
    UNUSED(w);
    UNUSED(h);
    UNUSED(dst_pixels);
    return false;
#endif
}

void lcd_log_readback_diagnostics(void) {
#ifdef PICO_BUILD
    NESCO_LOGF("[LCDRD] mode valid=%d seq=%s edge=%s shift=%d delay=%dus dummy=%d bpp=%d swap=%d\r\n",
               s_lcd_read_mode_valid ? 1 : 0,
               s_lcd_read_seq == LCD_BUS_SEQ_HOLD_CS ? "hold" : "split",
               s_lcd_read_edge == LCD_READ_EDGE_FALLING ? "fall" : "rise",
               s_lcd_read_bit_shift,
               s_lcd_read_half_cycle_us,
               s_lcd_read_mode.dummy_bytes,
               s_lcd_read_mode.bytes_per_pixel,
               s_lcd_read_mode.swap_bytes ? 1 : 0);
#endif
}
