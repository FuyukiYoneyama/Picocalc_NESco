#include "screenshot_storage.h"

#include "ff.h"
#include "rom_image.h"
#include "runtime_log.h"

#include <stdio.h>
#include <string.h>

enum {
    SCREENSHOT_WIDTH = 320,
    SCREENSHOT_HEIGHT = 320,
    SCREENSHOT_ROW_BYTES = SCREENSHOT_WIDTH * 3,
    SCREENSHOT_ROW_PADDING = (4 - (SCREENSHOT_ROW_BYTES & 3)) & 3,
};

static void screenshot_storage_basename_stem(char *dst, unsigned dst_size, const char *path)
{
    const char *base = path;
    const char *slash;
    const char *dot;
    size_t stem_len;

    if (!dst || dst_size == 0u) {
        return;
    }

    dst[0] = '\0';
    if (!path || path[0] == '\0') {
        snprintf(dst, dst_size, "NESCO");
        return;
    }

    slash = strrchr(path, '/');
    if (slash && slash[1] != '\0') {
        base = slash + 1;
    }

    dot = strrchr(base, '.');
    stem_len = dot ? (size_t)(dot - base) : strlen(base);
    if (stem_len == 0u) {
        snprintf(dst, dst_size, "NESCO");
        return;
    }
    if (stem_len >= dst_size) {
        stem_len = dst_size - 1u;
    }
    memcpy(dst, base, stem_len);
    dst[stem_len] = '\0';
}

static void screenshot_storage_bgr24_from_rgb565(BYTE dst[3], WORD pixel)
{
    unsigned red = (unsigned)((pixel >> 11) & 0x1Fu);
    unsigned green = (unsigned)((pixel >> 5) & 0x3Fu);
    unsigned blue = (unsigned)(pixel & 0x1Fu);

    dst[2] = (BYTE)((red << 3) | (red >> 2));
    dst[1] = (BYTE)((green << 2) | (green >> 4));
    dst[0] = (BYTE)((blue << 3) | (blue >> 2));
}

bool storage_build_screenshot_path(char *path, unsigned path_size)
{
    char stem[48];
    const char *rom_path;

    rom_path = rom_image_get_selected_path();
    if (!rom_path || rom_path[0] == '\0') {
        rom_path = rom_image_get_flash_source_path();
    }
    if (!rom_path || rom_path[0] == '\0') {
        rom_path = rom_image_get_selected_path();
    }
    screenshot_storage_basename_stem(stem, sizeof(stem), rom_path);
    return storage_build_screenshot_path_for_stem(path, path_size, stem);
}

bool storage_build_screenshot_path_for_stem(char *path, unsigned path_size, const char *stem)
{
    const char *effective_stem = stem;
    FILINFO fno;
    FRESULT fr;
    unsigned index;

    if (!path || path_size == 0u) {
        return false;
    }

    path[0] = '\0';
    if (!effective_stem || effective_stem[0] == '\0') {
        effective_stem = "NESCO";
    }

    if (!rom_image_ensure_sd_mount()) {
        return false;
    }
    fr = f_mkdir("0:/screenshots");
    if (fr != FR_OK && fr != FR_EXIST) {
        return false;
    }

    for (index = 1u; index <= 9999u; ++index) {
        snprintf(path, path_size, "0:/screenshots/%s_%04u.BMP", effective_stem, index);
        fr = f_stat(path, &fno);
        if (fr == FR_NO_FILE || fr == FR_NO_PATH) {
            return true;
        }
    }

    snprintf(path, path_size, "0:/screenshots/%s.BMP", effective_stem);
    return true;
}

