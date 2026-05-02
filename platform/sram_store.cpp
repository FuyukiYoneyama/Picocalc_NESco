#include "sram_store.h"

#include "InfoNES.h"
#include "InfoNES_Mapper.h"
#include "InfoNES_System.h"
#include "rom_image.h"
#include "runtime_log.h"
#include "ff.h"

#include <cstdio>
#include <cstring>

namespace {

char s_current_rom_path[160] = "";
char s_current_save_path[192] = "";
char s_current_map30_path[192] = "";
bool s_session_active = false;

struct Map30PersistHeader {
    char magic[4];
    BYTE version;
    BYTE reserved[3];
    BYTE used[2];
    BYTE bank[2];
    BYTE padding[2];
};

void sram_copy_string(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, dst_size, "%s", src);
}

const char *sram_basename(const char *path)
{
    const char *slash = std::strrchr(path ? path : "", '/');
    return slash ? slash + 1 : (path ? path : "");
}

void sram_make_leaf_with_ext(char *dst, size_t dst_size, const char *rom_path, const char *ext)
{
    const char *base = sram_basename(rom_path);
    const char *dot = std::strrchr(base, '.');
    size_t stem_len = dot ? (size_t)(dot - base) : std::strlen(base);
    size_t ext_len = ext ? std::strlen(ext) : 0u;

    if (!dst || dst_size == 0u) {
        return;
    }

    if (ext_len + 1u > dst_size) {
        dst[0] = '\0';
        return;
    }

    if (stem_len > dst_size - ext_len - 1u) {
        stem_len = dst_size - ext_len - 1u;
    }

    std::memcpy(dst, base, stem_len);
    dst[stem_len] = '\0';
    if (ext_len != 0u) {
        std::strncat(dst, ext, dst_size - std::strlen(dst) - 1u);
    }
}

void sram_build_path_with_ext(char *dst, size_t dst_size, const char *rom_path, const char *ext)
{
    char leaf[96];

    if (!dst || dst_size == 0u) {
        return;
    }

    dst[0] = '\0';
    if (!rom_path || rom_path[0] == '\0') {
        return;
    }

    if (std::strncmp(rom_path, "sd:/", 4) == 0) {
        std::snprintf(dst, dst_size, "0:/%s", rom_path + 4);
        char *dot = std::strrchr(dst, '.');
        if (dot) {
            std::snprintf(dot, dst_size - (size_t)(dot - dst), "%s", ext ? ext : "");
        } else {
            std::strncat(dst, ext ? ext : "", dst_size - std::strlen(dst) - 1u);
        }
        return;
    }

    sram_make_leaf_with_ext(leaf, sizeof(leaf), rom_path, ext);
    std::snprintf(dst, dst_size, "0:/saves/%s", leaf);
}

void sram_build_save_path(char *dst, size_t dst_size, const char *rom_path)
{
    sram_build_path_with_ext(dst, dst_size, rom_path, ".srm");
}

void sram_build_map30_path(char *dst, size_t dst_size, const char *rom_path)
{
    sram_build_path_with_ext(dst, dst_size, rom_path, ".m30");
}

bool sram_current_rom_uses_save(void)
{
    return ROM_SRAM;
}

const char *sram_resolve_save_basis_path(const char *rom_path)
{
    if (rom_path && std::strncmp(rom_path, "flash:/", 7) == 0) {
        const char *flash_source_path = rom_image_get_flash_source_path();
        if (flash_source_path && flash_source_path[0] != '\0') {
            return flash_source_path;
        }
    }
    return rom_path;
}

void sram_zero_buffer(void)
{
    std::memset(SRAM, 0, SRAM_SIZE);
    SRAMwritten = false;
}

void sram_ensure_save_dir(void)
{
    const bool save_path_uses_fallback = std::strncmp(s_current_save_path, "0:/saves/", 9) == 0;
    const bool map30_path_uses_fallback = std::strncmp(s_current_map30_path, "0:/saves/", 9) == 0;
    if (!save_path_uses_fallback && !map30_path_uses_fallback) {
        return;
    }

    FRESULT fr = f_mkdir("0:/saves");
    if (fr != FR_OK && fr != FR_EXIST) {
        NESCO_LOG_RUNTIME("[SRAM] mkdir failed path=0:/saves fr=%d\r\n", (int)fr);
    }
}

