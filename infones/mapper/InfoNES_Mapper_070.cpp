/*===================================================================*/
/*                                                                   */
/*                 Mapper 70 (74161/32 Bandai)                       */
/*                                                                   */
/*===================================================================*/

namespace
{
/*
 * BANDAI-74*161/161/32 is a bus-conflict board.  The pair of 74*161
 * latches sees the AND of the CPU write and the byte driven by the
 * currently selected PRG ROM at that address.
 */
BYTE Map70_ApplyBusConflict( WORD wAddr, BYTE byData )
{
  return (BYTE)(byData & ROMBANK[(wAddr - 0x8000) >> 13][wAddr & 0x1fff]);
}

void Map70_SetBanks( BYTE byData )
{
  const DWORD dwPrgPages = (DWORD)NesHeader.byRomSize << 1;
  const DWORD dwChrPages = (DWORD)NesHeader.byVRomSize << 3;
  const DWORD dwPrgPage = (DWORD)((byData >> 4) & 0x07) << 1;
  const DWORD dwChrPage = (DWORD)(byData & 0x0f) << 3;

  if ( dwPrgPages > 0 )
  {
    ROMBANK0 = ROMPAGE( (dwPrgPage + 0) % dwPrgPages );
    ROMBANK1 = ROMPAGE( (dwPrgPage + 1) % dwPrgPages );
    ROMBANK2 = ROMLASTPAGE( 1 );
    ROMBANK3 = ROMLASTPAGE( 0 );
  }

  /* The licensed Mapper 70 board uses CHR-ROM, but avoid a zero-size
   * calculation for malformed or future CHR-RAM images. */
  if ( dwChrPages > 0 )
  {
    for ( int nPage = 0; nPage < 8; ++nPage )
      PPUBANK[nPage] = VROMPAGE( (dwChrPage + nPage) % dwChrPages );
    InfoNES_SetupChr();
  }
}
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 70                                             */
/*-------------------------------------------------------------------*/
void Map70_Init()
{
  /* Initialize Mapper */
  MapperInit = Map70_Init;

  /* Write to Mapper */
  MapperWrite = Map70_Write;

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

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* BANDAI-74*161/161/32 starts with bank 0 at $8000 and the final
   * 16 KiB fixed at $C000. */
  Map70_SetBanks( 0 );

  /* Mapper 70's licensed BANDAI-74*161/161/32 boards are vertically
   * wired.  Bit 7 is not a mirroring register; one-screen control is
   * the distinct Mapper 152 hardware. */
  InfoNES_Mirroring( 1 );

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 70 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map70_Write( WORD wAddr, BYTE byData )
{
  Map70_SetBanks( Map70_ApplyBusConflict( wAddr, byData ) );
}
