/*===================================================================*/
/*                                                                   */
/*            Mapper 71 (Camerica Custom Mapper)                     */
/*                                                                   */
/*===================================================================*/

namespace
{
enum : BYTE
{
  MAP71_FIXED_MIRRORING = 0,
  MAP71_FIRE_HAWK = 1,
};

BYTE Map71_Mode = MAP71_FIXED_MIRRORING;

BYTE Map71_SelectMode()
{
  /* NES 2.0 submapper 1 is the mapper-controlled Fire Hawk board. */
  return (ROM_NES2 && ROM_Submapper == 1)
           ? MAP71_FIRE_HAWK
           : MAP71_FIXED_MIRRORING;
}

void Map71_SetPrgBank(BYTE byData)
{
  const DWORD dwPrgPages = (DWORD)NesHeader.byRomSize << 1;
  if (dwPrgPages < 2)
    return;

  const DWORD dwPage = ((DWORD)byData << 1) % dwPrgPages;
  ROMBANK0 = ROMPAGE((dwPage + 0) % dwPrgPages);
  ROMBANK1 = ROMPAGE((dwPage + 1) % dwPrgPages);
  ROMBANK2 = ROMPAGE(dwPrgPages - 2);
  ROMBANK3 = ROMPAGE(dwPrgPages - 1);
}
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 71                                             */
/*-------------------------------------------------------------------*/
void Map71_Init()
{
  /* Initialize Mapper */
  MapperInit = Map71_Init;

  /* Write to Mapper */
  MapperWrite = Map71_Write;

  /* Write to SRAM */
  MapperSram = Map0_Sram;

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

  Map71_Mode = Map71_SelectMode();

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* Set ROM Banks */
  Map71_SetPrgBank(0);

  /* Fixed boards use the header.  Fire Hawk starts from the same
   * deterministic state and changes to one-screen on its register write. */
  InfoNES_Mirroring(ROM_FourScr ? 4 : ROM_Mirroring);

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 71 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map71_Write( WORD wAddr, BYTE byData )
{
  if (Map71_Mode == MAP71_FIRE_HAWK && (wAddr & 0xe000) == 0x8000)
  {
    /* Fire Hawk connects data bit 4 to CIRAM A10. */
    InfoNES_Mirroring((byData & 0x10) ? 2 : 3);
    return;
  }

  /* All Mapper 71 boards select the switchable 16 KiB bank here. */
  if ((wAddr & 0xc000) == 0xc000)
  {
    Map71_SetPrgBank(byData);
  }
}
