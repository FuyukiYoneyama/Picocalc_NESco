/*===================================================================*/
/*                                                                   */
/*                        Mapper 66 (GNROM)                          */
/*                                                                   */
/*===================================================================*/

namespace
{
/*
 * GxROM exposes one 32 KiB PRG window and one 8 KiB CHR window.  The
 * original boards connect PRG A16:A15 to CPU D5:D4 and CHR A16:A15 to
 * CPU D1:D0.  The remaining data bits are not connected to the latch.
 *
 * Keep the capacity reduction explicit instead of deriving the page from
 * the complete nibble.  This makes the unused-bit behavior unambiguous for
 * both the 64 KiB MHROM fixture and the 128 KiB GNROM titles.
 */
void Map66_SetBanks(BYTE byData)
{
  const unsigned prg32Banks = (unsigned)NesHeader.byRomSize / 2u;
  const unsigned chr8Banks = (unsigned)NesHeader.byVRomSize;

  if (prg32Banks > 0u)
  {
    const unsigned prgBank = ((unsigned)(byData >> 4) & 0x03u) % prg32Banks;
    const unsigned prgPage = prgBank * 4u;

    ROMBANK0 = ROMPAGE(prgPage + 0u);
    ROMBANK1 = ROMPAGE(prgPage + 1u);
    ROMBANK2 = ROMPAGE(prgPage + 2u);
    ROMBANK3 = ROMPAGE(prgPage + 3u);
  }

  /* Mapper 66 is a CHR-ROM board.  CHR-RAM cartridges keep their fixed
   * CHR-RAM mapping and must not reach a zero-modulus bank calculation. */
  if (chr8Banks > 0u)
  {
    const unsigned chrBank = ((unsigned)byData & 0x03u) % chr8Banks;
    const unsigned chrPage = chrBank * 8u;

    for (int nPage = 0; nPage < 8; ++nPage)
      PPUBANK[nPage] = VROMPAGE(chrPage + (unsigned)nPage);
    InfoNES_SetupChr();
  }
}
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 66                                             */
/*-------------------------------------------------------------------*/
void Map66_Init()
{

  /* Initialize Mapper */
  MapperInit = Map66_Init;

  /* Write to Mapper */
  MapperWrite = Map66_Write;

  /* Mapper 66 has no PRG-RAM register at $6000-$7FFF. */
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
  Map66_SetBanks( 0 );

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 0 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 66 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map66_Write( WORD wAddr, BYTE byData )
{
  (void)wAddr;
  Map66_SetBanks( byData );
}
