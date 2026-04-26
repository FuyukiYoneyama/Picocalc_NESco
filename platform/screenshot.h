#pragma once

#include <stdbool.h>

#include "InfoNES_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nesco_screenshot_result_t {
    NESCO_SCREENSHOT_OK = 0,
    NESCO_SCREENSHOT_BUSY,
    NESCO_SCREENSHOT_NO_MEMORY,
    NESCO_SCREENSHOT_READBACK_FAILED,
    NESCO_SCREENSHOT_SD_OPEN_FAILED,
    NESCO_SCREENSHOT_SD_WRITE_FAILED,
} nesco_screenshot_result_t;

bool nesco_request_screenshot(void);
nesco_screenshot_result_t nesco_take_screenshot_now_with_stem(const char *stem);
void nesco_maybe_start_screenshot_on_vblank(void);
nesco_screenshot_result_t nesco_get_last_screenshot_result(void);

#ifdef __cplusplus
}
#endif
