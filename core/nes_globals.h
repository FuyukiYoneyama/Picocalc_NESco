/*
 * nes_globals.h — Shared emulator state (extern declarations)
 *
 * All global variables are defined in nes_globals.c.
 * Every subsystem that needs to access shared state includes this header.
 *
 * Part of Picocalc_NESco
 * MIT License
 */
#pragma once

#include "nes_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =====================================================================
 *  ROM / memory images
 * ===================================================================== */
extern BYTE        *ROM;          /* PRG-ROM image (Flash XIP or heap)        */
extern BYTE        *VROM;         /* CHR-ROM image (NULL when CHR-RAM is used) */
extern NES_HEADER   NesHeader;    /* Parsed iNES header                        */

/* =====================================================================
 *  NES work RAM
 * ===================================================================== */
extern BYTE  RAM   [NES_RAM_SIZE];   /* Internal 2 KB work RAM ($0000–$07FF)  */
extern BYTE  SRAM  [NES_SRAM_SIZE];  /* Battery-backed SRAM ($6000–$7FFF)     */
extern BYTE  SPRRAM[SPRRAM_SIZE];    /* OAM sprite RAM (64 sprites × 4 bytes) */
extern BYTE  PPURAM[PPURAM_SIZE];    /* CHR area + nametable pages             */

/* =====================================================================
 *  PRG bank windows  ($8000–$FFFF, 4 × 8 KB)
 * ===================================================================== */
extern const BYTE *ROMBANK[4];
/* Aliases for readability */
#define ROMBANK0  (ROMBANK[0])
#define ROMBANK1  (ROMBANK[1])
#define ROMBANK2  (ROMBANK[2])
#define ROMBANK3  (ROMBANK[3])

/* =====================================================================
 *  CHR + nametable bank windows  (PPU $0000–$2FFF, 12 × 1 KB)
 * ===================================================================== */
extern const BYTE *PPUBANK[12];

/* =====================================================================
 *  SRAM window  ($6000–$7FFF, mapper-switchable)
 * ===================================================================== */
extern BYTE *SRAMBANK;

/* =====================================================================
 *  6502 CPU registers
 * ===================================================================== */
extern WORD  PC;       /* Program counter                    */
extern BYTE  SP;       /* Stack pointer (stack at $0100+)    */
extern BYTE  A;        /* Accumulator                        */
extern BYTE  X;        /* Index register X                   */
extern BYTE  Y;        /* Index register Y                   */
extern BYTE  F;        /* Processor status flags             */

/* =====================================================================
 *  CPU interrupt state
 *  NMI pending: NMI_State != NMI_Wiring
 *  IRQ pending: IRQ_State != IRQ_Wiring && !(F & FLAG_I)
 * ===================================================================== */
extern BYTE  NMI_State;
extern BYTE  NMI_Wiring;
extern BYTE  IRQ_State;
extern BYTE  IRQ_Wiring;

/* =====================================================================
 *  CPU clock counters
 * ===================================================================== */
extern WORD   g_wPassedClocks;   /* Cycles executed in current K6502_Step() */
extern DWORD  g_wCurrentClocks;  /* Monotonic total (session-level counter)  */

/* =====================================================================
 *  PPU registers (CPU-visible)
 * ===================================================================== */
extern BYTE  PPU_R0;          /* $2000 write — control 1              */
extern BYTE  PPU_R1;          /* $2001 write — control 2 / mask       */
extern BYTE  PPU_R2;          /* $2002 read  — status                 */
extern BYTE  PPU_R3;          /* $2003 write — OAM address            */
extern BYTE  PPU_R7;          /* $2007 read latch                     */

/* Loopy scroll / address model */
extern WORD  PPU_Addr;        /* Current VRAM address (v)             */
extern WORD  PPU_Temp;        /* Temporary VRAM address (t)           */
extern BYTE  PPU_Scr_H_Bit;   /* Fine-X scroll (bits [2:0])           */
extern BYTE  PPU_Latch;       /* Write toggle (0=first, 1=second)     */

