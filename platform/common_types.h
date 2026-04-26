#pragma once

#include "InfoNES_Types.h"

#include <stddef.h>
#include <stdbool.h>

#ifdef PICO_BUILD
#  include "pico.h"
#  define RAMFUNC(f) __not_in_flash_func(f)
#else
#  define RAMFUNC(f) f
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define UNUSED(x) ((void)(x))
