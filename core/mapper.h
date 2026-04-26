/*
 * mapper.h — Mapper abstraction interface
 *
 * Each mapper sets nine function pointers at init time.
 * Mapper 0 provides no-op defaults for all callbacks.
 *
 * Spec reference: specs/30_mapper_interface.md
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
 *  Mapper callback function pointers
 *  Set by each mapper's init; must all be non-null during emulation.
 * ===================================================================== */
extern void   (*MapperWrite)     (WORD addr, BYTE data);
extern void   (*MapperSram)      (WORD addr, BYTE data);
extern void   (*MapperApu)       (WORD addr, BYTE data);
extern BYTE   (*MapperReadApu)   (WORD addr);
extern void   (*MapperVSync)     (void);
extern void   (*MapperHSync)     (void);
extern void   (*MapperPPU)       (WORD addr);
extern void   (*MapperRenderScreen)(BYTE mode);

/* MapperInit: pointer to the active mapper's init function.
 * Stored so soft-reset can re-call init without searching MapperTable. */
extern void   (*MapperInit)      (void);

/* =====================================================================
 *  Mapper table entry
 * ===================================================================== */
typedef struct {
    int   nMapperNo;
    void (*pMapperInit)(void);
} MAPPER_ENTRY;

extern const MAPPER_ENTRY MapperTable[];

/* =====================================================================
 *  Mapper selection
 *  Called from InfoNES_Reset().
 *  Returns 0 on success, -1 if mapper not found.
 * ===================================================================== */
int Mapper_Select(int mapperNo);

/* =====================================================================
 *  InfoNES_Mirroring — configure PPUBANK[8..11]
 *  Called by mappers to set nametable layout.
 * ===================================================================== */
void InfoNES_Mirroring(int type);

/* =====================================================================
 *  Mapper 0 (NROM) init — exported for use as default in other mappers
 * ===================================================================== */
void Map0_Init(void);

/* Mapper 0 no-op callbacks (usable by other mappers) */
void   Map0_Write       (WORD addr, BYTE data);
void   Map0_Sram        (WORD addr, BYTE data);
void   Map0_Apu         (WORD addr, BYTE data);
BYTE   Map0_ReadApu     (WORD addr);
void   Map0_VSync       (void);
void   Map0_HSync       (void);
void   Map0_PPU         (WORD addr);
void   Map0_RenderScreen(BYTE mode);

/* =====================================================================
 *  Mapper 30 (UNROM-512)
 * ===================================================================== */
void Map30_Init   (void);
void Map30_Release(void);

#ifdef __cplusplus
}
#endif
