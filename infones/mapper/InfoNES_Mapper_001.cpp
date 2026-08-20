/*===================================================================*/
/*                                                                   */
/*                            Mapper 1                               */
/*                                                                   */
/*===================================================================*/

/*
 * MMC1 / SxROM
 *
 * Mapper 1 is a serial device: five D0 writes, least significant bit
 * first, select one of four registers.  The important detail is that a
 * write made on the CPU cycle immediately following another mapper write
 * is ignored.  Several commercial games rely on that RMW behaviour.
 */

namespace {

BYTE Map1_Regs[4];
BYTE Map1_Latch;
BYTE Map1_Write_Count;
bool Map1_Has_Last_Write_Cycle;
int Map1_Last_Write_Cycle;

BYTE *Map1_Prg_Ram;
unsigned Map1_Prg_Ram_Size;

unsigned Map1_Prg_Page_Count()
{
  return (unsigned)NesHeader.byRomSize << 1;
}

unsigned Map1_Chr_Page_Count()
{
  return (unsigned)NesHeader.byVRomSize << 3;
}

unsigned Map1_Wrap_Prg_Page(unsigned page)
{
  const unsigned count = Map1_Prg_Page_Count();
  return count ? page % count : 0;
}

unsigned Map1_Outer_Prg_Bank()
{
  /*
   * SUROM uses CHR0 D4 as the 256 KiB PRG outer-bank select.  This is a
   * CHR-RAM board convention; applying it to CHR-ROM boards would corrupt
   * their CHR selection.
   */
  if (NesHeader.byVRomSize == 0 && Map1_Prg_Page_Count() > 32u)
  {
    return (Map1_Regs[1] & 0x10) ? 16u : 0u; /* 16 KiB pages */
  }
  return 0;
}

void Map1_Set_Mirroring()
{
  switch (Map1_Regs[0] & 0x03)
  {
  case 0: InfoNES_Mirroring(3); break; /* one-screen A ($2000) */
  case 1: InfoNES_Mirroring(2); break; /* one-screen B ($2400) */
  case 2: InfoNES_Mirroring(1); break; /* vertical */
  default: InfoNES_Mirroring(0); break; /* horizontal */
  }
}

void Map1_Set_Prg_Banks()
{
  const unsigned outer = Map1_Outer_Prg_Bank();
  const unsigned prg = Map1_Regs[3] & 0x0f;
  const unsigned control = Map1_Regs[0];
  unsigned bank0;
  unsigned bank1;

  /* NES 2.0 submapper 5 is the fixed-32 KiB MMC1 wiring. */
  if (ROM_NES2 && ROM_Submapper == 5)
  {
    bank0 = 0;
    bank1 = 1;
  }
  else if ((control & 0x08) == 0)
  {
    bank0 = outer + (prg & 0x0e);
    bank1 = bank0 + 1;
  }
  else if (control & 0x04)
  {
    bank0 = outer + prg;
    bank1 = outer + 0x0f;
  }
  else
  {
    bank0 = outer;
    bank1 = outer + prg;
  }

  ROMBANK0 = ROMPAGE(Map1_Wrap_Prg_Page(bank0 * 2u));
  ROMBANK1 = ROMPAGE(Map1_Wrap_Prg_Page(bank0 * 2u + 1u));
  ROMBANK2 = ROMPAGE(Map1_Wrap_Prg_Page(bank1 * 2u));
  ROMBANK3 = ROMPAGE(Map1_Wrap_Prg_Page(bank1 * 2u + 1u));
}

void Map1_Set_Chr_Banks()
{
  if (NesHeader.byVRomSize == 0)
  {
    /* Standard SNROM/SUROM CHR-RAM is the fixed first 8 KiB. */
    for (unsigned page = 0; page < 8; ++page)
    {
      PPUBANK[page] = CRAMPAGE(page);
    }
    InfoNES_SetupChr();
    return;
  }

  const unsigned count = Map1_Chr_Page_Count();
  const unsigned bank0 = (unsigned)Map1_Regs[1] << 2;
  const unsigned bank1 = (unsigned)Map1_Regs[2] << 2;

  if (Map1_Regs[0] & 0x10)
  {
    for (unsigned page = 0; page < 4; ++page)
    {
      PPUBANK[page] = VROMPAGE((bank0 + page) % count);
      PPUBANK[page + 4] = VROMPAGE((bank1 + page) % count);
    }
  }
  else
  {
    const unsigned bank = ((unsigned)(Map1_Regs[1] & 0x1e)) << 2;
    for (unsigned page = 0; page < 8; ++page)
    {
      PPUBANK[page] = VROMPAGE((bank + page) % count);
    }
  }
  InfoNES_SetupChr();
}

void Map1_Sync()
{
  Map1_Set_Mirroring();
  Map1_Set_Prg_Banks();
  Map1_Set_Chr_Banks();
}

unsigned Map1_Decode_Ram_Size()
{
  unsigned size = 0;

  if (ROM_NES2)
  {
    const BYTE ram = NesHeader.byReserve[2]; /* NES 2.0 byte 10 */
    const unsigned volatile_shift = ram & 0x0f;
    const unsigned nvram_shift = ram >> 4;
    if (volatile_shift)
      size += 64u << volatile_shift;
    if (nvram_shift)
      size += 64u << nvram_shift;
  }
  else if (NesHeader.byReserve[0])
  {
    size = (unsigned)NesHeader.byReserve[0] * 0x2000u;
  }
  else
  {
    /* iNES 1.0 byte 8 == 0 means the conventional 8 KiB of PRG-RAM.
     * ROM_SRAM only says that this RAM is non-volatile; it does not say
     * whether volatile work RAM is fitted. */
    size = 0x2000u;
  }

  /* The MMC1 boards in this support tier use at most four 8 KiB banks. */
  return size > 0x8000u ? 0x8000u : size;
}

void Map1_Configure_Prg_Ram()
{
  const unsigned required = Map1_Decode_Ram_Size();

  if (required != Map1_Prg_Ram_Size)
  {
    delete[] Map1_Prg_Ram;
    Map1_Prg_Ram = nullptr;
    Map1_Prg_Ram_Size = 0;

    if (required)
    {
      Map1_Prg_Ram = new (std::nothrow) BYTE[required];
      if (Map1_Prg_Ram)
      {
        InfoNES_MemorySet(Map1_Prg_Ram, 0, required);
        Map1_Prg_Ram_Size = required;
      }
    }
  }

  SRAMBANK = Map1_Prg_Ram ? Map1_Prg_Ram : SRAM;
}

bool Map1_Prg_Ram_Disabled()
{
  if (!Map1_Prg_Ram || Map1_Prg_Ram_Size == 0)
    return true;

  /* MMC1B's normal D4 PRG-RAM disable. */
  if (Map1_Regs[3] & 0x10)
    return true;

  /* SNROM puts the same protection function on CHR0 D4. */
  return NesHeader.byVRomSize == 0 &&
         Map1_Prg_Page_Count() <= 32u &&
         Map1_Prg_Ram_Size <= 0x2000u &&
         (Map1_Regs[1] & 0x10);
}

BYTE *Map1_Current_Prg_Ram_Bank()
{
  if (Map1_Prg_Ram_Disabled())
    return nullptr;

  unsigned bank = 0;
  if (Map1_Prg_Ram_Size > 0x4000u)
    bank = (Map1_Regs[1] >> 2) & 0x03;
  else if (Map1_Prg_Ram_Size > 0x2000u)
    bank = (Map1_Regs[1] >> 3) & 0x01;

  const unsigned offset = bank * 0x2000u;
  return Map1_Prg_Ram + (offset < Map1_Prg_Ram_Size ? offset : 0);
}

} // namespace

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 1                                              */
/*-------------------------------------------------------------------*/
void Map1_Init()
{
  MapperInit = Map1_Init;
  MapperWrite = Map1_Write;
  MapperSram = Map0_Sram;
  MapperApu = Map0_Apu;
  MapperReadApu = Map0_ReadApu;
  MapperVSync = Map0_VSync;
  MapperHSync = Map0_HSync;
  MapperPPU = Map0_PPU;
  MapperRenderScreen = Map0_RenderScreen;

  Map1_Configure_Prg_Ram();
  Map1_Latch = 0;
  Map1_Write_Count = 0;
  Map1_Has_Last_Write_Cycle = false;
  Map1_Last_Write_Cycle = 0;

  Map1_Regs[0] = 0x0c;
  Map1_Regs[1] = 0;
  Map1_Regs[2] = 0;
  Map1_Regs[3] = 0;

  Map1_Sync();
  K6502_Set_Int_Wiring(1, 1);
}

