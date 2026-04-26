/*
 * apu.h — APU (Audio Processing Unit) interface
 *
 * 5-channel NES APU synthesis:
 *   Channel 0: Pulse 1
 *   Channel 1: Pulse 2
 *   Channel 2: Triangle
 *   Channel 3: Noise
 *   Channel 4: DPCM (Delta PCM)
 *
 * Spec reference: specs/22_apu.md
 *
 * Part of Picocalc_NESco
 * MIT License
 */
#pragma once

#include "nes_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * APU_Init() — Reset all APU state to power-on defaults.
 */
void APU_Init(void);

/**
 * APU_RegisterWrite(addr, data) — Handle CPU write to $4000–$4017.
 */
void APU_RegisterWrite(WORD addr, BYTE data);

/**
 * APU_RegisterRead(addr) — Handle CPU read from $4015.
 */
BYTE APU_RegisterRead(WORD addr);

/**
 * InfoNES_pAPUHsync(render_enabled) — Generate audio samples for one H-Sync.
 *
 * Called every scanline (262× per frame).
 * Synthesises samples for all 5 channels and calls InfoNES_SoundOutput.
 * RAM-resident on RP2040.
 */
void InfoNES_pAPUHsync(int render_enabled);

/**
 * InfoNES_pAPUVsync() — V-Sync update: envelope, sweep, length counters.
 *
 * Called once per frame at VBlank start.
 */
void InfoNES_pAPUVsync(void);

#ifdef __cplusplus
}
#endif
