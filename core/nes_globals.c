/*
 * nes_globals.c — Global variable definitions
 *
 * Defines all shared emulator state declared in nes_globals.h.
 *
 * Part of Picocalc_NESco
 * MIT License
 */

#include "nes_globals.h"

/* =====================================================================
 *  ROM / memory images
 * ===================================================================== */
BYTE       *ROM        = NULL;
BYTE       *VROM       = NULL;
NES_HEADER  NesHeader;

/* =====================================================================
 *  NES work RAM  (static: no heap allocation needed)
 * ===================================================================== */
BYTE  RAM   [NES_RAM_SIZE];
BYTE  SRAM  [NES_SRAM_SIZE];
BYTE  SPRRAM[SPRRAM_SIZE];
BYTE  PPURAM[PPURAM_SIZE];

/* =====================================================================
 *  Bank window pointers
 * ===================================================================== */
const BYTE *ROMBANK[4]  = { NULL, NULL, NULL, NULL };
const BYTE *PPUBANK[12] = { NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL };
BYTE *SRAMBANK = SRAM;

/* =====================================================================
 *  6502 CPU registers
 * ===================================================================== */
WORD  PC = 0;
BYTE  SP = 0xFF;
BYTE  A  = 0;
BYTE  X  = 0;
BYTE  Y  = 0;
BYTE  F  = FLAG_Z | FLAG_R | FLAG_I;

/* =====================================================================
 *  CPU interrupt state
 * ===================================================================== */
BYTE  NMI_State   = 0;
BYTE  NMI_Wiring  = 0;
BYTE  IRQ_State   = 0;
BYTE  IRQ_Wiring  = 0;

/* =====================================================================
 *  CPU clock counters
 * ===================================================================== */
WORD   g_wPassedClocks  = 0;
DWORD  g_wCurrentClocks = 0;

/* =====================================================================
 *  PPU registers
 * ===================================================================== */
BYTE  PPU_R0       = 0;
BYTE  PPU_R1       = 0;
BYTE  PPU_R2       = 0;
BYTE  PPU_R3       = 0;
BYTE  PPU_R7       = 0;
WORD  PPU_Addr     = 0;
WORD  PPU_Temp     = 0;
BYTE  PPU_Scr_H_Bit = 0;
BYTE  PPU_Latch    = 0;

/* =====================================================================
 *  PPU rendering state
 * ===================================================================== */
WORD  PPU_LineBuf[256];
WORD *g_wScanLine      = PPU_LineBuf;
int   PPU_UpDown_Clip  = 4;    /* Clip 4 lines at top and bottom */
int   g_nScanLine      = 0;
BYTE  g_byFrameSkip    = 0;

/* =====================================================================
 *  Mapper state
 * ===================================================================== */
int   MapperNo       = 0;
int   ROM_Mirroring  = 0;
int   ROM_SRAM       = 0;
int   ROM_Trainer    = 0;
int   ROM_FourScr    = 0;
BYTE  SRAMwritten    = 0;
void (*MapperInit)(void) = NULL;

/* =====================================================================
 *  Joypad
 * ===================================================================== */
DWORD  PAD1_Latch = 0;
DWORD  PAD2_Latch = 0;
BYTE   PAD1_Bit   = 0;
BYTE   PAD2_Bit   = 0;

/* =====================================================================
 *  Unofficial opcode table
 *  1 = unofficial; 0 = official
 *  Covers the stable group from specs/20_cpu.md
 * ===================================================================== */
BYTE g_unofficialOpcodeTable[256] = {
    /* 00-0F */ 0,0,0,1, 0,0,0,1, 0,0,0,0, 0,0,0,1,
    /* 10-1F */ 0,0,0,1, 0,0,0,1, 0,0,0,1, 0,0,0,1,
    /* 20-2F */ 0,0,0,1, 0,0,0,1, 0,0,0,0, 0,0,0,1,
    /* 30-3F */ 0,0,0,1, 0,0,0,1, 0,0,0,1, 0,0,0,1,
    /* 40-4F */ 0,0,0,1, 0,0,0,1, 0,0,0,0, 0,0,0,1,
    /* 50-5F */ 0,0,0,1, 0,0,0,1, 0,0,0,1, 0,0,0,1,
    /* 60-6F */ 0,0,0,1, 0,0,0,1, 0,0,0,0, 0,0,0,1,
    /* 70-7F */ 0,0,0,1, 0,0,0,1, 0,0,0,1, 0,0,0,1,
    /* 80-8F */ 0,0,0,1, 0,0,0,1, 0,0,0,0, 0,0,0,1,
    /* 90-9F */ 0,0,0,0, 0,0,0,1, 0,0,0,0, 0,0,0,0,
    /* A0-AF */ 0,0,0,1, 0,0,0,1, 0,0,0,0, 0,0,0,1,
    /* B0-BF */ 0,0,0,1, 0,0,0,1, 0,0,0,1, 0,0,0,1,
    /* C0-CF */ 0,0,0,1, 0,0,0,1, 0,0,0,0, 0,0,0,1,
    /* D0-DF */ 0,0,0,1, 0,0,0,1, 0,0,0,1, 0,0,0,1,
    /* E0-EF */ 0,0,0,1, 0,0,0,1, 0,0,1,0, 0,0,0,1,
    /* F0-FF */ 0,0,0,1, 0,0,0,1, 0,0,0,1, 0,0,0,1,
};

/* =====================================================================
 *  APU wave buffers
 * ===================================================================== */
BYTE g_ApuWave[5][APU_WAVE_BUF_SIZE];
