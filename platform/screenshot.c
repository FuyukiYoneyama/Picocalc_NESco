#include "screenshot.h"

#include "InfoNES.h"
#include "display.h"
#include "audio.h"
#include "runtime_log.h"
#include "screenshot_storage.h"

#include <stdlib.h>
#include <string.h>

enum {
    SCREENSHOT_WIDTH = 320,
    SCREENSHOT_HEIGHT = 320,
    SCREENSHOT_CHUNK_ROWS = 16,
    SCREENSHOT_CHUNK_ROW_STRIDE = ((SCREENSHOT_WIDTH * 3) + 3) & ~3,
};

extern "C" void lcd_dma_wait(void);
extern "C" bool lcd_readback_rect_rgb565(int x, int y, int w, int h, WORD *dst_pixels);

static bool s_screenshot_pending = false;
static bool s_screenshot_busy = false;
static nesco_screenshot_result_t s_screenshot_last_result = NESCO_SCREENSHOT_OK;

typedef struct screenshot_chunk_buffers_t {
    WORD *pixels;
    BYTE *bmp;
} screenshot_chunk_buffers_t;

typedef struct screenshot_validation_t {
    int viewport_x;
    int viewport_y;
    int viewport_w;
    int viewport_h;
    unsigned viewport_pixels_seen;
    unsigned viewport_non_black_pixels;
    unsigned viewport_non_white_pixels;
    WORD sample_pixels[4];
    unsigned sample_count;
} screenshot_validation_t;

#if defined(NESCO_SCREENSHOT_DEBUG_LOGS) || defined(NESCO_RUNTIME_LOGS)
#define SCREENSHOT_LOGF(...) NESCO_LOGF(__VA_ARGS__)
#else
#define SCREENSHOT_LOGF(...) do { } while (0)
#endif

static void screenshot_set_result(nesco_screenshot_result_t result)
{
    s_screenshot_last_result = result;
    if (result != NESCO_SCREENSHOT_OK) {
        SCREENSHOT_LOGF("SS_ERROR result=%d\r\n", (int)result);
    }
}

static bool screenshot_alloc_chunk_buffers(screenshot_chunk_buffers_t *buffers)
{
    if (!buffers) {
        return false;
    }

    buffers->pixels = (WORD *)malloc(sizeof(WORD) * SCREENSHOT_WIDTH * SCREENSHOT_CHUNK_ROWS);
    buffers->bmp = (BYTE *)malloc(SCREENSHOT_CHUNK_ROW_STRIDE * SCREENSHOT_CHUNK_ROWS);
    if (!buffers->pixels || !buffers->bmp) {
        free(buffers->pixels);
        free(buffers->bmp);
        buffers->pixels = NULL;
        buffers->bmp = NULL;
        return false;
    }
    return true;
}

static void screenshot_free_chunk_buffers(screenshot_chunk_buffers_t *buffers)
{
    if (!buffers) {
        return;
    }
    free(buffers->pixels);
    free(buffers->bmp);
    buffers->pixels = NULL;
    buffers->bmp = NULL;
}

static void screenshot_resume_runtime(void)
{
    display_reset_frame_pacing();
    audio_resume_after_capture();
}

static void screenshot_resume_if_needed(bool pause_audio)
{
    if (pause_audio) {
        screenshot_resume_runtime();
    }
}

static void screenshot_validation_init(screenshot_validation_t *validation)
{
    if (!validation) {
        return;
    }

    display_get_viewport(&validation->viewport_x,
                         &validation->viewport_y,
                         &validation->viewport_w,
                         &validation->viewport_h);
    validation->viewport_pixels_seen = 0u;
    validation->viewport_non_black_pixels = 0u;
    validation->viewport_non_white_pixels = 0u;
    validation->sample_count = 0u;
    memset(validation->sample_pixels, 0, sizeof(validation->sample_pixels));
}

