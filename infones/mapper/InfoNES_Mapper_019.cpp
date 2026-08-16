/*===================================================================*/
/*                                                                   */
/*                    Mapper 19 (Namcot 106)                         */
/*                                                                   */
/*===================================================================*/

#include <cstring>

BYTE Map19_Regs[2];

BYTE Map19_IRQ_Enable;
DWORD Map19_IRQ_Cnt;
BYTE Map19_IRQ_Terminal;
BYTE Map19_IRQ_Pending;

static BYTE *Map19_Chr_Ram_Alloc = nullptr;
static BYTE Map19_N163_Ram[0x80];
static BYTE Map19_N163_BatteryRamDirty;
static BYTE Map19_N163_Address;
static bool Map19_N163_AutoIncrement;
static BYTE Map19_Wram_Protect;
static BYTE Map19_DeferredIrqWriteGroup;
static BYTE Map19_DeferredIrqWriteData;
static BYTE Map19_DeferredIrqWritePending;

#define MAP19_N163_AUDIO_EVENT_MAX 16

struct Map19_N163_AudioEvent
{
  uint16_t cycle;
  int16_t output;
};

static int16_t Map19_N163_ChannelOutput[8];
static Map19_N163_AudioEvent Map19_N163_AudioEvents[MAP19_N163_AUDIO_EVENT_MAX];
static BYTE Map19_N163_AudioEventCount;
static BYTE Map19_N163_AudioEventOverflow;
static BYTE Map19_N163_AudioUpdateCounter;
static BYTE Map19_N163_AudioCurrentChannel;
#if defined(NESCO_MAPPER19_N163_CACHE_CHANNEL_COUNT)
static BYTE Map19_N163_CachedChannelCount;
#endif
static uint32_t Map19_N163_PendingAudioCycles;
static uint16_t Map19_N163_AudioSliceCycles;
static int16_t Map19_N163_AudioOutput;
static int16_t Map19_N163_AudioSum;
static int16_t Map19_N163_AudioSliceStartOutput;
static BYTE Map19_N163_SoundDisabled;

static BYTE __not_in_flash_func(Map19_N163_AudioChannelCountFromRam)()
{
  return (BYTE)(((Map19_N163_Ram[0x7f] >> 4) & 0x07) + 1);
}

static BYTE __not_in_flash_func(Map19_N163_AudioChannelCount)()
{
#if defined(NESCO_MAPPER19_N163_CACHE_CHANNEL_COUNT)
  return Map19_N163_CachedChannelCount;
#else
  return Map19_N163_AudioChannelCountFromRam();
#endif
}

static uint32_t __not_in_flash_func(Map19_N163_AudioFrequency)(int nChannel)
{
  const BYTE byBase = (BYTE)(0x40 + nChannel * 8);
  return ((uint32_t)(Map19_N163_Ram[byBase + 4] & 0x03) << 16) |
         ((uint32_t)Map19_N163_Ram[byBase + 2] << 8) |
         Map19_N163_Ram[byBase + 0];
}

static uint32_t __not_in_flash_func(Map19_N163_AudioPhase)(int nChannel)
{
  const BYTE byBase = (BYTE)(0x40 + nChannel * 8);
  return ((uint32_t)Map19_N163_Ram[byBase + 5] << 16) |
         ((uint32_t)Map19_N163_Ram[byBase + 3] << 8) |
         Map19_N163_Ram[byBase + 1];
}

static void __not_in_flash_func(Map19_N163_AudioSetPhase)(int nChannel, uint32_t dwPhase)
{
  const BYTE byBase = (BYTE)(0x40 + nChannel * 8);
  Map19_N163_Ram[byBase + 5] = (BYTE)(dwPhase >> 16);
  Map19_N163_Ram[byBase + 3] = (BYTE)(dwPhase >> 8);
  Map19_N163_Ram[byBase + 1] = (BYTE)dwPhase;
}

