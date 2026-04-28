#pragma once

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

#ifdef __cplusplus
}
#endif