static void screenshot_validation_add_rows(screenshot_validation_t *validation,
                                           int chunk_y,
                                           int chunk_rows,
                                           const WORD *chunk_pixels)
{
    int row_begin;
    int row_end;
    int row;

    if (!validation || !chunk_pixels || chunk_rows <= 0) {
        return;
    }

    row_begin = validation->viewport_y;
    if (row_begin < chunk_y) {
        row_begin = chunk_y;
    }

    row_end = validation->viewport_y + validation->viewport_h;
    if (row_end > chunk_y + chunk_rows) {
        row_end = chunk_y + chunk_rows;
    }

    if (validation->viewport_w <= 0 || validation->viewport_h <= 0 || row_begin >= row_end) {
        return;
    }

    for (row = row_begin; row < row_end; ++row) {
        const WORD *src = &chunk_pixels[(row - chunk_y) * SCREENSHOT_WIDTH];
        int x_begin = validation->viewport_x;
        int x_end = validation->viewport_x + validation->viewport_w;
        int x;

        if (x_begin < 0) {
            x_begin = 0;
        }
        if (x_end > SCREENSHOT_WIDTH) {
            x_end = SCREENSHOT_WIDTH;
        }
        if (x_begin >= x_end) {
            continue;
        }

        for (x = x_begin; x < x_end; ++x) {
            WORD pixel = src[x];
            validation->viewport_pixels_seen++;
            if (pixel != 0x0000u) {
                validation->viewport_non_black_pixels++;
            }
            if (pixel != 0xFFFFu) {
                validation->viewport_non_white_pixels++;
            }
            if (validation->sample_count < (sizeof(validation->sample_pixels) / sizeof(validation->sample_pixels[0]))) {
                validation->sample_pixels[validation->sample_count++] = pixel;
            }
        }
    }
}

static bool screenshot_validation_ok(const screenshot_validation_t *validation)
{
    if (!validation) {
        return false;
    }

    if (validation->viewport_pixels_seen == 0u) {
        return false;
    }

    return validation->viewport_non_black_pixels != 0u &&
           validation->viewport_non_white_pixels != 0u;
}

bool nesco_request_screenshot(void)
{
    SCREENSHOT_LOGF("SS_REQ\r\n");

    if (s_screenshot_pending || s_screenshot_busy) {
        screenshot_set_result(NESCO_SCREENSHOT_BUSY);
        return false;
    }

    s_screenshot_pending = true;
    screenshot_set_result(NESCO_SCREENSHOT_OK);
    return true;
}

