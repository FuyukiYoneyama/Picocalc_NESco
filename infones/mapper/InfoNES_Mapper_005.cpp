/*===================================================================*/
/*                                                                   */
/*                        Mapper 5 (MMC5)                            */
/*                                                                   */
/*===================================================================*/

BYTE Map5_Ex_Ram[0x400];
/* ExRAM selected as a nametable in modes 2/3 is an empty nametable, not
 * fill mode. */
BYTE Map5_Empty_Nam[0x400];
BYTE Map5_Ex_Nam[0x400];
/* Unpopulated MMC5 PRG-RAM reads as open bus.  The test ROMs and Mesen2 use
 * $ff for that state; keep a full 8 KiB page so ROMBANK mappings remain
 * bounds-safe when a program deliberately maps an unavailable bank. */
BYTE Map5_Open_Bus[0x2000];

BYTE Map5_Prg_Reg[8];
BYTE Map5_Wram_Reg[8];
/* $5130 is latched into normal CHR bank writes; it is not retroactive. */
WORD Map5_Chr_Reg[8][2];

BYTE Map5_IRQ_Enable;
BYTE Map5_IRQ_Status;
BYTE Map5_IRQ_Line;
BYTE Map5_IRQ_Scanline;

DWORD Map5_Value0;
DWORD Map5_Value1;

BYTE Map5_Wram_Protect0;
BYTE Map5_Wram_Protect1;
BYTE Map5_Wram_Bank_Count;
BYTE Map5_Wram_Linear_16k;
uint16_t Map5_Battery_Wram_Mask;
bool Map5_Battery_Wram_Dirty;
bool Map5_Battery_Ex_Ram;
bool Map5_Battery_Ex_Ram_Dirty;
unsigned Map5_Work_Ram_Bytes;
unsigned Map5_Save_Ram_Bytes;
BYTE Map5_Prg_Size;
BYTE Map5_Chr_Size;
BYTE Map5_Gfx_Mode;
BYTE Map5_Chr_Upper;
/* In 8x16-sprite mode, CPU access to PPU pattern memory uses the CHR set
 * selected by the most recently written $5120-$512B register. */
BYTE Map5_Chr_Last_Reg;
/* The direct renderer calls Map5_RenderScreen once for background and once
 * for sprites on every scanline.  Rebuild decoded CHR only when the selected
 * bank set actually changes. */
BYTE Map5_Chr_Render_Mode;
BYTE Map5_Chr_Mapping_Dirty;
BYTE Map5_Nametable_Reg;
BYTE Map5_Split_Control;
BYTE Map5_Split_Scroll;
BYTE Map5_Split_Bank;

struct Map5_AudioPulse
{
  BYTE byReg[4];
  DWORD dwPhase;
  DWORD dwStep;
  BYTE byLength;
  BYTE byEnvelope;
  BYTE byEnvelopeDivider;
  BYTE byEnvelopeStart;
  BYTE byEnabled;
};

Map5_AudioPulse Map5_AudioPulse1;
Map5_AudioPulse Map5_AudioPulse2;
BYTE Map5_PcmReadMode;
BYTE Map5_PcmOutput;
BYTE Map5_PcmIrqEnable;
BYTE Map5_PcmIrqPending;

static BYTE *Map5_Chr_Ram_Alloc = nullptr;
static BYTE *Map5_Chr_Ram = nullptr;
static DWORD Map5_Chr_Ram_Page_Count = 8;
static BYTE *Map5_Wram_Alloc = nullptr;
static BYTE *Map5_Wram = nullptr;
static unsigned Map5_Wram_Storage_Bytes = 0;
static BYTE Map5_Wram_Large_Block = 0;
/* The ordinary PicoCalc image leaves less than 128 KiB of heap after the
 * firmware's global state and ROM/session arena.  Keep the largest resident
 * MMC5 RAM block explicit: larger NES 2.0 declarations are mirrored into
 * this capacity instead of leaving mapper startup half-initialized. */
static const unsigned Map5_MaxResidentWramBytes = 0x10000u;

#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
static uint32_t Map5_AudioDiagSampleCount;
static int16_t Map5_AudioDiagMinSample;
static int16_t Map5_AudioDiagMaxSample;
#endif

void Map5_Release()
{
  delete[] Map5_Chr_Ram_Alloc;
  Map5_Chr_Ram_Alloc = nullptr;
  Map5_Chr_Ram = nullptr;
  Map5_Chr_Ram_Page_Count = 8;

  delete[] Map5_Wram_Alloc;
  Map5_Wram_Alloc = nullptr;
  Map5_Wram = nullptr;
  Map5_Wram_Storage_Bytes = 0;
  Map5_Wram_Large_Block = 0;
}

static BYTE *Map5_ChrPage(DWORD dwPage)
{
  if (NesHeader.byVRomSize > 0)
  {
    const DWORD dwPageCount = (DWORD)NesHeader.byVRomSize << 3;
    return VROMPAGE(dwPage % dwPageCount);
  }

  if (Map5_Chr_Ram)
  {
    return &Map5_Chr_Ram[(dwPage % Map5_Chr_Ram_Page_Count) * 0x400];
  }

  /* Allocation failure is reported by Map5_Init.  Keep the fallback inside
   * the real 8 KiB pattern RAM instead of allowing an out-of-bounds pointer
   * through the generic CRAMPAGE(0..31) macro. */
  return &PPURAM[(dwPage & 0x07) * 0x400];
}

static const BYTE Map5_AudioLengthTable[32] = {
  10, 254, 20, 2, 40, 4, 80, 6,
  160, 8, 60, 10, 14, 12, 26, 14,
  12, 16, 24, 18, 48, 20, 96, 22,
  192, 24, 72, 26, 16, 28, 32, 30
};

static const BYTE Map5_AudioDuty[4][8] = {
  {0, 1, 0, 0, 0, 0, 0, 0},
  {0, 1, 1, 0, 0, 0, 0, 0},
  {0, 1, 1, 1, 1, 0, 0, 0},
  {1, 0, 0, 1, 1, 1, 1, 1}
};

static DWORD Map5_AudioStepForTimer(WORD wTimer)
{
  /* MMC5 pulse frequency is CPU / (16 * (timer + 1)).  The division is
   * deliberately performed on register writes, never in the per-sample
   * producer running on RP2040's Cortex-M0+.  The pre-divided Q32 base is
   * floor((1789773 << 32) / (16 * 22050)).  Divide that 35-bit constant by
   * the 11-bit timer with four 16-bit limbs; each limb division is an ordinary
   * 32-bit operation and avoids the RP2040 emulator's unsupported 64-bit
   * divide helper. */
  const uint64_t q32Base = UINT64_C(0x512b39547);
  const uint32_t divisor = (uint32_t)wTimer + 1u;
  uint32_t remainder = 0;
  uint32_t quotient = 0;

  for (int shift = 48; shift >= 0; shift -= 16)
  {
    const uint32_t limb = (uint32_t)(q32Base >> shift) & 0xffffu;
    const uint32_t current = (remainder << 16) | limb;
    const uint32_t qlimb = current / divisor;
    remainder = current % divisor;
    quotient = (quotient << 16) | (qlimb & 0xffffu);
  }
  return quotient;
}

/* The address of an 8 KiB unit of the MMC5 PRG-RAM backing store. */
static BYTE *Map5_WramPage(BYTE byBank)
{
  if (!Map5_Wram || byBank >= Map5_Wram_Storage_Bytes / 0x2000u)
  {
    return Map5_Open_Bus;
  }
  return &Map5_Wram[(unsigned)byBank * 0x2000u];
}