static void __not_in_flash_func(Map19_N163_AudioAppendEvent)()
{
  if (Map19_N163_AudioEventCount < MAP19_N163_AUDIO_EVENT_MAX)
  {
    Map19_N163_AudioEvents[Map19_N163_AudioEventCount].cycle =
        Map19_N163_AudioSliceCycles;
    Map19_N163_AudioEvents[Map19_N163_AudioEventCount].output =
        Map19_N163_AudioOutput;
    ++Map19_N163_AudioEventCount;
  }
  else
  {
    Map19_N163_AudioEventOverflow = 1;
    /* Keep the newest level without writing past the bounded event array. */
    Map19_N163_AudioEvents[MAP19_N163_AUDIO_EVENT_MAX - 1].cycle =
        Map19_N163_AudioSliceCycles;
    Map19_N163_AudioEvents[MAP19_N163_AUDIO_EVENT_MAX - 1].output =
        Map19_N163_AudioOutput;
  }
}

static int __not_in_flash_func(Map19_N163_Average)(int nSum, BYTE byCount)
{
#if defined(NESCO_MAPPER19_N163_AVERAGE_FAST)
  /*
   * The N163 channel count is always 1..8.  A channel output is
   * (sample - 8) * volume, hence it is bounded to [-120, 105] and nSum is
   * bounded to [-960, 840].  The reciprocal constants below therefore give
   * the exact quotient for this complete input domain.  Applying the sign
   * after the magnitude calculation preserves C/C++ signed truncation toward
   * zero without calling RP2040's variable-divisor software helper.
   */
  const int nMagnitude = nSum < 0 ? -nSum : nSum;
  int nQuotient;
  switch (byCount)
  {
  case 1:
    return nSum;
  case 2:
    nQuotient = nMagnitude >> 1;
    break;
  case 3:
    nQuotient = (nMagnitude * 0x5556) >> 16;
    break;
  case 4:
    nQuotient = nMagnitude >> 2;
    break;
  case 5:
    nQuotient = (nMagnitude * 0x3334) >> 16;
    break;
  case 6:
    nQuotient = (nMagnitude * 0x2aab) >> 16;
    break;
  case 7:
    nQuotient = (nMagnitude * 0x2493) >> 16;
    break;
  case 8:
    nQuotient = nMagnitude >> 3;
    break;
  default:
    return 0;
  }
  return nSum < 0 ? -nQuotient : nQuotient;
#else
  return nSum / byCount;
#endif
}

static void __not_in_flash_func(Map19_N163_AudioCommitOutput)(BYTE byCount, int nSum)
{
  const int16_t newOutput = (int16_t)Map19_N163_Average(nSum, byCount);
  if (newOutput != Map19_N163_AudioOutput)
  {
    Map19_N163_AudioOutput = newOutput;
    Map19_N163_AudioAppendEvent();
  }
}

static void __not_in_flash_func(Map19_N163_AudioUpdateOutput)()
{
  const BYTE byCount = Map19_N163_AudioChannelCount();
  int nSum = 0;
  const int nFirst = 8 - byCount;
  for (int i = nFirst; i < 8; ++i)
  {
    nSum += Map19_N163_ChannelOutput[i];
  }

  Map19_N163_AudioSum = (int16_t)nSum;
  Map19_N163_AudioCommitOutput(byCount, nSum);
}

