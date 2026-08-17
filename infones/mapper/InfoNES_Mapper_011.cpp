/*===================================================================*/
/*                                                                   */
/*                     Mapper 11 (Color Dreams)                      */
/*                                                                   */
/*===================================================================*/

#include "runtime_log.h"

namespace
{
#if defined(NESCO_RUNTIME_LOGS)
unsigned Map11_WriteLogCount = 0;
#endif
/*
 * Standard Color Dreams boards have an AND-type bus conflict: the value
 * reaching the 74LS377 is the CPU write value AND the PRG byte visible at
 * the write address.  MapperWrite receives the real CPU address, so use
 * the currently mapped ROM bank rather than a fixed ROM offset.
 */
inline BYTE Map11_ApplyBusConflict( WORD wAddr, BYTE byData )
{
  return (BYTE)(byData & ROMBANK[(wAddr - 0x8000) >> 13][wAddr & 0x1fff]);
}
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 11                                             */
/*-------------------------------------------------------------------*/
void Map11_Init()
{
  int nPage;

  /* Initialize Mapper */
  MapperInit = Map11_Init;

  /* Write to Mapper */
  MapperWrite = Map11_Write;

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
  ROMBANK2 = ROMPAGE( 2 );
  ROMBANK3 = ROMPAGE( 3 );

  /* Set PPU Banks */
  if ( NesHeader.byVRomSize > 0 )
  {
    for ( nPage = 0; nPage < 8; ++nPage )
      PPUBANK[ nPage ] = VROMPAGE( nPage );
    InfoNES_SetupChr();
  }

  /* Name Table Mirroring: fixed by the board/header, not by the latch. */
  InfoNES_Mirroring( ROM_Mirroring );

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 

#if defined(NESCO_RUNTIME_LOGS)
  Map11_WriteLogCount = 0;
  NESCO_LOG_RUNTIME("[M11] init prg16=%u chr8=%u mirroring=%u bus=AND\n",
                    (unsigned)NesHeader.byRomSize,
                    (unsigned)NesHeader.byVRomSize,
                    (unsigned)ROM_Mirroring);
#endif
}

/*-------------------------------------------------------------------*/
/*  Mapper 11 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map11_Write( WORD wAddr, BYTE byData )
{
  const DWORD dwPrgPages = (DWORD)NesHeader.byRomSize << 1;
  const DWORD dwChrPages = (DWORD)NesHeader.byVRomSize << 3;
  DWORD dwPrgBank;
  DWORD dwChrBank;
#if defined(NESCO_RUNTIME_LOGS)
  const BYTE rawData = byData;
  const BYTE visibleData = ROMBANK[(wAddr - 0x8000) >> 13][wAddr & 0x1fff];
#endif

  /* Color Dreams is a standard bus-conflict board. */
  byData = Map11_ApplyBusConflict( wAddr, byData );

#if defined(NESCO_RUNTIME_LOGS)
  if (Map11_WriteLogCount < 64u)
  {
    NESCO_LOG_RUNTIME("[M11] write n=%u addr=%04X raw=%02X rom=%02X effective=%02X\n",
                      Map11_WriteLogCount,
                      (unsigned)wAddr,
                      (unsigned)rawData,
                      (unsigned)visibleData,
                      (unsigned)byData);
    ++Map11_WriteLogCount;
  }
#endif

  /* D0-D1 select 32 KiB PRG; D2-D3 are CIC lockout bits. */
  dwPrgBank = (DWORD)(byData & 0x03) << 2;
  /* D4-D7 select 8 KiB CHR. */
  dwChrBank = (DWORD)((byData >> 4) & 0x0f) << 3;

  /* Set ROM Banks */
  if ( dwPrgPages > 0 )
  {
    ROMBANK0 = ROMPAGE( ( dwPrgBank + 0 ) % dwPrgPages );
    ROMBANK1 = ROMPAGE( ( dwPrgBank + 1 ) % dwPrgPages );
    ROMBANK2 = ROMPAGE( ( dwPrgBank + 2 ) % dwPrgPages );
    ROMBANK3 = ROMPAGE( ( dwPrgBank + 3 ) % dwPrgPages );
  }

  /* Set PPU Banks */
  if ( dwChrPages > 0 )
  {
    PPUBANK[ 0 ] = VROMPAGE( ( dwChrBank + 0 ) % dwChrPages );
    PPUBANK[ 1 ] = VROMPAGE( ( dwChrBank + 1 ) % dwChrPages );
    PPUBANK[ 2 ] = VROMPAGE( ( dwChrBank + 2 ) % dwChrPages );
    PPUBANK[ 3 ] = VROMPAGE( ( dwChrBank + 3 ) % dwChrPages );
    PPUBANK[ 4 ] = VROMPAGE( ( dwChrBank + 4 ) % dwChrPages );
    PPUBANK[ 5 ] = VROMPAGE( ( dwChrBank + 5 ) % dwChrPages );
    PPUBANK[ 6 ] = VROMPAGE( ( dwChrBank + 6 ) % dwChrPages );
    PPUBANK[ 7 ] = VROMPAGE( ( dwChrBank + 7 ) % dwChrPages );
    InfoNES_SetupChr();
  }
}
