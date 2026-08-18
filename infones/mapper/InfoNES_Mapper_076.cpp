/*===================================================================*/
/*                                                                   */
/*                    Mapper 76 (Namcot 109)                         */
/*                                                                   */
/*===================================================================*/

/* Mapper 76 is the Namcot 3446 CHR-wiring variant of the Namco 108
 * family.  Selectors 2..5 expose four 2 KiB CHR banks; the original
 * Mapper 206 selectors 0 and 1 are not connected on this board. */

#include "InfoNES_Namco108_Common.h"

InfoNES_Namco108_State Map76_State;

static void Map76_SetChrBanks(InfoNES_Namco108_State &state)
{
  const unsigned chrPages = InfoNES_Namco108_ChrPageCount();

  if (chrPages == 0)
    return;

  /* Mapper 76 uses four 2 KiB banks across the entire pattern table. */
  for (unsigned slot = 0; slot < 4; ++slot)
  {
    const unsigned bank = (unsigned)(state.regs[slot + 2] & 0x3f);
    const unsigned page = (bank * 2u) % chrPages;
    PPUBANK[slot * 2] = VROMPAGE(page);
    PPUBANK[slot * 2 + 1] = VROMPAGE((page + 1u) % chrPages);
  }

  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 76                                             */
/*-------------------------------------------------------------------*/
void Map76_Init()
{
  InfoNES_Namco108_Reset(Map76_State, Map76_Init, Map76_Write,
                         Map76_SetChrBanks);
}

/*-------------------------------------------------------------------*/
/*  Mapper 76 Write Function                                          */
/*-------------------------------------------------------------------*/
void Map76_Write(WORD wAddr, BYTE byData)
{
  InfoNES_Namco108_Write<InfoNES_Namco108_Layout_Mapper76,
                        Map76_SetChrBanks>(Map76_State, wAddr, byData);
}
