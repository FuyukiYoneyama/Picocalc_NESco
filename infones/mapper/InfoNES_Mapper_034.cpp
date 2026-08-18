/*===================================================================*/
/*                                                                   */
/*              Mapper 34 (BNROM / NINA-001/002)                    */
/*                                                                   */
/*===================================================================*/

namespace
{
enum : BYTE
{
  MAP34_BNROM = 0,
  MAP34_NINA = 1,
};

BYTE Map34_Mode = MAP34_BNROM;

/*
 * Mapper 34 is an iNES-number collision.  NES 2.0 submapper 1 and 2
 * identify NINA and BNROM respectively.  For legacy iNES, the established
 * compatibility rule follows NESdev: CHR-ROM of 0-8 KiB selects BNROM,
 * while larger CHR-ROM selects NINA.  This keeps the 8 KiB BNROM boundary
 * distinct from the 64 KiB NINA cartridges.
 */
BYTE Map34_SelectMode()
{
  if (ROM_NES2)
  {
    if (ROM_Submapper == 1)
      return MAP34_NINA;
    if (ROM_Submapper == 2)
      return MAP34_BNROM;
  }

  return NesHeader.byVRomSize > 1 ? MAP34_NINA : MAP34_BNROM;
}

void Map34_SetPrgBank(BYTE byData)
{
  const DWORD dwPrgPages = (DWORD)NesHeader.byRomSize << 1;
  if (dwPrgPages == 0)
    return;

  /* Both boards expose a 32 KiB PRG window.  NINA connects only A15. */
  DWORD dwPage = (Map34_Mode == MAP34_NINA)
                   ? ((DWORD)(byData & 0x01) << 2)
                   : ((DWORD)byData << 2);
  dwPage %= dwPrgPages;

  ROMBANK0 = ROMPAGE((dwPage + 0) % dwPrgPages);
  ROMBANK1 = ROMPAGE((dwPage + 1) % dwPrgPages);
  ROMBANK2 = ROMPAGE((dwPage + 2) % dwPrgPages);
  ROMBANK3 = ROMPAGE((dwPage + 3) % dwPrgPages);
}

void Map34_SetChrRam()
{
  for (int nPage = 0; nPage < 8; ++nPage)
    PPUBANK[nPage] = CRAMPAGE(nPage);
  InfoNES_SetupChr();
}

void Map34_SetChrRom()
{
  const DWORD dwChrPages = (DWORD)NesHeader.byVRomSize << 3;
  if (dwChrPages == 0)
  {
    Map34_SetChrRam();
    return;
  }

  for (int nPage = 0; nPage < 8; ++nPage)
    PPUBANK[nPage] = VROMPAGE((DWORD)nPage % dwChrPages);
  InfoNES_SetupChr();
}

void Map34_SetNinaChrWindow(int nWindow, BYTE byData)
{
  const DWORD dwChrPages = (DWORD)NesHeader.byVRomSize << 3;
  /* NINA registers connect only the low four data bits. */
  DWORD dwPage = (DWORD)(byData & 0x0f) << 2;

  if (dwChrPages == 0)
  {
    /* Be defensive for malformed/explicit NINA CHR-RAM images. */
    dwPage %= 8;
    for (int nPage = 0; nPage < 4; ++nPage)
      PPUBANK[nWindow * 4 + nPage] = CRAMPAGE((dwPage + nPage) & 0x07);
  }
  else
  {
    dwPage %= dwChrPages;
    for (int nPage = 0; nPage < 4; ++nPage)
      PPUBANK[nWindow * 4 + nPage] = VROMPAGE((dwPage + nPage) % dwChrPages);
  }

  InfoNES_SetupChr();
}

inline BYTE Map34_ApplyBusConflict(WORD wAddr, BYTE byData)
{
  /* BNROM's latch sees CPU data AND the PRG byte currently on the bus. */
  return (BYTE)(byData & ROMBANK[(wAddr - 0x8000) >> 13][wAddr & 0x1fff]);
}
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 34                                             */
/*-------------------------------------------------------------------*/
void Map34_Init()
{
  /* Initialize Mapper */
  MapperInit = Map34_Init;

  /* Write to Mapper */
  MapperWrite = Map34_Write;

  /* Write to SRAM / NINA registers */
  MapperSram = Map34_Sram;

  /* Write to APU */
  MapperApu = Map0_Apu;

  /* Read from APU */
  MapperReadApu = Map0_ReadApu;

  /* Callback at VSync */
  MapperVSync = Map0_VSync;

  /* Callback at HSync */
  MapperHSync = Map0_HSync;

  /* Callback at PPU */
  MapperPPU = Map0_PPU;

  /* Callback at Rendering Screen ( 1:BG, 0:Sprite ) */
  MapperRenderScreen = Map0_RenderScreen;

  Map34_Mode = Map34_SelectMode();

  /* Set SRAM Banks.  NINA exposes the 8 KiB region at $6000-$7FFF. */
  SRAMBANK = SRAM;

  /* Set initial 32 KiB PRG window. */
  Map34_SetPrgBank(0);

  /* BNROM has fixed 8 KiB CHR; NINA starts with both 4 KiB windows at 0. */
  Map34_SetChrRom();

  /* Mirroring is fixed by the cartridge header, not a mapper latch. */
  InfoNES_Mirroring(ROM_FourScr ? 4 : ROM_Mirroring);

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring(1, 1);
}

/*-------------------------------------------------------------------*/
/*  BNROM PRG write function                                         */
/*-------------------------------------------------------------------*/
void Map34_Write(WORD wAddr, BYTE byData)
{
  if (Map34_Mode != MAP34_BNROM)
    return;

  Map34_SetPrgBank(Map34_ApplyBusConflict(wAddr, byData));
}

/*-------------------------------------------------------------------*/
/*  NINA-001/002 writes in the $6000-$7FFF address space             */
/*-------------------------------------------------------------------*/
void Map34_Sram(WORD wAddr, BYTE byData)
{
  if (Map34_Mode != MAP34_NINA)
    return;

  switch (wAddr)
  {
    case 0x7ffd:
      Map34_SetPrgBank(byData);
      break;

    case 0x7ffe:
      Map34_SetNinaChrWindow(0, byData);
      break;

    case 0x7fff:
      Map34_SetNinaChrWindow(1, byData);
      break;
  }
}
