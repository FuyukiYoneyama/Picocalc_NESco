/*===================================================================*/
/*                                                                   */
/*  Common Namco 108 mapper core                                     */
/*                                                                   */
/*===================================================================*/

#ifndef INFONES_NAMCO108_COMMON_H_INCLUDED
#define INFONES_NAMCO108_COMMON_H_INCLUDED

/*
 * Mappers 76, 88, and 206 share the Namco 108 register and PRG-banking
 * core.  Their boards differ in how the six CHR selectors are wired, so
 * CHR mapping remains in each mapper implementation.  This small core
 * deliberately keeps the board-specific policy visible through an enum
 * instead of hiding it behind preprocessor macros.
 */

struct InfoNES_Namco108_State
{
  BYTE selector;
  BYTE regs[8];
};

enum InfoNES_Namco108_Layout
{
  InfoNES_Namco108_Layout_Mapper76,
  InfoNES_Namco108_Layout_Mapper88,
  InfoNES_Namco108_Layout_Mapper206
};

typedef void (*InfoNES_Namco108_SetChr)(InfoNES_Namco108_State &state);

static inline unsigned InfoNES_Namco108_PrgPageCount()
{
  return (unsigned)NesHeader.byRomSize << 1;
}

static inline unsigned InfoNES_Namco108_ChrPageCount()
{
  /* InfoNES addresses CHR in 1 KiB pages. */
  return (unsigned)NesHeader.byVRomSize << 3;
}

static inline void InfoNES_Namco108_SetCpuBanks(
    const InfoNES_Namco108_State &state)
{
  const unsigned prgPages = InfoNES_Namco108_PrgPageCount();

  if (prgPages == 0)
    return;

  /* The Namco 108 family keeps the final two 8 KiB banks fixed. */
  ROMBANK0 = ROMPAGE((unsigned)state.regs[6] % prgPages);
  ROMBANK1 = ROMPAGE((unsigned)state.regs[7] % prgPages);
  ROMBANK2 = ROMLASTPAGE(1);
  ROMBANK3 = ROMLASTPAGE(0);
}

static inline void InfoNES_Namco108_InstallHooks(
    void (*init)(), void (*write)(WORD wAddr, BYTE byData))
{
  MapperInit = init;
  MapperWrite = write;
  MapperSram = Map0_Sram;
  MapperApu = Map0_Apu;
  MapperReadApu = Map0_ReadApu;
  MapperVSync = Map0_VSync;
  MapperHSync = Map0_HSync;
  MapperPPU = Map0_PPU;
  MapperRenderScreen = Map0_RenderScreen;

  SRAMBANK = SRAM;
}

static inline void InfoNES_Namco108_Reset(
    InfoNES_Namco108_State &state,
    void (*init)(),
    void (*write)(WORD wAddr, BYTE byData),
    InfoNES_Namco108_SetChr setChr)
{
  InfoNES_Namco108_InstallHooks(init, write);

  /* Match the deterministic power-on state used by the existing tests. */
  state.selector = 0;
  state.regs[0] = 0;
  state.regs[1] = 2;
  state.regs[2] = 4;
  state.regs[3] = 5;
  state.regs[4] = 6;
  state.regs[5] = 7;
  state.regs[6] = 0;
  state.regs[7] = 1;

  InfoNES_Namco108_SetCpuBanks(state);
  setChr(state);

  /* These boards have no MMC3 mirroring register. */
  InfoNES_Mirroring(ROM_FourScr ? 4 : ROM_Mirroring);

  K6502_Set_Int_Wiring(1, 1);
}

template <InfoNES_Namco108_Layout Layout,
          InfoNES_Namco108_SetChr SetChr>
static inline void InfoNES_Namco108_Write(
    InfoNES_Namco108_State &state, WORD wAddr, BYTE byData)
{
  if (wAddr < 0x8000)
    return;

  /* Namco 108 decodes only CPU A15 and A0 for these two ports. */
  switch (wAddr & 0x8001)
  {
    case 0x8000:
      state.selector = byData & 0x07;
      break;

    case 0x8001:
      switch (state.selector)
      {
        case 0:
        case 1:
          if constexpr (Layout == InfoNES_Namco108_Layout_Mapper76)
          {
            /* Mapper 76 leaves the original 2 KiB selectors disconnected. */
            state.regs[state.selector] = byData & 0xfe;
          }
          else
          {
            /* Mapper 88 and 206 use even 2 KiB CHR bank numbers here. */
            state.regs[state.selector] = byData & 0x3e;
            SetChr(state);
          }
          break;

        case 2:
        case 3:
        case 4:
        case 5:
          state.regs[state.selector] = byData & 0x3f;
          SetChr(state);
          break;

        case 6:
        case 7:
          state.regs[state.selector] = byData & 0x0f;
          InfoNES_Namco108_SetCpuBanks(state);
          break;
      }
      break;
  }
}

#endif /* INFONES_NAMCO108_COMMON_H_INCLUDED */