void sram_store_restore_map30(void)
{
    FIL file;
    UINT bytes_read = 0;
    FRESULT fr;
    Map30PersistHeader header = {};
    BYTE slot_data[2][0x4000];
    unsigned restored_slots = 0;

    if (MapperNo != 30 || !Map30_IsFlashSaveEnabled()) {
        return;
    }

    fr = f_open(&file, s_current_map30_path, FA_READ);
    if (fr != FR_OK) {
        return;
    }

    fr = f_read(&file, &header, sizeof(header), &bytes_read);
    if (fr != FR_OK || bytes_read != sizeof(header) ||
        std::memcmp(header.magic, "M30S", 4) != 0 || header.version != 1) {
        f_close(&file);
        NESCO_LOG_RUNTIME("[M30] restore failed path=%s fr=%d header=%u\r\n",
                       s_current_map30_path,
                       (int)fr,
                       (unsigned)bytes_read);
        return;
    }

    fr = f_read(&file, slot_data, sizeof(slot_data), &bytes_read);
    f_close(&file);
    if (fr != FR_OK || bytes_read != sizeof(slot_data)) {
        NESCO_LOG_RUNTIME("[M30] restore failed path=%s fr=%d bytes=%u\r\n",
                       s_current_map30_path,
                       (int)fr,
                       (unsigned)bytes_read);
        return;
    }

    for (int i = 0; i < 2; ++i) {
        if (!header.used[i]) {
            continue;
        }
        if (Map30_RestoreOverlay(i, header.bank[i], slot_data[i], sizeof(slot_data[i]))) {
            ++restored_slots;
        }
    }

    Map30_ReapplyState();
    Map30_ClearFlashSaveDirty();
    NESCO_LOG_RUNTIME("[M30] restore path=%s slots=%u\r\n",
                   s_current_map30_path,
                   restored_slots);
}

void sram_store_flush_map30(void)
{
    FIL file;
    UINT bytes_written = 0;
    FRESULT fr;
    Map30PersistHeader header = {};
    const BYTE *slot0 = Map30_GetOverlayData(0);
    const BYTE *slot1 = Map30_GetOverlayData(1);

    if (MapperNo != 30 || !Map30_IsFlashSaveEnabled()) {
        return;
    }

    if (!Map30_IsFlashSaveDirty()) {
        return;
    }

    sram_ensure_save_dir();

    std::memcpy(header.magic, "M30S", 4);
    header.version = 1;
    header.used[0] = Map30_GetOverlayUsed(0);
    header.used[1] = Map30_GetOverlayUsed(1);
    header.bank[0] = Map30_GetOverlayBank(0);
    header.bank[1] = Map30_GetOverlayBank(1);

    fr = f_open(&file, s_current_map30_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        NESCO_LOG_RUNTIME("[M30] flush open failed path=%s fr=%d\r\n", s_current_map30_path, (int)fr);
        return;
    }

    fr = f_write(&file, &header, sizeof(header), &bytes_written);
    if (fr != FR_OK || bytes_written != sizeof(header)) {
        f_close(&file);
        NESCO_LOG_RUNTIME("[M30] flush failed path=%s fr=%d bytes=%u\r\n",
                       s_current_map30_path,
                       (int)fr,
                       (unsigned)bytes_written);
        return;
    }

    fr = f_write(&file, slot0, 0x4000, &bytes_written);
    if (fr != FR_OK || bytes_written != 0x4000u) {
        f_close(&file);
        NESCO_LOG_RUNTIME("[M30] flush failed path=%s fr=%d bytes=%u\r\n",
                       s_current_map30_path,
                       (int)fr,
                       (unsigned)bytes_written);
        return;
    }

    fr = f_write(&file, slot1, 0x4000, &bytes_written);
    f_close(&file);
    if (fr != FR_OK || bytes_written != 0x4000u) {
        NESCO_LOG_RUNTIME("[M30] flush failed path=%s fr=%d bytes=%u\r\n",
                       s_current_map30_path,
                       (int)fr,
                       (unsigned)bytes_written);
        return;
    }

    Map30_ClearFlashSaveDirty();
    NESCO_LOG_RUNTIME("[M30] flush path=%s slots=%u\r\n",
                   s_current_map30_path,
                   (unsigned)(header.used[0] + header.used[1]));
}

} // namespace

