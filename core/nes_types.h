/*
 * nes_types.h — Fundamental type definitions and constants
 *
 * Part of Picocalc_NESco
 * MIT License
 * Target: RP2040 / PicoCalc (250 MHz Cortex-M0+)
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =====================================================================
 *  Primitive types
 * ===================================================================== */
typedef uint8_t  BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int8_t   SBYTE;
typedef int16_t  SWORD;
typedef int32_t  SDWORD;

/* =====================================================================
 *  6502 Flag-register bits
 * ===================================================================== */
#define FLAG_C  (1u << 0)   /* Carry */
#define FLAG_Z  (1u << 1)   /* Zero */
#define FLAG_I  (1u << 2)   /* Interrupt disable */
#define FLAG_D  (1u << 3)   /* Decimal  (defined; no effect — BCD not implemented) */
#define FLAG_B  (1u << 4)   /* Break */
#define FLAG_R  (1u << 5)   /* Reserved — always 1 */
#define FLAG_V  (1u << 6)   /* Overflow */
#define FLAG_N  (1u << 7)   /* Negative */

/* =====================================================================
 *  NES timing constants
 * ===================================================================== */
#define STEP_PER_SCANLINE   114     /* ~114 CPU cycles per NTSC scanline */
#define TOTAL_SCANLINES     262     /* 0–261 per NTSC frame */
#define VBLANK_SCANLINE     241     /* VBlank starts here */
#define VBLANK_END_SCANLINE 261     /* VBlank ends / V-scroll reload */

/* =====================================================================
 *  NES memory sizes
 * ===================================================================== */
#define NES_RAM_SIZE    0x0800u  /* 2 KB internal work RAM */
#define NES_SRAM_SIZE   0x2000u  /* 8 KB battery-backed SRAM */
#define SPRRAM_SIZE     256u     /* Sprite RAM (64 sprites × 4 bytes) */
#define PPURAM_SIZE     0xA000u  /* 40 KB: 8 KB CHR area + 8 KB NT × 4 */
#define DRAM_SIZE       0xA000u  /* 40 KB mapper scratch (unused by M0/M30) */

/* =====================================================================
 *  Nametable / PPU bank indices
 * ===================================================================== */
#define NAME_TABLE0  8
#define NAME_TABLE1  9
#define NAME_TABLE2  10
#define NAME_TABLE3  11

/* =====================================================================
 *  Mirroring modes (InfoNES_Mirroring argument)
 * ===================================================================== */
#define MIRROR_HORIZONTAL   0   /* H: A A B B */
#define MIRROR_VERTICAL     1   /* V: A B A B */
#define MIRROR_ONESCREEN_A  2   /* Single-screen lower bank A */
#define MIRROR_ONESCREEN_B  3   /* Single-screen upper bank B */
#define MIRROR_FOURSCREEN   4   /* Four-screen: A B C D */

/* =====================================================================
 *  iNES 1.0 / NES 2.0 header layout
 * ===================================================================== */
typedef struct {
    BYTE byID[4];       /* 'N','E','S',0x1A                        */
    BYTE byRomSize;     /* Number of 16 KB PRG-ROM pages           */
    BYTE byVRomSize;    /* Number of 8 KB CHR-ROM pages (0=CHR-RAM)*/
    BYTE byInfo1;       /* Mapper low nibble; flags (mirror,SRAM…) */
    BYTE byInfo2;       /* Mapper high nibble; flags (NES 2.0)     */
    BYTE byReserve[8];  /* Padding / NES 2.0 extended fields       */
} NES_HEADER;

/* byInfo1 bit masks */
#define INFO1_MIRROR_V      (1u << 0)  /* 0=horizontal, 1=vertical   */
#define INFO1_SRAM          (1u << 1)  /* Battery-backed SRAM present */
#define INFO1_TRAINER       (1u << 2)  /* 512-byte trainer present    */
#define INFO1_FOURSCREEN    (1u << 3)  /* Four-screen VRAM layout     */
#define INFO1_MAPPER_LO     0xF0u      /* Mapper number bits [3:0]    */

/* NES 2.0 detection */
#define IS_NES20(h)  (((h).byInfo2 & 0x0Cu) == 0x08u)

/* =====================================================================
 *  ROM bank address macros
 *  (require ROM, VROM, PPURAM, NesHeader to be in scope)
 * ===================================================================== */
#define ROMPAGE(a)      (&ROM[(DWORD)(a) * 0x2000u])
#define ROMLASTPAGE(a)  (&ROM[(DWORD)NesHeader.byRomSize * 0x4000u \
                               - ((DWORD)(a) + 1u) * 0x2000u])
#define VROMPAGE(a)     (&VROM[(DWORD)(a) * 0x400u])
#define CRAMPAGE(a)     (&PPURAM[((DWORD)(a) & 0x1Fu) * 0x400u])
#define VRAMPAGE(a)     (&PPURAM[0x2000u + (DWORD)(a) * 0x400u])

/* =====================================================================
 *  RP2040 RAM-placement attribute
 *  — wraps __not_in_flash_func() from Pico SDK
 *  — on non-Pico targets (unit tests etc.) compiles to identity
 * ===================================================================== */
#ifdef PICO_BUILD
#  include "pico.h"
#  define RAMFUNC(f)  __not_in_flash_func(f)
#else
#  define RAMFUNC(f)  f
#endif

/* =====================================================================
 *  Utility macros
 * ===================================================================== */
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))

/* Suppress unused-parameter warnings */
#define UNUSED(x)  ((void)(x))