static BYTE *Map5_WritableWramPage(BYTE byBank)
{
  BYTE *const page = Map5_WramPage(byBank);
  return page == Map5_Open_Bus ? nullptr : page;
}

static BYTE Map5_NormalizeWramBank(BYTE byBank)
{
  const unsigned nStorageBanks = Map5_Wram_Storage_Bytes / 0x2000u;
  if (byBank == 0xff || nStorageBanks == 0)
  {
    return 0xff;
  }

  if (byBank >= nStorageBanks)
  {
    /* Mesen2's memory mapper wraps a selected page by the installed RAM
     * page count.  This matters for the NES 2.0 single-block topology: a
     * 64 KiB block has eight physical pages, while $5113 still exposes the
     * low four selector bits.  Treating selectors 8..15 as open bus would
     * disagree with the reference and break the 64 KiB RAM-size fixture. */
    if (!Map5_Wram_Large_Block)
    {
      return 0xff;
    }
    byBank = (BYTE)(byBank % nStorageBanks);
  }
  return byBank;
}

void Map5_Sync_Nametable();

static bool Map5_PpuRenderingActive()
{
  /* InfoNES marks scanline 240 as the post-render/unknown line.  MMC5's
   * in-frame detector is driven by visible PPU nametable fetches, so that
   * line must not extend the active window by one extra scanline. */
  return PPU_Scanline < SCAN_UNKNOWN_START &&
         (PPU_R1 & (R1_SHOW_SCR | R1_SHOW_SP)) != 0;
}

static void Map5_SyncCpuChrBanks()
{
  BYTE byMode;

  if (!(PPU_R0 & R0_SP_SIZE))
  {
    /* The MMC5 resets the last-register selection while sprites are 8x8. */
    Map5_Chr_Last_Reg = 0;
    byMode = 0;
  }
  else if (Map5_PpuRenderingActive())
  {
    /* While rendering, MMC5's live PPU fetch mapping is the background
     * (B) register set.  InfoNES_DrawLine independently selects B then A
     * for its background and sprite passes. */
    byMode = 1;
  }
  else
  {
    byMode = Map5_Chr_Last_Reg <= 7 ? 0 : 1;
  }

  Map5_RenderScreen(byMode);
}

void Map5_SyncCpuChrBanksForCpuRead()
{
  /* During 8x16 sprites, MMC5's CPU-visible pattern-table bank is selected
   * by the most recently written $5120-$512B register.  The renderer maps
   * B then A for its two passes, so PPUBANK may otherwise be left on A when
   * software subsequently reads $2007 with rendering disabled. */
  const BYTE byMode = (PPU_R0 & R0_SP_SIZE) && Map5_Chr_Last_Reg > 7 ? 1 : 0;
  Map5_RenderScreen(byMode);
}

static BYTE Map5_GetWramBank(BYTE bySelect)
{
  if (Map5_Wram_Large_Block)
  {
    /* Mesen2 models the NES 2.0 64/128 KiB single-block case with the
     * lower four bits of $5113-$5116. */
    return bySelect & 0x0f;
  }

  bySelect &= 0x07;

  /* MMC5 boards use bit 2 to select a second SRAM chip.  Within a chip,
   * 8 KiB devices mirror low select bits and a 32 KiB device uses bits 0-1.
   * A NES 2.0 image can instead describe one contiguous 16 KiB memory area;
   * retain that declared topology rather than treating it as two 8 KiB chips.
   * A split 8 KiB work + 8 KiB save declaration remains the commercial
   * two-chip arrangement. */
  switch (Map5_Wram_Bank_Count)
  {
  case 0:
    return 0xff;
  case 1:
    return bySelect < 4 ? 0 : 0xff;
  case 2:
    return Map5_Wram_Linear_16k ? (bySelect & 0x01) : (bySelect >> 2);
  case 4:
    return bySelect < 4 ? bySelect : 0xff;
  default:
    return bySelect;
  }
}

static BYTE *Map5_GetWramPage(BYTE bySelect, BYTE *pMappedBank)
{
  *pMappedBank = Map5_NormalizeWramBank(Map5_GetWramBank(bySelect));
  if (*pMappedBank == 0xff)
  {
    return Map5_Open_Bus;
  }
  return Map5_WramPage(*pMappedBank);
}

static unsigned Map5_DecodeNes2RamBytes(BYTE byShift)
{
  return byShift ? (64u << byShift) : 0u;
}

static BYTE Map5_DetectWramBankCount()
{
  unsigned nBanks;
  if (ROM_NES2)
  {
    const BYTE byRam = NesHeader.byReserve[2] & 0x0f;
    const BYTE byNvRam = NesHeader.byReserve[2] >> 4;
    const unsigned nRamBytes = Map5_DecodeNes2RamBytes(byRam) +
                               Map5_DecodeNes2RamBytes(byNvRam);
    nBanks = nRamBytes / 0x2000u;
    return nBanks > 16 ? 16 : (BYTE)nBanks;
  }

  /* iNES 1.0 has no reliable MMC5 PRG-RAM description.  A full 64 KiB
   * backing avoids a ROM-name database while retaining every conventional
   * board configuration for legacy game dumps. */
  return NesHeader.byReserve[0] ?
      (NesHeader.byReserve[0] > 8 ? 8 : NesHeader.byReserve[0]) : 8;
}

static bool Map5_IsBatteryWramSelect(BYTE bySelect)
{
  if (!ROM_SRAM)
  {
    return false;
  }

  /* iNES 1.0 does not describe MMC5 RAM topology.  Its battery bit has
   * historically meant a single battery-backed 64 KiB block, which is also
   * Mesen2's fallback for unknown Mapper 5 images. */
  if (!ROM_NES2)
  {
    return true;
  }

  if (Map5_Save_Ram_Bytes == 0)
  {
    return false;
  }

  /* NES 2.0 can explicitly describe a 64 KiB (or larger) battery block.
   * In that topology the raw bank number selects that one contiguous RAM. */
  if (Map5_Work_Ram_Bytes >= 0x10000u || Map5_Save_Ram_Bytes >= 0x10000u)
  {
    return true;
  }

  /* Licensed MMC5 boards put a battery SRAM in raw selections 0-3.  When
   * save RAM exceeds 8 KiB it is the only installed RAM and all mapped
   * selections are battery-backed. */
  return (bySelect & 0x07) <= 3 || Map5_Save_Ram_Bytes > 0x2000u;
}

static uint16_t Map5_BuildBatteryWramMask()
{
  uint16_t byMask = 0;
  const BYTE bySelectCount = Map5_Wram_Large_Block ? 16 : 8;
  for (BYTE bySelect = 0; bySelect < bySelectCount; ++bySelect)
  {
    BYTE byBank = 0xff;
    Map5_GetWramPage(bySelect, &byBank);
    if (byBank != 0xff && Map5_IsBatteryWramSelect(bySelect))
    {
      byMask |= (uint16_t)(1u << byBank);
    }
  }
  return byMask;
}

static void Map5_MarkWramDirty(BYTE byMappedBank)
{
  if (byMappedBank != 0xff &&
      (Map5_Battery_Wram_Mask & (uint16_t)(1u << byMappedBank)))
  {
    Map5_Battery_Wram_Dirty = true;
  }
}

static void Map5_MarkExRamDirty()
{
  if (Map5_Battery_Ex_Ram)
  {
    Map5_Battery_Ex_Ram_Dirty = true;
  }
}

static void Map5_AudioReset()
{
  InfoNES_MemorySet(&Map5_AudioPulse1, 0, sizeof(Map5_AudioPulse1));
  InfoNES_MemorySet(&Map5_AudioPulse2, 0, sizeof(Map5_AudioPulse2));
  Map5_PcmReadMode = 0;
  Map5_PcmOutput = 0;
  Map5_PcmIrqEnable = 0;
  Map5_PcmIrqPending = 0;
}