bool storage_save_screenshot_bmp(const char *path,
                                 int width,
                                 int height,
                                 const WORD *rgb565_pixels)
{
    FIL file;
    BYTE file_header[14];
    BYTE info_header[40];
    BYTE row_buf[SCREENSHOT_ROW_BYTES + SCREENSHOT_ROW_PADDING];
    UINT written = 0;
    FRESULT fr;
    unsigned row_stride;
    unsigned image_size;
    unsigned file_size;
    int src_y;
    int x;

    if (!path || !rgb565_pixels || width != SCREENSHOT_WIDTH || height != SCREENSHOT_HEIGHT) {
        return false;
    }

    row_stride = SCREENSHOT_ROW_BYTES + SCREENSHOT_ROW_PADDING;
    image_size = row_stride * (unsigned)height;
    file_size = (unsigned)(sizeof(file_header) + sizeof(info_header)) + image_size;

    memset(file_header, 0, sizeof(file_header));
    memset(info_header, 0, sizeof(info_header));

    file_header[0] = 'B';
    file_header[1] = 'M';
    file_header[2] = (BYTE)(file_size & 0xFFu);
    file_header[3] = (BYTE)((file_size >> 8) & 0xFFu);
    file_header[4] = (BYTE)((file_size >> 16) & 0xFFu);
    file_header[5] = (BYTE)((file_size >> 24) & 0xFFu);
    file_header[10] = (BYTE)((sizeof(file_header) + sizeof(info_header)) & 0xFFu);

    info_header[0] = 40u;
    info_header[4] = (BYTE)(width & 0xFFu);
    info_header[5] = (BYTE)((width >> 8) & 0xFFu);
    info_header[8] = (BYTE)(height & 0xFFu);
    info_header[9] = (BYTE)((height >> 8) & 0xFFu);
    info_header[12] = 1u;
    info_header[14] = 24u;
    info_header[20] = (BYTE)(image_size & 0xFFu);
    info_header[21] = (BYTE)((image_size >> 8) & 0xFFu);
    info_header[22] = (BYTE)((image_size >> 16) & 0xFFu);
    info_header[23] = (BYTE)((image_size >> 24) & 0xFFu);

    fr = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        return false;
    }

    fr = f_write(&file, file_header, sizeof(file_header), &written);
    if (fr != FR_OK || written != sizeof(file_header)) {
        f_close(&file);
        return false;
    }

    fr = f_write(&file, info_header, sizeof(info_header), &written);
    if (fr != FR_OK || written != sizeof(info_header)) {
        f_close(&file);
        return false;
    }

    for (src_y = height - 1; src_y >= 0; --src_y) {
        for (x = 0; x < width; ++x) {
            screenshot_storage_bgr24_from_rgb565(&row_buf[x * 3], rgb565_pixels[src_y * width + x]);
        }
        memset(&row_buf[width * 3], 0, SCREENSHOT_ROW_PADDING);
        fr = f_write(&file, row_buf, row_stride, &written);
        if (fr != FR_OK || written != row_stride) {
            f_close(&file);
            return false;
        }
    }

    fr = f_close(&file);
    if (fr != FR_OK) {
        return false;
    }

    NESCO_LOGF("[SS] saved bmp path=%s bytes=%u\r\n", path, file_size);
    return true;
}

