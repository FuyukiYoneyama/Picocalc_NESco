/*
 * rom_image.c — ROM image loader (PicoCalc / RP2040)
 *
 * Implements InfoNES_ReadRom() and InfoNES_ReleaseRom() from nes_system.h.
 *
 * ROM loading strategy:
 *   1. Flash XIP: if path is NULL or "flash:", ROM is read directly from
 *      RP2040 Flash memory at a fixed offset.
 *   2. SD card (FatFS): small Mapper 0 sized ROM files are loaded into RAM.
 *   3. Larger SD ROM files are staged into the Flash XIP ROM area, then read
 *      through the same Flash backend.
 *
 * iNES header parsing: validates magic, extracts PRG/CHR size.
 * Trainer (512 bytes): skipped if present.
 *
 * Part of Picocalc_NESco
 * MIT License
 */

#include "rom_image.h"
#include "sram_store.h"

#include "InfoNES.h"
#include "InfoNES_Mapper.h"
#include "InfoNES_System.h"
#include "runtime_log.h"

#include "ff.h"

#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#ifdef PICO_BUILD
#  include "pico/stdlib.h"
#  include "hardware/sync.h"
#  include "pico/multicore.h"
#endif

/* =====================================================================
 *  Flash XIP configuration
 *  ROM embedded at fixed offset from Flash base.
 *  Reserve one sector for persistent metadata so staged ROM info survives
 *  power cycle.
 * ===================================================================== */
#ifdef PICO_BUILD
#  include "hardware/flash.h"
#  define FLASH_BASE         0x10000000u  /* RP2040 Flash XIP base */
#  define XIP_ROM_OFFSET     0x00080000u  /* 512 KB reserved for firmware growth */
#  define XIP_ROM_METADATA_SIZE FLASH_SECTOR_SIZE
#  define XIP_ROM_DATA_OFFSET (XIP_ROM_OFFSET + XIP_ROM_METADATA_SIZE)
#  define XIP_ROM_MAX_BYTES  (PICO_FLASH_SIZE_BYTES - XIP_ROM_DATA_OFFSET)
#  define XIP_ROM_METADATA_MAGIC 0x4E455343u /* 'NESC' */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t rom_size;
    uint32_t mapper_no;
    char file_name[128];
    char source_path[160];
} xip_rom_metadata_t;
static const xip_rom_metadata_t *s_flash_meta =
    (const xip_rom_metadata_t *)(FLASH_BASE + XIP_ROM_OFFSET);
static const BYTE *s_flash_rom = (const BYTE *)(FLASH_BASE + XIP_ROM_DATA_OFFSET);
#else
static const BYTE *s_flash_rom = NULL;
#endif

/* SD-card-loaded ROM/VROM buffers */
static BYTE *s_rom_buf = NULL;
static BYTE *s_vrom_buf = NULL;

/*
 * Max iNES file size for Mapper 0:
 *   16-byte header
 *   512-byte trainer
 *   32 KB PRG-ROM
 *   8 KB CHR-ROM
 */
#define MAPPER0_ROM_IMAGE_MAX_SIZE (16u + 512u + 0x8000u + 0x2000u)

static const BYTE NES_MAGIC[4] = { 'N', 'E', 'S', 0x1A };

/* Mapper 0 RAM path uses a fixed-capacity buffer rather than per-ROM sizing. */
#define ROM_RAM_THRESHOLD_BYTES MAPPER0_ROM_IMAGE_MAX_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static rom_menu_entry_info_t s_builtin_menu_entry = {
    "BUILTIN.NES",
    "flash:/BUILTIN.NES",
    "SYSTEM FLASH",
    ROM_ENTRY_FILE,
    ROM_STORAGE_FLASH,
    1
};
static char s_flash_entry_label[128] = "BUILTIN.NES";
static char s_flash_entry_path[160] = "flash:/BUILTIN.NES";
static char s_flash_entry_detail[40] = "SYSTEM FLASH";

static FATFS s_fatfs;
static bool s_fatfs_mounted = false;
static bool s_fatfs_attempted = false;
static char s_last_status[64] = "SELECT *.NES FILE";
static char s_current_dir[128] = "0:/";
static char s_selected_path[160] = "";
static char s_last_rom_dir[128] = "";

enum {
    ROM_MENU_ENTRY_CAPACITY = 64,
};

typedef struct rom_menu_storage {
    rom_menu_entry_info_t entries[ROM_MENU_ENTRY_CAPACITY];
    char labels[ROM_MENU_ENTRY_CAPACITY][128];
    char paths[ROM_MENU_ENTRY_CAPACITY][160];
    char details[ROM_MENU_ENTRY_CAPACITY][40];
} rom_menu_storage_t;

static rom_menu_storage_t *s_menu_storage = NULL;
static rom_menu_entry_info_t *s_menu_entries = NULL;
static char (*s_menu_labels)[128] = NULL;
static char (*s_menu_paths)[160] = NULL;
static char (*s_menu_details)[40] = NULL;

static bool rom_sd_mount(void);
static void rom_copy_string(char *dst, size_t dst_size, const char *src);
static void rom_append_string(char *dst, size_t dst_size, const char *src);

#ifdef PICO_BUILD
extern char __end__;
extern char __HeapLimit;
#endif

static bool rom_has_nes_extension(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return false;
    }
    return strcasecmp(dot, ".nes") == 0;
}

