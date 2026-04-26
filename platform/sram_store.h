#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void sram_store_begin_rom(const char *rom_path);
void sram_store_restore_for_current_rom(void);
void sram_store_flush_current_rom(void);
void sram_store_clear_session(void);
bool sram_store_has_save_for_rom(const char *rom_path);

#ifdef __cplusplus
}
#endif
