/*
 * mapper0.c — Mapper 0 (NROM) implementation
 *
 * NROM supports no bank switching.  All callbacks are no-ops except Init,
 * which wires PRG-ROM and CHR-ROM (or CHR-RAM) directly into the bank windows.
 *
 * Map0_* callbacks also serve as default no-op implementations for other
 * mappers that don't need a particular callback.
 *
 * RAM-resident (RAMFUNC): Map0_Init through Map0_RenderScreen.
 * On RP2040 all are placed in RAM because Mapper 0 is on the hot scanline
 * critical path for NROM ROMs.
 *
 * Spec reference: specs/30_mapper_interface.md
 *
 * Part of Picocalc_NESco
 * MIT License
 */

#include "mapper.h"
#include "nes_globals.h"
#include "nes_system.h"
#include "cpu.h"

#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
#include <stdio.h>
#endif

/* =====================================================================
 *  Map0_Init — NROM mapper initialisation
 *
 *  PRG bank layout (depends on ROM size):
 *    16 KB (byRomSize == 1): $8000–$BFFF = page 0; $C000–$FFFF = page 0 (mirror)
 *    32 KB (byRomSize >= 2): $8000–$BFFF = page 0; $C000–$FFFF = page 1
 *
 *  CHR layout:
 *    byVRomSize > 0: use VROM (CHR-ROM pages)
 *    byVRomSize == 0: use PPURAM CHR area (CHR-RAM)
 * ===================================================================== */
void RAMFUNC(Map0_Init)(void) {
    /* Set all callbacks to no-ops */
    MapperWrite      = Map0_Write;
    MapperSram       = Map0_Sram;
    MapperApu        = Map0_Apu;
    MapperReadApu    = Map0_ReadApu;
    MapperVSync      = Map0_VSync;
    MapperHSync      = Map0_HSync;
    MapperPPU        = Map0_PPU;
    MapperRenderScreen = Map0_RenderScreen;

    /* SRAM bank */
    SRAMBANK = SRAM;

    /* PRG-ROM windows */
    if (NesHeader.byRomSize == 1) {
        /* 16 KB: mirror lower half into upper */
        ROMBANK[0] = ROMPAGE(0);
        ROMBANK[1] = ROMPAGE(1);
        ROMBANK[2] = ROMPAGE(0);
        ROMBANK[3] = ROMPAGE(1);
    } else {
        /* 32 KB or more: first 16 KB + last 16 KB */
        ROMBANK[0] = ROMPAGE(0);
        ROMBANK[1] = ROMPAGE(1);
        ROMBANK[2] = ROMLASTPAGE(1);
        ROMBANK[3] = ROMLASTPAGE(0);
    }

    /* CHR windows */
    if (NesHeader.byVRomSize > 0 && VROM != NULL) {
        /* CHR-ROM: 8 KB = 8 × 1 KB pages */
        for (int i = 0; i < 8; i++) {
            PPUBANK[i] = VROMPAGE(i);
        }
    } else {
        /* CHR-RAM: use PPURAM CHR area */
        for (int i = 0; i < 8; i++) {
            PPUBANK[i] = CRAMPAGE(i);
        }
    }

    /* Nametable mirroring */
    if (ROM_FourScr) {
        InfoNES_Mirroring(MIRROR_FOURSCREEN);
    } else {
        InfoNES_Mirroring(ROM_Mirroring ? MIRROR_VERTICAL : MIRROR_HORIZONTAL);
    }

#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
    printf("[M0] init prg=%u*16KB chr=%u*8KB mirror=%s four=%u trainer=%u sram=%u\r\n",
           (unsigned int)NesHeader.byRomSize,
           (unsigned int)NesHeader.byVRomSize,
           ROM_FourScr ? "FOUR" : (ROM_Mirroring ? "VERT" : "HORIZ"),
           (unsigned int)ROM_FourScr,
           (unsigned int)ROM_Trainer,
           (unsigned int)ROM_SRAM);
    fflush(stdout);
#endif

    /* Default interrupt wiring: NMI active-low, IRQ active-low */
    K6502_Set_Int_Wiring(1, 1);
}

/* =====================================================================
 *  No-op default callbacks
 * ===================================================================== */
void RAMFUNC(Map0_Write)(WORD addr, BYTE data) {
    UNUSED(addr); UNUSED(data);
}

void RAMFUNC(Map0_Sram)(WORD addr, BYTE data) {
    SRAMBANK[addr & 0x1FFFu] = data;
    SRAMwritten = 1;
}

void RAMFUNC(Map0_Apu)(WORD addr, BYTE data) {
    UNUSED(addr); UNUSED(data);
}

BYTE RAMFUNC(Map0_ReadApu)(WORD addr) {
    /* Default: return high byte of address (open bus) */
    return (BYTE)(addr >> 8);
}

void RAMFUNC(Map0_VSync)(void) { }
void RAMFUNC(Map0_HSync)(void) { }
void RAMFUNC(Map0_PPU)(WORD addr) { UNUSED(addr); }
void RAMFUNC(Map0_RenderScreen)(BYTE mode) { UNUSED(mode); }