/* PPU_R0 bit accessors */
#define R0_NTA       (PPU_R0 & 0x03u)           /* Nametable select [1:0] */
#define R0_INC_ADDR  ((PPU_R0 >> 2) & 1u)       /* VRAM addr increment    */
#define R0_SP_BASE   ((PPU_R0 >> 3) & 1u)       /* Sprite pattern table   */
#define R0_BG_BASE   ((PPU_R0 >> 4) & 1u)       /* BG pattern table       */
#define R0_SP_SIZE   ((PPU_R0 >> 5) & 1u)       /* Sprite size (0=8×8)    */
#define R0_NMI_VB    ((PPU_R0 >> 7) & 1u)       /* NMI on VBlank          */

/* PPU_R1 bit accessors */
#define R1_MONOCHROME ((PPU_R1) & 1u)
#define R1_CLIP_BG    ((PPU_R1 >> 1) & 1u)
#define R1_CLIP_SP    ((PPU_R1 >> 2) & 1u)
#define R1_BG_ENABLE  ((PPU_R1 >> 3) & 1u)
#define R1_SP_ENABLE  ((PPU_R1 >> 4) & 1u)
#define R1_RENDERING  (PPU_R1 & 0x18u)          /* BG or SP enable        */

/* PPU_R2 bit flags */
#define R2_SP_OVER    (1u << 5)
#define R2_SP_HIT     (1u << 6)
#define R2_IN_VBLANK  (1u << 7)

/* =====================================================================
 *  PPU scanline rendering state
 * ===================================================================== */
extern WORD  PPU_LineBuf[256];   /* Per-scanline pixel buffer (RGB444)        */
extern WORD *g_wScanLine;        /* Platform-supplied pointer to line buffer  */
extern int   PPU_UpDown_Clip;    /* Border lines clipped at top/bottom (typ 4) */
extern int   g_nScanLine;        /* Current scanline index (0–261)            */
extern BYTE  g_byFrameSkip;      /* 0=render frame, 1=skip frame drawing      */

/* =====================================================================
 *  Mapper state
 * ===================================================================== */
extern int   MapperNo;
extern int   ROM_Mirroring;   /* 0=horizontal, 1=vertical (from header)       */
extern int   ROM_SRAM;        /* 1 if battery SRAM present                    */
extern int   ROM_Trainer;     /* 1 if 512-byte trainer present                */
extern int   ROM_FourScr;     /* 1 if four-screen VRAM layout                 */
extern BYTE  SRAMwritten;     /* Dirty flag for SRAM persistence              */

/* Active mapper init function pointer (used for soft reset) */
extern void (*MapperInit)(void);

/* =====================================================================
 *  Joypad latches
 * ===================================================================== */
extern DWORD  PAD1_Latch;   /* Player 1 button state (bits 0–7 = buttons)    */
extern DWORD  PAD2_Latch;   /* Player 2 button state                          */
extern BYTE   PAD1_Bit;     /* Current bit position being read (0–23)         */
extern BYTE   PAD2_Bit;

/* System control bits in pad latches */
#define PAD_SYS_QUIT   (1UL << 16)
#define PAD_SYS_RESET  (1UL << 17)

/* =====================================================================
 *  Unofficial opcode table  (256-byte RAM table)
 *  0 = official opcode, 1 = unofficial opcode
 * ===================================================================== */
extern BYTE  g_unofficialOpcodeTable[256];

/* =====================================================================
 *  APU wave output buffers  (filled by APU; passed to SoundOutput)
 * ===================================================================== */
#define APU_WAVE_BUF_SIZE  800   /* Max samples per H-Sync at 44100 Hz */
extern BYTE  g_ApuWave[5][APU_WAVE_BUF_SIZE];

#ifdef __cplusplus
}
#endif
