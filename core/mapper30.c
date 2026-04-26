/*
 * mapper30.c — Mapper 30: UNROM-512 (NESdev mapper 030)
 *
 * Features:
 *  - PRG-ROM: 32 switchable banks × 16 KB (upper 16 KB fixed to last page)
 *  - CHR-RAM: up to 32 KB (4 banks × 8 KB), heap-allocated
 *  - Nametable mirroring: horizontal, vertical, one-screen, four-screen
 *  - Submapper variants (0–4): bus conflict, latch address range
 *  - Flash emulation: SST 39SF-compatible byte-write + sector erase
 *    (active when ROM_SRAM flag set in header)
 *  - PRG overlay: up to 2 slots × 16 KB for in-game Flash writes
 *
 * Open Questions:
 *  OQ-M30-1: Flash command addresses ($9555/$AAAA/$C000) differ from
 *             canonical SST 39SF ($5555/$2AAA). Implemented as-per-spec.
 *  OQ-M30-2: Diagnostic logging disabled in this build.
 *
 * Spec reference: specs/31_mapper30.md
 *
 * Part of Picocalc_NESco
 * MIT License
 */

#include "mapper.h"
#include "nes_globals.h"
#include "nes_system.h"
#include "cpu.h"

#include <stdlib.h>
#include <string.h>

/* =====================================================================
 *  Flash state machine states
 * ===================================================================== */
typedef enum {
    FS_IDLE       = 0,
    FS_C1         = 1,   /* saw C000←0x01 */
    FS_AA         = 2,   /* saw 9555←0xAA */
    FS_C0         = 3,   /* saw C000←0x00 */
    FS_55         = 4,   /* saw AAAA←0x55 */
    FS_C1_2       = 5,   /* saw C000←0x01 again */
    FS_WRITE_BANK = 6,   /* saw 9555←0xA0; waiting for target bank */
    FS_WRITE_DATA = 7,   /* have target bank; waiting for data byte */
    FS_ERASE_C1   = 8,   /* saw 9555←0x80 from C1_2 */
    FS_ERASE_AA   = 9,
    FS_ERASE_C0   = 10,
    FS_ERASE_55   = 11,
    FS_ERASE_BANK = 12,  /* waiting for erase target bank */
    FS_ERASE_DATA = 13,  /* waiting for 0x30 confirm */
} FlashState;

/* =====================================================================
 *  Mapper 30 state
 * ===================================================================== */
static BYTE   Map30_Latch;
static BYTE   Map30_NametableMode;
static BYTE   Map30_Flashable;
static BYTE   Map30_Submapper;

/* Flash state machine */
static FlashState Map30_FlashState;
static BYTE       Map30_FlashIdMode;
static BYTE       Map30_FlashTargetBank;

/* Heap allocations */
static BYTE  *Map30_ChrRam;
static DWORD  Map30_ChrRamSize;
static BYTE  *Map30_FlashIdBank;      /* 8 KB dummy ROM for Flash ID response */

/* PRG overlays (up to 2 × 16 KB) */
static BYTE   Map30_PrgOverlayUsed[2];
static BYTE   Map30_PrgOverlayBank[2];
static BYTE  *Map30_PrgOverlayData[2];

/* Mirroring mode constants (per spec) */
#define M30_MIRROR_H          0
#define M30_MIRROR_V          1
#define M30_MIRROR_ONESCREEN  2
#define M30_MIRROR_FOURSCREEN 3

/* =====================================================================
 *  Forward declarations
 * ===================================================================== */
static void Map30_Write(WORD addr, BYTE data);
static void Map30_ApplyLatch(BYTE data);
static void Map30_SetPrgBanks(BYTE bank);
static void Map30_SetChrBanks(BYTE bank);
static void Map30_UpdateMirroring(BYTE data);
static void Map30_ProcessFlashWrite(WORD addr, BYTE data);
static BYTE *Map30_GetOrAllocOverlay(BYTE bank16);

/* =====================================================================
 *  Map30_Release — free all heap allocations
 * ===================================================================== */