static void rom_append_save_tag(char *detail, size_t detail_size, const char *rom_path) {
    if (!detail || detail_size == 0u || !rom_path) {
        return;
    }
    if (!sram_store_has_save_for_rom(rom_path)) {
        return;
    }
    if (strlen(detail) + 5u >= detail_size) {
        return;
    }
    rom_append_string(detail, detail_size, " SAVE");
}

static const char *rom_basename(const char *path) {
    const char *slash;

    if (!path || !*path) {
        return "BUILTIN.NES";
    }

    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void rom_set_flash_entry_default(void) {
    rom_copy_string(s_flash_entry_label, sizeof(s_flash_entry_label), "BUILTIN.NES");
    rom_copy_string(s_flash_entry_path, sizeof(s_flash_entry_path), "flash:/BUILTIN.NES");
    rom_copy_string(s_flash_entry_detail, sizeof(s_flash_entry_detail), "SYSTEM FLASH");
    rom_append_save_tag(s_flash_entry_detail, sizeof(s_flash_entry_detail), s_flash_entry_path);
    s_builtin_menu_entry.label = s_flash_entry_label;
    s_builtin_menu_entry.path = s_flash_entry_path;
    s_builtin_menu_entry.detail = s_flash_entry_detail;
    s_builtin_menu_entry.kind = ROM_ENTRY_FILE;
    s_builtin_menu_entry.storage = ROM_STORAGE_FLASH;
    s_builtin_menu_entry.enabled = 1;
}

static void rom_set_flash_entry_from_path(const char *source_path) {
    const char *name = rom_basename(source_path);

    rom_copy_string(s_flash_entry_label, sizeof(s_flash_entry_label), name);
    rom_copy_string(s_flash_entry_path, sizeof(s_flash_entry_path), "flash:/");
    rom_append_string(s_flash_entry_path, sizeof(s_flash_entry_path), name);
    rom_copy_string(s_flash_entry_detail, sizeof(s_flash_entry_detail), "SYSTEM FLASH");
    rom_append_save_tag(s_flash_entry_detail, sizeof(s_flash_entry_detail), s_flash_entry_path);
    s_builtin_menu_entry.label = s_flash_entry_label;
    s_builtin_menu_entry.path = s_flash_entry_path;
    s_builtin_menu_entry.detail = s_flash_entry_detail;
    s_builtin_menu_entry.kind = ROM_ENTRY_FILE;
    s_builtin_menu_entry.storage = ROM_STORAGE_FLASH;
    s_builtin_menu_entry.enabled = 1;
}

#ifdef PICO_BUILD
static bool rom_flash_metadata_is_valid(void) {
    return s_flash_meta->magic == XIP_ROM_METADATA_MAGIC &&
           (s_flash_meta->version == 1u || s_flash_meta->version == 2u) &&
           s_flash_meta->file_name[0] != '\0';
}

static void rom_restore_flash_entry_from_metadata(void) {
    if (!rom_flash_metadata_is_valid() ||
        memcmp(s_flash_rom, NES_MAGIC, sizeof(NES_MAGIC)) != 0) {
        return;
    }

    rom_copy_string(s_flash_entry_label, sizeof(s_flash_entry_label), s_flash_meta->file_name);
    rom_copy_string(s_flash_entry_path, sizeof(s_flash_entry_path), "flash:/");
    rom_append_string(s_flash_entry_path, sizeof(s_flash_entry_path), s_flash_meta->file_name);
    snprintf(s_flash_entry_detail, sizeof(s_flash_entry_detail),
             "M%lu FLASH %lu KB",
             (unsigned long)s_flash_meta->mapper_no,
             (unsigned long)((s_flash_meta->rom_size + 1023u) / 1024u));
    rom_append_save_tag(s_flash_entry_detail, sizeof(s_flash_entry_detail), s_flash_entry_path);
    s_builtin_menu_entry.label = s_flash_entry_label;
    s_builtin_menu_entry.path = s_flash_entry_path;
    s_builtin_menu_entry.detail = s_flash_entry_detail;
    s_builtin_menu_entry.kind = ROM_ENTRY_FILE;
    s_builtin_menu_entry.storage = ROM_STORAGE_FLASH;
    s_builtin_menu_entry.enabled = 1;
}
#endif

void rom_image_log_heap(const char *tag) {
#if defined(NESCO_RUNTIME_LOGS)
    struct mallinfo mi = mallinfo();
    NESCO_LOG_RUNTIME("[ROM] heap %s arena=%lu used=%lu free=%lu keep=%lu\r\n",
           tag ? tag : "(null)",
           (unsigned long)mi.arena,
           (unsigned long)mi.uordblks,
           (unsigned long)mi.fordblks,
           (unsigned long)mi.keepcost);
#else
    (void)tag;
#endif
}

void rom_image_log_heap_estimate(const char *tag) {
#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
    const uintptr_t heap_start = (uintptr_t)&__end__;
    const uintptr_t heap_limit = (uintptr_t)&__HeapLimit;
    uintptr_t reserved_limit = heap_start + (uintptr_t)PICO_HEAP_SIZE;
    const unsigned long heap_gap_bytes =
        (heap_limit > heap_start) ? (unsigned long)(heap_limit - heap_start) : 0ul;
    if (reserved_limit > heap_limit) {
        reserved_limit = heap_limit;
    }

    NESCO_LOG_RUNTIME("[ROM] heap-est %s start=0x%08lx reserve_end=0x%08lx limit=0x%08lx reserve=%lu gap=%lu\r\n",
           tag ? tag : "(null)",
           (unsigned long)heap_start,
           (unsigned long)reserved_limit,
           (unsigned long)heap_limit,
           (unsigned long)(reserved_limit - heap_start),
           heap_gap_bytes);
#else
    (void)tag;
#endif
}

static void rom_set_statusf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_last_status, sizeof(s_last_status), fmt, ap);
    va_end(ap);
}

