/*===================================================================*/
/*                                                                   */
/*                  Mapper 206 (Namco 108 / DxROM)                  */
/*                                                                   */
/*===================================================================*/

/* Mapper 206 is the Namco 108 / MIMIC-1 simplified MMC3 family.  It
 * keeps the MMC3 register shape but has no mode bits, mirroring
 * register, work RAM register, IRQ counter, or expansion audio. */

BYTE Map206_Reg;
BYTE Map206_Regs[8];

static unsigned Map206_PrgPageCount()
{
  return (unsigned)NesHeader.byRomSize << 1;
}

static unsigned Map206_ChrPageCount()
{
  return (unsigned)NesHeader.byVRomSize << 3;
}

static void Map206_SetCpuBanks()
{
  const unsigned prgPages = Map206_PrgPageCount();

  if (prgPages == 0)
    return;

  /* Mapper 206 always fixes the final two 8 KiB banks. */
  ROMBANK0 = ROMPAGE((unsigned)Map206_Regs[6] % prgPages);
  ROMBANK1 = ROMPAGE((unsigned)Map206_Regs[7] % prgPages);
  ROMBANK2 = ROMLASTPAGE(1);
  ROMBANK3 = ROMLASTPAGE(0);
}

static void Map206_SetChrBanks()
{
  const unsigned chrPages = Map206_ChrPageCount();

  if (chrPages == 0)
    return;

  /* The two left-table registers select even 2 KiB banks. */
  const unsigned chr01 = (unsigned)(Map206_Regs[0] & 0x3e) % chrPages;
  const unsigned chr23 = (unsigned)(Map206_Regs[1] & 0x3e) % chrPages;

  PPUBANK[0] = VROMPAGE(chr01);
  PPUBANK[1] = VROMPAGE((chr01 + 1u) % chrPages);
  PPUBANK[2] = VROMPAGE(chr23);
  PPUBANK[3] = VROMPAGE((chr23 + 1u) % chrPages);

  /* The right-table registers select independent 1 KiB banks. */
  PPUBANK[4] = VROMPAGE((unsigned)(Map206_Regs[2] & 0x3f) % chrPages);
  PPUBANK[5] = VROMPAGE((unsigned)(Map206_Regs[3] & 0x3f) % chrPages);
  PPUBANK[6] = VROMPAGE((unsigned)(Map206_Regs[4] & 0x3f) % chrPages);
  PPUBANK[7] = VROMPAGE((unsigned)(Map206_Regs[5] & 0x3f) % chrPages);

  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 206                                            */
/*-------------------------------------------------------------------*/
void Map206_Init()
{
  MapperInit = Map206_Init;
  MapperWrite = Map206_Write;
  MapperSram = Map0_Sram;
  MapperApu = Map0_Apu;
  MapperReadApu = Map0_ReadApu;
  MapperVSync = Map0_VSync;
  MapperHSync = Map0_HSync;
  MapperPPU = Map0_PPU;
  MapperRenderScreen = Map0_RenderScreen;

  SRAMBANK = SRAM;

  Map206_Reg = 0;
  Map206_Regs[0] = 0;
  Map206_Regs[1] = 2;
  Map206_Regs[2] = 4;
  Map206_Regs[3] = 5;
  Map206_Regs[4] = 6;
  Map206_Regs[5] = 7;
  Map206_Regs[6] = 0;
  Map206_Regs[7] = 1;

  Map206_SetCpuBanks();
  Map206_SetChrBanks();

  /* Mapper 206 has board-fixed mirroring.  Header four-screen wins over
   * the ordinary H/V flag for the DRROM/Gauntlet case. */
  InfoNES_Mirroring(ROM_FourScr ? 4 : ROM_Mirroring);

  K6502_Set_Int_Wiring(1, 1);
}

/*-------------------------------------------------------------------*/
/*  Mapper 206 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map206_Write(WORD wAddr, BYTE byData)
{
  if (wAddr < 0x8000)
    return;

  /* Mesen2's Namco108 model and the hardware register aliasing reduce
   * the full mapper write range to A15 and A0.  There are no distinct
   * MMC3 A000/C000/E000 control registers on Mapper 206. */
  switch (wAddr & 0x8001)
  {
    case 0x8000:
      Map206_Reg = byData & 0x07;
      break;

    case 0x8001:
      switch (Map206_Reg)
      {
        case 0:
        case 1:
          Map206_Regs[Map206_Reg] = byData & 0x3e;
          Map206_SetChrBanks();
          break;

        case 2:
        case 3:
        case 4:
        case 5:
          Map206_Regs[Map206_Reg] = byData & 0x3f;
          Map206_SetChrBanks();
          break;

        case 6:
        case 7:
          Map206_Regs[Map206_Reg] = byData & 0x0f;
          Map206_SetCpuBanks();
          break;
      }
      break;
  }
}