void Map30_Release(void) {
    for (int i = 0; i < 2; i++) {
        if (Map30_PrgOverlayData[i]) {
            free(Map30_PrgOverlayData[i]);
            Map30_PrgOverlayData[i] = NULL;
        }
        Map30_PrgOverlayUsed[i] = 0;
        Map30_PrgOverlayBank[i] = 0;
    }
    if (Map30_ChrRam) {
        free(Map30_ChrRam);
        Map30_ChrRam    = NULL;
        Map30_ChrRamSize = 0;
    }
    if (Map30_FlashIdBank) {
        free(Map30_FlashIdBank);
        Map30_FlashIdBank = NULL;
    }
}

/* =====================================================================
 *  Map30_Init
 * ===================================================================== */
void Map30_Init(void) {
    /* ---- Set callbacks ---- */
    MapperWrite       = Map30_Write;
    MapperSram        = Map0_Sram;
    MapperApu         = Map0_Apu;
    MapperReadApu     = Map0_ReadApu;
    MapperVSync       = Map0_VSync;
    MapperHSync       = Map0_HSync;
    MapperPPU         = Map0_PPU;
    MapperRenderScreen = Map0_RenderScreen;

    SRAMBANK = SRAM;

    /* ---- Flashable: set if battery SRAM flag present ---- */
    Map30_Flashable = ROM_SRAM ? 1 : 0;

    /* ---- Submapper from NES 2.0 header ---- */
    if (IS_NES20(NesHeader)) {
        Map30_Submapper = NesHeader.byReserve[0] >> 4;
    } else {
        Map30_Submapper = 0;
    }

    /* ---- Nametable mode from header ---- */
    BYTE mirror_bits = NesHeader.byInfo1 & 0x09u;
    switch (mirror_bits) {
    case 0x00: Map30_NametableMode = M30_MIRROR_H;          break;
    case 0x01: Map30_NametableMode = M30_MIRROR_V;          break;
    case 0x08: Map30_NametableMode = M30_MIRROR_ONESCREEN;  break;
    default:   Map30_NametableMode = M30_MIRROR_FOURSCREEN; break;
    }
    /* Submapper 3: mirroring controlled per-write via D7 */
    if (Map30_Submapper == 3) {
        Map30_NametableMode = M30_MIRROR_H;
    }

    /* ---- CHR-RAM size ---- */
    DWORD chrRamSize = 0x8000u;  /* Default: 32 KB */
    if (IS_NES20(NesHeader)) {
        BYTE vramShift = NesHeader.byReserve[3] & 0x0Fu;
        if (vramShift != 0) {
            chrRamSize = (DWORD)(64u << vramShift);
        }
        if (NesHeader.byVRomSize != 0) {
            chrRamSize = 0;   /* CHR-ROM provided; no CHR-RAM */
        }
    }
    /* Clamp [8 KB, 32 KB] */
    if (chrRamSize < 0x2000u && chrRamSize > 0) chrRamSize = 0x2000u;
    if (chrRamSize > 0x8000u) chrRamSize = 0x8000u;

    /* ---- Release previous allocations ---- */
    Map30_Release();

    /* ---- Allocate CHR-RAM ---- */
    Map30_ChrRamSize = chrRamSize;
    if (chrRamSize > 0) {
        Map30_ChrRam = (BYTE *)calloc(1, chrRamSize);
        if (!Map30_ChrRam) {
            InfoNES_Error("Mapper 30: CHR-RAM alloc failed (%lu bytes)", (unsigned long)chrRamSize);
            return;
        }
    }

    /* ---- Allocate Flash ID buffer (8 KB, filled 0xFF) ---- */
    Map30_FlashIdBank = (BYTE *)malloc(0x2000u);
    if (!Map30_FlashIdBank) {
        InfoNES_Error("Mapper 30: FlashIdBank alloc failed");
        return;
    }
    memset(Map30_FlashIdBank, 0xFF, 0x2000u);

    /* Set Flash ID bytes */
    Map30_FlashIdBank[0] = 0xBF;   /* Manufacturer: SST */
    BYTE romSz = NesHeader.byRomSize;
    if      (romSz <=  16) Map30_FlashIdBank[1] = 0xB5;  /* < 256 KB */
    else if (romSz <=  32) Map30_FlashIdBank[1] = 0xB6;  /* < 512 KB */
    else                   Map30_FlashIdBank[1] = 0xB7;  /* >= 512 KB */

    /* ---- Reset flash state machine ---- */
    Map30_FlashState    = FS_IDLE;
    Map30_FlashIdMode   = 0;
    Map30_FlashTargetBank = 0;

    /* ---- Apply initial latch (bank 0) ---- */
    Map30_ApplyLatch(0);

    /* ---- CPU interrupt wiring ---- */
    K6502_Set_Int_Wiring(1, 1);
}