static bool rom_is_root_dir(void) {
    return strcmp(s_current_dir, "0:/") == 0;
}

static void rom_set_directory(const char *path) {
    snprintf(s_current_dir, sizeof(s_current_dir), "%s", path ? path : "0:/");
}

static void rom_copy_string(char *dst, size_t dst_size, const char *src) {
    size_t copy_len;

    if (!dst || dst_size == 0u) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    copy_len = strlen(src);
    if (copy_len >= dst_size) {
        copy_len = dst_size - 1u;
    }
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

static void rom_append_string(char *dst, size_t dst_size, const char *src) {
    size_t pos;

    if (!dst || dst_size == 0u) {
        return;
    }

    pos = strlen(dst);
    if (pos >= dst_size) {
        dst[dst_size - 1u] = '\0';
        return;
    }
    rom_copy_string(dst + pos, dst_size - pos, src);
}

static void rom_path_join(char *dst, size_t dst_size, const char *base, const char *name) {
    size_t base_len;
    size_t name_len;
    size_t pos = 0u;

    if (!dst || dst_size == 0u) {
        return;
    }

    dst[0] = '\0';
    if (!base) {
        base = "";
    }
    if (!name) {
        name = "";
    }

    base_len = strlen(base);
    name_len = strlen(name);

    if (base_len >= dst_size) {
        base_len = dst_size - 1u;
    }
    memcpy(dst, base, base_len);
    pos = base_len;
    dst[pos] = '\0';

    if (pos + 1u < dst_size && pos > 0u && dst[pos - 1u] != '/') {
        dst[pos++] = '/';
        dst[pos] = '\0';
    }

    if (pos < dst_size) {
        size_t room = dst_size - pos - 1u;
        if (name_len > room) {
            name_len = room;
        }
        memcpy(dst + pos, name, name_len);
        dst[pos + name_len] = '\0';
    }
}

static void rom_make_sd_path(char *dst, size_t dst_size, const char *dir_path, const char *name) {
    char joined[160];

    rom_path_join(joined, sizeof(joined), dir_path, name);
    if (strncmp(joined, "0:/", 3) == 0) {
        rom_copy_string(dst, dst_size, "sd:/");
        rom_append_string(dst, dst_size, joined + 3);
    } else {
        rom_copy_string(dst, dst_size, "sd:/");
        rom_append_string(dst, dst_size, joined);
    }
}

static void rom_release_mapper0_buffer(void) {
    free(s_rom_buf);
    s_rom_buf = NULL;
}

static void rom_menu_storage_release(void) {
    free(s_menu_storage);
    s_menu_storage = NULL;
    s_menu_entries = NULL;
    s_menu_labels = NULL;
    s_menu_paths = NULL;
    s_menu_details = NULL;
}

static bool rom_menu_storage_prepare(void) {
    if (s_menu_entries && s_menu_labels && s_menu_paths && s_menu_details) {
        return true;
    }

    rom_menu_storage_release();

    s_menu_storage = (rom_menu_storage_t *)malloc(sizeof(*s_menu_storage));
    if (!s_menu_storage) {
        rom_set_statusf("Menu alloc failed");
        return false;
    }

    s_menu_entries = s_menu_storage->entries;
    s_menu_labels = s_menu_storage->labels;
    s_menu_paths = s_menu_storage->paths;
    s_menu_details = s_menu_storage->details;

    memset(s_menu_storage, 0, sizeof(*s_menu_storage));
    return true;
}

static bool rom_path_exists(const char *path) {
    FILINFO fno;
    return path && f_stat(path, &fno) == FR_OK;
}

static void rom_extract_selected_dir(const char *path, char *dst, size_t dst_size) {
    char tmp[160];
    char *slash;

    if (!dst || dst_size == 0u) {
        return;
    }

    dst[0] = '\0';
    if (!path || strncmp(path, "sd:/", 4) != 0) {
        return;
    }

    snprintf(tmp, sizeof(tmp), "0:/%s", path + 4);
    slash = strrchr(tmp, '/');
    if (!slash || slash <= tmp + 1) {
        rom_copy_string(dst, dst_size, "0:/");
        return;
    }

    *slash = '\0';
    if (strcmp(tmp, "0:") == 0) {
        rom_copy_string(dst, dst_size, "0:/");
    } else {
        rom_copy_string(dst, dst_size, tmp);
    }
}

void rom_image_set_selected_path(const char *path) {
    rom_copy_string(s_selected_path, sizeof(s_selected_path), path);
    rom_extract_selected_dir(path, s_last_rom_dir, sizeof(s_last_rom_dir));
}

const char *rom_image_get_selected_path(void) {
    if (s_selected_path[0] == '\0') {
        return NULL;
    }
    return s_selected_path;
}

void rom_image_clear_selected_path(void) {
    s_selected_path[0] = '\0';
    s_last_rom_dir[0] = '\0';
}

bool rom_image_menu_begin(void) {
    if (!rom_sd_mount()) {
        rom_set_directory("0:/");
        rom_set_statusf("SD mount error");
        return false;
    }

    rom_release_mapper0_buffer();
    rom_image_log_heap("before menu alloc");

    if (!rom_menu_storage_prepare()) {
        return false;
    }
    rom_image_log_heap("after menu alloc");

    if (s_last_rom_dir[0] != '\0' && rom_path_exists(s_last_rom_dir)) {
        rom_set_directory(s_last_rom_dir);
    } else if (rom_path_exists("0:/nes")) {
        rom_set_directory("0:/nes");
    } else {
        rom_set_directory("0:/");
    }

    rom_set_statusf("DIR %s", rom_is_root_dir() ? "/" : s_current_dir + 2);
    return true;
}

void rom_image_menu_end(void) {
    rom_menu_storage_release();
}

static bool rom_sd_mount(void) {
    FRESULT fr;

    if (s_fatfs_mounted) {
        NESCO_LOG_RUNTIME("[ROM] SD already mounted\r\n");
        rom_set_statusf("SD mount ok");
        return true;
    }
    if (s_fatfs_attempted) {
        NESCO_LOG_RUNTIME("[ROM] SD mount previously failed\r\n");
        return false;
    }
    s_fatfs_attempted = true;

    NESCO_LOG_RUNTIME("[ROM] mounting SD as 0:/\r\n");
    fr = f_mount(&s_fatfs, "0:", 1);
    if (fr != FR_OK) {
        NESCO_LOG_RUNTIME("[ROM] f_mount failed: %d\r\n", (int)fr);
        rom_set_statusf("SD mount error (%d)", (int)fr);
        return false;
    }

    s_fatfs_mounted = true;
    NESCO_LOG_RUNTIME("[ROM] SD mount ok\r\n");
    rom_set_statusf("SD mount ok");
    return true;
}

static int rom_probe_mapper_number(const char *fatfs_path) {
    FIL file;
    BYTE header_buf[sizeof(struct NesHeader_tag)];
    UINT bytes_read = 0;

    if (f_open(&file, fatfs_path, FA_READ) != FR_OK) {
        return -1;
    }
    if (f_read(&file, header_buf, sizeof(header_buf), &bytes_read) != FR_OK ||
        bytes_read != sizeof(header_buf)) {
        f_close(&file);
        return -1;
    }
    f_close(&file);

    if (memcmp(header_buf, NES_MAGIC, sizeof(NES_MAGIC)) != 0) {
        return -1;
    }

    return (int)((header_buf[6] >> 4) | (header_buf[7] & 0xF0u));
}

static int rom_scan_sd_menu_entries(rom_menu_entry_info_t *out_entries, int max_entries, int start_index) {
    DIR dir;
    FILINFO fno;
    int count = start_index;

    if (!out_entries || max_entries <= start_index || !rom_sd_mount()) {
        NESCO_LOG_RUNTIME("[ROM] skip SD scan\r\n");
        return start_index;
    }

    NESCO_LOG_RUNTIME("[ROM] scanning %s\r\n", s_current_dir);
    if (f_opendir(&dir, s_current_dir) != FR_OK) {
        NESCO_LOG_RUNTIME("[ROM] f_opendir failed for %s\r\n", s_current_dir);
        return start_index;
    }

    if (!rom_is_root_dir() && count < max_entries && count < ROM_MENU_ENTRY_CAPACITY) {
        snprintf(s_menu_labels[count], sizeof(s_menu_labels[count]), "[..]");
        snprintf(s_menu_paths[count], sizeof(s_menu_paths[count]), "%s", s_current_dir);
        snprintf(s_menu_details[count], sizeof(s_menu_details[count]), "PARENT DIRECTORY");
        s_menu_entries[count].label = s_menu_labels[count];
        s_menu_entries[count].path = s_menu_paths[count];
        s_menu_entries[count].detail = s_menu_details[count];
        s_menu_entries[count].kind = ROM_ENTRY_PARENT;
        s_menu_entries[count].storage = ROM_STORAGE_UNKNOWN;
        s_menu_entries[count].enabled = 1;
        out_entries[count] = s_menu_entries[count];
        count++;
    }

    for (;;) {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == '\0') {
            break;
        }
        NESCO_LOG_RUNTIME("[ROM] dir entry: %s size=%lu attr=%02X\r\n",
               fno.fname,
               (unsigned long)fno.fsize,
               (unsigned int)fno.fattrib);
        if ((fno.fattrib & AM_DIR) != 0) {
            if (count >= max_entries || count >= ROM_MENU_ENTRY_CAPACITY) {
                NESCO_LOG_RUNTIME("[ROM] menu entry buffer full\r\n");
                break;
            }
            rom_copy_string(s_menu_labels[count], sizeof(s_menu_labels[count]), "[");
            rom_append_string(s_menu_labels[count], sizeof(s_menu_labels[count]), fno.fname);
            rom_append_string(s_menu_labels[count], sizeof(s_menu_labels[count]), "]");
            rom_path_join(s_menu_paths[count], sizeof(s_menu_paths[count]), s_current_dir, fno.fname);
            snprintf(s_menu_details[count], sizeof(s_menu_details[count]), "DIRECTORY");
            s_menu_entries[count].label = s_menu_labels[count];
            s_menu_entries[count].path = s_menu_paths[count];
            s_menu_entries[count].detail = s_menu_details[count];
            s_menu_entries[count].kind = ROM_ENTRY_DIRECTORY;
            s_menu_entries[count].storage = ROM_STORAGE_UNKNOWN;
            s_menu_entries[count].enabled = 1;
            out_entries[count] = s_menu_entries[count];
            NESCO_LOG_RUNTIME("[ROM] add dir entry: %s path=%s\r\n",
                   s_menu_entries[count].label,
                   s_menu_entries[count].path);
            count++;
            continue;
        }
        if (!rom_has_nes_extension(fno.fname)) {
            continue;
        }
        if (count >= max_entries || count >= ROM_MENU_ENTRY_CAPACITY) {
            NESCO_LOG_RUNTIME("[ROM] menu entry buffer full\r\n");
            break;
        }

        char fatfs_path[160];
        int mapper_no;

        rom_copy_string(s_menu_labels[count], sizeof(s_menu_labels[count]), fno.fname);
        rom_make_sd_path(s_menu_paths[count], sizeof(s_menu_paths[count]), s_current_dir, fno.fname);
        rom_path_join(fatfs_path, sizeof(fatfs_path), s_current_dir, fno.fname);
        mapper_no = rom_probe_mapper_number(fatfs_path);

        s_menu_entries[count].label = s_menu_labels[count];
        s_menu_entries[count].path = s_menu_paths[count];
        s_menu_entries[count].kind = ROM_ENTRY_FILE;
        if (mapper_no == 0 && fno.fsize < ROM_RAM_THRESHOLD_BYTES) {
            snprintf(s_menu_details[count], sizeof(s_menu_details[count]),
                     "M0 RAM %lu KB", (unsigned long)((fno.fsize + 1023u) / 1024u));
            s_menu_entries[count].storage = ROM_STORAGE_RAM;
            s_menu_entries[count].enabled = 1;
        } else {
            s_menu_entries[count].storage = ROM_STORAGE_FLASH;
            s_menu_entries[count].enabled = (mapper_no >= 0) ? 1 : 0;
            if (mapper_no < 0) {
                snprintf(s_menu_details[count], sizeof(s_menu_details[count]), "FLASH HEADER ERR");
            } else {
                snprintf(s_menu_details[count], sizeof(s_menu_details[count]),
                         "M%d FLASH %lu KB", mapper_no,
                         (unsigned long)((fno.fsize + 1023u) / 1024u));
            }
        }
        rom_append_save_tag(s_menu_details[count], sizeof(s_menu_details[count]), s_menu_paths[count]);
        s_menu_entries[count].detail = s_menu_details[count];
        out_entries[count] = s_menu_entries[count];
        NESCO_LOG_RUNTIME("[ROM] add menu entry: %s path=%s mapper=%d mode=%s enabled=%u\r\n",
               s_menu_entries[count].label,
               s_menu_entries[count].path,
               mapper_no,
               (s_menu_entries[count].storage == ROM_STORAGE_RAM) ? "RAM" :
               (s_menu_entries[count].storage == ROM_STORAGE_FLASH) ? "FLASH" : "UNSUPPORTED",
               (unsigned int)s_menu_entries[count].enabled);
        count++;
    }

    f_closedir(&dir);
    NESCO_LOG_RUNTIME("[ROM] scan complete, %d entries total\r\n", count);
    if (count == start_index) {
        rom_set_statusf("No *.nes files on SD");
    } else {
        rom_set_statusf("DIR %s", rom_is_root_dir() ? "/" : s_current_dir + 2);
    }
    return count;
}