#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
void Map5_AudioDiagnosticsReset()
{
  Map5_AudioDiagSampleCount = 0;
  Map5_AudioDiagMinSample = 0;
  Map5_AudioDiagMaxSample = 0;
}

void Map5_AudioDiagnosticsSnapshot(uint32_t *sample_count,
                                   int16_t *min_sample,
                                   int16_t *max_sample)
{
  if (sample_count) *sample_count = Map5_AudioDiagSampleCount;
  if (min_sample) *min_sample = Map5_AudioDiagMinSample;
  if (max_sample) *max_sample = Map5_AudioDiagMaxSample;
}
#endif

static void Map5_AudioWritePulse(Map5_AudioPulse *pPulse,
                                  BYTE byReg, BYTE byData)
{
  pPulse->byReg[byReg] = byData;
  if (byReg == 2 || byReg == 3)
  {
    const WORD wTimer = (WORD)pPulse->byReg[2] |
                        (WORD)((pPulse->byReg[3] & 0x07) << 8);
    pPulse->dwStep = Map5_AudioStepForTimer(wTimer);
  }
  if (byReg == 3)
  {
    /* MMC5 pulse channels reset phase on the high timer write, just as
     * the console APU pulse channels do. */
    pPulse->dwPhase = 0;
    if (pPulse->byEnabled)
    {
      pPulse->byLength = Map5_AudioLengthTable[byData >> 3];
    }
    pPulse->byEnvelopeStart = 1;
  }
}

static void Map5_AudioClockQuarter(Map5_AudioPulse *pPulse)
{
  /* Envelope clocks at the APU quarter-frame rate (240 Hz), even while the
   * channel is disabled.  $5015 disables the length counter but does not
   * stop the envelope unit itself. */
  const BYTE byPeriod = (pPulse->byReg[0] & 0x0f) + 1;
  if (pPulse->byEnvelopeStart)
  {
    pPulse->byEnvelopeStart = 0;
    pPulse->byEnvelope = 15;
    pPulse->byEnvelopeDivider = byPeriod;
  }
  else if (pPulse->byEnvelopeDivider)
  {
    --pPulse->byEnvelopeDivider;
  }
  else
  {
    pPulse->byEnvelopeDivider = byPeriod;
    if (pPulse->byEnvelope)
    {
      --pPulse->byEnvelope;
    }
    else if (pPulse->byReg[0] & 0x20)
    {
      pPulse->byEnvelope = 15;
    }
  }

  /* Unlike the NES APU, MMC5 has no half-frame sequencer.  Its length
   * counter advances together with the envelope at the fixed ~240 Hz rate. */
  if (pPulse->byEnabled && pPulse->byLength &&
      !(pPulse->byReg[0] & 0x20))
  {
    --pPulse->byLength;
  }
}