bool storage_open_screenshot_bmp_writer(screenshot_bmp_writer_t *writer,
                                        const char *path,
                                        int width,
                                        int height)
{
    BYTE file_header[14];
    BYTE info_header[40];
    UINT written = 0;
    FRESULT fr;
    unsigned row_stride;
    unsigned image_size;
    unsigned file_size;
    unsigned neg_height;

    if (!writer || !path || width <= 0 || height <= 0 || width > SCREENSHOT_WIDTH) {
        return false;
    }

    memset(writer, 0, sizeof(*writer));
    row_stride = (unsigned)(width * 3);
    row_stride += (4u - (row_stride & 3u)) & 3u;
    image_size = row_stride * (unsigned)height;
    file_size = (unsigned)(sizeof(file_header) + sizeof(info_header)) + image_size;
    neg_height = (unsigned)(0u - (unsigned)height);

    memset(file_header, 0, sizeof(file_header));
    memset(info_header, 0, sizeof(info_header));

    file_header[0] = 'B';
    file_header[1] = 'M';
    file_header[2] = (BYTE)(file_size & 0xFFu);
    file_header[3] = (BYTE)((file_size >> 8) & 0xFFu);
    file_header[4] = (BYTE)((file_size >> 16) & 0xFFu);
    file_header[5] = (BYTE)((file_size >> 24) & 0xFFu);
    file_header[10] = (BYTE)((sizeof(file_header) + sizeof(info_header)) & 0xFFu);

    info_header[0] = 40u;
    info_header[4] = (BYTE)(width & 0xFFu);
    info_header[5] = (BYTE)((width >> 8) & 0xFFu);
    info_header[8] = (BYTE)(neg_height & 0xFFu);
    info_header[9] = (BYTE)((neg_height >> 8) & 0xFFu);
    info_header[10] = (BYTE)((neg_height >> 16) & 0xFFu);
    info_header[11] = (BYTE)((neg_height >> 24) & 0xFFu);
    info_header[12] = 1u;
    info_header[14] = 24u;
    info_header[20] = (BYTE)(image_size & 0xFFu);
    info_header[21] = (BYTE)((image_size >> 8) & 0xFFu);
    info_header[22] = (BYTE)((image_size >> 16) & 0xFFu);
    info_header[23] = (BYTE)((image_size >> 24) & 0xFFu);

    fr = f_open(&writer->file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        return false;
    }

    fr = f_write(&writer->file, file_header, sizeof(file_header), &written);
    if (fr != FR_OK || written != sizeof(file_header)) {
        f_close(&writer->file);
        return false;
    }

    fr = f_write(&writer->file, info_header, sizeof(info_header), &written);
    if (fr != FR_OK || written != sizeof(info_header)) {
        f_close(&writer->file);
        return false;
    }

    writer->width = width;
    writer->height = height;
    writer->row_stride = row_stride;
    writer->open = true;
    return true;
}

bool storage_write_screenshot_bmp_row_rgb565(screenshot_bmp_writer_t *writer,
                                             const WORD *rgb565_pixels,
                                             int width)
{
    UINT written = 0;
    FRESULT fr;
    int x;

    if (!writer || !writer->open || !rgb565_pixels || width != writer->width) {
        return false;
    }

    for (x = 0; x < width; ++x) {
        screenshot_storage_bgr24_from_rgb565(&writer->row_buf[x * 3], rgb565_pixels[x]);
    }
    memset(&writer->row_buf[width * 3], 0, writer->row_stride - (unsigned)(width * 3));

    fr = f_write(&writer->file, writer->row_buf, writer->row_stride, &written);
    return fr == FR_OK && written == writer->row_stride;
}

bool storage_write_screenshot_bmp_rows_rgb565(screenshot_bmp_writer_t *writer,
                                              const WORD *rgb565_pixels,
                                              int width,
                                              int rows,
                                              BYTE *chunk_buf,
                                              unsigned chunk_buf_size)
{
    UINT written = 0;
    FRESULT fr;
    unsigned required_size;
    int row;
    int x;

    if (!writer || !writer->open || !rgb565_pixels || !chunk_buf || width != writer->width || rows <= 0) {
        return false;
    }

    required_size = writer->row_stride * (unsigned)rows;
    if (chunk_buf_size < required_size) {
        return false;
    }

    for (row = 0; row < rows; ++row) {
        BYTE *dst_row = &chunk_buf[row * writer->row_stride];
        const WORD *src_row = &rgb565_pixels[row * width];

        for (x = 0; x < width; ++x) {
            screenshot_storage_bgr24_from_rgb565(&dst_row[x * 3], src_row[x]);
        }
        memset(&dst_row[width * 3], 0, writer->row_stride - (unsigned)(width * 3));
    }

    fr = f_write(&writer->file, chunk_buf, required_size, &written);
    return fr == FR_OK && written == required_size;
}

bool storage_close_screenshot_bmp_writer(screenshot_bmp_writer_t *writer)
{
    FRESULT fr;

    if (!writer || !writer->open) {
        return false;
    }

    fr = f_close(&writer->file);
    writer->open = false;
    return fr == FR_OK;
}