static void __not_in_flash_func(Map19_N163_AudioUpdateChannel)(int nChannel)
{
  const BYTE byBase = (BYTE)(0x40 + nChannel * 8);
  const uint32_t dwFrequency = Map19_N163_AudioFrequency(nChannel);
  const uint16_t wLength = (uint16_t)(256 - (Map19_N163_Ram[byBase + 4] & 0xfc));
  const BYTE byWaveAddress = Map19_N163_Ram[byBase + 6];
  const BYTE byVolume = Map19_N163_Ram[byBase + 7] & 0x0f;
  uint32_t dwPhase = Map19_N163_AudioPhase(nChannel);

  const uint32_t phaseLimit = (uint32_t)wLength << 16;
  dwPhase += dwFrequency;
  /*
   * dwFrequency is 18-bit (0..0x3ffff), while dwPhase is always below
   * phaseLimit.  Since wLength is at least 4, dwFrequency is smaller than
   * every phaseLimit; therefore the sum is below 2 * phaseLimit and one
   * subtraction is equivalent to the modulo operation.
   */
  if (dwPhase >= phaseLimit)
  {
    dwPhase -= phaseLimit;
  }
  const BYTE bySamplePosition = (BYTE)(((dwPhase >> 16) + byWaveAddress) & 0xff);
  const BYTE byWaveByte = Map19_N163_Ram[bySamplePosition >> 1];
  const int nSample = (bySamplePosition & 1) ? (byWaveByte >> 4) : (byWaveByte & 0x0f);
  const int16_t newChannelOutput = (int16_t)((nSample - 8) * byVolume);
  Map19_N163_AudioSetPhase(nChannel, dwPhase);
#if defined(NESCO_MAPPER19_N163_SKIP_SAME_OUTPUT)
  if (newChannelOutput == Map19_N163_ChannelOutput[nChannel])
  {
    return;
  }
#endif
  const int16_t previousChannelOutput = Map19_N163_ChannelOutput[nChannel];
  Map19_N163_ChannelOutput[nChannel] = newChannelOutput;
#if defined(NESCO_MAPPER19_N163_INCREMENTAL_SUM)
  const BYTE byCount = Map19_N163_AudioChannelCount();
  if (nChannel >= (int)(8 - byCount))
  {
    Map19_N163_AudioSum = (int16_t)(Map19_N163_AudioSum +
                                    (int)newChannelOutput -
                                    (int)previousChannelOutput);
    Map19_N163_AudioCommitOutput(byCount, Map19_N163_AudioSum);
    return;
  }
#endif
  Map19_N163_AudioUpdateOutput();
}

static void Map19_N163_ResetAudioState()
{
  for (int i = 0; i < 8; ++i)
  {
    Map19_N163_ChannelOutput[i] = 0;
  }
  Map19_N163_AudioEventCount = 0;
  Map19_N163_AudioEventOverflow = 0;
  Map19_N163_AudioUpdateCounter = 0;
  Map19_N163_AudioCurrentChannel = 7;
  Map19_N163_PendingAudioCycles = 0;
  Map19_N163_AudioSliceCycles = 0;
  Map19_N163_AudioOutput = 0;
  Map19_N163_AudioSum = 0;
#if defined(NESCO_MAPPER19_N163_CACHE_CHANNEL_COUNT)
  Map19_N163_CachedChannelCount = Map19_N163_AudioChannelCountFromRam();
#endif
  Map19_N163_AudioSliceStartOutput = 0;
  Map19_N163_SoundDisabled = 0;
}

static void __not_in_flash_func(Map19_N163_ClockAudioCycles)(int clocks)
{
  if (clocks <= 0)
  {
    return;
  }

  if (Map19_N163_SoundDisabled)
  {
    const uint32_t nextSliceCycles =
        (uint32_t)Map19_N163_AudioSliceCycles + (uint32_t)clocks;
    Map19_N163_AudioSliceCycles =
        (nextSliceCycles > 0xffff) ? 0xffff : (uint16_t)nextSliceCycles;
    return;
  }

  int remaining = clocks;
  while (remaining > 0)
  {
    const int toBoundary = 15 - Map19_N163_AudioUpdateCounter;
    const int chunk = (remaining < toBoundary) ? remaining : toBoundary;
    Map19_N163_AudioUpdateCounter = (BYTE)(Map19_N163_AudioUpdateCounter + chunk);
    const uint32_t nextSliceCycles = (uint32_t)Map19_N163_AudioSliceCycles + chunk;
    Map19_N163_AudioSliceCycles = (nextSliceCycles > 0xffff) ? 0xffff : (uint16_t)nextSliceCycles;
    remaining -= chunk;

    if (Map19_N163_AudioUpdateCounter == 15)
    {
      Map19_N163_AudioUpdateChannel(Map19_N163_AudioCurrentChannel);
      Map19_N163_AudioUpdateCounter = 0;
      if (Map19_N163_AudioCurrentChannel == 0)
      {
        Map19_N163_AudioCurrentChannel = 7;
      }
      else
      {
        --Map19_N163_AudioCurrentChannel;
        if (Map19_N163_AudioCurrentChannel <
            (BYTE)(8 - Map19_N163_AudioChannelCount()))
        {
          Map19_N163_AudioCurrentChannel = 7;
        }
      }
    }
  }
}

static void __not_in_flash_func(Map19_N163_FlushAudioCycles)()
{
  if (Map19_N163_PendingAudioCycles == 0)
  {
    return;
  }

  const uint32_t pending = Map19_N163_PendingAudioCycles;
  Map19_N163_PendingAudioCycles = 0;
  Map19_N163_ClockAudioCycles((int)pending);
}

