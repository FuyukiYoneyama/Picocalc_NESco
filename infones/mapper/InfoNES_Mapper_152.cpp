/*===================================================================*/
/*                                                                   */
/*                 Mapper 152 (Bandai 74161/7432)                    */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 152                                            */
/*-------------------------------------------------------------------*/
void Map152_Init()
{
  /* Initialize Mapper */
  MapperInit = Map152_Init;

  /* Write to Mapper */
  MapperWrite = Map152_Write;

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
  ROMBANK0 = ROMPAGE( 0 );
  ROMBANK1 = ROMPAGE( 1 );
  ROMBANK2 = ROMLASTPAGE( 1 );
  ROMBANK3 = ROMLASTPAGE( 0 );

  /* Set PPU Banks */
  if ( NesHeader.byVRomSize > 0 )
  {
    for ( int nPage = 0; nPage < 8; ++nPage )
      PPUBANK[ nPage ] = VROMPAGE( nPage );
    InfoNES_SetupChr();
  }

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 );
}

/*-------------------------------------------------------------------*/
/*  Mapper 152 Write Function                                        */
/*-------------------------------------------------------------------*/
void Map152_Write( WORD wAddr, BYTE byData )
{
  (void)wAddr;

  BYTE byChrBank = byData & 0x0f;
  BYTE byPrgBank = ( byData & 0x70 ) >> 4;

  /* Set ROM Banks */
  byPrgBank <<= 1;
  byPrgBank %= ( NesHeader.byRomSize << 1 );

  ROMBANK0 = ROMPAGE( byPrgBank );
  ROMBANK1 = ROMPAGE( byPrgBank + 1 );
  ROMBANK2 = ROMLASTPAGE( 1 );
  ROMBANK3 = ROMLASTPAGE( 0 );

  /* Set PPU Banks */
  if ( NesHeader.byVRomSize > 0 )
  {
    byChrBank <<= 3;
    byChrBank %= ( NesHeader.byVRomSize << 3 );

    PPUBANK[ 0 ] = VROMPAGE( byChrBank + 0 );
    PPUBANK[ 1 ] = VROMPAGE( byChrBank + 1 );
    PPUBANK[ 2 ] = VROMPAGE( byChrBank + 2 );
    PPUBANK[ 3 ] = VROMPAGE( byChrBank + 3 );
    PPUBANK[ 4 ] = VROMPAGE( byChrBank + 4 );
    PPUBANK[ 5 ] = VROMPAGE( byChrBank + 5 );
    PPUBANK[ 6 ] = VROMPAGE( byChrBank + 6 );
    PPUBANK[ 7 ] = VROMPAGE( byChrBank + 7 );
    InfoNES_SetupChr();
  }

  /* Name Table Mirroring
   * Mapper152 bit7: 0 = one-screen A/lower 0x2000, 1 = one-screen B/upper 0x2400.
   */
  if ( byData & 0x80 )
  {
    InfoNES_Mirroring( 2 );
  } else {
    InfoNES_Mirroring( 3 );
  }
}
