/*
 * ppu.h — PPU (Picture Processing Unit) interface
 *
 * The PPU renders one NES scanline at a time into a 256-pixel line buffer,
 * handles VBlank / NMI generation, and drives H-Sync and V-Sync hooks.
 *
 * Spec reference: specs/21_ppu.md
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
 * PPU_Init() — Reset all PPU state to power-on defaults.
 * Called from InfoNES_Reset().
 */
void PPU_Init(void);

/**
 * PPU_DrawLine() — Render the current scanline into g_wScanLine.
 *
 * Renders background tiles and up to 8 sprites.
 * Checks sprite 0 hit.
 * RAM-resident on RP2040.
 */
void PPU_DrawLine(void);

/**
 * PPU_HSync() — Per-scanline bookkeeping (VBlank, scroll reload, advance).
 *
 * Must be called after every scanline (visible and blank).
 * Handles:
 *   - VBlank start (scanline 241): set R2_IN_VBLANK, LoadFrame, PadState, VSync, NMI
 *   - VBlank end (scanline 261): clear flags, reload PPU_Addr vertical
 *   - Always: advance g_nScanLine; wrap at 262
 * RAM-resident on RP2040.
 */
void PPU_HSync(void);

/**
 * PPU_Cycle() — Execute one full scanline.
 *
 * Called from the main emulation loop for each of the 262 scanlines.
 * Sequence:
 *   K6502_Step(STEP_PER_SCANLINE)
 *   InfoNES_pAPUHsync()
 *   if (visible && !skip): InfoNES_PreDrawLine / PPU_DrawLine / InfoNES_PostDrawLine
 *   MapperHSync()
 *   PPU_HSync()
 * RAM-resident on RP2040.
 */
void PPU_Cycle(void);

/**
 * PPU_SyncVBlankEdge() — advance any pending VBlank/NMI edge work based on
 * the current CPU position within the active scanline.
 */
void PPU_SyncVBlankEdge(void);

/**
 * PPU_DiagSecondWindowActive() — true while the Lode Runner second PPUMASK=00
 * background-upload observation window is active.
 */
BYTE PPU_DiagSecondWindowActive(void);

/**
 * PPU_InvalidateSpriteScanlineCache() — invalidate any cached per-scanline
 * sprite collect state after OAM or sprite-size changes.
 */
void PPU_InvalidateSpriteScanlineCache(void);

/**
 * PPU_RegisterRead(reg) — Read PPU register (reg = $2000–$2007 masked to 0–7).
 * RAM-resident on RP2040.
 */
BYTE PPU_RegisterRead(BYTE reg);

/**
 * PPU_RegisterWrite(reg, data) — Write PPU register.
 * RAM-resident on RP2040.
 */
void PPU_RegisterWrite(BYTE reg, BYTE data);

/**
 * PPU_VRamRead(addr) — Read from PPU address space (via PPUBANK).
 */
BYTE PPU_VRamRead(WORD addr);

/**
 * PPU_VRamWrite(addr, data) — Write to PPU address space.
 */
void PPU_VRamWrite(WORD addr, BYTE data);

/* Palette RAM: 32 entries (background + sprite palettes) */
extern BYTE g_PalRAM[32];

#ifdef __cplusplus
}
#endif
