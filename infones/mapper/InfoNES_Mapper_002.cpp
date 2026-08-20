/*===================================================================*/
/*                                                                   */
/*                        Mapper 2 (UNROM)                           */
/*                                                                   */
/*===================================================================*/

namespace
{
/*
 * NES 2.0 gives UxROM's historically ambiguous bus-conflict wiring an
 * explicit identity: submapper 1 has no conflict, and submapper 2 latches
 * CPU data AND the PRG byte that is driving the bus at the write address.
 * Keep legacy iNES Mapper 2 on its established no-conflict path; many
 * unlicensed and homebrew images rely on it and have no submapper field to
 * describe the board.
 */
bool Map2_HasBusConflict()
{
  return ROM_NES2 && ROM_Submapper == 2;
}

BYTE Map2_ApplyBusConflict( WORD wAddr, BYTE byData )
{
  return (BYTE)(byData & ROMBANK[(wAddr - 0x8000) >> 13][wAddr & 0x1fff]);
}

void Map2_SetPrgBank( BYTE bank )
{
  /* The register is byte-wide; wrap it only at the actual ROM capacity. */
  bank %= NesHeader.byRomSize;
  bank <<= 1;
  ROMBANK0 = ROMPAGE( bank );
  ROMBANK1 = ROMPAGE( bank + 1 );
}
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 2                                              */
/*-------------------------------------------------------------------*/
void Map2_Init()
{
  /* Initialize Mapper */
  MapperInit = Map2_Init;

  /* Write to Mapper */
  MapperWrite = Map2_Write;

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

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 2 Write Function                                          */
/*-------------------------------------------------------------------*/
void Map2_Write( WORD wAddr, BYTE byData )
{
  if ( Map2_HasBusConflict() )
  {
    byData = Map2_ApplyBusConflict( wAddr, byData );
  }

  Map2_SetPrgBank( byData );
}