void Map1_set_ROM_banks()
{
  Map1_Set_Prg_Banks();
}

/*-------------------------------------------------------------------*/
/*  Mapper 1 Write Function                                          */
/*-------------------------------------------------------------------*/
void Map1_Write(WORD wAddr, BYTE byData)
{
  const int now = getCurrentClocks32();
  const bool consecutive = Map1_Has_Last_Write_Cycle &&
                           now - Map1_Last_Write_Cycle < 2;
  /* A D7 reset is never suppressed by the consecutive-write rule. */
  if (byData & 0x80)
  {
    Map1_Latch = 0;
    Map1_Write_Count = 0;
    Map1_Regs[0] |= 0x0c;
    Map1_Sync();
  }
  else if (!consecutive)
  {
    Map1_Latch = (BYTE)((Map1_Latch >> 1) | ((byData & 0x01) << 4));
    ++Map1_Write_Count;

    if (Map1_Write_Count == 5)
    {
      const unsigned reg = (wAddr >> 13) & 0x03;
      Map1_Regs[reg] = Map1_Latch;
      Map1_Latch = 0;
      Map1_Write_Count = 0;
      Map1_Sync();
    }
  }

  Map1_Last_Write_Cycle = now;
  Map1_Has_Last_Write_Cycle = true;
}

BYTE Map1_ReadSram(WORD wAddr)
{
  BYTE *const ram = Map1_Current_Prg_Ram_Bank();
  return ram ? ram[wAddr & 0x1fff] : 0xff;
}

void Map1_WriteSram(WORD wAddr, BYTE byData)
{
  BYTE *const ram = Map1_Current_Prg_Ram_Bank();
  if (!ram)
    return;

  ram[wAddr & 0x1fff] = byData;
  SRAMwritten = true;
}

BYTE *Map1_GetPrgRamData()
{
  return Map1_Prg_Ram;
}

unsigned Map1_GetPrgRamSize()
{
  return Map1_Prg_Ram_Size;
}

void Map1_Release()
{
  delete[] Map1_Prg_Ram;
  Map1_Prg_Ram = nullptr;
  Map1_Prg_Ram_Size = 0;
  SRAMBANK = SRAM;
}