/* =====================================================================
 *  Map30_SetPrgBanks — wire ROMBANK[0..3]
 * ===================================================================== */
static void Map30_SetPrgBanks(BYTE bank) {
    BYTE bankCount = NesHeader.byRomSize ? NesHeader.byRomSize * 2u : 1u;
    bank &= 0x1Fu;
    bank %= bankCount;

    BYTE b16 = bank;   /* 16 KB bank index */

    /* Lower two 8 KB banks: switchable */
    for (int i = 0; i < 2; i++) {
        BYTE *overlay = NULL;
        /* Check if this 16 KB bank has an overlay */
        for (int s = 0; s < 2; s++) {
            if (Map30_PrgOverlayUsed[s] && Map30_PrgOverlayBank[s] == b16) {
                overlay = Map30_PrgOverlayData[s];
                break;
            }
        }
        if (overlay) {
            ROMBANK[i] = overlay + (DWORD)i * 0x2000u;
        } else {
            ROMBANK[i] = ROMPAGE((DWORD)b16 * 2u + (DWORD)i);
        }
    }

    /* Upper two 8 KB banks: fixed to last 16 KB of ROM */
    ROMBANK[2] = ROMLASTPAGE(1);
    ROMBANK[3] = ROMLASTPAGE(0);
}

/* =====================================================================
 *  Map30_SetChrBanks — wire PPUBANK[0..7] from CHR-RAM
 * ===================================================================== */
static void Map30_SetChrBanks(BYTE bank) {
    if (!Map30_ChrRam || Map30_ChrRamSize == 0) return;

    BYTE bankMask;
    if      (Map30_ChrRamSize >= 0x8000u) bankMask = 0x03u;  /* 32 KB: 4 banks */
    else if (Map30_ChrRamSize >= 0x4000u) bankMask = 0x01u;  /* 16 KB: 2 banks */
    else                                   bankMask = 0x00u;  /* 8 KB: 1 bank  */

    DWORD page = (DWORD)(bank & bankMask) * 8u;  /* 8 × 1 KB pages */
    for (int i = 0; i < 8; i++) {
        PPUBANK[i] = Map30_ChrRam + (page + (DWORD)i) * 0x400u;
    }
}

/* =====================================================================
 *  Map30_UpdateMirroring
 * ===================================================================== */
static void Map30_UpdateMirroring(BYTE data) {
    switch (Map30_NametableMode) {
    case M30_MIRROR_H:
        InfoNES_Mirroring(MIRROR_HORIZONTAL);
        break;
    case M30_MIRROR_V:
        InfoNES_Mirroring(MIRROR_VERTICAL);
        break;
    case M30_MIRROR_ONESCREEN:
        InfoNES_Mirroring((data & 0x80u) ? MIRROR_ONESCREEN_A : MIRROR_ONESCREEN_B);
        break;
    case M30_MIRROR_FOURSCREEN:
        /* Use last 8 KB of CHR-RAM as four nametable pages (1 KB each) */
        if (Map30_ChrRam && Map30_ChrRamSize >= 0x8000u) {
            BYTE *base = Map30_ChrRam + Map30_ChrRamSize - 0x2000u;
            PPUBANK[8]  = base + 0x000u;
            PPUBANK[9]  = base + 0x400u;
            PPUBANK[10] = base + 0x800u;
            PPUBANK[11] = base + 0xC00u;
        } else {
            InfoNES_Mirroring(MIRROR_FOURSCREEN);
        }
        break;
    }
    /* Submapper 3: D7 controls H/V */
    if (Map30_Submapper == 3) {
        InfoNES_Mirroring((data & 0x80u) ? MIRROR_VERTICAL : MIRROR_HORIZONTAL);
    }
}

