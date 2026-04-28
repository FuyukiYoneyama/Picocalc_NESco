#include "screenshot_viewer.h"

#include "display.h"
#include "ff.h"
#include "InfoNES_Types.h"
#include "rom_image.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern void lcd_set_window(int x, int y, int w, int h);
extern void lcd_dma_write_rgb565_async(const WORD *buf, int n_pixels);
extern void lcd_dma_wait(void);

enum {
    SCREENSHOT_VIEWER_BMP_WIDTH = 320,
    SCREENSHOT_VIEWER_BMP_HEIGHT = 320,
    SCREENSHOT_VIEWER_BMP_HEADER_SIZE = 54,
    SCREENSHOT_VIEWER_DIB_HEADER_SIZE = 40,
};

static int screenshot_viewer_has_bmp_extension(const char *name)
{
    const char *dot;

    if (!name) {
        return 0;
    }

    dot = strrchr(name, '.');
    if (!dot) {
        return 0;
    }

    return (dot[0] == '.') &&
           (dot[1] == 'B' || dot[1] == 'b') &&
           (dot[2] == 'M' || dot[2] == 'm') &&
           (dot[3] == 'P' || dot[3] == 'p') &&
           dot[4] == '\0';
}

static uint16_t screenshot_viewer_le16(const BYTE *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t screenshot_viewer_le32(const BYTE *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t screenshot_viewer_le32s(const BYTE *p)
{
    return (int32_t)screenshot_viewer_le32(p);
}

static void screenshot_viewer_set_status(char *status, unsigned status_size, const char *text)
{
    if (status && status_size > 0u) {
        snprintf(status, status_size, "%s", text ? text : "");
    }
}

static WORD screenshot_viewer_bgr_to_rgb565(const BYTE *bgr)
{
    BYTE b = bgr[0];
    BYTE g = bgr[1];
    BYTE r = bgr[2];

    return (WORD)(((WORD)(r & 0xF8u) << 8) |
                  ((WORD)(g & 0xFCu) << 3) |
                  ((WORD)b >> 3));
}

int screenshot_viewer_load_entries(screenshot_viewer_entry_t *entries,
                                   int max_entries,
                                   char *status,
                                   unsigned status_size)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    int count = 0;

    if (status && status_size > 0u) {
        status[0] = '\0';
    }

    if (!entries || max_entries <= 0) {
        if (status && status_size > 0u) {
            snprintf(status, status_size, "NO MEMORY");
        }
        return 0;
    }

    if (!rom_image_ensure_sd_mount()) {
        if (status && status_size > 0u) {
            snprintf(status, status_size, "SD MOUNT FAILED");
        }
        return 0;
    }

    fr = f_opendir(&dir, "0:/screenshots");
    if (fr != FR_OK) {
        if (status && status_size > 0u) {
            snprintf(status, status_size, "SCREENSHOT DIR FAILED %d", (int)fr);
        }
        return 0;
    }

    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == '\0') {
            break;
        }
        if (fno.fattrib & AM_DIR) {
            continue;
        }
        if (!screenshot_viewer_has_bmp_extension(fno.fname)) {
            continue;
        }
        if (count >= max_entries) {
            continue;
        }
        if (snprintf(entries[count].name, sizeof(entries[count].name), "%s", fno.fname) >=
            (int)sizeof(entries[count].name)) {
            continue;
        }
        if (snprintf(entries[count].path, sizeof(entries[count].path), "0:/screenshots/%s", fno.fname) >=
            (int)sizeof(entries[count].path)) {
            continue;
        }
        count++;
    }

    f_closedir(&dir);

    if (status && status_size > 0u) {
        if (fr != FR_OK) {
            snprintf(status, status_size, "SCREENSHOT READ FAILED %d", (int)fr);
        } else if (count == 0) {
            snprintf(status, status_size, "NO SCREENSHOTS");
        } else {
            snprintf(status, status_size, "%d SCREENSHOTS", count);
        }
    }

    return count;
}

