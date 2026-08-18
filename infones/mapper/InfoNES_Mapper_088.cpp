/*===================================================================*/
/*                                                                   */
/*              Mapper 88 (Namco 108 CHR-A16 variant)                */
/*                                                                   */
/*===================================================================*/

/* Mapper 88 uses the Namco 108 register shape, but connects PPU A12
 * directly to CHR A16.  With the 128 KiB CHR used by known Mapper 88
 * titles, the left pattern table is therefore in the lower 64 KiB and
 * the right pattern table is in the upper 64 KiB. */

BYTE Map88_Reg;
BYTE Map88_Regs[8];

static unsigned Map88_PrgPageCount()
{
  return (unsigned)NesHeader.byRomSize << 1;
}

static unsigned Map88_ChrPageCount()
{
  /* InfoNES addresses CHR in 1 KiB pages. */
  return (unsigned)NesHeader.byVRomSize << 3;
}

static void Map88_SetCpuBanks()
{
  const unsigned prgPages = Map88_PrgPageCount();

  if (prgPages == 0)
    return;

  ROMBANK0 = ROMPAGE((unsigned)Map88_Regs[6] % prgPages);
  ROMBANK1 = ROMPAGE((unsigned)Map88_Regs[7] % prgPages);
  ROMBANK2 = ROMLASTPAGE(1);
  ROMBANK3 = ROMLASTPAGE(0);
}

static void Map88_SetChrBanks()
{
  const unsigned chrPages = Map88_ChrPageCount();

  if (chrPages == 0)
    return;

  /* R0/R1 select even 1 KiB pages in the lower 64 KiB.  The low bit is
   * not connected because these registers select 2 KiB banks. */
  const unsigned chr01 = (unsigned)(Map88_Regs[0] & 0x3e) % chrPages;
  const unsigned chr23 = (unsigned)(Map88_Regs[1] & 0x3e) % chrPages;

  PPUBANK[0] = VROMPAGE(chr01);
  PPUBANK[1] = VROMPAGE((chr01 + 1u) % chrPages);
  PPUBANK[2] = VROMPAGE(chr23);
  PPUBANK[3] = VROMPAGE((chr23 + 1u) % chrPages);

  /* Mapper 88 wires PPU A12 to CHR A16.  The four 1 KiB registers on
   * the right table consequently always select the upper 64 KiB. */
  PPUBANK[4] = VROMPAGE(((unsigned)(Map88_Regs[2] & 0x3f) | 0x40u) % chrPages);
  PPUBANK[5] = VROMPAGE(((unsigned)(Map88_Regs[3] & 0x3f) | 0x40u) % chrPages);
  PPUBANK[6] = VROMPAGE(((unsigned)(Map88_Regs[4] & 0x3f) | 0x40u) % chrPages);
  PPUBANK[7] = VROMPAGE(((unsigned)(Map88_Regs[5] & 0x3f) | 0x40u) % chrPages);

  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 88                                             */
/*-------------------------------------------------------------------*/
void Map88_Init()
{
  MapperInit = Map88_Init;
  MapperWrite = Map88_Write;

  MapperSram = Map0_Sram;
  MapperApu = Map0_Apu;
  MapperReadApu = Map0_ReadApu;
  MapperVSync = Map0_VSync;
  MapperHSync = Map0_HSync;
  MapperPPU = Map0_PPU;
  MapperRenderScreen = Map0_RenderScreen;

  SRAMBANK = SRAM;

  /* Match the deterministic initial state used by Mesen2's Namco108
   * base.  Titles normally establish their own banks during reset. */
  Map88_Reg = 0;
  Map88_Regs[0] = 0;
  Map88_Regs[1] = 2;
  Map88_Regs[2] = 4;
  Map88_Regs[3] = 5;
  Map88_Regs[4] = 6;
  Map88_Regs[5] = 7;
  Map88_Regs[6] = 0;
  Map88_Regs[7] = 1;

  Map88_SetCpuBanks();
  Map88_SetChrBanks();

  /* Mirroring is board-fixed; Mapper 88 has no C000 mirroring register. */
  InfoNES_Mirroring(ROM_FourScr ? 4 : ROM_Mirroring);

  K6502_Set_Int_Wiring(1, 1);
}

/*-------------------------------------------------------------------*/
/*  Mapper 88 Write Function                                          */
/*-------------------------------------------------------------------*/
void Map88_Write(WORD wAddr, BYTE byData)
{
  if (wAddr < 0x8000)
    return;

  /* Namco 108 decodes only CPU A15 and A0 for these ports.  Mapper 88
   * has no MMC3 mirroring, WRAM, or IRQ registers on the other ranges. */
  switch (wAddr & 0x8001)
  {
    case 0x8000:
      Map88_Reg = byData & 0x07;
      break;

    case 0x8001:
      switch (Map88_Reg)
      {
        case 0:
        case 1:
          /* 2 KiB bank selectors: register bit 0 is disconnected. */
          Map88_Regs[Map88_Reg] = byData & 0x3e;
          Map88_SetChrBanks();
          break;

        case 2:
        case 3:
        case 4:
        case 5:
          /* The physical CHR A16 connection supplies the upper-half bit. */
          Map88_Regs[Map88_Reg] = byData & 0x3f;
          Map88_SetChrBanks();
          break;

        case 6:
        case 7:
          Map88_Regs[Map88_Reg] = byData & 0x0f;
          Map88_SetCpuBanks();
          break;
      }
      break;
  }
}
