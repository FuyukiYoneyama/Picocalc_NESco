/*===================================================================*/
/*                                                                   */
/*                        Mapper 5 (MMC5)                            */
/*                                                                   */
/*===================================================================*/

BYTE Map5_Wram[0x2000 * 8];
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

DWORD Map5_Value0;
DWORD Map5_Value1;

BYTE Map5_Wram_Protect0;
BYTE Map5_Wram_Protect1;
BYTE Map5_Wram_Bank_Count;
BYTE Map5_Wram_Linear_16k;
BYTE Map5_Battery_Wram_Mask;
bool Map5_Battery_Wram_Dirty;
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

#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
static uint32_t Map5_AudioDiagSampleCount;
static int16_t Map5_AudioDiagMinSample;
static int16_t Map5_AudioDiagMaxSample;
#endif

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

/* The address of an 8 KiB unit of the MMC5 PRG-RAM backing store. */
#define Map5_WRAMPAGE(a) &Map5_Wram[((a)&0x07) * 0x2000]

void Map5_Sync_Nametable();

static void Map5_SyncCpuChrBanks()
{
  BYTE byMode;

  if (!(PPU_R0 & R0_SP_SIZE))
  {
    /* The MMC5 resets the last-register selection while sprites are 8x8. */
    Map5_Chr_Last_Reg = 0;
    byMode = 0;
  }
  else if (Map5_IRQ_Status & 0x40)
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
  *pMappedBank = Map5_GetWramBank(bySelect);
  return *pMappedBank == 0xff ? Map5_Open_Bus : Map5_WRAMPAGE(*pMappedBank);
}

static unsigned Map5_DecodeNes2RamBytes(BYTE byShift)
{
  return byShift ? (64u << byShift) : 0u;
}