/* =====================================================================
 *  Map30_ApplyLatch — decode latch and apply all bank/mirror settings
 * ===================================================================== */
static void Map30_ApplyLatch(BYTE data) {
    Map30_Latch = data;
    Map30_SetPrgBanks(data & 0x1Fu);
    Map30_SetChrBanks((data >> 5) & 0x03u);
    Map30_UpdateMirroring(data);
}

/* =====================================================================
 *  Map30_GetOrAllocOverlay — get or allocate PRG overlay for a 16 KB bank
 * ===================================================================== */
static BYTE *Map30_GetOrAllocOverlay(BYTE bank16) {
    /* Check if already exists */
    for (int s = 0; s < 2; s++) {
        if (Map30_PrgOverlayUsed[s] && Map30_PrgOverlayBank[s] == bank16) {
            return Map30_PrgOverlayData[s];
        }
    }
    /* Find free slot */
    for (int s = 0; s < 2; s++) {
        if (!Map30_PrgOverlayUsed[s]) {
            Map30_PrgOverlayData[s] = (BYTE *)malloc(0x4000u);
            if (!Map30_PrgOverlayData[s]) {
                InfoNES_Error("Mapper 30: PRG overlay alloc failed");
                return NULL;
            }
            /* Initialize from ROM */
            BYTE bankCount = NesHeader.byRomSize ? NesHeader.byRomSize * 2u : 1u;
            BYTE b = bank16 % bankCount;
            memcpy(Map30_PrgOverlayData[s],
                   ROMPAGE((DWORD)b * 2u),
                   0x4000u);
            Map30_PrgOverlayUsed[s] = 1;
            Map30_PrgOverlayBank[s] = bank16;
            return Map30_PrgOverlayData[s];
        }
    }
    /* No free slot */
    InfoNES_Error("Mapper 30: PRG overlay limit reached (max 2 × 16 KB)");
    return NULL;
}

/* =====================================================================
 *  Map30_ProcessFlashWrite — Flash emulation state machine
 * ===================================================================== */
