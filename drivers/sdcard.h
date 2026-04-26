#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool sdcard_is_present(void);
bool sdcard_init(void);
bool sdcard_is_initialized(void);
bool sdcard_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t count);
bool sdcard_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t count);
bool sdcard_get_sector_count(uint32_t *sector_count);
void sdcard_reset(void);

#ifdef __cplusplus
}
#endif
