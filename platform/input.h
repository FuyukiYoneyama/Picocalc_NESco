/*
 * input.h — Keyboard / joypad input interface (PicoCalc)
 *
 * PicoCalc uses an I2C keyboard matrix.
 * This layer maps key events to NES joypad bits and system control flags.
 *
 * Part of Picocalc_NESco
 * MIT License
 */
#pragma once
#include "InfoNES_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

void input_init(void);
void input_poll(DWORD *pad1, DWORD *pad2, DWORD *system);
unsigned input_consume_event_count(void);

#ifdef __cplusplus
}
#endif
