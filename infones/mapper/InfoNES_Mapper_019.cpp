/*===================================================================*/
/*                                                                   */
/*                    Mapper 19 (Namcot 106)                         */
/*                                                                   */
/*===================================================================*/

#include "../InfoNES.h"
#include "../InfoNES_Mapper.h"
#include "../K6502.h"
#include "runtime_log.h"

#include <cstring>

namespace
{

struct Map19_BoardProfile
{
  const char *name;
  uint32_t payloadCrc32;
  bool externalWram;
  bool externalWramBattery;
  bool n163Battery;
  bool n163Audio;
};

/*
 * Mapper 19 is not one electrically identical board in the database.  Keep
 * the two known iNES inputs explicit, while retaining the old 8 KiB WRAM
 * default for unidentified legacy images.  This prevents a target game's
 * battery/gain assumptions from silently becoming a Mapper 19-wide rule.
 */
static Map19_BoardProfile Map19_Profile = {
    "iNES-default", 0, true, false, false, true};

static uint32_t Map19_Crc32(const BYTE *data, size_t size, uint32_t crc)
{
  for (size_t i = 0; i < size; ++i)
  {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
    {
      crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int)(crc & 1u));
    }
  }
  return crc;
}

static uint32_t Map19_CalculatePayloadCrc32()
{
  const size_t prgBytes = (size_t)NesHeader.byRomSize * 0x4000u;
  const size_t chrBytes = (size_t)NesHeader.byVRomSize * 0x2000u;
  uint32_t crc = 0xffffffffu;

  if (ROM && prgBytes != 0)
  {
    crc = Map19_Crc32(ROM, prgBytes, crc);
  }
  if (VROM && chrBytes != 0)
  {
    crc = Map19_Crc32(VROM, chrBytes, crc);
  }
  return crc ^ 0xffffffffu;
}

} // namespace

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
BYTE Map19_DeferredIrqWritePending;

#if defined(NESCO_AUDIO_BLOCK_PRODUCER)
/*
 * The block producer keeps the same per-HSync timing, but postpones the
 * sample conversion until a complete 64-sample mixer block is ready.  A
 * block spans roughly 46 HSyncs; retain the bounded output transitions and
 * the HSync segment boundaries needed to reproduce the old per-slice
 * rounding exactly.
 */
#define MAP19_N163_AUDIO_EVENT_MAX 128
#define MAP19_N163_AUDIO_SEGMENT_MAX 64
#else
#define MAP19_N163_AUDIO_EVENT_MAX 16
#endif

struct Map19_N163_AudioEvent
{
  uint16_t cycle;
  int16_t output;
};

static int16_t Map19_N163_ChannelOutput[8];
static Map19_N163_AudioEvent Map19_N163_AudioEvents[MAP19_N163_AUDIO_EVENT_MAX];
static uint16_t Map19_N163_AudioEventCount;
static BYTE Map19_N163_AudioEventOverflow;
static BYTE Map19_N163_AudioUpdateCounter;
static BYTE Map19_N163_AudioCurrentChannel;
#if defined(NESCO_MAPPER19_N163_CACHE_CHANNEL_COUNT)
static BYTE Map19_N163_CachedChannelCount;
#endif
uint32_t Map19_N163_PendingAudioCycles;
static uint16_t Map19_N163_AudioSliceCycles;
static int16_t Map19_N163_AudioOutput;
static int16_t Map19_N163_AudioSum;
static int16_t Map19_N163_AudioSliceStartOutput;
static BYTE Map19_N163_SoundDisabled;

#if defined(NESCO_AUDIO_BLOCK_PRODUCER)
struct Map19_N163_AudioSegment
{
  uint16_t cycleStart;
  uint16_t cycleEnd;
  uint16_t eventBegin;
  uint16_t eventEnd;
  int16_t startOutput;
  int16_t endOutput;
  uint16_t sampleCount;
  uint8_t enabled;
};

static Map19_N163_AudioSegment
    Map19_N163_AudioSegments[MAP19_N163_AUDIO_SEGMENT_MAX];
