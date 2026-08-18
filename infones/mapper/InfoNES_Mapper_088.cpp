/*===================================================================*/
/*                                                                   */
/*              Mapper 88 (Namco 108 CHR-A16 variant)                */
/*                                                                   */
/*===================================================================*/

/* Mapper 88 uses the Namco 108 register shape, but connects PPU A12
 * directly to CHR A16.  With the 128 KiB CHR used by known Mapper 88
 * titles, the left pattern table is therefore in the lower 64 KiB and
 * the right pattern table is in the upper 64 KiB. */

#include "InfoNES_Namco108_Common.h"

InfoNES_Namco108_State Map88_State;

static void Map88_SetChrBanks(InfoNES_Namco108_State &state)
{
  const unsigned chrPages = InfoNES_Namco108_ChrPageCount();

  if (chrPages == 0)
    return;

  /* R0/R1 select even 1 KiB pages in the lower 64 KiB.  The low bit is
   * not connected because these registers select 2 KiB banks. */
  const unsigned chr01 = (unsigned)(state.regs[0] & 0x3e) % chrPages;
  const unsigned chr23 = (unsigned)(state.regs[1] & 0x3e) % chrPages;

  PPUBANK[0] = VROMPAGE(chr01);
  PPUBANK[1] = VROMPAGE((chr01 + 1u) % chrPages);
  PPUBANK[2] = VROMPAGE(chr23);
  PPUBANK[3] = VROMPAGE((chr23 + 1u) % chrPages);

  /* Mapper 88 wires PPU A12 to CHR A16.  The four 1 KiB registers on
   * the right table consequently always select the upper 64 KiB. */
  PPUBANK[4] = VROMPAGE(((unsigned)(state.regs[2] & 0x3f) | 0x40u) % chrPages);
  PPUBANK[5] = VROMPAGE(((unsigned)(state.regs[3] & 0x3f) | 0x40u) % chrPages);
  PPUBANK[6] = VROMPAGE(((unsigned)(state.regs[4] & 0x3f) | 0x40u) % chrPages);
  PPUBANK[7] = VROMPAGE(((unsigned)(state.regs[5] & 0x3f) | 0x40u) % chrPages);

  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 88                                             */
/*-------------------------------------------------------------------*/
void Map88_Init()
{
  InfoNES_Namco108_Reset(Map88_State, Map88_Init, Map88_Write,
                         Map88_SetChrBanks);
}

/*-------------------------------------------------------------------*/
/*  Mapper 88 Write Function                                          */
/*-------------------------------------------------------------------*/
void Map88_Write(WORD wAddr, BYTE byData)
{
  InfoNES_Namco108_Write<InfoNES_Namco108_Layout_Mapper88,
                        Map88_SetChrBanks>(Map88_State, wAddr, byData);
}
