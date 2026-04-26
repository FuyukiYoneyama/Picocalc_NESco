#pragma once

#include <stdbool.h>

#include "InfoNES_Types.h"
#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct screenshot_bmp_writer_t {
    FIL file;
    int width;
    int height;
    unsigned row_stride;
    BYTE row_buf[(320 * 3) + 4];
    bool open;
} screenshot_bmp_writer_t;

bool storage_save_screenshot_bmp(const char *path,
                                 int width,
                                 int height,
                                 const WORD *rgb565_pixels);
bool storage_build_screenshot_path(char *path, unsigned path_size);
bool storage_build_screenshot_path_for_stem(char *path, unsigned path_size, const char *stem);
bool storage_open_screenshot_bmp_writer(screenshot_bmp_writer_t *writer,
                                        const char *path,
                                        int width,
                                        int height);
bool storage_write_screenshot_bmp_row_rgb565(screenshot_bmp_writer_t *writer,
                                             const WORD *rgb565_pixels,
                                             int width);
bool storage_write_screenshot_bmp_rows_rgb565(screenshot_bmp_writer_t *writer,
                                              const WORD *rgb565_pixels,
                                              int width,
                                              int rows,
                                              BYTE *chunk_buf,
                                              unsigned chunk_buf_size);
bool storage_close_screenshot_bmp_writer(screenshot_bmp_writer_t *writer);

#ifdef __cplusplus
}
#endif
