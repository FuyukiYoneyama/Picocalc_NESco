/*
 * rom_image.h — ROM image loader (Flash XIP / SD card backend)
 *
 * Implements InfoNES_ReadRom() and InfoNES_ReleaseRom().
 * Supports two ROM backends:
 *   - Flash XIP: ROM embedded in RP2040 Flash; accessed via const pointer
 *   - File: ROM loaded from SD card into heap (fallback)
 *
 * Part of Picocalc_NESco
 * MIT License
 */
#pragma once
#include <stdbool.h>

#include "InfoNES_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rom_storage_kind {
    ROM_STORAGE_UNKNOWN = 0,
    ROM_STORAGE_RAM,
    ROM_STORAGE_FLASH,
} rom_storage_kind_t;

typedef enum rom_menu_entry_kind {
    ROM_ENTRY_FILE = 0,
    ROM_ENTRY_DIRECTORY,
    ROM_ENTRY_PARENT,
} rom_menu_entry_kind_t;

typedef struct rom_menu_entry_info {
    const char *label;
    const char *path;
    const char *detail;
    rom_menu_entry_kind_t kind;
    rom_storage_kind_t storage;
    BYTE enabled;
} rom_menu_entry_info_t;

/**
 * rom_image_init() — Detect available ROM storage and prepare loader.
 */
void rom_image_init(void);
void rom_image_log_heap_estimate(const char *tag);
void rom_image_log_heap(const char *tag);
bool rom_image_menu_begin(void);
void rom_image_menu_end(void);

int rom_image_menu_entries(rom_menu_entry_info_t *out_entries, int max_entries);
const char *rom_image_last_status(void);
bool rom_image_enter_directory(const char *path);
const char *rom_image_current_directory(void);
void rom_image_set_selected_path(const char *path);
const char *rom_image_get_selected_path(void);
void rom_image_clear_selected_path(void);
const char *rom_image_get_flash_source_path(void);
bool rom_image_ensure_sd_mount(void);

/* InfoNES_ReadRom / InfoNES_ReleaseRom implemented in rom_image.c */

#ifdef __cplusplus
}
#endif
