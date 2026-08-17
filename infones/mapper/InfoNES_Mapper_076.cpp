/*===================================================================*/
/*                                                                   */
/*                    Mapper 76 (Namcot 109)                         */
/*                                                                   */
/*===================================================================*/

/* Mapper 76 is the Namcot 3446 CHR-wiring variant of the Namco 108
 * family.  Selectors 2..5 expose four 2 KiB CHR banks; the original
 * Mapper 206 selectors 0 and 1 are not connected on this board. */

BYTE Map76_Reg;
BYTE Map76_Regs[8];

static unsigned Map76_PrgPageCount()
{
  return (unsigned)NesHeader.byRomSize << 1;
}

static unsigned Map76_ChrPageCount()
{
  /* InfoNES addresses CHR in 1 KiB pages. */
  return (unsigned)NesHeader.byVRomSize << 3;
}

static void Map76_SetCpuBanks()
{
  const unsigned prgPages = Map76_PrgPageCount();

  if (prgPages == 0)
    return;

  ROMBANK0 = ROMPAGE((unsigned)Map76_Regs[6] % prgPages);
  ROMBANK1 = ROMPAGE((unsigned)Map76_Regs[7] % prgPages);
  ROMBANK2 = ROMLASTPAGE(1);
  ROMBANK3 = ROMLASTPAGE(0);
}

static void Map76_SetChrBanks()
{
  const unsigned chrPages = Map76_ChrPageCount();

  if (chrPages == 0)
    return;

  /* Mapper 76 uses four 2 KiB banks across the entire pattern table. */
  for (unsigned slot = 0; slot < 4; ++slot)
  {
    const unsigned bank = (unsigned)(Map76_Regs[slot + 2] & 0x3f);
    const unsigned page = (bank * 2u) % chrPages;
    PPUBANK[slot * 2] = VROMPAGE(page);
    PPUBANK[slot * 2 + 1] = VROMPAGE((page + 1u) % chrPages);
  }

  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 76                                             */
/*-------------------------------------------------------------------*/
void Map76_Init()
{
  MapperInit = Map76_Init;
  MapperWrite = Map76_Write;
  MapperSram = Map0_Sram;
  MapperApu = Map0_Apu;
  MapperReadApu = Map0_ReadApu;
  MapperVSync = Map0_VSync;
  MapperHSync = Map0_HSync;
  MapperPPU = Map0_PPU;
  MapperRenderScreen = Map0_RenderScreen;

  SRAMBANK = SRAM;

  /* Match the deterministic power-on state used by Mesen2's MMC3 base. */
  Map76_Reg = 0;
  Map76_Regs[0] = 0;
  Map76_Regs[1] = 2;
  Map76_Regs[2] = 4;
  Map76_Regs[3] = 5;
  Map76_Regs[4] = 6;
  Map76_Regs[5] = 7;
  Map76_Regs[6] = 0;
  Map76_Regs[7] = 1;

  Map76_SetCpuBanks();
  Map76_SetChrBanks();

  /* Mapper 76 has fixed board mirroring; the header supplies H/V and the
   * four-screen flag is preserved when present. */
  InfoNES_Mirroring(ROM_FourScr ? 4 : ROM_Mirroring);

  K6502_Set_Int_Wiring(1, 1);
}

/*-------------------------------------------------------------------*/
/*  Mapper 76 Write Function                                          */
/*-------------------------------------------------------------------*/
void Map76_Write(WORD wAddr, BYTE byData)
{
  if (wAddr < 0x8000)
    return;

  /* Mesen2's Namco108 base aliases all mapper writes through A15/A0;
   * Mapper 76 has no distinct A000/C000/E000 control registers. */
  switch (wAddr & 0x8001)
  {
    case 0x8000:
      Map76_Reg = byData & 0x07;
      break;

    case 0x8001:
      switch (Map76_Reg)
      {
        case 0:
        case 1:
          /* These are the inaccessible original Mapper 206 CHR registers. */
          Map76_Regs[Map76_Reg] = byData & 0xfe;
          break;

        case 2:
        case 3:
        case 4:
        case 5:
          Map76_Regs[Map76_Reg] = byData & 0x3f;
          Map76_SetChrBanks();
          break;

        case 6:
        case 7:
          Map76_Regs[Map76_Reg] = byData & 0x0f;
          Map76_SetCpuBanks();
          break;
      }
      break;
  }
}