static int Map5_AudioRenderPulse(Map5_AudioPulse *pPulse)
{
  if (!pPulse->byEnabled || !pPulse->byLength)
  {
    return 0;
  }

  pPulse->dwPhase += pPulse->dwStep;

  const BYTE byDuty = pPulse->byReg[0] >> 6;
  const BYTE byPhase = (BYTE)(pPulse->dwPhase >> 29);
  if (!Map5_AudioDuty[byDuty][byPhase])
  {
    return 0;
  }

  return (pPulse->byReg[0] & 0x10) ?
      (pPulse->byReg[0] & 0x0f) : pPulse->byEnvelope;
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 5                                              */
/*-------------------------------------------------------------------*/
void Map5_Init()
{
  int nPage;

  /* Initialize Mapper */
  MapperInit = Map5_Init;

  /* Write to Mapper */
  MapperWrite = Map5_Write;

  /* Write to SRAM */
  MapperSram = Map5_Sram;

  /* Read from SRAM */
  MapperReadSram = Map5_ReadSram;

  /* Source query used when an APU or MMC5 register acknowledges one IRQ. */
  MapperIrqPending = Map5_IrqPending;

  /* Write to APU */
  MapperApu = Map5_Apu;

  /* Read from APU */
  MapperReadApu = Map5_ReadApu;

  /* Callback at VSync */
  MapperVSync = Map5_VSync;

  /* Callback at HSync */
  MapperHSync = Map5_HSync;

  /* Callback at PPU */
  MapperPPU = Map0_PPU;

  /* Callback at Rendering Screen ( 1:BG, 0:Sprite ) */
  MapperRenderScreen = Map5_RenderScreen;

  Map5_Release();

  /* Resolve the declared PRG-RAM topology before allocating the backing
   * store.  Conventional MMC5 boards use at most eight 8 KiB banks, while
   * NES 2.0 can describe one 64/128 KiB block selected by the low four bits
   * of the PRG registers. */
  Map5_Work_Ram_Bytes = ROM_NES2 ?
      Map5_DecodeNes2RamBytes(NesHeader.byReserve[2] & 0x0f) : 0;
  Map5_Save_Ram_Bytes = ROM_NES2 ?
      Map5_DecodeNes2RamBytes(NesHeader.byReserve[2] >> 4) :
      (ROM_SRAM ? 0x10000u : 0u);
  Map5_Wram_Large_Block =
      ROM_NES2 && (Map5_Work_Ram_Bytes >= 0x10000u ||
                   Map5_Save_Ram_Bytes >= 0x10000u);
  Map5_Wram_Bank_Count = Map5_DetectWramBankCount();
  Map5_Wram_Storage_Bytes = (unsigned)Map5_Wram_Bank_Count * 0x2000u;
  if (Map5_Wram_Storage_Bytes > Map5_MaxResidentWramBytes)
  {
    InfoNES_Error("Mapper 5 PRG-RAM capped [requested=%u resident=%u]",
                  Map5_Wram_Storage_Bytes, Map5_MaxResidentWramBytes);
    Map5_Wram_Bank_Count = (BYTE)(Map5_MaxResidentWramBytes / 0x2000u);
    Map5_Wram_Storage_Bytes = Map5_MaxResidentWramBytes;
    Map5_Wram_Large_Block = 1;
  }
  if (Map5_Wram_Storage_Bytes != 0)
  {
    Map5_Wram_Alloc = new (std::nothrow) BYTE[Map5_Wram_Storage_Bytes];
    if (!Map5_Wram_Alloc)
    {
      InfoNES_Error("Mapper 5 startup alloc failed [prg-ram size=%u]",
                    Map5_Wram_Storage_Bytes);
      /* Keep the mapper state internally consistent and let the normal
       * initialization path finish.  Unavailable PRG-RAM then reads as the
       * documented open-bus page instead of retaining stale bank pointers. */
      Map5_Wram_Storage_Bytes = 0;
      Map5_Wram = nullptr;
    }
    else
    {
      Map5_Wram = Map5_Wram_Alloc;
      InfoNES_MemorySet(Map5_Wram, 0x00, Map5_Wram_Storage_Bytes);
    }
  }

  if (NesHeader.byVRomSize == 0)
  {
    /* MMC5 boards may provide up to 32 KiB of banked CHR-RAM.  PPURAM is
     * only the console's 8 KiB pattern RAM plus nametables, so it cannot be
     * used as the backing store for all four 8 KiB CHR banks. */
    unsigned chrRamBytes = 0x2000;
    if (ROM_NES2)
    {
      const unsigned volatileBytes =
          Map5_DecodeNes2RamBytes(NesHeader.byReserve[3] & 0x0f);
      const unsigned nonvolatileBytes =
          Map5_DecodeNes2RamBytes(NesHeader.byReserve[3] >> 4);
      if (volatileBytes + nonvolatileBytes)
      {
        chrRamBytes = volatileBytes + nonvolatileBytes;
      }
    }
    if (chrRamBytes < 0x2000)
    {
      chrRamBytes = 0x2000;
    }
    if (chrRamBytes > 0x8000)
    {
      chrRamBytes = 0x8000;
    }
    Map5_Chr_Ram_Page_Count = chrRamBytes / 0x400;
    Map5_Chr_Ram_Alloc = new (std::nothrow) BYTE[chrRamBytes];
    if (!Map5_Chr_Ram_Alloc)
    {
      InfoNES_Error("Mapper 5 startup alloc failed [chr-ram size=%u]", chrRamBytes);
      Map5_Release();
      return;
    }
    Map5_Chr_Ram = Map5_Chr_Ram_Alloc;
    InfoNES_MemorySet(Map5_Chr_Ram, 0x00, chrRamBytes);
  }

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* Set ROM Banks */
  ROMBANK0 = ROMLASTPAGE(0);
  ROMBANK1 = ROMLASTPAGE(0);
  ROMBANK2 = ROMLASTPAGE(0);
  ROMBANK3 = ROMLASTPAGE(0);

  /* Set PPU Banks */
  if (NesHeader.byVRomSize > 0)
  {
    for (nPage = 0; nPage < 8; ++nPage)
      PPUBANK[nPage] = VROMPAGE(nPage);
    InfoNES_SetupChr();
  }
  else
  {
    /* A CHR-RAM MMC5 starts with its first 8 KiB bank selected. */
    for (nPage = 0; nPage < 8; ++nPage)
      PPUBANK[nPage] = Map5_ChrPage(nPage);
    InfoNES_SetupChr();
  }

  /* Initialize State Registers */
  /* Mesen2's power-on oracle exposes $5113-$5116 as zero and the only
   * reset-detected PRG register, $5117, as $ff.  In particular, $6000 must
   * start on bank 0 before a title first writes $5113. */
  InfoNES_MemorySet(Map5_Prg_Reg, 0x00, sizeof(Map5_Prg_Reg));
  Map5_Prg_Reg[7] = 0xff;
  InfoNES_MemorySet(Map5_Wram_Reg, 0xff, sizeof(Map5_Wram_Reg));

  /* Mesen2's MMC5 reset state leaves all twelve CHR registers at zero.  The
   * B set is not preloaded with an A-set mirror; it becomes visible only
   * after a $5128-$512B write (or when the title selects the corresponding
   * 8x16-sprite latch). */
  InfoNES_MemorySet(Map5_Chr_Reg, 0x00, sizeof(Map5_Chr_Reg));

  InfoNES_MemorySet(Map5_Ex_Ram, 0x00, sizeof(Map5_Ex_Ram));
  InfoNES_MemorySet(Map5_Empty_Nam, 0x00, sizeof(Map5_Empty_Nam));
  InfoNES_MemorySet(Map5_Ex_Nam, 0x00, sizeof(Map5_Ex_Nam));
  InfoNES_MemorySet(Map5_Open_Bus, 0xff, sizeof(Map5_Open_Bus));

  Map5_Prg_Size = 3;
  /* Mesen2's MMC5 reset state leaves both protection latches at zero.
   * Writes remain disabled until the documented $5102=$02, $5103=$01
   * sequence is written. */
  Map5_Wram_Protect0 = 0x00;
  Map5_Wram_Protect1 = 0x00;
  Map5_Wram_Linear_16k = 0;
  if (ROM_NES2 && Map5_Wram_Bank_Count == 2)
  {
    const BYTE byRam = NesHeader.byReserve[2] & 0x0f;
    const BYTE byNvRam = NesHeader.byReserve[2] >> 4;
    Map5_Wram_Linear_16k = (byRam == 0) != (byNvRam == 0);
  }
  Map5_Battery_Wram_Mask = Map5_BuildBatteryWramMask();
  Map5_Battery_Wram_Dirty = false;
  Map5_Battery_Ex_Ram = ROM_SRAM != 0;
  Map5_Battery_Ex_Ram_Dirty = false;
  SRAMBANK = Map5_GetWramPage(Map5_Prg_Reg[3], &Map5_Wram_Reg[3]);
  /* $5101 powers up in 8 KiB CHR mode.  A number of fixtures write the
   * register explicitly, so the old 1 KiB default was easy to miss; keep
   * the reset state aligned with the MMC5 register model instead. */
  Map5_Chr_Size = 0;
  Map5_Gfx_Mode = 0;
  Map5_Chr_Upper = 0;
  Map5_Chr_Last_Reg = 0;
  Map5_Chr_Render_Mode = 0xff;
  Map5_Chr_Mapping_Dirty = 1;
  Map5_Nametable_Reg = 0;
  Map5_Split_Control = 0;
  Map5_Split_Scroll = 0;
  Map5_Split_Bank = 0;

  Map5_IRQ_Enable = 0;
  Map5_IRQ_Status = 0;
  Map5_IRQ_Line = 0;
  Map5_IRQ_Scanline = 0;
  Map5_Value0 = 0;
  Map5_Value1 = 0;

  /* $5105 defaults to CIRAM page 0 in every nametable slot. */
  Map5_Sync_Nametable();
  Map5_Sync_Prg_Banks();
  Map5_AudioReset();
#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
  Map5_AudioDiagnosticsReset();
#endif

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring(1, 1);
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Read from APU Function                                  */
/*-------------------------------------------------------------------*/
BYTE Map5_ReadApu(WORD wAddr)
{
  BYTE byRet = (BYTE)(wAddr >> 8);

  switch (wAddr)
  {
  case 0x5010:
    /* MMC5A reports revision bit 0 as one.  Reading this register
     * acknowledges the PCM stop-code IRQ, but does not acknowledge the
     * scanline IRQ at $5204. */
    byRet = (BYTE)(0x01 |
                   ((Map5_PcmIrqEnable && Map5_PcmIrqPending) ? 0x80 : 0));
    Map5_PcmIrqPending = 0;
    K6502_RefreshIrqLine();
    break;

  case 0x5015:
    byRet = (Map5_AudioPulse1.byEnabled && Map5_AudioPulse1.byLength ? 0x01 : 0x00) |
            (Map5_AudioPulse2.byEnabled && Map5_AudioPulse2.byLength ? 0x02 : 0x00);
    break;

  case 0x5204:
    byRet = Map5_IRQ_Status;
    Map5_IRQ_Status &= (BYTE)~0x80;
    K6502_RefreshIrqLine();
    break;

  case 0x5205:
    byRet = (BYTE)((Map5_Value0 * Map5_Value1) & 0x00ff);
    break;

  case 0x5206:
    byRet = (BYTE)(((Map5_Value0 * Map5_Value1) & 0xff00) >> 8);
    break;

  default:
    if (0x5c00 <= wAddr && wAddr <= 0x5fff)
    {
      /* MMC5 exposes ExRAM to the CPU only in modes 2 (read/write)
       * and 3 (read-only).  Modes 0 and 1 are write-only/open-bus. */
      if (Map5_Gfx_Mode >= 2)
      {
        byRet = Map5_Ex_Ram[wAddr - 0x5c00];
      }
    }
    break;
  }
  return byRet;
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 nametable mapping                                       */
/*-------------------------------------------------------------------*/
void Map5_Sync_Nametable()
{
  BYTE byNametable = Map5_Nametable_Reg;

  for (int nPage = 0; nPage < 4; ++nPage)
  {
    const BYTE byNamReg = byNametable & 0x03;
    byNametable >>= 2;

    BYTE *pNametable;

    switch (byNamReg)
    {
    case 0:
      pNametable = VRAMPAGE(0);
      break;
    case 1:
      pNametable = VRAMPAGE(1);
      break;
    case 2:
      /* ExRAM is visible to the PPU only in modes 0 and 1. */
      pNametable = (Map5_Gfx_Mode <= 1) ? Map5_Ex_Ram : Map5_Empty_Nam;
      break;
    default:
      pNametable = Map5_Ex_Nam;
      break;
    }

    PPUBANK[nPage + 8] = pNametable;
    /* $3000-$3eff is the PPU mirror of $2000-$2eff.  The common $2007
     * path accesses both address ranges directly, so keep the mirror slots
     * in step with the four MMC5-selected nametables. */
    PPUBANK[nPage + 12] = pNametable;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Write to APU Function                                   */
/*-------------------------------------------------------------------*/
void Map5_Apu(WORD wAddr, BYTE byData)
{
  switch (wAddr)
  {
  case 0x5000:
  case 0x5001:
  case 0x5002:
  case 0x5003:
    Map5_AudioWritePulse(&Map5_AudioPulse1, wAddr & 0x03, byData);
    break;

  case 0x5004:
  case 0x5005:
  case 0x5006:
  case 0x5007:
    Map5_AudioWritePulse(&Map5_AudioPulse2, wAddr & 0x03, byData);
    break;

  case 0x5010:
    Map5_PcmReadMode = byData & 0x01;
    Map5_PcmIrqEnable = byData & 0x80;
    /* Disabling the IRQ gate does not acknowledge the stop-code latch.
     * Re-enabling it must expose a previously detected zero byte. */
    K6502_RefreshIrqLine();
    break;

  case 0x5011:
    if (!Map5_PcmReadMode)
    {
      /* A zero is a stop code: it leaves the DAC unchanged and trips the
       * PCM IRQ latch.  Any non-zero write updates the DAC and clears the
       * stop-code latch. */
      const BYTE byPreviousPending = Map5_PcmIrqPending;
      Map5_PcmIrqPending = (byData == 0);
      if (byData)
      {
        Map5_PcmOutput = byData;
      }
      if (byPreviousPending != Map5_PcmIrqPending)
      {
        K6502_RefreshIrqLine();
      }
    }
    break;

  case 0x5015:
    Map5_AudioPulse1.byEnabled = byData & 0x01;
    Map5_AudioPulse2.byEnabled = byData & 0x02;
    if (!Map5_AudioPulse1.byEnabled)
    {
      Map5_AudioPulse1.byLength = 0;
    }
    if (!Map5_AudioPulse2.byEnabled)
    {
      Map5_AudioPulse2.byLength = 0;
    }
    break;

  case 0x5100:
    Map5_Prg_Size = byData & 0x03;
    Map5_Sync_Prg_Banks();
    break;

  case 0x5101:
    Map5_Chr_Size = byData & 0x03;
    Map5_Chr_Mapping_Dirty = 1;
    Map5_SyncCpuChrBanks();
    break;

  case 0x5102:
    Map5_Wram_Protect0 = byData & 0x03;
    break;

  case 0x5103:
    Map5_Wram_Protect1 = byData & 0x03;
    break;

  case 0x5104:
    Map5_Gfx_Mode = byData & 0x03;
    Map5_Sync_Nametable();
    break;

  case 0x5105:
    Map5_Nametable_Reg = byData;
    Map5_Sync_Nametable();
    break;

  case 0x5106:
    InfoNES_MemorySet(Map5_Ex_Nam, byData, 0x3c0);
    break;

  case 0x5107:
    byData &= 0x03;
    byData = byData | (byData << 2) | (byData << 4) | (byData << 6);
    InfoNES_MemorySet(&(Map5_Ex_Nam[0x3c0]), byData, 0x400 - 0x3c0);
    break;

  case 0x5113:
    SRAMBANK = Map5_GetWramPage(byData, &Map5_Wram_Reg[3]);
    break;

  case 0x5114:
  case 0x5115:
  case 0x5116:
  case 0x5117:
    Map5_Prg_Reg[wAddr & 0x07] = byData;
    Map5_Sync_Prg_Banks();
    break;

  case 0x5120:
  case 0x5121:
  case 0x5122:
  case 0x5123:
  case 0x5124:
  case 0x5125:
  case 0x5126:
  case 0x5127:
    Map5_Chr_Reg[wAddr & 0x07][0] =
        (WORD)byData | ((WORD)Map5_Chr_Upper << 8);
    Map5_Chr_Last_Reg = wAddr & 0x0f;
    Map5_Chr_Mapping_Dirty = 1;
    Map5_SyncCpuChrBanks();
    break;

  case 0x5128:
  case 0x5129:
  case 0x512a:
  case 0x512b:
    Map5_Chr_Reg[(wAddr & 0x03) + 0][1] =
        (WORD)byData | ((WORD)Map5_Chr_Upper << 8);
    Map5_Chr_Reg[(wAddr & 0x03) + 4][1] =
        (WORD)byData | ((WORD)Map5_Chr_Upper << 8);
    Map5_Chr_Last_Reg = wAddr & 0x0f;
    Map5_Chr_Mapping_Dirty = 1;
    Map5_SyncCpuChrBanks();
    break;

  case 0x5130:
    Map5_Chr_Upper = byData & 0x03;
    break;

  case 0x5200:
    Map5_Split_Control = byData;
    break;

  case 0x5201:
    Map5_Split_Scroll = byData;
    break;

  case 0x5202:
    Map5_Split_Bank = byData;
    break;

  case 0x5203:
    Map5_IRQ_Line = byData;
    break;

  case 0x5204:
    Map5_IRQ_Enable = byData & 0x80;
    K6502_RefreshIrqLine();
    break;

  case 0x5205:
    Map5_Value0 = byData;
    break;

  case 0x5206:
    Map5_Value1 = byData;
    break;

  default:
    if (0x5000 <= wAddr && wAddr <= 0x5015)
    {
      /* Extra Sound */
    }
    else if (0x5c00 <= wAddr && wAddr <= 0x5fff)
    {
      if (Map5_Gfx_Mode <= 1)
      {
        /* In modes 0/1, CPU writes are accepted only while rendering; a
         * write outside that interval stores zero.  Use the live $2001
         * rendering bits here rather than the previous HSync's status
         * snapshot, so a write immediately after rendering is disabled is
         * not accidentally accepted. */
        const bool bRendering = Map5_PpuRenderingActive();
        Map5_Ex_Ram[wAddr - 0x5c00] = bRendering ? byData : 0;
        Map5_MarkExRamDirty();
      }
      else if (Map5_Gfx_Mode == 2)
      {
        /* Mode 2 is ordinary CPU-readable/writable ExRAM.  Mode 3 stays
         * read-only and deliberately falls through without a write. */
        Map5_Ex_Ram[wAddr - 0x5c00] = byData;
        Map5_MarkExRamDirty();
      }
    }
    break;
  }

}

/*-------------------------------------------------------------------*/
/*  Mapper 5 V-Sync Function                                         */
/*-------------------------------------------------------------------*/
void Map5_VSync()
{
  /* The mapper callback is 60 Hz.  MMC5 has no frame sequencer of its own;
   * both envelope and length state advance at the fixed roughly-240 Hz
   * cadence used by the expansion audio. */
  for (int nQuarter = 0; nQuarter < 4; ++nQuarter)
  {
    Map5_AudioClockQuarter(&Map5_AudioPulse1);
    Map5_AudioClockQuarter(&Map5_AudioPulse2);
  }
}

/*-------------------------------------------------------------------*/
/*  Render one MMC5 expansion-audio slice                            */
/*-------------------------------------------------------------------*/
void Map5_RenderAudioSlice(int16_t *pDst, int n, bool enabled)
{
  for (int i = 0; i < n; ++i)
  {
    if (!enabled)
    {
      pDst[i] = 0;
    }
    else
    {
      /* Pulse amplitude matches the base APU's 0-15 scale.  MMC5's PCM DAC
       * uses all 8 bits, unlike the 4-bit pulse channels.  Both sources have
       * the opposite polarity to the console APU, so preserve that sign
       * before the platform's centered mixer removes DC bias. */
      pDst[i] = (int16_t)-(Map5_AudioRenderPulse(&Map5_AudioPulse1) +
                           Map5_AudioRenderPulse(&Map5_AudioPulse2) +
                           (int)Map5_PcmOutput);
    }

#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
    if (Map5_AudioDiagSampleCount == 0 ||
        pDst[i] < Map5_AudioDiagMinSample)
    {
      Map5_AudioDiagMinSample = pDst[i];
    }
    if (Map5_AudioDiagSampleCount == 0 ||
        pDst[i] > Map5_AudioDiagMaxSample)
    {
      Map5_AudioDiagMaxSample = pDst[i];
    }
    ++Map5_AudioDiagSampleCount;
#endif
  }
}

/*-------------------------------------------------------------------*/
/*  Resolve one MMC5 background tile which overrides normal PPU data */
/*-------------------------------------------------------------------*/
bool Map5_ResolveBackgroundTile(int nTileY, int nTileX, int nScreenTileX,
                                WORD wPpuPatternAddr,
                                BYTE *pTile,
                                BYTE *pPaletteBase,
                                BYTE **ppPatternRow)
{
  /* The vertical split substitutes both nametable and attribute data
   * from ExRAM.  $5200 selects the left or right side of the displayed
   * scanline; its delimiter is counted from the first visible tile rather
   * than from the nametable address, so horizontal scrolling does not move
   * the split boundary. */
  if (Map5_Gfx_Mode <= 1 && (Map5_Split_Control & 0x80))
  {
    const int nDelimiter = Map5_Split_Control & 0x1f;
    const bool bRightSide = (Map5_Split_Control & 0x40) != 0;
    const bool bInSplit = bRightSide ? (nScreenTileX >= nDelimiter) :
                                       (nScreenTileX < nDelimiter);
    if (bInSplit && nScreenTileX < 32)
    {
      const BYTE bySplitY = (BYTE)((PPU_Scanline + Map5_Split_Scroll) % 240);
      const WORD wExOffset = (WORD)((bySplitY & 0xf8) << 2) | (BYTE)nScreenTileX;
      const BYTE byTile = Map5_Ex_Ram[wExOffset];
      const WORD wAttrOffset = (WORD)(0x3c0 | ((wExOffset & 0x380) >> 4) |
                                      ((wExOffset & 0x01f) >> 2));
      const BYTE byAttrShift = (BYTE)(((wExOffset >> 4) & 0x04) |
                                      (wExOffset & 0x02));

      *pTile = byTile;
      *pPaletteBase = (BYTE)(((Map5_Ex_Ram[wAttrOffset] >> byAttrShift) & 0x03) << 2);
      *ppPatternRow = Map5_ChrPage((DWORD)Map5_Split_Bank << 2) +
                      ((byTile << 4) | (bySplitY & 0x07));
      return true;
    }
  }

  if (Map5_Gfx_Mode != 1)
  {
    return false;
  }

  /* In extended-attribute mode ExRAM is one-screen mirrored: the
   * 32x30 nametable offset, not the selected nametable page, selects
   * its byte.  Bits 7-6 select the palette and bits 5-0, together
   * with $5130, select a 4 KiB background CHR bank. */
  const BYTE byExAttr = Map5_Ex_Ram[((nTileY & 0x1f) << 5) | (nTileX & 0x1f)];
  const DWORD dwBank = (DWORD)(byExAttr & 0x3f) | ((DWORD)Map5_Chr_Upper << 6);
  const DWORD dwPage = (dwBank << 2) | ((wPpuPatternAddr >> 10) & 0x03);

  *pTile = (BYTE)(wPpuPatternAddr >> 4);
  *pPaletteBase = (BYTE)((byExAttr >> 6) << 2);
  *ppPatternRow = Map5_ChrPage(dwPage) + (wPpuPatternAddr & 0x03ff);
  return true;
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Write to SRAM Function                                  */
/*-------------------------------------------------------------------*/
void Map5_Sram(WORD wAddr, BYTE byData)
{
  if (Map5_Wram_Protect0 == 0x02 && Map5_Wram_Protect1 == 0x01)
  {
    BYTE *const page = Map5_WritableWramPage(Map5_Wram_Reg[3]);
    if (page)
    {
      page[wAddr - 0x6000] = byData;
      Map5_MarkWramDirty(Map5_Wram_Reg[3]);
    }
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Read from SRAM Function                                 */
/*-------------------------------------------------------------------*/
BYTE Map5_ReadSram(WORD wAddr)
{
  return Map5_Wram_Reg[3] == 0xff ? 0xff :
      Map5_WramPage(Map5_Wram_Reg[3])[wAddr - 0x6000];
}

bool Map5_HasBatteryWram()
{
  return Map5_Battery_Wram_Mask != 0;
}

uint16_t Map5_GetBatteryWramMask()
{
  return Map5_Battery_Wram_Mask;
}

BYTE *Map5_GetWramBankData(BYTE byBank)
{
  return byBank < 16 && byBank < Map5_Wram_Storage_Bytes / 0x2000u ?
      Map5_WramPage(byBank) : nullptr;
}

bool Map5_RestoreBatteryWramBank(BYTE byBank, const BYTE *pData, unsigned nBytes)
{
  if (byBank >= 16 || !pData || nBytes != 0x2000u ||
      !(Map5_Battery_Wram_Mask & (uint16_t)(1u << byBank)) ||
      byBank >= Map5_Wram_Storage_Bytes / 0x2000u)
  {
    return false;
  }
  InfoNES_MemoryCopy(Map5_WramPage(byBank), pData, nBytes);
  return true;
}

bool Map5_IsBatteryWramDirty()
{
  return Map5_Battery_Wram_Dirty;
}

void Map5_ClearBatteryWramDirty()
{
  Map5_Battery_Wram_Dirty = false;
}

bool Map5_HasBatteryExRam()
{
  return Map5_Battery_Ex_Ram;
}

const BYTE *Map5_GetBatteryExRamData()
{
  return Map5_Battery_Ex_Ram ? Map5_Ex_Ram : nullptr;
}

bool Map5_RestoreBatteryExRam(const BYTE *pData, unsigned nBytes)
{
  if (!Map5_Battery_Ex_Ram || !pData || nBytes != sizeof(Map5_Ex_Ram))
  {
    return false;
  }
  InfoNES_MemoryCopy(Map5_Ex_Ram, pData, nBytes);
  return true;
}

bool Map5_IsBatteryExRamDirty()
{
  return Map5_Battery_Ex_Ram_Dirty;
}

void Map5_ClearBatteryExRamDirty()
{
  Map5_Battery_Ex_Ram_Dirty = false;
}

void Map5_NotePpuNametableWrite(WORD wAddr)
{
  if (!Map5_Battery_Ex_Ram || wAddr < 0x2000 || wAddr >= 0x3f00)
  {
    return;
  }

  const BYTE byPage = (BYTE)(wAddr >> 10);
  /* $2000-$2fff writes are mirrored by the common PPU path.  Mark the
   * battery only when one of the two mapped destinations is the actual MMC5
   * ExRAM page; CIRAM and fill nametable writes must not create a save. */
  if (PPUBANK[byPage] == Map5_Ex_Ram ||
      PPUBANK[byPage ^ 0x04] == Map5_Ex_Ram)
  {
    Map5_MarkExRamDirty();
  }
}

BYTE Map5_IrqPending()
{
  return ((Map5_IRQ_Enable && (Map5_IRQ_Status & 0x80)) ||
          (Map5_PcmIrqEnable && Map5_PcmIrqPending)) ? 1 : 0;
}

void Map5_OamDmaReset()
{
  /* $4014 is a scanline-counter reset input.  It does not capture the DMA
   * page value and does not acknowledge an already pending IRQ. */
  Map5_IRQ_Scanline = 0;
}

BYTE __not_in_flash_func(Map5_ReadRom)(WORD wAddr, BYTE byData)
{
  if (wAddr == 0xfffa || wAddr == 0xfffb)
  {
    /* The MMC5 clears its scanline IRQ state when the CPU fetches the NMI
     * vector.  This is the vector-fetch equivalent of the hardware's
     * vertical-blank reset detection. */
    Map5_IRQ_Status &= (BYTE)~0xc0;
    Map5_IRQ_Scanline = 0;
    K6502_RefreshIrqLine();
  }

  if (Map5_PcmReadMode && wAddr >= 0x8000 && wAddr <= 0xbfff)
  {
    /* Read-mode PCM watches only $8000-$BFFF.  A non-zero byte becomes the
     * DAC value; zero leaves the DAC unchanged and trips the stop-code IRQ. */
    const BYTE byPreviousPending = Map5_PcmIrqPending;
    Map5_PcmIrqPending = (byData == 0);
    if (byData)
    {
      Map5_PcmOutput = byData;
    }
    if (byPreviousPending != Map5_PcmIrqPending)
    {
      K6502_RefreshIrqLine();
    }
  }
  return byData;
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Write Function                                          */
/*-------------------------------------------------------------------*/
void Map5_Write(WORD wAddr, BYTE byData)
{
  if (Map5_Wram_Protect0 == 0x02 && Map5_Wram_Protect1 == 0x01)
  {
    switch (wAddr & 0xe000)
    {
    case 0x8000: /* $8000-$9fff */
      if (BYTE *const page = Map5_WritableWramPage(Map5_Wram_Reg[4]))
      {
        page[wAddr - 0x8000] = byData;
        Map5_MarkWramDirty(Map5_Wram_Reg[4]);
      }
      break;

    case 0xa000: /* $a000-$bfff */
      if (BYTE *const page = Map5_WritableWramPage(Map5_Wram_Reg[5]))
      {
        page[wAddr - 0xa000] = byData;
        Map5_MarkWramDirty(Map5_Wram_Reg[5]);
      }
      break;

    case 0xc000: /* $c000-$dfff */
      if (BYTE *const page = Map5_WritableWramPage(Map5_Wram_Reg[6]))
      {
        page[wAddr - 0xc000] = byData;
        Map5_MarkWramDirty(Map5_Wram_Reg[6]);
      }
      break;
    }
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 scanline-start event                                    */
/*-------------------------------------------------------------------*/
void Map5_ScanlineStart()
{
  if (!Map5_PpuRenderingActive())
  {
    Map5_IRQ_Status &= (BYTE)~0x40;
    Map5_IRQ_Scanline = 0;
    return;
  }

  /* The first active scanline establishes the zero origin.  Subsequent
   * detected scanlines increment before comparing with $5203, which is the
   * same ordering as MMC5's PPU read sequence. */
  if (Map5_IRQ_Status & 0x40)
  {
    if (Map5_IRQ_Scanline != 0xff)
    {
      ++Map5_IRQ_Scanline;
    }

    /* A compare value of zero never raises the MMC5 scanline IRQ. */
    if (Map5_IRQ_Line && Map5_IRQ_Scanline == Map5_IRQ_Line)
    {
      Map5_IRQ_Status |= 0x80;
      if (Map5_IRQ_Enable)
      {
        K6502_RefreshIrqLine();
      }
    }
  }
  Map5_IRQ_Status |= 0x40;
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 H-Sync Function                                         */
/*-------------------------------------------------------------------*/
void Map5_HSync()
{
  if (!Map5_PpuRenderingActive())
  {
    Map5_IRQ_Status &= (BYTE)~0x40;
    Map5_IRQ_Scanline = 0;
  }
  else
  {
    /* Keep bit 6 visible for the full rendered scanline.  The compare itself
     * was already performed by Map5_ScanlineStart(). */
    Map5_IRQ_Status |= 0x40;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Rendering Screen Function                               */
/*-------------------------------------------------------------------*/
void Map5_RenderScreen(BYTE byMode)
{
  DWORD dwPage[8];
  const DWORD dwPageCount = NesHeader.byVRomSize > 0 ?
      ((DWORD)NesHeader.byVRomSize << 3) : Map5_Chr_Ram_Page_Count;

  /* In normal 8x8-sprite mode, $5120-$5127 select CHR for both
   * background and sprites.  The alternate $5128-$512B background set
   * is used only while 8x16 sprites are enabled. */
  if (!(PPU_R0 & R0_SP_SIZE))
  {
    byMode = 0;
  }

  if (!Map5_Chr_Mapping_Dirty && Map5_Chr_Render_Mode == byMode)
  {
    return;
  }

  switch (Map5_Chr_Size)
  {
  case 0:
    dwPage[7] = ((DWORD)Map5_Chr_Reg[7][byMode] << 3) % dwPageCount;

    PPUBANK[0] = Map5_ChrPage(dwPage[7] + 0);
    PPUBANK[1] = Map5_ChrPage(dwPage[7] + 1);
    PPUBANK[2] = Map5_ChrPage(dwPage[7] + 2);
    PPUBANK[3] = Map5_ChrPage(dwPage[7] + 3);
    PPUBANK[4] = Map5_ChrPage(dwPage[7] + 4);
    PPUBANK[5] = Map5_ChrPage(dwPage[7] + 5);
    PPUBANK[6] = Map5_ChrPage(dwPage[7] + 6);
    PPUBANK[7] = Map5_ChrPage(dwPage[7] + 7);
    InfoNES_SetupChr();
    break;

  case 1:
    dwPage[3] = ((DWORD)Map5_Chr_Reg[3][byMode] << 2) % dwPageCount;
    dwPage[7] = ((DWORD)Map5_Chr_Reg[7][byMode] << 2) % dwPageCount;

    PPUBANK[0] = Map5_ChrPage(dwPage[3] + 0);
    PPUBANK[1] = Map5_ChrPage(dwPage[3] + 1);
    PPUBANK[2] = Map5_ChrPage(dwPage[3] + 2);
    PPUBANK[3] = Map5_ChrPage(dwPage[3] + 3);
    PPUBANK[4] = Map5_ChrPage(dwPage[7] + 0);
    PPUBANK[5] = Map5_ChrPage(dwPage[7] + 1);
    PPUBANK[6] = Map5_ChrPage(dwPage[7] + 2);
    PPUBANK[7] = Map5_ChrPage(dwPage[7] + 3);
    InfoNES_SetupChr();
    break;

  case 2:
    dwPage[1] = ((DWORD)Map5_Chr_Reg[1][byMode] << 1) % dwPageCount;
    dwPage[3] = ((DWORD)Map5_Chr_Reg[3][byMode] << 1) % dwPageCount;
    dwPage[5] = ((DWORD)Map5_Chr_Reg[5][byMode] << 1) % dwPageCount;
    dwPage[7] = ((DWORD)Map5_Chr_Reg[7][byMode] << 1) % dwPageCount;

    PPUBANK[0] = Map5_ChrPage(dwPage[1] + 0);
    PPUBANK[1] = Map5_ChrPage(dwPage[1] + 1);
    PPUBANK[2] = Map5_ChrPage(dwPage[3] + 0);
    PPUBANK[3] = Map5_ChrPage(dwPage[3] + 1);
    PPUBANK[4] = Map5_ChrPage(dwPage[5] + 0);
    PPUBANK[5] = Map5_ChrPage(dwPage[5] + 1);
    PPUBANK[6] = Map5_ChrPage(dwPage[7] + 0);
    PPUBANK[7] = Map5_ChrPage(dwPage[7] + 1);
    InfoNES_SetupChr();
    break;

  default:
    dwPage[0] = (DWORD)Map5_Chr_Reg[0][byMode] % dwPageCount;
    dwPage[1] = (DWORD)Map5_Chr_Reg[1][byMode] % dwPageCount;
    dwPage[2] = (DWORD)Map5_Chr_Reg[2][byMode] % dwPageCount;
    dwPage[3] = (DWORD)Map5_Chr_Reg[3][byMode] % dwPageCount;
    dwPage[4] = (DWORD)Map5_Chr_Reg[4][byMode] % dwPageCount;
    dwPage[5] = (DWORD)Map5_Chr_Reg[5][byMode] % dwPageCount;
    dwPage[6] = (DWORD)Map5_Chr_Reg[6][byMode] % dwPageCount;
    dwPage[7] = (DWORD)Map5_Chr_Reg[7][byMode] % dwPageCount;

    PPUBANK[0] = Map5_ChrPage(dwPage[0]);
    PPUBANK[1] = Map5_ChrPage(dwPage[1]);
    PPUBANK[2] = Map5_ChrPage(dwPage[2]);
    PPUBANK[3] = Map5_ChrPage(dwPage[3]);
    PPUBANK[4] = Map5_ChrPage(dwPage[4]);
    PPUBANK[5] = Map5_ChrPage(dwPage[5]);
    PPUBANK[6] = Map5_ChrPage(dwPage[6]);
    PPUBANK[7] = Map5_ChrPage(dwPage[7]);
    InfoNES_SetupChr();
    break;
  }

  Map5_Chr_Render_Mode = byMode;
  Map5_Chr_Mapping_Dirty = 0;
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Sync Program Banks Function                             */
/*-------------------------------------------------------------------*/
void Map5_Sync_Prg_Banks(void)
{
  switch (Map5_Prg_Size)
  {
  case 0:
    Map5_Wram_Reg[4] = 0xff;
    Map5_Wram_Reg[5] = 0xff;
    Map5_Wram_Reg[6] = 0xff;

    ROMBANK0 = ROMPAGE(((Map5_Prg_Reg[7] & 0x7c) + 0) % (NesHeader.byRomSize << 1));
    ROMBANK1 = ROMPAGE(((Map5_Prg_Reg[7] & 0x7c) + 1) % (NesHeader.byRomSize << 1));
    ROMBANK2 = ROMPAGE(((Map5_Prg_Reg[7] & 0x7c) + 2) % (NesHeader.byRomSize << 1));
    ROMBANK3 = ROMPAGE(((Map5_Prg_Reg[7] & 0x7c) + 3) % (NesHeader.byRomSize << 1));
    break;

  case 1:
    if (Map5_Prg_Reg[5] & 0x80)
    {
      Map5_Wram_Reg[4] = 0xff;
      Map5_Wram_Reg[5] = 0xff;
      ROMBANK0 = ROMPAGE(((Map5_Prg_Reg[5] & 0x7e) + 0) % (NesHeader.byRomSize << 1));
      ROMBANK1 = ROMPAGE(((Map5_Prg_Reg[5] & 0x7e) + 1) % (NesHeader.byRomSize << 1));
    }
    else
    {
      ROMBANK0 = Map5_GetWramPage((Map5_Prg_Reg[5] & 0x06) + 0,
                                  &Map5_Wram_Reg[4]);
      ROMBANK1 = Map5_GetWramPage((Map5_Prg_Reg[5] & 0x06) + 1,
                                  &Map5_Wram_Reg[5]);
    }

    Map5_Wram_Reg[6] = 0xff;
    ROMBANK2 = ROMPAGE(((Map5_Prg_Reg[7] & 0x7e) + 0) % (NesHeader.byRomSize << 1));
    ROMBANK3 = ROMPAGE(((Map5_Prg_Reg[7] & 0x7e) + 1) % (NesHeader.byRomSize << 1));
    break;

  case 2:
    if (Map5_Prg_Reg[5] & 0x80)
    {
      Map5_Wram_Reg[4] = 0xff;
      Map5_Wram_Reg[5] = 0xff;
      ROMBANK0 = ROMPAGE(((Map5_Prg_Reg[5] & 0x7e) + 0) % (NesHeader.byRomSize << 1));
      ROMBANK1 = ROMPAGE(((Map5_Prg_Reg[5] & 0x7e) + 1) % (NesHeader.byRomSize << 1));
    }
    else
    {
      ROMBANK0 = Map5_GetWramPage((Map5_Prg_Reg[5] & 0x06) + 0,
                                  &Map5_Wram_Reg[4]);
      ROMBANK1 = Map5_GetWramPage((Map5_Prg_Reg[5] & 0x06) + 1,
                                  &Map5_Wram_Reg[5]);
    }

    if (Map5_Prg_Reg[6] & 0x80)
    {
      Map5_Wram_Reg[6] = 0xff;
      ROMBANK2 = ROMPAGE((Map5_Prg_Reg[6] & 0x7f) % (NesHeader.byRomSize << 1));
    }
    else
    {
      ROMBANK2 = Map5_GetWramPage(Map5_Prg_Reg[6], &Map5_Wram_Reg[6]);
    }

    ROMBANK3 = ROMPAGE((Map5_Prg_Reg[7] & 0x7f) % (NesHeader.byRomSize << 1));
    break;

  default:
    if (Map5_Prg_Reg[4] & 0x80)
    {
      Map5_Wram_Reg[4] = 0xff;
      ROMBANK0 = ROMPAGE((Map5_Prg_Reg[4] & 0x7f) % (NesHeader.byRomSize << 1));
    }
    else
    {
      ROMBANK0 = Map5_GetWramPage(Map5_Prg_Reg[4], &Map5_Wram_Reg[4]);
    }

    if (Map5_Prg_Reg[5] & 0x80)
    {
      Map5_Wram_Reg[5] = 0xff;
      ROMBANK1 = ROMPAGE((Map5_Prg_Reg[5] & 0x7f) % (NesHeader.byRomSize << 1));
    }
    else
    {
      ROMBANK1 = Map5_GetWramPage(Map5_Prg_Reg[5], &Map5_Wram_Reg[5]);
    }

    if (Map5_Prg_Reg[6] & 0x80)
    {
      Map5_Wram_Reg[6] = 0xff;
      ROMBANK2 = ROMPAGE((Map5_Prg_Reg[6] & 0x7f) % (NesHeader.byRomSize << 1));
    }
    else
    {
      ROMBANK2 = Map5_GetWramPage(Map5_Prg_Reg[6], &Map5_Wram_Reg[6]);
    }

    ROMBANK3 = ROMPAGE((Map5_Prg_Reg[7] & 0x7f) % (NesHeader.byRomSize << 1));
    break;
  }
}