static void Map30_ProcessFlashWrite(WORD wAddr, BYTE byData) {
    /* Soft reset: any write of 0xF0 returns to IDLE and exits ID mode */
    if (byData == 0xF0u) {
        Map30_FlashState  = FS_IDLE;
        if (Map30_FlashIdMode) {
            Map30_FlashIdMode = 0;
            /* Restore normal ROM banks */
            Map30_SetPrgBanks(Map30_Latch & 0x1Fu);
        }
        return;
    }

    switch (Map30_FlashState) {
    case FS_IDLE:
        if (wAddr == 0xC000u && byData == 0x01u) Map30_FlashState = FS_C1;
        break;
    case FS_C1:
        if (wAddr == 0x9555u && byData == 0xAAu) Map30_FlashState = FS_AA;
        else                                       Map30_FlashState = FS_IDLE;
        break;
    case FS_AA:
        if (wAddr == 0xC000u && byData == 0x00u) Map30_FlashState = FS_C0;
        else                                       Map30_FlashState = FS_IDLE;
        break;
    case FS_C0:
        if (wAddr == 0xAAAAu && byData == 0x55u) Map30_FlashState = FS_55;
        else                                       Map30_FlashState = FS_IDLE;
        break;
    case FS_55:
        if (wAddr == 0xC000u && byData == 0x01u) Map30_FlashState = FS_C1_2;
        else                                       Map30_FlashState = FS_IDLE;
        break;
    case FS_C1_2:
        if (wAddr == 0x9555u) {
            if (byData == 0x90u) {
                /* Enter Flash ID mode */
                Map30_FlashIdMode = 1;
                ROMBANK[0] = Map30_FlashIdBank;
                ROMBANK[1] = Map30_FlashIdBank;
                Map30_FlashState = FS_IDLE;
            } else if (byData == 0xA0u) {
                Map30_FlashState = FS_WRITE_BANK;
            } else if (byData == 0x80u) {
                Map30_FlashState = FS_ERASE_C1;
            } else {
                Map30_FlashState = FS_IDLE;
            }
        } else {
            Map30_FlashState = FS_IDLE;
        }
        break;

    /* ---- Byte write sequence ---- */
    case FS_WRITE_BANK:
        if (wAddr >= 0xC000u) {
            Map30_FlashTargetBank = byData & 0x1Fu;
            Map30_FlashState      = FS_WRITE_DATA;
        } else {
            Map30_FlashState = FS_IDLE;
        }
        break;
    case FS_WRITE_DATA:
        if (wAddr < 0xC000u) {
            /* AND-write to overlay */
            BYTE *overlay = Map30_GetOrAllocOverlay(Map30_FlashTargetBank);
            if (overlay) {
                overlay[wAddr & 0x3FFFu] &= byData;
                /* Re-wire if this bank is currently active */
                Map30_SetPrgBanks(Map30_Latch & 0x1Fu);
            }
            Map30_FlashState = FS_IDLE;
        } else {
            Map30_FlashState = FS_IDLE;
        }
        break;

    /* ---- Sector erase sequence ---- */
    case FS_ERASE_C1:
        if (wAddr == 0x9555u && byData == 0xAAu) Map30_FlashState = FS_ERASE_AA;
        else                                       Map30_FlashState = FS_IDLE;
        break;
    case FS_ERASE_AA:
        if (wAddr == 0xC000u && byData == 0x00u) Map30_FlashState = FS_ERASE_C0;
        else                                       Map30_FlashState = FS_IDLE;
        break;
    case FS_ERASE_C0:
        if (wAddr == 0xAAAAu && byData == 0x55u) Map30_FlashState = FS_ERASE_55;
        else                                       Map30_FlashState = FS_IDLE;
        break;
    case FS_ERASE_55:
        if (wAddr >= 0xC000u) {
            Map30_FlashTargetBank = byData & 0x1Fu;
            Map30_FlashState      = FS_ERASE_BANK;
        } else {
            Map30_FlashState = FS_IDLE;
        }
        break;
    case FS_ERASE_BANK:
        Map30_FlashState = FS_ERASE_DATA;
        break;
    case FS_ERASE_DATA:
        if (wAddr < 0xC000u && byData == 0x30u) {
            /* Erase 4 KB sector: fill 0xFF at wAddr & 0x3000 within overlay */
            BYTE *overlay = Map30_GetOrAllocOverlay(Map30_FlashTargetBank);
            if (overlay) {
                WORD sector = wAddr & 0x3000u;
                memset(overlay + sector, 0xFF, 0x1000u);
                Map30_SetPrgBanks(Map30_Latch & 0x1Fu);
            }
        }
        Map30_FlashState = FS_IDLE;
        break;

    default:
        Map30_FlashState = FS_IDLE;
        break;
    }
}

/* =====================================================================
 *  Map30_Write — CPU write to $8000–$FFFF
 * ===================================================================== */
static void Map30_Write(WORD wAddr, BYTE byData) {
    if (!Map30_Flashable) {
        /* ---- Non-flashable path ---- */
        /* Submapper 1, 3, 4: latch only responds to $C000–$FFFF */
        if ((Map30_Submapper == 1 || Map30_Submapper == 3 || Map30_Submapper == 4)
             && wAddr < 0xC000u) {
            return;
        }
        /* Bus conflict: submapper 0 (no battery) or submapper 2 */
        if (Map30_Submapper == 0 || Map30_Submapper == 2) {
            BYTE bank_idx = (Map30_Latch & 0x1Fu) * 2u
                            + (BYTE)((wAddr - 0x8000u) >> 13);
            BYTE rom_byte = ROMPAGE(bank_idx)[wAddr & 0x1FFFu];
            byData &= rom_byte;
        }
        Map30_ApplyLatch(byData);
    } else {
        /* ---- Flashable path ---- */
        if (wAddr < 0xC000u) {
            /* Below $C000: only Flash state machine */
            Map30_ProcessFlashWrite(wAddr, byData);
        } else {
            /* $C000–$FFFF: apply latch first, then Flash state machine */
            Map30_ApplyLatch(byData);
            Map30_ProcessFlashWrite(wAddr, byData);
        }
    }
}