static void Map19_UpdateIrqTerminal()
{
  Map19_IRQ_Terminal = (Map19_IRQ_Cnt == 0x7fff) ? 1 : 0;
}

static void Map19_ApplyIrqRegisterWrite(BYTE byGroup, BYTE byData)
{
  if (byGroup == 0x50)
  {
    Map19_IRQ_Cnt = (Map19_IRQ_Cnt & 0xff00) | byData;
  }
  else
  {
    Map19_IRQ_Cnt = (Map19_IRQ_Cnt & 0x00ff) | ((DWORD)(byData & 0x7f) << 8);
    Map19_IRQ_Enable = (byData & 0x80) >> 7;
  }

  Map19_UpdateIrqTerminal();
  Map19_IRQ_Pending = 0;
  IRQ_State = 1;
}

static void Map19_DeferIrqRegisterWrite(BYTE byGroup, BYTE byData)
{
  if (Map19_DeferredIrqWritePending)
  {
    Map19_ApplyIrqRegisterWrite(Map19_DeferredIrqWriteGroup,
                                Map19_DeferredIrqWriteData);
    Map19_DeferredIrqWritePending = 0;
  }

  Map19_DeferredIrqWriteGroup = byGroup;
  Map19_DeferredIrqWriteData = byData;
  Map19_DeferredIrqWritePending = 1;
}

/* The address of 1Kbytes unit of the Map19 Chr RAM */
#define Map19_VROMPAGE(a) &Map19_Chr_Ram[(a)*0x400]

static BYTE *Map19_ChrSourcePage(BYTE byData)
{
  if (NesHeader.byVRomSize == 0)
  {
    return Map19_VROMPAGE(byData & 0x07);
  }

  const DWORD dwPageCount = (DWORD)NesHeader.byVRomSize << 3;
  return VROMPAGE((DWORD)byData % dwPageCount);
}

static void Map19_SetPatternBank(int nSlot, BYTE byData)
{
  const BYTE byMode = (nSlot < 4) ? Map19_Regs[0] : Map19_Regs[1];

  if (byData >= 0xe0 && byMode == 0)
  {
    PPUBANK[nSlot] = VRAMPAGE(byData & 0x01);
  }
  else
  {
    PPUBANK[nSlot] = Map19_ChrSourcePage(byData);
  }

  InfoNES_SetupChr();
}

static void Map19_SetNameTableBank(int nSlot, BYTE byData)
{
  if (byData >= 0xe0)
  {
    PPUBANK[NAME_TABLE0 + nSlot] = VRAMPAGE(byData & 0x01);
  }
  else
  {
    PPUBANK[NAME_TABLE0 + nSlot] = Map19_ChrSourcePage(byData);
  }
}

static void Map19_N163_AdvanceAddress()
{
  if (Map19_N163_AutoIncrement && Map19_N163_Address < 0x7f)
  {
    ++Map19_N163_Address;
  }
}

bool Map19_WramWriteAllowed(WORD wAddr)
{
  if ((Map19_Wram_Protect & 0xf0) != 0x40)
  {
    return false;
  }

  const BYTE byPage = (BYTE)(((wAddr - 0x6000) >> 11) & 0x03);
  return (Map19_Wram_Protect & (BYTE)(1u << byPage)) == 0;
}

void Map19_Release()
{
  delete[] Map19_Chr_Ram_Alloc;
  Map19_Chr_Ram_Alloc = nullptr;
  Map19_Chr_Ram = nullptr;
}

void Map19_N163_GetBatteryRam(const BYTE **data, unsigned *size)
{
  Map19_N163_FlushAudioCycles();
  if (data)
  {
    *data = Map19_N163_Ram;
  }
  if (size)
  {
    *size = sizeof(Map19_N163_Ram);
  }
}

void Map19_N163_RestoreBatteryRam(const BYTE *data, unsigned size)
{
  if (!data || size != sizeof(Map19_N163_Ram))
  {
    return;
  }

  Map19_N163_FlushAudioCycles();
  std::memcpy(Map19_N163_Ram, data, sizeof(Map19_N163_Ram));
  Map19_N163_BatteryRamDirty = 0;
}

