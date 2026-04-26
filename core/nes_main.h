/*
 * nes_main.h — Emulation session control interface
 *
 * Manages the full emulation session lifecycle:
 *   InfoNES_Reset()  — initialise all subsystems and load mapper
 *   InfoNES_Main()   — outer loop: menu → emulation → menu
 *   InfoNES_Cycle()  — inner loop: execute one frame (262 scanlines)
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
 * InfoNES_Reset() — Full system reset.
 *
 * Sequence:
 *   1. Parse iNES header flags (MapperNo, ROM_SRAM, ROM_Mirroring, …)
 *   2. Mapper_Select(MapperNo)  → sets all mapper callbacks; calls mapper init
 *   3. APU_Init()
 *   4. PPU_Init()
 *   5. K6502_Reset()            → PC loaded from $FFFC/$FFFD
 *
 * Returns 0 on success, -1 on unsupported mapper.
 */
int InfoNES_Reset(void);

/**
 * InfoNES_Main() — Run one complete emulation session.
 *
 * Sequence:
 *   InfoNES_ReadRom()
 *   InfoNES_Reset()
 *   InfoNES_SoundOpen()
 *   scanline loop (262 × N frames):
 *     PPU_Cycle()
 *     if PAD_SYS_QUIT: break
 *     if PAD_SYS_RESET: InfoNES_Reset()
 *   InfoNES_SoundClose()
 *   InfoNES_ReleaseRom()
 */
void InfoNES_Main(const char *rom_path);

#ifdef __cplusplus
}
#endif
