/*===================================================================*/
/*                                                                   */
/*                        Mapper 7 (AOROM)                           */
/*                                                                   */
/*===================================================================*/

#include "runtime_log.h"

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 7                                              */
/*-------------------------------------------------------------------*/
namespace
{
/*
 * NES 2.0 Mapper 7 submapper 2 uses an AND bus conflict.  The value
 * reaching the mapper latch is the CPU write value ANDed with the byte
 * currently visible at the write address.  Legacy iNES Mapper 7 and
 * submapper 1 do not have this conflict.
 */
BYTE Map7_BusConflict = 0;

inline bool Map7_IsNes20()
{
  return (NesHeader.byInfo2 & 0x0c) == 0x08;
}

inline BYTE Map7_Submapper()
{
  if (!Map7_IsNes20())
    return 0;

  return (BYTE)(NesHeader.byReserve[0] >> 4);
}

DWORD Map7_PrgCrc32()
{
  DWORD crc = 0xffffffffUL;
  const DWORD size = (DWORD)NesHeader.byRomSize * 0x4000UL;

  for (DWORD i = 0; i < size; ++i)
  {
    crc ^= ROM[i];
    for (int bit = 0; bit < 8; ++bit)
    {
      crc = (crc >> 1) ^ (0xedb88320UL & (0UL - (crc & 1UL)));
    }
  }

  return ~crc;
}

bool Map7_IsKnownLegacyAmrom()
{
  /* Mesen2 DB: Solstice (Japan), HVC-AMROM, PRG CRC A91460B8. */
  return !Map7_IsNes20() && NesHeader.byRomSize == 8 &&
         Map7_PrgCrc32() == 0xa91460b8UL;
}

#if defined(NESCO_RUNTIME_LOGS)
unsigned Map7_WriteLogCount = 0;
#endif
}

void Map7_Init()
{
  /* Initialize Mapper */
  MapperInit = Map7_Init;

  /* Write to Mapper */
  MapperWrite = Map7_Write;

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

  Map7_BusConflict = (Map7_Submapper() == 2 || Map7_IsKnownLegacyAmrom()) ? 1 : 0;

#if defined(NESCO_RUNTIME_LOGS)
  Map7_WriteLogCount = 0;
#endif

  NESCO_LOG_RUNTIME("[M7] nes20=%u sub=%u bus=%u prg_crc=%08lX\n",
                    Map7_IsNes20() ? 1u : 0u,
                    (unsigned)Map7_Submapper(),
                    (unsigned)Map7_BusConflict,
                    (unsigned long)Map7_PrgCrc32());

  /* Set initial ROM bank */
  ROMBANK0 = ROMPAGE( 0 );
  ROMBANK1 = ROMPAGE( 1 );
  ROMBANK2 = ROMPAGE( 2 );
  ROMBANK3 = ROMPAGE( 3 );

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 7 Write Function                                          */
/*-------------------------------------------------------------------*/
void Map7_Write( WORD wAddr, BYTE byData )
{
  BYTE byBank;

#if defined(NESCO_RUNTIME_LOGS)
  const BYTE rawData = byData;
  const BYTE romData = ROMBANK[(wAddr - 0x8000) >> 13][wAddr & 0x1fff];
#endif

  if (Map7_BusConflict)
  {
    byData &= ROMBANK[(wAddr - 0x8000) >> 13][wAddr & 0x1fff];
  }

#if defined(NESCO_RUNTIME_LOGS)
  if (Map7_WriteLogCount < 256u)
  {
    NESCO_LOG_RUNTIME("[M7W] n=%u pc=%04X addr=%04X raw=%02X rom=%02X data=%02X bank=%u\n",
                      Map7_WriteLogCount++,
                      (unsigned)PC,
                      (unsigned)wAddr,
                      (unsigned)rawData,
                      (unsigned)romData,
                      (unsigned)byData,
                      (unsigned)((byData & 0x0fu)));
  }
#endif

  /* Set ROM Banks.  AXROM uses four latch bits for 32 KiB pages. */
  byBank = ( byData & 0x0f ) << 2;
  byBank %= ( NesHeader.byRomSize << 1 );

  ROMBANK0 = ROMPAGE( byBank );
  ROMBANK1 = ROMPAGE( byBank + 1 );
  ROMBANK2 = ROMPAGE( byBank + 2 );
  ROMBANK3 = ROMPAGE( byBank + 3 );

  /* Name Table Mirroring */
  InfoNES_Mirroring( byData & 0x10 ? 2 : 3 );
}