extern "C" void sram_store_begin_rom(const char *rom_path)
{
    const char *save_basis_path = sram_resolve_save_basis_path(rom_path);

    sram_copy_string(s_current_rom_path, sizeof(s_current_rom_path), rom_path);
    sram_build_save_path(s_current_save_path, sizeof(s_current_save_path), save_basis_path);
    sram_build_map30_path(s_current_map30_path, sizeof(s_current_map30_path), save_basis_path);
    s_session_active = s_current_save_path[0] != '\0';
}

extern "C" bool sram_store_has_save_for_rom(const char *rom_path)
{
    char save_path[192];
    FILINFO fno;
    const char *save_basis_path = sram_resolve_save_basis_path(rom_path);

    sram_build_save_path(save_path, sizeof(save_path), save_basis_path);
    if (save_path[0] == '\0') {
        return false;
    }
    return f_stat(save_path, &fno) == FR_OK;
}

extern "C" void sram_store_restore_for_current_rom(void)
{
    FIL file;
    UINT bytes_read = 0;
    FRESULT fr;

    if (!s_session_active) {
        return;
    }

    sram_zero_buffer();

    if (!sram_current_rom_uses_save()) {
        NESCO_LOG_RUNTIME("[SRAM] restore skip no-sram path=%s\r\n", s_current_rom_path);
        sram_store_restore_map30();
        return;
    }

    fr = f_open(&file, s_current_save_path, FA_READ);
    if (fr != FR_OK) {
        NESCO_LOG_RUNTIME("[SRAM] no save found path=%s fr=%d\r\n", s_current_save_path, (int)fr);
        sram_store_restore_map30();
        return;
    }

    fr = f_read(&file, SRAM, SRAM_SIZE, &bytes_read);
    f_close(&file);
    if (fr != FR_OK) {
        sram_zero_buffer();
        NESCO_LOG_RUNTIME("[SRAM] restore failed path=%s fr=%d\r\n", s_current_save_path, (int)fr);
        sram_store_restore_map30();
        return;
    }

    if (bytes_read < SRAM_SIZE) {
        std::memset(SRAM + bytes_read, 0, SRAM_SIZE - bytes_read);
    }
    SRAMwritten = false;
    NESCO_LOG_RUNTIME("[SRAM] restore path=%s bytes=%u\r\n",
                   s_current_save_path,
                   (unsigned)bytes_read);

    sram_store_restore_map30();
}

extern "C" void sram_store_flush_current_rom(void)
{
    FIL file;
    UINT bytes_written = 0;
    FRESULT fr;

    if (!s_session_active) {
        return;
    }

    if (!sram_current_rom_uses_save()) {
        NESCO_LOG_RUNTIME("[SRAM] flush skip no-sram path=%s\r\n", s_current_rom_path);
        sram_store_flush_map30();
        return;
    }

    if (!SRAMwritten) {
        NESCO_LOG_RUNTIME("[SRAM] flush skip clean path=%s\r\n", s_current_save_path);
        sram_store_flush_map30();
        return;
    }

    sram_ensure_save_dir();

    fr = f_open(&file, s_current_save_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        NESCO_LOG_RUNTIME("[SRAM] flush open failed path=%s fr=%d\r\n", s_current_save_path, (int)fr);
        sram_store_flush_map30();
        return;
    }

    fr = f_write(&file, SRAM, SRAM_SIZE, &bytes_written);
    f_close(&file);
    if (fr != FR_OK || bytes_written != SRAM_SIZE) {
        NESCO_LOG_RUNTIME("[SRAM] flush failed path=%s fr=%d bytes=%u\r\n",
                       s_current_save_path,
                       (int)fr,
                       (unsigned)bytes_written);
        sram_store_flush_map30();
        return;
    }

    SRAMwritten = false;
    NESCO_LOG_RUNTIME("[SRAM] flush path=%s bytes=%u\r\n",
                   s_current_save_path,
                   (unsigned)bytes_written);

    sram_store_flush_map30();
}

extern "C" void sram_store_clear_session(void)
{
    s_current_rom_path[0] = '\0';
    s_current_save_path[0] = '\0';
    s_current_map30_path[0] = '\0';
    s_session_active = false;
}