void rom_image_init(void) {
    s_rom_buf = NULL;
    s_vrom_buf = NULL;
    s_fatfs_mounted = false;
    s_fatfs_attempted = false;
    s_last_rom_dir[0] = '\0';
    rom_menu_storage_release();
    rom_set_flash_entry_default();
#ifdef PICO_BUILD
    rom_restore_flash_entry_from_metadata();
#endif
    rom_set_directory("0:/");
    rom_set_statusf("SELECT *.NES FILE");
}

const char *rom_image_last_status(void) {
    return s_last_status;
}

const char *rom_image_current_directory(void) {
    return s_current_dir;
}

bool rom_image_enter_directory(const char *path) {
    char next_dir[128];
    size_t len;

    if (!path || !*path) {
        return false;
    }

    if (strcmp(path, s_current_dir) == 0) {
        if (rom_is_root_dir()) {
            return true;
        }
        snprintf(next_dir, sizeof(next_dir), "%s", s_current_dir);
        len = strlen(next_dir);
        if (len > 0 && next_dir[len - 1] == '/') {
            next_dir[len - 1] = '\0';
        }
        char *slash = strrchr(next_dir, '/');
        if (slash && slash > next_dir + 1) {
            *slash = '\0';
        } else {
            snprintf(next_dir, sizeof(next_dir), "0:/");
        }
        if (strcmp(next_dir, "0:") == 0) {
            snprintf(next_dir, sizeof(next_dir), "0:/");
        }
        rom_set_directory(next_dir);
        rom_set_statusf("DIR %s", rom_is_root_dir() ? "/" : s_current_dir + 2);
        NESCO_LOG_RUNTIME("[ROM] cd parent -> %s\r\n", s_current_dir);
        return true;
    }

    rom_set_directory(path);
    rom_set_statusf("DIR %s", rom_is_root_dir() ? "/" : s_current_dir + 2);
    NESCO_LOG_RUNTIME("[ROM] cd %s\r\n", s_current_dir);
    return true;
}