static nesco_screenshot_result_t screenshot_capture_and_save_with_stem(const char *stem_or_null,
                                                                       bool pause_audio)
{
    screenshot_bmp_writer_t writer;
    screenshot_validation_t validation;
    screenshot_chunk_buffers_t buffers = { NULL, NULL };
    char path[192];
    bool ok = false;
    int y;
    int chunk_rows;

    if (stem_or_null) {
        ok = storage_build_screenshot_path_for_stem(path, sizeof(path), stem_or_null);
    } else {
        ok = storage_build_screenshot_path(path, sizeof(path));
    }
    if (!ok) {
        return NESCO_SCREENSHOT_SD_OPEN_FAILED;
    }

    if (!screenshot_alloc_chunk_buffers(&buffers)) {
        return NESCO_SCREENSHOT_NO_MEMORY;
    }

    SCREENSHOT_LOGF("SS_SAVE_BEGIN path=%s\r\n", path);
    if (!storage_open_screenshot_bmp_writer(&writer, path, SCREENSHOT_WIDTH, SCREENSHOT_HEIGHT)) {
        screenshot_free_chunk_buffers(&buffers);
        return NESCO_SCREENSHOT_SD_WRITE_FAILED;
    }

    if (pause_audio) {
        audio_pause_for_capture();
    }
    screenshot_validation_init(&validation);
    SCREENSHOT_LOGF("SS_CAPTURE_BEGIN\r\n");
    for (y = 0; y < SCREENSHOT_HEIGHT; y += SCREENSHOT_CHUNK_ROWS) {
        chunk_rows = SCREENSHOT_HEIGHT - y;
        if (chunk_rows > SCREENSHOT_CHUNK_ROWS) {
            chunk_rows = SCREENSHOT_CHUNK_ROWS;
        }

        ok = lcd_readback_rect_rgb565(0, y, SCREENSHOT_WIDTH, chunk_rows, buffers.pixels);
        if (!ok) {
            screenshot_resume_if_needed(pause_audio);
            storage_close_screenshot_bmp_writer(&writer);
            screenshot_free_chunk_buffers(&buffers);
            SCREENSHOT_LOGF("SS_CAPTURE_DONE ok=0 row=%d rows=%d\r\n", y, chunk_rows);
            return NESCO_SCREENSHOT_READBACK_FAILED;
        }

        screenshot_validation_add_rows(&validation, y, chunk_rows, buffers.pixels);

        ok = storage_write_screenshot_bmp_rows_rgb565(&writer,
                                                      buffers.pixels,
                                                      SCREENSHOT_WIDTH,
                                                      chunk_rows,
                                                      buffers.bmp,
                                                      SCREENSHOT_CHUNK_ROW_STRIDE * SCREENSHOT_CHUNK_ROWS);
        if (!ok) {
            screenshot_resume_if_needed(pause_audio);
            storage_close_screenshot_bmp_writer(&writer);
            screenshot_free_chunk_buffers(&buffers);
            SCREENSHOT_LOGF("SS_CAPTURE_DONE ok=0 row=%d rows=%d\r\n", y, chunk_rows);
            return NESCO_SCREENSHOT_SD_WRITE_FAILED;
        }
    }

    if (!screenshot_validation_ok(&validation)) {
        screenshot_resume_if_needed(pause_audio);
        storage_close_screenshot_bmp_writer(&writer);
        screenshot_free_chunk_buffers(&buffers);
        SCREENSHOT_LOGF("SS_CAPTURE_DONE ok=0 viewport=%d,%d %dx%d seen=%u nb=%u nw=%u samples=%04X %04X %04X %04X\r\n",
                        validation.viewport_x,
                        validation.viewport_y,
                        validation.viewport_w,
                        validation.viewport_h,
                        validation.viewport_pixels_seen,
                        validation.viewport_non_black_pixels,
                        validation.viewport_non_white_pixels,
                        validation.sample_pixels[0],
                        validation.sample_pixels[1],
                        validation.sample_pixels[2],
                        validation.sample_pixels[3]);
        return NESCO_SCREENSHOT_READBACK_FAILED;
    }
    SCREENSHOT_LOGF("SS_CAPTURE_DONE ok=1\r\n");

    if (!storage_close_screenshot_bmp_writer(&writer)) {
        screenshot_resume_if_needed(pause_audio);
        screenshot_free_chunk_buffers(&buffers);
        return NESCO_SCREENSHOT_SD_WRITE_FAILED;
    }

    screenshot_resume_if_needed(pause_audio);
    screenshot_free_chunk_buffers(&buffers);
    SCREENSHOT_LOGF("SS_SAVE_DONE path=%s\r\n", path);
    return NESCO_SCREENSHOT_OK;
}

nesco_screenshot_result_t nesco_take_screenshot_now_with_stem(const char *stem)
{
    nesco_screenshot_result_t result;

    if (s_screenshot_pending || s_screenshot_busy) {
        screenshot_set_result(NESCO_SCREENSHOT_BUSY);
        return NESCO_SCREENSHOT_BUSY;
    }

    s_screenshot_busy = true;
    SCREENSHOT_LOGF("SS_NOW_DMA_WAIT_BEGIN\r\n");
    lcd_dma_wait();
    SCREENSHOT_LOGF("SS_NOW_DMA_WAIT_DONE\r\n");

    result = screenshot_capture_and_save_with_stem(stem, false);
    screenshot_set_result(result);
    s_screenshot_busy = false;
    return result;
}

void nesco_maybe_start_screenshot_on_vblank(void)
{
    nesco_screenshot_result_t result;

    if (!s_screenshot_pending) {
        return;
    }

    SCREENSHOT_LOGF("SS_VBLANK\r\n");
    s_screenshot_busy = true;

    SCREENSHOT_LOGF("SS_DMA_WAIT_BEGIN\r\n");
    lcd_dma_wait();
    SCREENSHOT_LOGF("SS_DMA_WAIT_DONE\r\n");

    result = screenshot_capture_and_save_with_stem(NULL, true);
    screenshot_set_result(result);

    s_screenshot_pending = false;
    s_screenshot_busy = false;
}

nesco_screenshot_result_t nesco_get_last_screenshot_result(void)
{
    return s_screenshot_last_result;
}
