#include "screenshot_viewer.h"

#include "ff.h"
#include "rom_image.h"

#include <stdio.h>
#include <string.h>

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