static Map19_N163_AudioEvent
    Map19_N163_AudioBlockEvents[MAP19_N163_AUDIO_EVENT_MAX];
static uint16_t Map19_N163_AudioSegmentCount;
static uint16_t Map19_N163_AudioBlockEventCount;
#endif

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

#if defined(NESCO_MAPPER19_N163_RENDER_FAST_DIVISION)
/*
 * Map19_RenderAudioSlice normally receives one or two output samples per
 * HSync.  In that path the weighted event average is divided by a width in
 * the 48..128 cycle range.  The table stores ceil(2^16 / width); the product
 * is exact for the bounded N163 level range after one correction step.  Keep
 * it in time-critical RAM so the render path does not trade a flash divide
 * for a flash table read.
 */
#define MAP19_N163_RENDER_RECIPROCAL_MIN 48u
#define MAP19_N163_RENDER_RECIPROCAL_MAX 128u
static const uint16_t __not_in_flash("map19_n163_render_reciprocal")
    Map19_N163_RenderReciprocalQ16[] = {
    0x0556, 0x053a, 0x051f, 0x0506, 0x04ed, 0x04d5, 0x04be, 0x04a8, 0x0493, 0x047e, 0x046a, 0x0457,
    0x0445, 0x0433, 0x0422, 0x0411, 0x0400, 0x03f1, 0x03e1, 0x03d3, 0x03c4, 0x03b6, 0x03a9, 0x039c,
    0x038f, 0x0382, 0x0376, 0x036a, 0x035f, 0x0354, 0x0349, 0x033e, 0x0334, 0x032a, 0x0320, 0x0316,
    0x030d, 0x0304, 0x02fb, 0x02f2, 0x02e9, 0x02e1, 0x02d9, 0x02d1, 0x02c9, 0x02c1, 0x02ba, 0x02b2,
    0x02ab, 0x02a4, 0x029d, 0x0296, 0x0290, 0x0289, 0x0283, 0x027d, 0x0277, 0x0271, 0x026b, 0x0265,
    0x025f, 0x025a, 0x0254, 0x024f, 0x024a, 0x0244, 0x023f, 0x023a, 0x0235, 0x0231, 0x022c, 0x0227,
    0x0223, 0x021e, 0x021a, 0x0215, 0x0211, 0x020d, 0x0209, 0x0205, 0x0200,
};

static int __not_in_flash("map19_n163_render_round_divide")
Map19_N163_RenderRoundDivide(int nValue, uint32_t denominator)
{
  if (denominator >= MAP19_N163_RENDER_RECIPROCAL_MIN &&
      denominator <= MAP19_N163_RENDER_RECIPROCAL_MAX)
  {
    const uint32_t magnitude = (uint32_t)(nValue < 0 ? -nValue : nValue);
    const uint32_t numerator = magnitude + denominator / 2u;
    const uint32_t reciprocal =
        Map19_N163_RenderReciprocalQ16[denominator -
                                       MAP19_N163_RENDER_RECIPROCAL_MIN];
    uint32_t quotient = (numerator * reciprocal) >> 16;

    /* ceil reciprocal can overshoot by one; it cannot undershoot here. */
    if (quotient * denominator > numerator)
    {
      --quotient;
    }

    const int result = (int)quotient;
    return nValue < 0 ? -result : result;
  }

  return denominator == 0u ? 0 : nValue / (int)denominator;
}
#endif

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
#if defined(NESCO_AUDIO_BLOCK_PRODUCER)
  Map19_N163_AudioSegmentCount = 0;
  Map19_N163_AudioBlockEventCount = 0;
#endif
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
  if (!Map19_WramReadAllowed(wAddr))
  {
    return false;
  }

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