int rom_image_menu_entries(rom_menu_entry_info_t *out_entries, int max_entries) {
    int count = 0;

    if (!out_entries || max_entries <= 0) {
        return 0;
    }
    if (!s_menu_entries || !s_menu_labels || !s_menu_paths || !s_menu_details) {
        rom_set_statusf("Menu not ready");
        return 0;
    }

    if (count < max_entries) {
        out_entries[count++] = s_builtin_menu_entry;
    }

    count = rom_scan_sd_menu_entries(out_entries, max_entries, count);

    return count;
}

#ifdef PICO_BUILD
static int rom_verify_flash_against_file(FIL *file, FSIZE_t file_size) {
    BYTE page_buf[FLASH_PAGE_SIZE];
    UINT bytes_read = 0;
    uint32_t verified = 0;

    if (f_lseek(file, 0) != FR_OK) {
        NESCO_LOG_RUNTIME("[ROM] flash verify seek failed\r\n");
        return -1;
    }

    while (verified < file_size) {
        UINT to_read = (UINT)((file_size - verified) > FLASH_PAGE_SIZE ? FLASH_PAGE_SIZE : (file_size - verified));
        if (f_read(file, page_buf, to_read, &bytes_read) != FR_OK || bytes_read != to_read) {
            NESCO_LOG_RUNTIME("[ROM] flash verify read failed at %lu bytes=%u\r\n",
                   (unsigned long)verified,
                   (unsigned int)bytes_read);
            return -1;
        }
        if (memcmp(s_flash_rom + verified, page_buf, to_read) != 0) {
            NESCO_LOG_RUNTIME("[ROM] flash verify mismatch at %lu size=%u\r\n",
                   (unsigned long)verified,
                   (unsigned int)to_read);
            return -1;
        }
        verified += to_read;
    }

    NESCO_LOG_RUNTIME("[ROM] flash verify complete verified=%lu\r\n", (unsigned long)verified);
    return 0;
}

