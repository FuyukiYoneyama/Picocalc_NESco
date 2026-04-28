#pragma once

#include "InfoNES_Types.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CORE1_SERVICE_KEYBOARD = 1u << 0,
    CORE1_SERVICE_LCD = 1u << 1,
};

void core1_worker_init(void);
void core1_set_services(unsigned services);
bool core1_wait_idle_ack(unsigned timeout_ms);
bool core1_keyboard_snapshot_active(void);
void core1_keyboard_read_snapshot(DWORD *pad1, DWORD *pad2, DWORD *system);
unsigned core1_keyboard_consume_event_count(void);

#ifdef __cplusplus
}
#endif
