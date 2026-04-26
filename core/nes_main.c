/*
 * nes_main.c — Emulation session control
 *
 * Implements InfoNES_Reset() and InfoNES_Main().
 * Orchestrates the per-scanline loop per specs/90_integrated_spec.md.
 *
 * Part of Picocalc_NESco
 * MIT License
 */

#include "nes_main.h"
#include "nes_globals.h"
#include "nes_system.h"
#include "mapper.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "display.h"

#include <string.h>

/* =====================================================================
 *  InfoNES_Reset
 * ===================================================================== */
int InfoNES_Reset(void) {
    /* ---- Parse header flags ---- */
    MapperNo     = (int)((NesHeader.byInfo1 >> 4) | (NesHeader.byInfo2 & 0xF0u));
    ROM_Mirroring = (NesHeader.byInfo1 & INFO1_MIRROR_V) ? 1 : 0;
    ROM_SRAM      = (NesHeader.byInfo1 & INFO1_SRAM)     ? 1 : 0;
    ROM_Trainer   = (NesHeader.byInfo1 & INFO1_TRAINER)  ? 1 : 0;
    ROM_FourScr   = (NesHeader.byInfo1 & INFO1_FOURSCREEN) ? 1 : 0;

    /* ---- Zero NES RAM and SRAM ---- */
    memset(RAM,  0, sizeof(RAM));
    memset(SRAM, 0, sizeof(SRAM));
    SRAMwritten = 0;

    /* Clear any latched menu/system key state before starting a new session. */
    PAD1_Latch = 0;
    PAD2_Latch = 0;
    PAD1_Bit   = 0;
    PAD2_Bit   = 0;

    /* ---- Mapper select and init ---- */
    int rc = Mapper_Select(MapperNo);
    if (rc != 0) return -1;

    /* ---- APU ---- */
    APU_Init();

    /* ---- PPU ---- */
    PPU_Init();

    /* ---- CPU ---- */
    K6502_Reset();

    return 0;
}

/* =====================================================================
 *  InfoNES_Main — complete session loop
 * ===================================================================== */
void InfoNES_Main(const char *rom_path) {
    /* Drop any fullscreen menu residue before enabling the NES viewport. */
    display_set_mode(DISPLAY_MODE_FULLSCREEN);
    display_clear_rgb565(0x0000);
    display_set_mode(DISPLAY_MODE_NES_VIEW);
    display_clear_rgb565(0x0000);

    /* Load ROM from platform storage */
    if (InfoNES_ReadRom(rom_path) != 0) {
        InfoNES_Error("Failed to load ROM: %s", rom_path);
        return;
    }

    /* Full reset */
    if (InfoNES_Reset() != 0) {
        InfoNES_ReleaseRom();
        return;
    }

    /* Open audio output */
    InfoNES_SoundOpen(3, STEP_PER_SCANLINE);

    /* ---- Per-scanline emulation loop ---- */
    for (;;) {
        /* Execute one scanline */
        PPU_Cycle();

        /* Check system control pad bits */
        if (PAD1_Latch & PAD_SYS_QUIT) {
            break;
        }
        if (PAD1_Latch & PAD_SYS_RESET) {
            PAD1_Latch &= ~PAD_SYS_RESET;
            InfoNES_Reset();
        }
    }

    /* ---- Teardown ---- */
    InfoNES_SoundClose();
    Map30_Release();   /* No-op for non-M30 mappers; safe to call always */
    InfoNES_ReleaseRom();
    display_set_mode(DISPLAY_MODE_FULLSCREEN);
    display_clear_rgb565(0x0000);
}