static void rom_flash_range_erase_locked(uint32_t flash_offs, size_t count);
static void rom_flash_range_program_locked(uint32_t flash_offs, const BYTE *data, size_t count);

static int rom_write_flash_metadata(const char *source_path, FSIZE_t file_size, int mapper_no) {
    xip_rom_metadata_t meta;
    BYTE page_buf[FLASH_PAGE_SIZE];
    const BYTE *meta_bytes = (const BYTE *)&meta;
    uint32_t written = 0;

    memset(&meta, 0xFF, sizeof(meta));
    meta.magic = XIP_ROM_METADATA_MAGIC;
    meta.version = 2u;
    meta.rom_size = (uint32_t)file_size;
    meta.mapper_no = (uint32_t)mapper_no;
    rom_copy_string(meta.file_name, sizeof(meta.file_name), rom_basename(source_path));
    rom_copy_string(meta.source_path, sizeof(meta.source_path), source_path);

    while (written < XIP_ROM_METADATA_SIZE) {
        size_t chunk = sizeof(page_buf);
        size_t offset = written;

        memset(page_buf, 0xFF, sizeof(page_buf));
        if (offset < sizeof(meta)) {
            size_t remaining = sizeof(meta) - offset;
            if (remaining < chunk) {
                chunk = remaining;
            }
            memcpy(page_buf, meta_bytes + offset, chunk);
        }

        rom_flash_range_program_locked(XIP_ROM_OFFSET + written, page_buf, FLASH_PAGE_SIZE);

        written += FLASH_PAGE_SIZE;
    }

    if (!rom_flash_metadata_is_valid() ||
        s_flash_meta->rom_size != (uint32_t)file_size ||
        s_flash_meta->mapper_no != (uint32_t)mapper_no ||
        strcmp(s_flash_meta->file_name, rom_basename(source_path)) != 0) {
        NESCO_LOG_RUNTIME("[ROM] flash metadata verify failed\r\n");
        return -1;
    }

    NESCO_LOG_RUNTIME("[ROM] flash metadata complete size=%lu mapper=%d name=%s\r\n",
           (unsigned long)file_size,
           mapper_no,
           s_flash_meta->file_name);
    return 0;
}

static void rom_flash_range_erase_locked(uint32_t flash_offs, size_t count) {
    multicore_lockout_start_blocking();
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(flash_offs, count);
    restore_interrupts(irq);
    multicore_lockout_end_blocking();
}

static void rom_flash_range_program_locked(uint32_t flash_offs, const BYTE *data, size_t count) {
    multicore_lockout_start_blocking();
    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(flash_offs, data, count);
    restore_interrupts(irq);
    multicore_lockout_end_blocking();
}

const char *rom_image_get_flash_source_path(void) {
#ifdef PICO_BUILD
    if (!rom_flash_metadata_is_valid()) {
        return NULL;
    }
    if (s_flash_meta->version >= 2u && s_flash_meta->source_path[0] != '\0') {
        return s_flash_meta->source_path;
    }
#endif
    return NULL;
}