bool Map19_N163_IsBatteryRamDirty()
{
  return Map19_N163_BatteryRamDirty != 0;
}

void Map19_N163_ClearBatteryRamDirty()
{
  Map19_N163_BatteryRamDirty = 0;
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 19                                             */
/*-------------------------------------------------------------------*/
void Map19_Init()
{
  /* Initialize Mapper */
  MapperInit = Map19_Init;

  /* Write to Mapper */
  MapperWrite = Map19_Write;

  /* Write to SRAM */
  MapperSram = Map0_Sram;

  /* Write to APU */
  MapperApu = Map19_Apu;

  /* Read from APU */
  MapperReadApu = Map19_ReadApu;

  /* Callback at VSync */
  MapperVSync = Map0_VSync;

  /* Callback at HSync */
  MapperHSync = Map19_HSync;

  /* Callback at PPU */
  MapperPPU = Map0_PPU;

  /* Callback at Rendering Screen ( 1:BG, 0:Sprite ) */
  MapperRenderScreen = Map0_RenderScreen;

  Map19_Release();
  Map19_Chr_Ram_Alloc = new (std::nothrow) BYTE[0x2000];
  if (!Map19_Chr_Ram_Alloc)
  {
    InfoNES_Error("Mapper 19 startup alloc failed [1/1 chr-ram size=%u]", 0x2000);
    Map19_Release();
    return;
  }
  for (DWORD i = 0; i < 0x2000; ++i)
  {
    Map19_Chr_Ram_Alloc[i] = 0;
  }
  Map19_Chr_Ram = Map19_Chr_Ram_Alloc;
  for (DWORD i = 0; i < 0x80; ++i)
  {
    Map19_N163_Ram[i] = 0;
  }
  Map19_N163_BatteryRamDirty = 0;
  Map19_N163_ResetAudioState();
  Map19_N163_Address = 0;
  Map19_N163_AutoIncrement = false;
  Map19_Wram_Protect = 0;
  Map19_DeferredIrqWriteGroup = 0;
  Map19_DeferredIrqWriteData = 0;
  Map19_DeferredIrqWritePending = 0;

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* Set ROM Banks */
  ROMBANK0 = ROMPAGE(0);
  ROMBANK1 = ROMPAGE(1);
  ROMBANK2 = ROMLASTPAGE(1);
  ROMBANK3 = ROMLASTPAGE(0);

  /* Set PPU Banks */
  if (NesHeader.byVRomSize > 0)
  {
    DWORD dwLastPage = (DWORD)NesHeader.byVRomSize << 3;
    PPUBANK[0] = VROMPAGE(dwLastPage - 8);
    PPUBANK[1] = VROMPAGE(dwLastPage - 7);
    PPUBANK[2] = VROMPAGE(dwLastPage - 6);
    PPUBANK[3] = VROMPAGE(dwLastPage - 5);
    PPUBANK[4] = VROMPAGE(dwLastPage - 4);
    PPUBANK[5] = VROMPAGE(dwLastPage - 3);
    PPUBANK[6] = VROMPAGE(dwLastPage - 2);
    PPUBANK[7] = VROMPAGE(dwLastPage - 1);
  }
  else
  {
    for (int nPage = 0; nPage < 8; ++nPage)
    {
      PPUBANK[nPage] = Map19_VROMPAGE(nPage);
    }
  }
  InfoNES_SetupChr();

  /* Initialize State Register */
  Map19_Regs[0] = 0x00;
  Map19_Regs[1] = 0x00;

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring(1, 1);
  Map19_IRQ_Cnt = 0;
  Map19_IRQ_Enable = 0;
  Map19_IRQ_Terminal = 0;
  Map19_IRQ_Pending = 0;
  IRQ_State = 1;
}

/*-------------------------------------------------------------------*/
/*  Mapper 19 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map19_Write(WORD wAddr, BYTE byData)
{
  /* Set PPU Banks */
  switch (wAddr & 0xf800)
  {
  case 0x8000: /* $8000-87ff */
  case 0x8800: /* $8800-8fff */
  case 0x9000: /* $9000-97ff */
  case 0x9800: /* $9800-9fff */
  case 0xa000: /* $a000-a7ff */
  case 0xa800: /* $a800-afff */
  case 0xb000: /* $b000-b7ff */
  case 0xb800: /* $b800-bfff */
    Map19_SetPatternBank((wAddr - 0x8000) >> 11, byData);
    break;

  case 0xc000: /* $c000-c7ff */
  case 0xc800: /* $c800-cfff */
  case 0xd000: /* $d000-d7ff */
  case 0xd800: /* $d800-dfff */
    Map19_SetNameTableBank((wAddr - 0xc000) >> 11, byData);
    break;

  case 0xe000: /* $e000-e7ff */
    {
    Map19_N163_FlushAudioCycles();
    const BYTE byPreviousDisable = Map19_N163_SoundDisabled;
    Map19_N163_SoundDisabled = (byData & 0x40) ? 1 : 0;
    if (!byPreviousDisable && Map19_N163_SoundDisabled && Map19_N163_AudioOutput != 0)
    {
      Map19_N163_AudioOutput = 0;
      Map19_N163_AudioAppendEvent();
    }
    byData &= 0x3f;
    byData %= (NesHeader.byRomSize << 1);
    ROMBANK0 = ROMPAGE(byData);
    break;
    }

  case 0xe800: /* $e800-efff */
    Map19_Regs[0] = (byData & 0x40) >> 6;
    Map19_Regs[1] = (byData & 0x80) >> 7;

    byData &= 0x3f;
    byData %= (NesHeader.byRomSize << 1);
    ROMBANK1 = ROMPAGE(byData);
    break;

  case 0xf000: /* $f000-f7ff */
    byData &= 0x3f;
    byData %= (NesHeader.byRomSize << 1);
    ROMBANK2 = ROMPAGE(byData);
    break;

  case 0xf800: /* $f800-ffff */
    Map19_Wram_Protect = byData;
    Map19_N163_Address = byData & 0x7f;
    Map19_N163_AutoIncrement = (byData & 0x80) != 0;
    break;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 19 Write to APU Function                                  */
/*-------------------------------------------------------------------*/
void Map19_Apu(WORD wAddr, BYTE byData)
{
  switch (wAddr & 0xf800)
  {
  case 0x4800:
    {
    /* The audio clock is intentionally accumulated between CPU bus events.
     * Commit it before changing the RAM that the oscillator reads; otherwise
     * already-elapsed cycles would be evaluated with the newly written
     * waveform/register bytes. */
    Map19_N163_FlushAudioCycles();
    const BYTE byAddress = Map19_N163_Address;
    Map19_N163_Ram[Map19_N163_Address] = byData;
    Map19_N163_BatteryRamDirty = 1;
    Map19_N163_AdvanceAddress();
    if (byAddress == 0x7f)
    {
#if defined(NESCO_MAPPER19_N163_CACHE_CHANNEL_COUNT)
      Map19_N163_CachedChannelCount = Map19_N163_AudioChannelCountFromRam();
#endif
      /* Channel-count writes must immediately recompute the active mix. */
      Map19_N163_AudioUpdateOutput();
    }
    break;
    }

  case 0x5000: /* $5000-57ff */
    Map19_DeferIrqRegisterWrite(0x50, byData);
    break;

  case 0x5800: /* $5800-5fff */
    Map19_DeferIrqRegisterWrite(0x58, byData);
    break;
  }
}

void Map19_CommitCpuBoundary()
{
  if (!Map19_DeferredIrqWritePending)
  {
    return;
  }

  const BYTE byGroup = Map19_DeferredIrqWriteGroup;
  const BYTE byData = Map19_DeferredIrqWriteData;
  Map19_DeferredIrqWritePending = 0;
  Map19_ApplyIrqRegisterWrite(byGroup, byData);
}

/*-------------------------------------------------------------------*/
/*  Mapper 19 Read from APU Function                                 */
/*-------------------------------------------------------------------*/
BYTE Map19_ReadApu(WORD wAddr)
{
  switch (wAddr & 0xf800)
  {
  case 0x4800:
    {
      Map19_N163_FlushAudioCycles();
      const BYTE byData = Map19_N163_Ram[Map19_N163_Address];
      Map19_N163_AdvanceAddress();
      return byData;
    }

  case 0x5000: /* $5000-57ff */
    return (BYTE)(Map19_IRQ_Cnt & 0x00ff);

  case 0x5800: /* $5800-5fff */
    return (BYTE)(((Map19_IRQ_Cnt & 0x7f00) >> 8) | (Map19_IRQ_Enable << 7));

  default:
    return (BYTE)(wAddr >> 8);
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 19 H-Sync Function                                        */
/*-------------------------------------------------------------------*/
void Map19_HSync()
{
}

void __not_in_flash_func(Map19_ClockCpuCycles)(int clocks)
{
  if (clocks <= 0)
  {
    return;
  }

  if (Map19_IRQ_Enable && !Map19_IRQ_Terminal)
  {
    const DWORD next = Map19_IRQ_Cnt + (DWORD)clocks;
    if (next >= 0x7fff)
    {
      Map19_IRQ_Cnt = 0x7fff;
      Map19_IRQ_Terminal = 1;
      Map19_IRQ_Pending = 1;
    }
    else
    {
      Map19_IRQ_Cnt = next;
    }
  }

  Map19_N163_PendingAudioCycles += (uint32_t)clocks;
}

void __attribute__((optimize("Os"), noinline)) __not_in_flash_func(Map19_RenderAudioSlice)(
    int16_t *dst, int n, bool enabled)
{
  Map19_N163_FlushAudioCycles();

  if (n < 0)
  {
    n = 0;
  }
  for (int i = 0; i < n; ++i)
  {
    if (dst)
    {
      dst[i] = 0;
    }
  }

  if (enabled && dst && n > 0)
  {
    const uint16_t sliceCycles = Map19_N163_AudioSliceCycles;

    if (sliceCycles == 0)
    {
      for (int i = 0; i < n; ++i)
      {
        dst[i] = 0;
      }
      goto finish_slice;
    }

    int16_t currentOutput = Map19_N163_AudioSliceStartOutput;
    int nEvent = 0;
    uint32_t sampleStart = 0;
    while (nEvent < Map19_N163_AudioEventCount &&
           Map19_N163_AudioEvents[nEvent].cycle == 0)
    {
      currentOutput = Map19_N163_AudioEvents[nEvent].output;
      ++nEvent;
    }

    for (int i = 0; i < n; ++i)
    {
      const uint32_t sampleEnd =
          ((uint32_t)(i + 1) * (uint32_t)sliceCycles) / (uint32_t)n;
      const int32_t sampleWidth = (int32_t)(sampleEnd - sampleStart);

      if (sampleWidth <= 0)
      {
        dst[i] = currentOutput;
        continue;
      }

      int32_t weightedSum = 0;
      uint32_t cursor = sampleStart;
      while (nEvent < Map19_N163_AudioEventCount &&
             (uint32_t)Map19_N163_AudioEvents[nEvent].cycle <= sampleStart)
      {
        currentOutput = Map19_N163_AudioEvents[nEvent].output;
        ++nEvent;
      }

      while (nEvent < Map19_N163_AudioEventCount &&
             (uint32_t)Map19_N163_AudioEvents[nEvent].cycle < sampleEnd)
      {
        const uint32_t eventCycle = (uint32_t)Map19_N163_AudioEvents[nEvent].cycle;
        weightedSum += (int32_t)(eventCycle - cursor) * (int32_t)currentOutput;
        cursor = eventCycle;
        currentOutput = Map19_N163_AudioEvents[nEvent].output;
        ++nEvent;
      }

      weightedSum += (int32_t)(sampleEnd - cursor) * (int32_t)currentOutput;

      if (weightedSum >= 0)
      {
        weightedSum += sampleWidth / 2;
        dst[i] = (int16_t)(weightedSum / sampleWidth);
      }
      else
      {
        weightedSum -= sampleWidth / 2;
        dst[i] = (int16_t)(weightedSum / sampleWidth);
      }
      sampleStart = sampleEnd;
    }
  }

  /* A slice is a bounded handoff. Always drain it, including mute/n=0. */
finish_slice:
  Map19_N163_AudioSliceStartOutput = Map19_N163_AudioOutput;
  Map19_N163_AudioEventCount = 0;
  Map19_N163_AudioEventOverflow = 0;
  Map19_N163_AudioSliceCycles = 0;
}
