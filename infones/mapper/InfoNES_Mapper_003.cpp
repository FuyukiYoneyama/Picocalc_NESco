/*===================================================================*/
/*                                                                   */
/*                     Mapper 3 (VROM Switch)                        */
/*                                                                   */
/*===================================================================*/

namespace
{
/*
 * CNROM's original discrete logic board drives PRG-ROM while its CHR latch
 * is written, producing an AND-type bus conflict.  NES 2.0 identifies the
 * wiring explicitly: submapper 1 has no conflict and submapper 2 has one.
 * Keep legacy Mapper 3 on InfoNES' established no-conflict path; old iNES
 * images cannot describe the board variant and several mapper-hack images
 * rely on that compatibility behaviour.
 */
bool Map3_HasBusConflict()
{
  return ROM_NES2 && ROM_Submapper == 2;
}

BYTE Map3_ApplyBusConflict( WORD wAddr, BYTE byData )
{
  return (BYTE)(byData & ROMBANK[(wAddr - 0x8000) >> 13][wAddr & 0x1fff]);
}

void Map3_SetChrBank( BYTE bank )
{
  DWORD dwBase;
  int nPage;

  /* CNROM requires CHR-ROM.  A malformed CHR-RAM image has no valid latch. */
  if ( NesHeader.byVRomSize == 0 )
  {
    return;
  }

  bank %= NesHeader.byVRomSize;
  dwBase = ( (DWORD)bank ) << 3;
  for ( nPage = 0; nPage < 8; ++nPage )
  {
    PPUBANK[ nPage ] = VROMPAGE( dwBase + nPage );
  }
  InfoNES_SetupChr();
}
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 3                                              */
/*-------------------------------------------------------------------*/
void Map3_Init()
{
  int nPage;

  /* Initialize Mapper */
  MapperInit = Map3_Init;

  /* Write to Mapper */
  MapperWrite = Map3_Write;

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

  /* Set ROM Banks */
  if ( ( NesHeader.byRomSize << 1 ) > 2 )
  {
    ROMBANK0 = ROMPAGE( 0 );
    ROMBANK1 = ROMPAGE( 1 );
    ROMBANK2 = ROMPAGE( 2 );
    ROMBANK3 = ROMPAGE( 3 );    
  } else {
    ROMBANK0 = ROMPAGE( 0 );
    ROMBANK1 = ROMPAGE( 1 );
    ROMBANK2 = ROMPAGE( 0 );
    ROMBANK3 = ROMPAGE( 1 );
  }

  /* Set PPU Banks */
  if ( NesHeader.byVRomSize > 0 )
  {
    for ( nPage = 0; nPage < 8; ++nPage )
    {
      PPUBANK[ nPage ] = VROMPAGE( nPage );
    }
    InfoNES_SetupChr();
  }

  /* Set up wiring of the interrupt pin */
  /* "DragonQuest" doesn't run if IRQ isn't made to occur in CLI */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 3 Write Function                                          */
/*-------------------------------------------------------------------*/
void Map3_Write( WORD wAddr, BYTE byData )
{
  if ( Map3_HasBusConflict() )
  {
    byData = Map3_ApplyBusConflict( wAddr, byData );
  }

  Map3_SetChrBank( byData );
}