static bool rom_flash_matches_source_path(const char *source_path, FSIZE_t file_size, int mapper_no) {
#ifdef PICO_BUILD
    if (!source_path || !rom_flash_metadata_is_valid() ||
        memcmp(s_flash_rom, NES_MAGIC, sizeof(NES_MAGIC)) != 0) {
        return false;
    }
    if (s_flash_meta->rom_size != (uint32_t)file_size) {
        return false;
    }
    if (mapper_no >= 0 && s_flash_meta->mapper_no != (uint32_t)mapper_no) {
        return false;
    }
    if (s_flash_meta->version >= 2u &&
        s_flash_meta->source_path[0] != '\0' &&
        strcmp(s_flash_meta->source_path, source_path) == 0) {
        return true;
    }
#else
    (void)source_path;
    (void)file_size;
    (void)mapper_no;
#endif
    return false;
}

bool rom_image_ensure_sd_mount(void) {
    return rom_sd_mount();
}

static int rom_stage_file_to_flash(FIL *file, FSIZE_t file_size, const char *source_path, int mapper_no) {
    BYTE page_buf[FLASH_PAGE_SIZE];
    UINT bytes_read = 0;
    uint32_t flash_offs = XIP_ROM_DATA_OFFSET;
    uint32_t erase_offs = XIP_ROM_OFFSET;
    uint32_t erase_count = XIP_ROM_METADATA_SIZE +
        (uint32_t)((file_size + FLASH_SECTOR_SIZE - 1u) & ~(FLASH_SECTOR_SIZE - 1u));
    uint32_t written = 0;

    if (file_size > XIP_ROM_MAX_BYTES) {
        NESCO_LOG_RUNTIME("[ROM] flash stage too large size=%lu max=%lu\r\n",
               (unsigned long)file_size,
               (unsigned long)XIP_ROM_MAX_BYTES);
        return -1;
    }

    NESCO_LOG_RUNTIME("[ROM] flash stage begin size=%lu erase=%lu\r\n",
           (unsigned long)file_size,
           (unsigned long)erase_count);

    rom_flash_range_erase_locked(erase_offs, erase_count);

    if (f_lseek(file, 0) != FR_OK) {
        NESCO_LOG_RUNTIME("[ROM] flash stage seek failed\r\n");
        return -1;
    }

    while (written < file_size) {
        memset(page_buf, 0xFF, sizeof(page_buf));
        UINT to_read = (UINT)((file_size - written) > FLASH_PAGE_SIZE ? FLASH_PAGE_SIZE : (file_size - written));
        if (f_read(file, page_buf, to_read, &bytes_read) != FR_OK || bytes_read != to_read) {
            NESCO_LOG_RUNTIME("[ROM] flash stage read failed at %lu bytes=%u\r\n",
                   (unsigned long)written,
                   (unsigned int)bytes_read);
            return -1;
        }

        rom_flash_range_program_locked(flash_offs + written, page_buf, FLASH_PAGE_SIZE);

        written += FLASH_PAGE_SIZE;
    }

    if (rom_verify_flash_against_file(file, file_size) != 0) {
        NESCO_LOG_RUNTIME("[ROM] flash stage verify failed\r\n");
        return -1;
    }

    if (rom_write_flash_metadata(source_path, file_size, mapper_no) != 0) {
        NESCO_LOG_RUNTIME("[ROM] flash metadata write failed\r\n");
        return -1;
    }

    {
        uint32_t irq = save_and_disable_interrupts();
        flash_flush_cache();
        restore_interrupts(irq);
    }

    NESCO_LOG_RUNTIME("[ROM] flash stage complete written=%lu verified=%lu\r\n",
           (unsigned long)written,
           (unsigned long)file_size);
    return 0;
}
#endif

/* =====================================================================
 *  InfoNES_ReadRom
 * ===================================================================== */