void Map19_SelectBoardProfile(BYTE nes2_header, BYTE submapper)
{
  const uint32_t payloadCrc32 = Map19_CalculatePayloadCrc32();

  Map19_Profile = {
      nes2_header ? "NES2-default" : "iNES-default",
      payloadCrc32,
      true,
      ROM_SRAM != 0,
      ROM_SRAM != 0,
      true};

  if (nes2_header)
  {
    /* NES 2.0 byte 10 carries PRG-RAM (low nibble) and PRG-NVRAM
     * (high nibble) size exponents.  A zero byte means that Mapper 19 has
     * no external $6000 RAM in this image; the 128-byte N163 battery RAM is
     * still selected independently by the battery flag. */
    const BYTE prgRamConfig = NesHeader.byReserve[2];
    Map19_Profile.externalWram = prgRamConfig != 0;
    Map19_Profile.externalWramBattery =
        ROM_SRAM != 0 && (prgRamConfig & 0xf0u) != 0;
  }

  /* Mesen's database identifies these iNES payloads as the NAMCOT-163
   * boards 60-21 and 60-12 respectively.  The CRC is over PRG+CHR, matching
   * the database key, and is intentionally narrower than a filename match. */
  if (payloadCrc32 == 0x684b292fu)
  {
    Map19_Profile.name = "NAMCOT-163/60-21";
    Map19_Profile.externalWram = false;
    Map19_Profile.externalWramBattery = false;
    Map19_Profile.n163Battery = false;
  }
  else if (payloadCrc32 == 0x10c8f2fau)
  {
    Map19_Profile.name = "NAMCOT-163/60-12";
    Map19_Profile.externalWram = false;
    Map19_Profile.externalWramBattery = false;
    Map19_Profile.n163Battery = true;
    Map19_Profile.n163Audio = false;
  }

  NESCO_LOG_RUNTIME(
      "[M19_BOARD] crc=%08lX nes2=%u submapper=%u profile=%s audio=%u "
      "external_wram=%u external_battery=%u n163_battery=%u\r\n",
      (unsigned long)Map19_Profile.payloadCrc32,
      (unsigned)nes2_header,
      (unsigned)submapper,
      Map19_Profile.name,
      Map19_Profile.n163Audio ? 1u : 0u,
      Map19_Profile.externalWram ? 1u : 0u,
      Map19_Profile.externalWramBattery ? 1u : 0u,
      Map19_Profile.n163Battery ? 1u : 0u);
}

