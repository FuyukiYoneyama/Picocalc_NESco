/*===================================================================*/
/*                                                                   */
/*                  Mapper 206 (Namco 108 / DxROM)                  */
/*                                                                   */
/*===================================================================*/

/* Mapper 206 is the Namco 108 / MIMIC-1 simplified MMC3 family.  It
 * keeps the MMC3 register shape but has no mode bits, mirroring
 * register, work RAM register, IRQ counter, or expansion audio. */

#include "InfoNES_Namco108_Common.h"

InfoNES_Namco108_State Map206_State;

static void Map206_SetChrBanks(InfoNES_Namco108_State &state)
{
  const unsigned chrPages = InfoNES_Namco108_ChrPageCount();

  if (chrPages == 0)
    return;

  /* The two left-table registers select even 2 KiB banks. */
  const unsigned chr01 = (unsigned)(state.regs[0] & 0x3e) % chrPages;
  const unsigned chr23 = (unsigned)(state.regs[1] & 0x3e) % chrPages;

  PPUBANK[0] = VROMPAGE(chr01);
  PPUBANK[1] = VROMPAGE((chr01 + 1u) % chrPages);
  PPUBANK[2] = VROMPAGE(chr23);
  PPUBANK[3] = VROMPAGE((chr23 + 1u) % chrPages);

  /* The right-table registers select independent 1 KiB banks. */
  PPUBANK[4] = VROMPAGE((unsigned)(state.regs[2] & 0x3f) % chrPages);
  PPUBANK[5] = VROMPAGE((unsigned)(state.regs[3] & 0x3f) % chrPages);
  PPUBANK[6] = VROMPAGE((unsigned)(state.regs[4] & 0x3f) % chrPages);
  PPUBANK[7] = VROMPAGE((unsigned)(state.regs[5] & 0x3f) % chrPages);

  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 206                                            */
/*-------------------------------------------------------------------*/
void Map206_Init()
{
  InfoNES_Namco108_Reset(Map206_State, Map206_Init, Map206_Write,
                         Map206_SetChrBanks);
}

/*-------------------------------------------------------------------*/
/*  Mapper 206 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map206_Write(WORD wAddr, BYTE byData)
{
  InfoNES_Namco108_Write<InfoNES_Namco108_Layout_Mapper206,
                        Map206_SetChrBanks>(Map206_State, wAddr, byData);
}