int InfoNES_ReadRom(const char *path) {
    const BYTE *src = NULL;
    FIL file;
    UINT bytes_read = 0;
    FSIZE_t file_size = 0;
    char fatfs_path[128];
    int mapper_no = -1;

    /* ---- Determine source ---- */
    if (path == NULL || strncmp(path, "flash:", 6) == 0) {
        NESCO_LOG_RUNTIME("[ROM] load flash path=%s\r\n", path ? path : "(null)");
#ifdef PICO_BUILD
        src = s_flash_rom;
#else
        InfoNES_Error("Flash ROM backend not available on this platform");
        return -1;
#endif
    } else if (strncmp(path, "sd:/", 4) == 0) {
        NESCO_LOG_RUNTIME("[ROM] load sd path=%s\r\n", path);
        if (!rom_sd_mount()) {
            NESCO_LOG_RUNTIME("[ROM] SD mount unavailable during load\r\n");
            return -1;
        }
        snprintf(fatfs_path, sizeof(fatfs_path), "0:/%s", path + 4);
        if (f_open(&file, fatfs_path, FA_READ) != FR_OK) {
            NESCO_LOG_RUNTIME("[ROM] f_open failed: %s\r\n", fatfs_path);
            return -1;
        }

        file_size = f_size(&file);
        mapper_no = rom_probe_mapper_number(fatfs_path);
        NESCO_LOG_RUNTIME("[ROM] file size=%lu threshold=%lu\r\n",
               (unsigned long)file_size,
               (unsigned long)ROM_RAM_THRESHOLD_BYTES);
        if (file_size >= ROM_RAM_THRESHOLD_BYTES) {
#ifdef PICO_BUILD
            if (rom_flash_matches_source_path(path, file_size, mapper_no)) {
                f_close(&file);
                rom_set_flash_entry_from_path(path);
                src = s_flash_rom;
                NESCO_LOG_RUNTIME("[ROM] flash-backed load reused existing staged ROM\r\n");
            } else
            if (rom_stage_file_to_flash(&file, file_size, path, mapper_no) != 0) {
                f_close(&file);
                NESCO_LOG_RUNTIME("[ROM] flash stage failed\r\n");
                return -1;
            }
            f_close(&file);
            rom_set_flash_entry_from_path(path);
            src = s_flash_rom;
            NESCO_LOG_RUNTIME("[ROM] flash-backed load ready\r\n");
#else
            f_close(&file);
            NESCO_LOG_RUNTIME("[ROM] file too large for RAM path\r\n");
            return -1;
#endif
        } else {
            const unsigned long ram_request_bytes =
                (unsigned long)ROM_RAM_THRESHOLD_BYTES;
#if !defined(NESCO_RUNTIME_LOGS)
            (void)ram_request_bytes;
#endif
#if defined(NESCO_RUNTIME_LOGS)
            struct mallinfo mi_before = mallinfo();
#ifdef PICO_BUILD
            const uintptr_t heap_start = (uintptr_t)&__end__;
            const uintptr_t heap_limit = (uintptr_t)&__HeapLimit;
            const unsigned long heap_gap_bytes =
                (heap_limit > heap_start) ? (unsigned long)(heap_limit - heap_start) : 0ul;
            const unsigned long heap_after_alloc =
                (heap_gap_bytes > ram_request_bytes)
                    ? (unsigned long)(heap_gap_bytes - ram_request_bytes)
                    : 0ul;
#endif
            NESCO_LOG_RUNTIME("[ROM] heap before RAM copy arena=%lu used=%lu free=%lu keep=%lu request=%lu\r\n",
                   (unsigned long)mi_before.arena,
                   (unsigned long)mi_before.uordblks,
                   (unsigned long)mi_before.fordblks,
                   (unsigned long)mi_before.keepcost,
                   ram_request_bytes);
#ifdef PICO_BUILD
            NESCO_LOG_RUNTIME("[ROM] heap calc before RAM copy gap=%lu request=%lu remain=%lu\r\n",
                   heap_gap_bytes,
                   ram_request_bytes,
                   heap_after_alloc);
#endif
#endif
            if (file_size > ROM_RAM_THRESHOLD_BYTES) {
                f_close(&file);
	                NESCO_LOG_RUNTIME("[ROM] mapper0 RAM image too large size=%lu max=%lu\r\n",
	                       (unsigned long)file_size,
	                       (unsigned long)ROM_RAM_THRESHOLD_BYTES);
	                return -1;
            }
            rom_release_mapper0_buffer();
            s_rom_buf = (BYTE *)malloc(ROM_RAM_THRESHOLD_BYTES);
            if (!s_rom_buf) {
                f_close(&file);
                NESCO_LOG_RUNTIME("[ROM] mapper0 RAM malloc failed size=%lu\r\n",
                       ram_request_bytes);
                return -1;
            }
	            if (f_read(&file, s_rom_buf, (UINT)file_size, &bytes_read) != FR_OK || bytes_read != (UINT)file_size) {
	                rom_release_mapper0_buffer();
	                f_close(&file);
	                NESCO_LOG_RUNTIME("[ROM] f_read failed bytes=%u\r\n", (unsigned int)bytes_read);
	                return -1;
	            }
	            f_close(&file);
	            src = s_rom_buf;
	            NESCO_LOG_RUNTIME("[ROM] SD load complete using mapper0 malloc buffer=%lu\r\n",
	                   (unsigned long)file_size);
	        }
    } else {
        /* Load from SD card / file system */
        NESCO_LOG_RUNTIME("[ROM] unsupported path=%s\r\n", path ? path : "(null)");
        return -1;
    }

    if (!src) return -1;

    /* ---- Validate iNES magic ---- */
    if (memcmp(src, NES_MAGIC, 4) != 0) {
        InfoNES_Error("Invalid iNES header magic");
        return -1;
    }

    /* ---- Copy header ---- */
    memcpy(&NesHeader, src, sizeof(struct NesHeader_tag));

    /* ---- Pointer arithmetic into ROM image ---- */
    const BYTE *p = src + 16;  /* Skip 16-byte header */

    /* Skip 512-byte trainer if present */
    if (NesHeader.byInfo1 & 0x04u) p += 512;

    /* PRG-ROM */
    ROM  = (BYTE *)p;
    p   += (DWORD)NesHeader.byRomSize * 0x4000u;

    /* CHR-ROM (if any) */
    if (NesHeader.byVRomSize > 0) {
        VROM = (BYTE *)p;
    } else {
        VROM = NULL;
    }

    return 0;
}

/* =====================================================================
 *  InfoNES_ReleaseRom
 * ===================================================================== */
void InfoNES_ReleaseRom(void) {
    /* Flash XIP: nothing to free (ROM is a pointer into Flash) */
    NESCO_LOG_RUNTIME("[ROM] release begin\r\n");
    sram_store_flush_current_rom();
    InfoNES_Mapper_ReleaseCurrent();
    rom_release_mapper0_buffer();
    if (s_vrom_buf) {
        free(s_vrom_buf);
        s_vrom_buf = NULL;
    }
    ROM  = NULL;
    VROM = NULL;
    sram_store_clear_session();
    NESCO_LOG_RUNTIME("[ROM] release end\r\n");
}
