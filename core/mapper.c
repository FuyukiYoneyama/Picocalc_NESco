/*
 * mapper.c — Mapper abstraction layer
 *
 * Implements:
 *   - Global function pointer variables
 *   - Mapper_Select(): searches MapperTable and calls init
 *   - InfoNES_Mirroring(): sets PPUBANK[8..11]
 *   - MapperTable[]: supported mapper registry (M0 + M30 for now;
 *     additional mappers can be added without touching core logic)
 *
 * Spec reference: specs/30_mapper_interface.md
 *
 * Part of Picocalc_NESco
 * MIT License
 */

#include "mapper.h"
#include "nes_globals.h"
#include "nes_system.h"

/* =====================================================================
 *  Global mapper callback pointers
 * ===================================================================== */
void  (*MapperWrite)      (WORD addr, BYTE data) = Map0_Write;
void  (*MapperSram)       (WORD addr, BYTE data) = Map0_Sram;
void  (*MapperApu)        (WORD addr, BYTE data) = Map0_Apu;
BYTE  (*MapperReadApu)    (WORD addr)            = Map0_ReadApu;
void  (*MapperVSync)      (void)                 = Map0_VSync;
void  (*MapperHSync)      (void)                 = Map0_HSync;
void  (*MapperPPU)        (WORD addr)            = Map0_PPU;
void  (*MapperRenderScreen)(BYTE mode)           = Map0_RenderScreen;

/* =====================================================================
 *  MapperTable — registered mapper initialisers
 *  Terminated by {-1, NULL}.
 *  Add more mappers here as they are implemented.
 * ===================================================================== */
const MAPPER_ENTRY MapperTable[] = {
    {  0, Map0_Init  },
    { 30, Map30_Init },
    { -1, NULL       }   /* sentinel */
};

/* =====================================================================
 *  Mapper_Select
 * ===================================================================== */
int Mapper_Select(int mapperNo) {
    for (int i = 0; MapperTable[i].pMapperInit != NULL; i++) {
        if (MapperTable[i].nMapperNo == mapperNo) {
            MapperInit = MapperTable[i].pMapperInit;
            MapperInit();
            return 0;
        }
    }
    InfoNES_Error("Mapper #%d is unsupported.", mapperNo);
    return -1;
}

/* =====================================================================
 *  InfoNES_Mirroring — configure nametable bank pointers
 *
 *  PPUBANK[8..11] are set to alias into PPURAM's nametable area.
 *  Layout (from specs/30_mapper_interface.md):
 *    VRAMPAGE(0) = PPURAM + 0x2000   (NT A, 1 KB)
 *    VRAMPAGE(1) = PPURAM + 0x2400   (NT B, 1 KB)
 *    VRAMPAGE(2) = PPURAM + 0x2800   (NT C, 1 KB)
 *    VRAMPAGE(3) = PPURAM + 0x2C00   (NT D, 1 KB)
 * ===================================================================== */
void InfoNES_Mirroring(int type) {
    const BYTE *A = VRAMPAGE(0);
    const BYTE *B = VRAMPAGE(1);
    const BYTE *C = VRAMPAGE(2);
    const BYTE *D = VRAMPAGE(3);

    switch (type) {
    case MIRROR_HORIZONTAL:   /* A A B B */
        PPUBANK[8]  = A; PPUBANK[9]  = A;
        PPUBANK[10] = B; PPUBANK[11] = B;
        break;
    case MIRROR_VERTICAL:     /* A B A B */
        PPUBANK[8]  = A; PPUBANK[9]  = B;
        PPUBANK[10] = A; PPUBANK[11] = B;
        break;
    case MIRROR_ONESCREEN_A:  /* A A A A */
        PPUBANK[8]  = A; PPUBANK[9]  = A;
        PPUBANK[10] = A; PPUBANK[11] = A;
        break;
    case MIRROR_ONESCREEN_B:  /* B B B B */
        PPUBANK[8]  = B; PPUBANK[9]  = B;
        PPUBANK[10] = B; PPUBANK[11] = B;
        break;
    case MIRROR_FOURSCREEN:   /* A B C D */
        PPUBANK[8]  = A; PPUBANK[9]  = B;
        PPUBANK[10] = C; PPUBANK[11] = D;
        break;
    default:                  /* Fallback: horizontal */
        PPUBANK[8]  = A; PPUBANK[9]  = A;
        PPUBANK[10] = B; PPUBANK[11] = B;
        break;
    }
}