int screenshot_viewer_show_bmp(const char *path, char *status, unsigned status_size)
{
    FIL file;
    FRESULT fr;
    BYTE header[SCREENSHOT_VIEWER_BMP_HEADER_SIZE];
    UINT read_bytes = 0;
    uint32_t pixel_offset;
    uint32_t dib_size;
    int32_t width;
    int32_t height;
    int32_t abs_height;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t compression;
    uint32_t row_stride;
    uint64_t pixel_end;
    BYTE *row = NULL;
    WORD line[SCREENSHOT_VIEWER_BMP_WIDTH];
    int ok = 0;

    screenshot_viewer_set_status(status, status_size, "");

    if (!path || !*path) {
        screenshot_viewer_set_status(status, status_size, "READ FAILED");
        return 0;
    }

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        screenshot_viewer_set_status(status, status_size, "READ FAILED");
        return 0;
    }

    fr = f_read(&file, header, sizeof(header), &read_bytes);
    if (fr != FR_OK || read_bytes != sizeof(header)) {
        screenshot_viewer_set_status(status, status_size, "READ FAILED");
        goto done;
    }

    if (header[0] != 'B' || header[1] != 'M') {
        screenshot_viewer_set_status(status, status_size, "UNSUPPORTED BMP");
        goto done;
    }

    pixel_offset = screenshot_viewer_le32(&header[10]);
    dib_size = screenshot_viewer_le32(&header[14]);
    width = screenshot_viewer_le32s(&header[18]);
    height = screenshot_viewer_le32s(&header[22]);
    planes = screenshot_viewer_le16(&header[26]);
    bit_count = screenshot_viewer_le16(&header[28]);
    compression = screenshot_viewer_le32(&header[30]);

    if (height == INT32_MIN) {
        screenshot_viewer_set_status(status, status_size, "UNSUPPORTED BMP");
        goto done;
    }
    abs_height = (height < 0) ? -height : height;

    if (dib_size != SCREENSHOT_VIEWER_DIB_HEADER_SIZE ||
        width != SCREENSHOT_VIEWER_BMP_WIDTH ||
        abs_height != SCREENSHOT_VIEWER_BMP_HEIGHT ||
        planes != 1u ||
        bit_count != 24u ||
        compression != 0u) {
        screenshot_viewer_set_status(status, status_size, "UNSUPPORTED BMP");
        goto done;
    }

    row_stride = (uint32_t)(((SCREENSHOT_VIEWER_BMP_WIDTH * 3) + 3) & ~3);
    pixel_end = (uint64_t)pixel_offset + (uint64_t)row_stride * (uint64_t)abs_height;
    if (pixel_offset < SCREENSHOT_VIEWER_BMP_HEADER_SIZE || pixel_end > (uint64_t)f_size(&file)) {
        screenshot_viewer_set_status(status, status_size, "UNSUPPORTED BMP");
        goto done;
    }

    row = (BYTE *)malloc(row_stride);
    if (!row) {
        screenshot_viewer_set_status(status, status_size, "NO MEMORY");
        goto done;
    }

    display_set_mode(DISPLAY_MODE_FULLSCREEN);
    lcd_dma_wait();
    lcd_set_window(0, 0, SCREENSHOT_VIEWER_BMP_WIDTH, SCREENSHOT_VIEWER_BMP_HEIGHT);

    for (int y = 0; y < SCREENSHOT_VIEWER_BMP_HEIGHT; y++) {
        uint32_t source_y = (height < 0) ? (uint32_t)y : (uint32_t)(SCREENSHOT_VIEWER_BMP_HEIGHT - 1 - y);
        uint32_t row_offset = pixel_offset + source_y * row_stride;

        fr = f_lseek(&file, row_offset);
        if (fr != FR_OK) {
            screenshot_viewer_set_status(status, status_size, "READ FAILED");
            goto done;
        }

        fr = f_read(&file, row, row_stride, &read_bytes);
        if (fr != FR_OK || read_bytes != row_stride) {
            screenshot_viewer_set_status(status, status_size, "READ FAILED");
            goto done;
        }

        for (int x = 0; x < SCREENSHOT_VIEWER_BMP_WIDTH; x++) {
            line[x] = screenshot_viewer_bgr_to_rgb565(&row[x * 3]);
        }
        lcd_dma_write_rgb565_async(line, SCREENSHOT_VIEWER_BMP_WIDTH);
        lcd_dma_wait();
    }

    lcd_dma_wait();
    screenshot_viewer_set_status(status, status_size, "BMP VIEWED");
    ok = 1;

done:
    free(row);
    f_close(&file);
    return ok;
}