static BYTE Map5_DetectWramBankCount()
{
  if (ROM_NES2)
  {
    const BYTE byRam = NesHeader.byReserve[2] & 0x0f;
    const BYTE byNvRam = NesHeader.byReserve[2] >> 4;
    const unsigned nRamBytes = Map5_DecodeNes2RamBytes(byRam) +
                               Map5_DecodeNes2RamBytes(byNvRam);
    const unsigned nBanks = nRamBytes / 0x2000u;
    return nBanks > 8 ? 8 : (BYTE)nBanks;
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

static BYTE Map5_BuildBatteryWramMask()
{
  BYTE byMask = 0;
  for (BYTE bySelect = 0; bySelect < 8; ++bySelect)
  {
    const BYTE byBank = Map5_GetWramBank(bySelect);
    if (byBank != 0xff && Map5_IsBatteryWramSelect(bySelect))
    {
      byMask |= (BYTE)(1u << byBank);
    }
  }
  return byMask;
}

static void Map5_MarkWramDirty(BYTE byMappedBank)
{
  if (byMappedBank != 0xff && (Map5_Battery_Wram_Mask & (BYTE)(1u << byMappedBank)))
  {
    Map5_Battery_Wram_Dirty = true;
  }
}

static void Map5_AudioReset()
{
  InfoNES_MemorySet(&Map5_AudioPulse1, 0, sizeof(Map5_AudioPulse1));
  InfoNES_MemorySet(&Map5_AudioPulse2, 0, sizeof(Map5_AudioPulse2));
  Map5_PcmReadMode = 0;
  Map5_PcmOutput = 0;
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
  if (!pPulse->byEnabled)
  {
    return;
  }

  /* Envelope clocks at the APU quarter-frame rate (240 Hz). */
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
}

static void Map5_AudioClockHalf(Map5_AudioPulse *pPulse)
{
  if (pPulse->byEnabled && pPulse->byLength &&
      !(pPulse->byReg[0] & 0x20))
  {
    /* MMC5 pulse length counters use the APU half-frame rate (120 Hz),
     * while their envelopes are handled above at 240 Hz. */
    --pPulse->byLength;
  }
}

static int Map5_AudioRenderPulse(Map5_AudioPulse *pPulse)
{
  if (!pPulse->byEnabled || !pPulse->byLength)
  {
    return 0;
  }

  const WORD wTimer = (WORD)pPulse->byReg[2] |
                      (WORD)((pPulse->byReg[3] & 0x07) << 8);
  const uint64_t qwDenominator =
      (uint64_t)(wTimer + 1) * 16u * 22050u;
  const DWORD dwStep = (DWORD)((1789773ull << 32) / qwDenominator);
  pPulse->dwPhase += dwStep;

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
    /* A CHR-RAM MMC5 starts with the common fixed 8 KiB pattern RAM. */
    for (nPage = 0; nPage < 8; ++nPage)
      PPUBANK[nPage] = CRAMPAGE(nPage);
    InfoNES_SetupChr();
  }

  /* Initialize State Registers */
  /* Mesen2's power-on oracle exposes $5113-$5116 as zero and the only
   * reset-detected PRG register, $5117, as $ff.  In particular, $6000 must
   * start on bank 0 before a title first writes $5113. */
  InfoNES_MemorySet(Map5_Prg_Reg, 0x00, sizeof(Map5_Prg_Reg));
  Map5_Prg_Reg[7] = 0xff;
  InfoNES_MemorySet(Map5_Wram_Reg, 0xff, sizeof(Map5_Wram_Reg));

  for (BYTE byPage = 4; byPage < 8; ++byPage)
  {
    Map5_Chr_Reg[byPage][0] = byPage;
    Map5_Chr_Reg[byPage][1] = (byPage & 0x03) + 4;
  }

  InfoNES_MemorySet(Map5_Wram, 0x00, sizeof(Map5_Wram));
  InfoNES_MemorySet(Map5_Ex_Ram, 0x00, sizeof(Map5_Ex_Ram));
  InfoNES_MemorySet(Map5_Empty_Nam, 0x00, sizeof(Map5_Empty_Nam));
  InfoNES_MemorySet(Map5_Ex_Nam, 0x00, sizeof(Map5_Ex_Nam));
  InfoNES_MemorySet(Map5_Open_Bus, 0xff, sizeof(Map5_Open_Bus));

  Map5_Prg_Size = 3;
  /* Power-on values leave PRG-RAM write-protected.  Writes become
   * enabled only after the MMC5's documented $5102=$02, $5103=$01
   * sequence. */
  Map5_Wram_Protect0 = 0x01;
  Map5_Wram_Protect1 = 0x02;
  Map5_Wram_Bank_Count = Map5_DetectWramBankCount();
  Map5_Work_Ram_Bytes = ROM_NES2 ?
      Map5_DecodeNes2RamBytes(NesHeader.byReserve[2] & 0x0f) : 0;
  Map5_Save_Ram_Bytes = ROM_NES2 ?
      Map5_DecodeNes2RamBytes(NesHeader.byReserve[2] >> 4) :
      (ROM_SRAM ? 0x10000u : 0u);
  Map5_Wram_Linear_16k = 0;
  if (ROM_NES2 && Map5_Wram_Bank_Count == 2)
  {
    const BYTE byRam = NesHeader.byReserve[2] & 0x0f;
    const BYTE byNvRam = NesHeader.byReserve[2] >> 4;
    Map5_Wram_Linear_16k = (byRam == 0) != (byNvRam == 0);
  }
  Map5_Battery_Wram_Mask = Map5_BuildBatteryWramMask();
  Map5_Battery_Wram_Dirty = false;
  SRAMBANK = Map5_GetWramPage(Map5_Prg_Reg[3], &Map5_Wram_Reg[3]);
  Map5_Chr_Size = 3;
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
    byRet = 0;
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

    switch (byNamReg)
    {
    case 0:
      PPUBANK[nPage + 8] = VRAMPAGE(0);
      break;
    case 1:
      PPUBANK[nPage + 8] = VRAMPAGE(1);
      break;
    case 2:
      /* ExRAM is visible to the PPU only in modes 0 and 1. */
      PPUBANK[nPage + 8] = (Map5_Gfx_Mode <= 1) ? Map5_Ex_Ram : Map5_Empty_Nam;
      break;
    default:
      PPUBANK[nPage + 8] = Map5_Ex_Nam;
      break;
    }
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
    break;

  case 0x5011:
    /* Direct PCM writes of zero leave the previous DAC level unchanged. */
    if (!Map5_PcmReadMode && byData)
    {
      Map5_PcmOutput = byData;
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
         * write outside that interval stores zero.  HSync tracks the same
         * in-frame condition reported by $5204 bit 6. */
        Map5_Ex_Ram[wAddr - 0x5c00] = (Map5_IRQ_Status & 0x40) ? byData : 0;
      }
      else if (Map5_Gfx_Mode == 2)
      {
        /* Mode 2 is ordinary CPU-readable/writable ExRAM.  Mode 3 stays
         * read-only and deliberately falls through without a write. */
        Map5_Ex_Ram[wAddr - 0x5c00] = byData;
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
  /* The mapper callback is 60 Hz.  MMC5 envelopes clock at 240 Hz and
   * length counters at the 120 Hz half-frame cadence. */
  for (int nQuarter = 0; nQuarter < 4; ++nQuarter)
  {
    Map5_AudioClockQuarter(&Map5_AudioPulse1);
    Map5_AudioClockQuarter(&Map5_AudioPulse2);
    if (nQuarter & 1)
    {
      Map5_AudioClockHalf(&Map5_AudioPulse1);
      Map5_AudioClockHalf(&Map5_AudioPulse2);
    }
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
      /* Pulse amplitude matches the base APU's 0-15 scale.  MMC5's pulse and
       * PCM DACs have the opposite polarity to the console APU, so preserve
       * that sign before the platform's centered mixer removes DC bias. */
      pDst[i] = (int16_t)-(Map5_AudioRenderPulse(&Map5_AudioPulse1) +
                           Map5_AudioRenderPulse(&Map5_AudioPulse2) +
                           (Map5_PcmOutput >> 4));
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
  if (NesHeader.byVRomSize == 0)
  {
    return false;
  }

  const DWORD dwPageCount = (DWORD)NesHeader.byVRomSize << 3;

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
      *ppPatternRow = VROMPAGE(((DWORD)Map5_Split_Bank << 2) % dwPageCount) +
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
  *ppPatternRow = VROMPAGE(dwPage % dwPageCount) + (wPpuPatternAddr & 0x03ff);
  return true;
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Write to SRAM Function                                  */
/*-------------------------------------------------------------------*/
void Map5_Sram(WORD wAddr, BYTE byData)
{
  if (Map5_Wram_Protect0 == 0x02 && Map5_Wram_Protect1 == 0x01)
  {
    if (Map5_Wram_Reg[3] != 0xff)
    {
      Map5_Wram[0x2000 * Map5_Wram_Reg[3] + (wAddr - 0x6000)] = byData;
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
      Map5_Wram[0x2000 * Map5_Wram_Reg[3] + (wAddr - 0x6000)];
}

bool Map5_HasBatteryWram()
{
  return Map5_Battery_Wram_Mask != 0;
}

BYTE Map5_GetBatteryWramMask()
{
  return Map5_Battery_Wram_Mask;
}

BYTE *Map5_GetWramBankData(BYTE byBank)
{
  return byBank < 8 ? Map5_WRAMPAGE(byBank) : nullptr;
}

bool Map5_RestoreBatteryWramBank(BYTE byBank, const BYTE *pData, unsigned nBytes)
{
  if (byBank >= 8 || !pData || nBytes != 0x2000u ||
      !(Map5_Battery_Wram_Mask & (BYTE)(1u << byBank)))
  {
    return false;
  }
  InfoNES_MemoryCopy(Map5_WRAMPAGE(byBank), pData, nBytes);
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

BYTE Map5_IrqPending()
{
  return (Map5_IRQ_Enable && (Map5_IRQ_Status & 0x80)) ? 1 : 0;
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
      if (Map5_Wram_Reg[4] != 0xff)
      {
        Map5_Wram[0x2000 * Map5_Wram_Reg[4] + (wAddr - 0x8000)] = byData;
        Map5_MarkWramDirty(Map5_Wram_Reg[4]);
      }
      break;

    case 0xa000: /* $a000-$bfff */
      if (Map5_Wram_Reg[5] != 0xff)
      {
        Map5_Wram[0x2000 * Map5_Wram_Reg[5] + (wAddr - 0xa000)] = byData;
        Map5_MarkWramDirty(Map5_Wram_Reg[5]);
      }
      break;

    case 0xc000: /* $c000-$dfff */
      if (Map5_Wram_Reg[6] != 0xff)
      {
        Map5_Wram[0x2000 * Map5_Wram_Reg[6] + (wAddr - 0xc000)] = byData;
        Map5_MarkWramDirty(Map5_Wram_Reg[6]);
      }
      break;
    }
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 H-Sync Function                                         */
/*-------------------------------------------------------------------*/
void Map5_HSync()
{
  if (PPU_Scanline < 240)
  {
    /* $5204 bit 6 means PPU rendering, not merely a visible scanline. */
    if (PPU_R1 & (R1_SHOW_SCR | R1_SHOW_SP))
    {
      Map5_IRQ_Status |= 0x40;

      /* A compare value of zero never raises the MMC5 scanline IRQ. */
      if (Map5_IRQ_Line && PPU_Scanline == Map5_IRQ_Line)
      {
        Map5_IRQ_Status |= 0x80;

        if (Map5_IRQ_Enable)
        {
          K6502_RefreshIrqLine();
        }
      }
    }
    else
    {
      Map5_IRQ_Status &= (BYTE)~0x40;
    }
  }
  else
  {
    Map5_IRQ_Status &= (BYTE)~0x40;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Rendering Screen Function                               */
/*-------------------------------------------------------------------*/
void Map5_RenderScreen(BYTE byMode)
{
  DWORD dwPage[8];

  if (NesHeader.byVRomSize == 0)
  {
    /* MMC5 CHR-RAM titles use the common CRAM mapping established at ROM
     * load.  There is no CHR-ROM page count to modulo here. */
    return;
  }

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
    dwPage[7] = ((DWORD)Map5_Chr_Reg[7][byMode] << 3) % (NesHeader.byVRomSize << 3);

    PPUBANK[0] = VROMPAGE(dwPage[7] + 0);
    PPUBANK[1] = VROMPAGE(dwPage[7] + 1);
    PPUBANK[2] = VROMPAGE(dwPage[7] + 2);
    PPUBANK[3] = VROMPAGE(dwPage[7] + 3);
    PPUBANK[4] = VROMPAGE(dwPage[7] + 4);
    PPUBANK[5] = VROMPAGE(dwPage[7] + 5);
    PPUBANK[6] = VROMPAGE(dwPage[7] + 6);
    PPUBANK[7] = VROMPAGE(dwPage[7] + 7);
    InfoNES_SetupChr();
    break;

  case 1:
    dwPage[3] = ((DWORD)Map5_Chr_Reg[3][byMode] << 2) % (NesHeader.byVRomSize << 3);
    dwPage[7] = ((DWORD)Map5_Chr_Reg[7][byMode] << 2) % (NesHeader.byVRomSize << 3);

    PPUBANK[0] = VROMPAGE(dwPage[3] + 0);
    PPUBANK[1] = VROMPAGE(dwPage[3] + 1);
    PPUBANK[2] = VROMPAGE(dwPage[3] + 2);
    PPUBANK[3] = VROMPAGE(dwPage[3] + 3);
    PPUBANK[4] = VROMPAGE(dwPage[7] + 0);
    PPUBANK[5] = VROMPAGE(dwPage[7] + 1);
    PPUBANK[6] = VROMPAGE(dwPage[7] + 2);
    PPUBANK[7] = VROMPAGE(dwPage[7] + 3);
    InfoNES_SetupChr();
    break;

  case 2:
    dwPage[1] = ((DWORD)Map5_Chr_Reg[1][byMode] << 1) % (NesHeader.byVRomSize << 3);
    dwPage[3] = ((DWORD)Map5_Chr_Reg[3][byMode] << 1) % (NesHeader.byVRomSize << 3);
    dwPage[5] = ((DWORD)Map5_Chr_Reg[5][byMode] << 1) % (NesHeader.byVRomSize << 3);
    dwPage[7] = ((DWORD)Map5_Chr_Reg[7][byMode] << 1) % (NesHeader.byVRomSize << 3);

    PPUBANK[0] = VROMPAGE(dwPage[1] + 0);
    PPUBANK[1] = VROMPAGE(dwPage[1] + 1);
    PPUBANK[2] = VROMPAGE(dwPage[3] + 0);
    PPUBANK[3] = VROMPAGE(dwPage[3] + 1);
    PPUBANK[4] = VROMPAGE(dwPage[5] + 0);
    PPUBANK[5] = VROMPAGE(dwPage[5] + 1);
    PPUBANK[6] = VROMPAGE(dwPage[7] + 0);
    PPUBANK[7] = VROMPAGE(dwPage[7] + 1);
    InfoNES_SetupChr();
    break;

  default:
    dwPage[0] = (DWORD)Map5_Chr_Reg[0][byMode] % (NesHeader.byVRomSize << 3);
    dwPage[1] = (DWORD)Map5_Chr_Reg[1][byMode] % (NesHeader.byVRomSize << 3);
    dwPage[2] = (DWORD)Map5_Chr_Reg[2][byMode] % (NesHeader.byVRomSize << 3);
    dwPage[3] = (DWORD)Map5_Chr_Reg[3][byMode] % (NesHeader.byVRomSize << 3);
    dwPage[4] = (DWORD)Map5_Chr_Reg[4][byMode] % (NesHeader.byVRomSize << 3);
    dwPage[5] = (DWORD)Map5_Chr_Reg[5][byMode] % (NesHeader.byVRomSize << 3);
    dwPage[6] = (DWORD)Map5_Chr_Reg[6][byMode] % (NesHeader.byVRomSize << 3);
    dwPage[7] = (DWORD)Map5_Chr_Reg[7][byMode] % (NesHeader.byVRomSize << 3);

    PPUBANK[0] = VROMPAGE(dwPage[0]);
    PPUBANK[1] = VROMPAGE(dwPage[1]);
    PPUBANK[2] = VROMPAGE(dwPage[2]);
    PPUBANK[3] = VROMPAGE(dwPage[3]);
    PPUBANK[4] = VROMPAGE(dwPage[4]);
    PPUBANK[5] = VROMPAGE(dwPage[5]);
    PPUBANK[6] = VROMPAGE(dwPage[6]);
    PPUBANK[7] = VROMPAGE(dwPage[7]);
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