bool Map19_WramReadAllowed(WORD wAddr)
{
  return wAddr >= 0x6000 && wAddr <= 0x7fff && Map19_Profile.externalWram;
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

bool Map19_ExternalWramBatteryEnabled()
{
  return Map19_Profile.externalWramBattery;
}

bool Map19_N163BatteryEnabled()
{
  return Map19_Profile.n163Battery;
}

bool Map19_N163AudioEnabled()
{
  return Map19_Profile.n163Audio;
}

const char *Map19_BoardProfileName()
{
  return Map19_Profile.name;
}

uint32_t Map19_BoardPayloadCrc32()
{
  return Map19_Profile.payloadCrc32;
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

/*
 * The audio block producer calls this on the deadline-critical path.  Keep
 * the function out of flash, but optimize the bounded renderer for speed;
 * the previous size-optimized body left the event walk and signed division
 * bookkeeping on the hot path.  `noinline` keeps the RAM placement and the
 * call boundary stable while allowing the renderer's loop to be optimized as
 * one unit.
 */
void __attribute__((optimize("O2"), noinline)) __not_in_flash_func(Map19_RenderAudioSlice)(
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
#if defined(NESCO_MAPPER19_N163_RENDER_FAST_DIVISION)
      uint32_t sampleEnd;
      if (n == 1)
      {
        sampleEnd = sliceCycles;
      }
      else if (n == 2)
      {
        sampleEnd = (i == 0) ? ((uint32_t)sliceCycles >> 1) : sliceCycles;
      }
      else
      {
        sampleEnd = ((uint32_t)(i + 1) * (uint32_t)sliceCycles) /
                    (uint32_t)n;
      }
#else
      const uint32_t sampleEnd =
          ((uint32_t)(i + 1) * (uint32_t)sliceCycles) / (uint32_t)n;
#endif
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

#if defined(NESCO_MAPPER19_N163_RENDER_FAST_DIVISION)
      dst[i] = (int16_t)Map19_N163_RenderRoundDivide(weightedSum,
                                                      (uint32_t)sampleWidth);
#else
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
#endif
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

#if defined(NESCO_AUDIO_BLOCK_PRODUCER)
/*
 * Record one HSync's already-clocked interval without doing sample-rate
 * conversion on the HSync deadline.  Audio cycles are deliberately kept
 * cumulative until the complete mixer block is rendered; this makes the
 * event positions absolute within the block while preserving every mapper
 * write boundary.
 */
void __not_in_flash_func(Map19_QueueAudioSlice)(int n, bool enabled)
{
  Map19_N163_FlushAudioCycles();

  if (n < 0)
  {
    n = 0;
  }

  if (Map19_N163_AudioSegmentCount >= MAP19_N163_AUDIO_SEGMENT_MAX)
  {
    /* A block normally spans fewer than 64 HSyncs.  Keep the failure
     * observable and do not write past the bounded ledger. */
    Map19_N163_AudioEventOverflow = 1;
    return;
  }

  Map19_N163_AudioSegment &segment =
      Map19_N163_AudioSegments[Map19_N163_AudioSegmentCount];
  const uint16_t cycleEnd = Map19_N163_AudioSliceCycles;
  const uint16_t eventBegin = Map19_N163_AudioBlockEventCount;

  /* Keep events relative to this HSync.  This is intentionally a copy: the
   * block may not render for another 64 samples, but the mapper clock state
   * and raw event list must be reset at the same per-HSync boundary as the
   * old renderer. */
  for (uint16_t i = 0; i < Map19_N163_AudioEventCount; ++i)
  {
    if (Map19_N163_AudioBlockEventCount >= MAP19_N163_AUDIO_EVENT_MAX)
    {
      Map19_N163_AudioEventOverflow = 1;
      break;
    }
    Map19_N163_AudioBlockEvents[Map19_N163_AudioBlockEventCount++] =
        Map19_N163_AudioEvents[i];
  }
  const uint16_t eventEnd = Map19_N163_AudioBlockEventCount;

  segment.cycleStart = 0;
  segment.cycleEnd = cycleEnd;
  segment.eventBegin = eventBegin;
  segment.eventEnd = eventEnd;
  segment.startOutput =
      Map19_N163_AudioSegmentCount == 0
          ? Map19_N163_AudioSliceStartOutput
          : Map19_N163_AudioSegments[Map19_N163_AudioSegmentCount - 1].endOutput;
  segment.endOutput = Map19_N163_AudioOutput;
  segment.sampleCount = (uint16_t)n;
  segment.enabled = enabled ? 1 : 0;
  ++Map19_N163_AudioSegmentCount;

  Map19_N163_AudioEventCount = 0;
  Map19_N163_AudioSliceCycles = 0;
}

static void __attribute__((optimize("O2"), noinline))
    __not_in_flash_func(Map19_RenderAudioSegment)(
        int16_t *dst, int n, bool enabled, uint16_t cycleStart,
        uint16_t cycleEnd, uint16_t eventBegin, uint16_t eventEnd,
        int16_t startOutput)
{
  if (!dst || n <= 0)
  {
    return;
  }

  for (int i = 0; i < n; ++i)
  {
    dst[i] = 0;
  }

  if (!enabled || cycleEnd <= cycleStart)
  {
    return;
  }

  const uint32_t segmentCycles = (uint32_t)cycleEnd - cycleStart;
  int16_t currentOutput = startOutput;
  uint16_t nEvent = eventBegin;
  uint32_t sampleStart = cycleStart;

  for (int i = 0; i < n; ++i)
  {
    uint32_t sampleEnd;
#if defined(NESCO_MAPPER19_N163_RENDER_FAST_DIVISION)
    if (n == 1)
    {
      sampleEnd = cycleEnd;
    }
    else if (n == 2)
    {
      sampleEnd = (i == 0) ? (cycleStart + (segmentCycles >> 1)) : cycleEnd;
    }
    else
    {
      sampleEnd = cycleStart +
                  ((uint32_t)(i + 1) * segmentCycles) / (uint32_t)n;
    }
#else
    sampleEnd = cycleStart +
                ((uint32_t)(i + 1) * segmentCycles) / (uint32_t)n;
#endif

    const int32_t sampleWidth = (int32_t)(sampleEnd - sampleStart);
    if (sampleWidth <= 0)
    {
      dst[i] = currentOutput;
      continue;
    }

    int32_t weightedSum = 0;
    uint32_t cursor = sampleStart;
    while (nEvent < eventEnd &&
           (uint32_t)Map19_N163_AudioBlockEvents[nEvent].cycle <= sampleStart)
    {
      currentOutput = Map19_N163_AudioBlockEvents[nEvent].output;
      ++nEvent;
    }

    while (nEvent < eventEnd &&
           (uint32_t)Map19_N163_AudioBlockEvents[nEvent].cycle < sampleEnd)
    {
      const uint32_t eventCycle =
          (uint32_t)Map19_N163_AudioBlockEvents[nEvent].cycle;
      weightedSum +=
          (int32_t)(eventCycle - cursor) * (int32_t)currentOutput;
      cursor = eventCycle;
      currentOutput = Map19_N163_AudioBlockEvents[nEvent].output;
      ++nEvent;
    }

    weightedSum +=
        (int32_t)(sampleEnd - cursor) * (int32_t)currentOutput;

#if defined(NESCO_MAPPER19_N163_RENDER_FAST_DIVISION)
    dst[i] = (int16_t)Map19_N163_RenderRoundDivide(
        weightedSum, (uint32_t)sampleWidth);
#else
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
#endif
    sampleStart = sampleEnd;
  }
}

/* Diagnostic-only shadow handoff.  It observes the just-recorded HSync
 * without changing the block ledger; the normal mixer still renders the
 * complete block at its deadline.  This keeps the generator fixture's exact
 * sample count while exercising the same segment renderer used by blocks. */
void __not_in_flash_func(Map19_RenderQueuedAudioSlice)(int16_t *dst, int n)
{
  if (Map19_N163_AudioSegmentCount == 0)
  {
    if (dst && n > 0)
    {
      for (int i = 0; i < n; ++i)
      {
        dst[i] = 0;
      }
    }
    return;
  }

  const Map19_N163_AudioSegment &segment =
      Map19_N163_AudioSegments[Map19_N163_AudioSegmentCount - 1];
  Map19_RenderAudioSegment(dst, n, segment.enabled != 0,
                           segment.cycleStart, segment.cycleEnd,
                           segment.eventBegin, segment.eventEnd,
                           segment.startOutput);
}

void __not_in_flash_func(Map19_RenderAudioBlock)(int16_t *dst, int n)
{
  if (dst && n > 0)
  {
    for (int i = 0; i < n; ++i)
    {
      dst[i] = 0;
    }
  }

  int dstOffset = 0;
  for (uint16_t i = 0; i < Map19_N163_AudioSegmentCount; ++i)
  {
    const Map19_N163_AudioSegment &segment = Map19_N163_AudioSegments[i];
    const int segmentSamples = (int)segment.sampleCount;
    if (dst && dstOffset < n && segmentSamples > 0)
    {
      const int renderSamples =
          (segmentSamples < n - dstOffset) ? segmentSamples : n - dstOffset;
      Map19_RenderAudioSegment(
          dst + dstOffset, renderSamples, segment.enabled != 0,
          segment.cycleStart, segment.cycleEnd, segment.eventBegin,
          segment.eventEnd, segment.startOutput);
    }
    dstOffset += segmentSamples;
  }

  /* Match the old slice handoff: any endpoint event becomes the starting
   * state of the next block, and all block-local ledger storage is drained. */
  Map19_N163_AudioSliceStartOutput = Map19_N163_AudioOutput;
  Map19_N163_AudioEventCount = 0;
  Map19_N163_AudioEventOverflow = 0;
  Map19_N163_AudioSliceCycles = 0;
  Map19_N163_AudioSegmentCount = 0;
  Map19_N163_AudioBlockEventCount = 0;
}
#endif
