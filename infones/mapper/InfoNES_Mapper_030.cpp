/*===================================================================*/
/*                                                                   */
/*                     Mapper 30 (UNROM 512)                         */
/*                                                                   */
/*===================================================================*/

#include "../InfoNES.h"
#include "../InfoNES_Mapper.h"
#include "../InfoNES_StructuredLog.h"
#include "../InfoNES_System.h"
#include "../K6502.h"
#include "InfoNES_Mapper_030_Diagnostics.h"
#include <cstdlib>
#include <new>

namespace {

enum : BYTE {
  MAP30_MIRROR_FIXED_H = 0,
  MAP30_MIRROR_FIXED_V = 1,
  MAP30_MIRROR_ONESCREEN = 2,
  MAP30_MIRROR_FOURSCREEN = 3,
};

enum : BYTE {
  MAP30_FLASH_IDLE = 0,
  MAP30_FLASH_C1,
  MAP30_FLASH_AA,
  MAP30_FLASH_C0,
  MAP30_FLASH_55,
  MAP30_FLASH_C1_2,
  MAP30_FLASH_WRITE_BANK,
  MAP30_FLASH_WRITE_DATA,
  MAP30_FLASH_ERASE_C1,
  MAP30_FLASH_ERASE_AA,
  MAP30_FLASH_ERASE_C0,
  MAP30_FLASH_ERASE_55,
  MAP30_FLASH_ERASE_BANK,
  MAP30_FLASH_ERASE_DATA,
};

BYTE* Map30_ChrRam = nullptr;
BYTE* Map30_FlashIdBank = nullptr;
BYTE* Map30_ChrRamAlloc = nullptr;
BYTE* Map30_FlashIdAlloc = nullptr;
BYTE* Map30_OverlayAlloc = nullptr;
DWORD Map30_ChrRamSize = 0;
BYTE Map30_NametableMode = MAP30_MIRROR_FIXED_H;
BYTE Map30_Flashable = 0;
BYTE Map30_Submapper = 0;
BYTE Map30_Latch = 0;
BYTE Map30_FlashState = MAP30_FLASH_IDLE;
BYTE Map30_FlashIdMode = 0;
BYTE Map30_FlashTargetBank = 0;
BYTE Map30_FlashDirty = 0;
BYTE Map30_PrgOverlayUsed[2] = { 0, 0 };
BYTE Map30_PrgOverlayBank[2] = { 0, 0 };
BYTE* Map30_PrgOverlayData[2] = { nullptr, nullptr };

#if INFONES_MAPPER30_ENABLE_DIAGNOSTICS
constexpr int MAP30_DEBUG_MAX = 16;
WORD Map30_DebugAddr[MAP30_DEBUG_MAX] = {};
BYTE Map30_DebugData[MAP30_DEBUG_MAX] = {};
BYTE Map30_DebugCount = 0;

void Map30_DebugReset()
{
  Map30_DebugCount = 0;
  for (int i = 0; i < MAP30_DEBUG_MAX; ++i)
  {
    Map30_DebugAddr[i] = 0;
    Map30_DebugData[i] = 0;
  }
}

void Map30_DebugRecord(WORD wAddr, BYTE byData)
{
  if (Map30_DebugCount < MAP30_DEBUG_MAX)
  {
    Map30_DebugAddr[Map30_DebugCount] = wAddr;
    Map30_DebugData[Map30_DebugCount] = byData;
    ++Map30_DebugCount;
  }
}

extern "C" BYTE Map30_DebugGetCount()
{
  return Map30_DebugCount;
}

extern "C" WORD Map30_DebugGetAddr(int index)
{
  return (index >= 0 && index < MAP30_DEBUG_MAX) ? Map30_DebugAddr[index] : 0;
}

extern "C" BYTE Map30_DebugGetData(int index)
{
  return (index >= 0 && index < MAP30_DEBUG_MAX) ? Map30_DebugData[index] : 0;
}
#else
void Map30_DebugReset()
{
}

void Map30_DebugRecord(WORD wAddr, BYTE byData)
{
  (void)wAddr;
  (void)byData;
}

extern "C" BYTE Map30_DebugGetCount()
{
  return 0;
}

extern "C" WORD Map30_DebugGetAddr(int index)
{
  (void)index;
  return 0;
}

extern "C" BYTE Map30_DebugGetData(int index)
{
  (void)index;
  return 0;
}
#endif

bool Map30_HasBusConflict()
{
  // submapper 0 without battery: bus conflict present (original UxROM behavior)
  // submapper 2: bus conflict present
  // submapper 1/3/4 and submapper 0 with battery: no bus conflict
  if (Map30_Submapper == 2)
    return true;
  if (Map30_Submapper == 0 && !Map30_Flashable)
    return true;
  return false;
}

BYTE Map30_ApplyBusConflict(WORD wAddr, BYTE byData)
{
  BYTE romVal;
  const WORD offset = wAddr & 0x1FFF;
  if      (wAddr < 0xA000) romVal = ROMBANK0[offset];
  else if (wAddr < 0xC000) romVal = ROMBANK1[offset];
  else if (wAddr < 0xE000) romVal = ROMBANK2[offset];
  else                     romVal = ROMBANK3[offset];
  return byData & romVal;
}

BYTE* Map30_FourScreenBase()
{
  if (!Map30_ChrRam || Map30_ChrRamSize < 0x8000)
    return nullptr;
  return Map30_ChrRam + (Map30_ChrRamSize - 0x2000);
}

void Map30_ApplyFourScreenNametables()
{
  BYTE* base = Map30_FourScreenBase();
  if (!base)
  {
    InfoNES_Mirroring(4);
    return;
  }

  PPUBANK[NAME_TABLE0] = base + 0x0000;
  PPUBANK[NAME_TABLE1] = base + 0x0400;
  PPUBANK[NAME_TABLE2] = base + 0x0800;
  PPUBANK[NAME_TABLE3] = base + 0x0c00;
}

BYTE Map30_DeviceId()
{
  const DWORD prgSize = static_cast<DWORD>(NesHeader.byRomSize) * 0x4000UL;

  if (prgSize >= 0x80000UL)
    return 0xB7;
  if (prgSize >= 0x40000UL)
    return 0xB6;
  return 0xB5;
}

BYTE* Map30_EnsurePrgOverlay(BYTE bank16k)
{
  bank16k &= 0x1f;

  for (int i = 0; i < 2; ++i)
  {
    if (Map30_PrgOverlayUsed[i] && Map30_PrgOverlayBank[i] == bank16k)
      return Map30_PrgOverlayData[i];
  }

  for (int i = 0; i < 2; ++i)
  {
    if (!Map30_PrgOverlayUsed[i])
    {
      BYTE* overlay = Map30_PrgOverlayData[i];
      if (!overlay)
      {
        InfoNES_Error("Mapper 30 overlay alloc failed", 0);
        return nullptr;
      }
      InfoNES_MemoryCopy(overlay + 0x0000, ROMPAGE(bank16k * 2 + 0), 0x2000);
      InfoNES_MemoryCopy(overlay + 0x2000, ROMPAGE(bank16k * 2 + 1), 0x2000);
      Map30_PrgOverlayUsed[i] = 1;
      Map30_PrgOverlayBank[i] = bank16k;
      return overlay;
    }
  }

  InfoNES_Error("Mapper 30 PRG-RAM > 32KB (bank %u)", static_cast<unsigned>(bank16k));
  return nullptr;
}

void Map30_SetPrgBanks(BYTE bank)
{
  const BYTE bankCount = NesHeader.byRomSize ? NesHeader.byRomSize : 1;
  bank &= 0x1f;
  bank %= bankCount;

  BYTE* overlay = nullptr;
  for (int i = 0; i < 2; ++i)
  {
    if (Map30_PrgOverlayUsed[i] && Map30_PrgOverlayBank[i] == bank)
    {
      overlay = Map30_PrgOverlayData[i];
      break;
    }
  }

  if (overlay)
  {
    ROMBANK0 = overlay + 0x0000;
    ROMBANK1 = overlay + 0x2000;
  }
  else
  {
    const BYTE page = static_cast<BYTE>(bank << 1);
    ROMBANK0 = ROMPAGE(page + 0);
    ROMBANK1 = ROMPAGE(page + 1);
  }

  ROMBANK2 = ROMLASTPAGE(1);
  ROMBANK3 = ROMLASTPAGE(0);
}

void Map30_SetChrBanks(BYTE bank)
{
  if (!Map30_ChrRam || !Map30_ChrRamSize)
    return;

  BYTE bankMask = 0;
  if (Map30_ChrRamSize >= 0x8000)
    bankMask = 0x03;
  else if (Map30_ChrRamSize >= 0x4000)
    bankMask = 0x01;

  const BYTE page = static_cast<BYTE>((bank & bankMask) << 3);

  PPUBANK[0] = &Map30_ChrRam[(page + 0) * 0x400];
  PPUBANK[1] = &Map30_ChrRam[(page + 1) * 0x400];
  PPUBANK[2] = &Map30_ChrRam[(page + 2) * 0x400];
  PPUBANK[3] = &Map30_ChrRam[(page + 3) * 0x400];
  PPUBANK[4] = &Map30_ChrRam[(page + 4) * 0x400];
  PPUBANK[5] = &Map30_ChrRam[(page + 5) * 0x400];
  PPUBANK[6] = &Map30_ChrRam[(page + 6) * 0x400];
  PPUBANK[7] = &Map30_ChrRam[(page + 7) * 0x400];
  ChrBufUpdate = 0xff;
  InfoNES_SetupChr();
}

void Map30_UpdateMirroring(BYTE data)
{
  if (Map30_Submapper == 3)
  {
    // D7=0 → Horizontal(0), D7=1 → Vertical(1)  [NESdev UNROM-512-C spec]
    InfoNES_Mirroring((data & 0x80) ? 1 : 0);
    return;
  }

  switch (Map30_NametableMode)
  {
  case MAP30_MIRROR_FIXED_V:
    InfoNES_Mirroring(1);
    break;

  case MAP30_MIRROR_ONESCREEN:
    InfoNES_Mirroring((data & 0x80) ? 2 : 3);
    break;

  case MAP30_MIRROR_FOURSCREEN:
    Map30_ApplyFourScreenNametables();
    break;

  default:
    InfoNES_Mirroring(0);
    break;
  }
}

void Map30_ApplyLatch(BYTE data)
{
  Map30_Latch = data;
  Map30_SetPrgBanks(data & 0x1f);
  Map30_SetChrBanks((data >> 5) & 0x03);
  Map30_UpdateMirroring(data);
}

void Map30_LogLatchEvent(WORD wAddr, BYTE byData)
{
#if INFONES_ENABLE_PPU2006_EVT_LOG
  if (structured_log_event_enabled())
  {
    const BYTE retLo = RAM[0x100 + ((SP + 1) & 0xff)];
    const BYTE retHi = RAM[0x100 + ((SP + 2) & 0xff)];
    const WORD ret = (WORD)((retHi << 8) | retLo) + 1;
    std::printf("[EVT] f=%lu sl=%u MAP30 addr=%04X data=%02X pc=%04X a=%02X sp=%02X ret=%04X prg=%u chr=%u mode=%u nt=%u latch=%02X\n",
                structured_log_current_frame(),
                (unsigned)PPU_Scanline,
                (unsigned)wAddr,
                (unsigned)byData,
                (unsigned)PC,
                (unsigned)A,
                (unsigned)SP,
                (unsigned)ret,
                (unsigned)(Map30_Latch & 0x1f),
                (unsigned)((Map30_Latch >> 5) & 0x03),
                (unsigned)Map30_NametableMode,
                (unsigned)PPU_NameTableBank,
                (unsigned)Map30_Latch);
  }
#endif
}

void Map30_EnterFlashIdMode()
{
  if (!Map30_FlashIdBank) return;
  Map30_FlashIdMode = 1;
  InfoNES_MemorySet(Map30_FlashIdBank, 0xff, 0x2000);
  Map30_FlashIdBank[0] = 0xBF;
  Map30_FlashIdBank[1] = Map30_DeviceId();
  ROMBANK0 = Map30_FlashIdBank;
  ROMBANK1 = Map30_FlashIdBank;
}

void Map30_LeaveFlashIdMode()
{
  if (!Map30_FlashIdMode)
    return;

  Map30_FlashIdMode = 0;
  Map30_ApplyLatch(Map30_Latch);
}

void Map30_FlashWriteByte(WORD wAddr, BYTE byData)
{
  BYTE* overlay = Map30_EnsurePrgOverlay(Map30_FlashTargetBank);
  if (!overlay)
    return;
  overlay[wAddr & 0x3fff] &= byData;
  Map30_FlashDirty = 1;
}

void Map30_FlashEraseSector(WORD wAddr)
{
  BYTE* overlay = Map30_EnsurePrgOverlay(Map30_FlashTargetBank);
  if (!overlay)
    return;
  WORD sector = wAddr & 0x3000;
  InfoNES_MemorySet(overlay + sector, 0xff, 0x1000);
  Map30_FlashDirty = 1;
}

void Map30_ProcessFlashWrite(WORD wAddr, BYTE byData)
{
  if (!Map30_Flashable)
    return;

  if (byData == 0xF0)
  {
    Map30_FlashState = MAP30_FLASH_IDLE;
    Map30_LeaveFlashIdMode();
    return;
  }

  switch (Map30_FlashState)
  {
  case MAP30_FLASH_IDLE:
    Map30_FlashState = (wAddr == 0xC000 && byData == 0x01) ? MAP30_FLASH_C1 : MAP30_FLASH_IDLE;
    break;

  case MAP30_FLASH_C1:
    Map30_FlashState = (wAddr == 0x9555 && byData == 0xAA) ? MAP30_FLASH_AA : MAP30_FLASH_IDLE;
    break;

  case MAP30_FLASH_AA:
    Map30_FlashState = (wAddr == 0xC000 && byData == 0x00) ? MAP30_FLASH_C0 : MAP30_FLASH_IDLE;
    break;

  case MAP30_FLASH_C0:
    Map30_FlashState = (wAddr == 0xAAAA && byData == 0x55) ? MAP30_FLASH_55 : MAP30_FLASH_IDLE;
    break;

  case MAP30_FLASH_55:
    Map30_FlashState = (wAddr == 0xC000 && byData == 0x01) ? MAP30_FLASH_C1_2 : MAP30_FLASH_IDLE;
    break;

  case MAP30_FLASH_C1_2:
    if (wAddr == 0x9555 && byData == 0x90)
    {
      Map30_EnterFlashIdMode();
      Map30_FlashState = MAP30_FLASH_IDLE;
    }
    else if (wAddr == 0x9555 && byData == 0xA0)
    {
      Map30_FlashState = MAP30_FLASH_WRITE_BANK;
    }
    else if (wAddr == 0x9555 && byData == 0x80)
    {
      Map30_FlashState = MAP30_FLASH_ERASE_C1;
    }
    else
    {
      Map30_FlashState = MAP30_FLASH_IDLE;
    }
    break;

  case MAP30_FLASH_WRITE_BANK:
    if (wAddr >= 0xC000)
    {
      Map30_FlashTargetBank = byData & 0x1f;
      Map30_FlashState = MAP30_FLASH_WRITE_DATA;
    }
    else
    {
      Map30_FlashState = MAP30_FLASH_IDLE;
    }
    break;

  case MAP30_FLASH_WRITE_DATA:
    if (wAddr < 0xC000)
      Map30_FlashWriteByte(wAddr, byData);
    Map30_FlashState = MAP30_FLASH_IDLE;
    break;

  case MAP30_FLASH_ERASE_C1:
    Map30_FlashState = (wAddr == 0x9555 && byData == 0xAA) ? MAP30_FLASH_ERASE_AA : MAP30_FLASH_IDLE;
    break;

  case MAP30_FLASH_ERASE_AA:
    Map30_FlashState = (wAddr == 0xC000 && byData == 0x00) ? MAP30_FLASH_ERASE_C0 : MAP30_FLASH_IDLE;
    break;

  case MAP30_FLASH_ERASE_C0:
    Map30_FlashState = (wAddr == 0xAAAA && byData == 0x55) ? MAP30_FLASH_ERASE_55 : MAP30_FLASH_IDLE;
    break;

  case MAP30_FLASH_ERASE_55:
    if (wAddr >= 0xC000)
    {
      Map30_FlashTargetBank = byData & 0x1f;
      Map30_FlashState = MAP30_FLASH_ERASE_DATA;
    }
    else
    {
      Map30_FlashState = MAP30_FLASH_IDLE;
    }
    break;

  case MAP30_FLASH_ERASE_DATA:
    if (wAddr < 0xC000 && byData == 0x30)
      Map30_FlashEraseSector(wAddr);
    Map30_FlashState = MAP30_FLASH_IDLE;
    break;

  default:
    Map30_FlashState = MAP30_FLASH_IDLE;
    break;
  }
}

} // namespace

void Map30_Release()
{
  if (Map30_OverlayAlloc)
  {
    delete[] Map30_OverlayAlloc;
    Map30_OverlayAlloc = nullptr;
  }
  if (Map30_FlashIdAlloc)
  {
    delete[] Map30_FlashIdAlloc;
    Map30_FlashIdAlloc = nullptr;
  }
  if (Map30_ChrRamAlloc)
  {
    delete[] Map30_ChrRamAlloc;
    Map30_ChrRamAlloc = nullptr;
  }
  Map30_ChrRam = nullptr;
  Map30_ChrRamSize = 0;
  Map30_FlashIdBank = nullptr;
  Map30_PrgOverlayData[0] = nullptr;
  Map30_PrgOverlayData[1] = nullptr;
  Map30_FlashDirty = 0;
  InfoNES_MemorySet(Map30_PrgOverlayUsed, 0, sizeof(Map30_PrgOverlayUsed));
  InfoNES_MemorySet(Map30_PrgOverlayBank, 0, sizeof(Map30_PrgOverlayBank));
}

bool Map30_IsFlashSaveEnabled()
{
  return Map30_Flashable != 0
      && Map30_PrgOverlayData[0] != nullptr
      && Map30_PrgOverlayData[1] != nullptr;
}

bool Map30_IsFlashSaveDirty()
{
  return Map30_FlashDirty != 0;
}

void Map30_ClearFlashSaveDirty()
{
  Map30_FlashDirty = 0;
}

BYTE Map30_GetOverlayUsed(int index)
{
  return (index >= 0 && index < 2) ? Map30_PrgOverlayUsed[index] : 0;
}

BYTE Map30_GetOverlayBank(int index)
{
  return (index >= 0 && index < 2) ? Map30_PrgOverlayBank[index] : 0;
}

const BYTE *Map30_GetOverlayData(int index)
{
  return (index >= 0 && index < 2) ? Map30_PrgOverlayData[index] : nullptr;
}

bool Map30_RestoreOverlay(int index, BYTE bank, const BYTE *data, unsigned size)
{
  if (index < 0 || index >= 2 || !data || size != 0x4000u || !Map30_PrgOverlayData[index])
    return false;

  InfoNES_MemoryCopy(Map30_PrgOverlayData[index], data, 0x4000);
  Map30_PrgOverlayUsed[index] = 1;
  Map30_PrgOverlayBank[index] = static_cast<BYTE>(bank & 0x1f);
  return true;
}

void Map30_ReapplyState()
{
  Map30_ApplyLatch(Map30_Latch);
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 30                                             */
/*-------------------------------------------------------------------*/
void Map30_Init()
{
  MapperInit = Map30_Init;
  MapperWrite = Map30_Write;
  MapperSram = Map0_Sram;
  MapperApu = Map0_Apu;
  MapperReadApu = Map0_ReadApu;
  MapperVSync = Map0_VSync;
  MapperHSync = Map0_HSync;
  MapperPPU = Map0_PPU;
  MapperRenderScreen = Map0_RenderScreen;

  SRAMBANK = SRAM;

  Map30_Flashable = ROM_SRAM ? 1 : 0;
  Map30_Submapper = ((NesHeader.byInfo2 & 0x0c) == 0x08) ? (NesHeader.byReserve[0] >> 4) : 0;
  Map30_Latch = 0;
  Map30_FlashState = MAP30_FLASH_IDLE;
  Map30_FlashIdMode = 0;
  Map30_FlashTargetBank = 0;
  Map30_Release();
  DWORD chrRamSize = 0x8000;
  if ((NesHeader.byInfo2 & 0x0c) == 0x08)
  {
    const BYTE vramShift = NesHeader.byReserve[3] & 0x0f;
    if (vramShift)
      chrRamSize = 64UL << vramShift;
    else if (NesHeader.byVRomSize != 0)
      chrRamSize = 0;
    // When NES 2.0 explicitly provides VRAM size, follow it. Otherwise keep the
    // mapper30 default at 32KB CHR-RAM for four-screen capable configurations.
  }
  if (chrRamSize < 0x2000)
    chrRamSize = 0x2000;
  if (chrRamSize > 0x8000)
    chrRamSize = 0x8000;

  Map30_OverlayAlloc = new (std::nothrow) BYTE[0x4000 * 2];
  if (!Map30_OverlayAlloc)
  {
    InfoNES_Error("Mapper 30 startup alloc failed [1/3 overlay-pool size=%u]", 0x4000 * 2);
    Map30_Release();
    return;
  }

  Map30_ChrRamAlloc = new (std::nothrow) BYTE[chrRamSize];
  if (!Map30_ChrRamAlloc)
  {
    InfoNES_Error("Mapper 30 startup alloc failed [2/3 chr-ram size=%lu]",
                  static_cast<unsigned long>(chrRamSize));
    Map30_Release();
    return;
  }

  Map30_FlashIdAlloc = new (std::nothrow) BYTE[0x2000];
  if (!Map30_FlashIdAlloc)
  {
    InfoNES_Error("Mapper 30 startup alloc failed [3/3 flash-id size=%u]", 0x2000);
    Map30_Release();
    return;
  }

  Map30_ChrRam = Map30_ChrRamAlloc;
  Map30_FlashIdBank = Map30_FlashIdAlloc;
  Map30_PrgOverlayData[0] = Map30_OverlayAlloc;
  Map30_PrgOverlayData[1] = Map30_OverlayAlloc + 0x4000;
  Map30_ChrRamSize = chrRamSize;

  if (Map30_Submapper == 3)
  {
    Map30_NametableMode = MAP30_MIRROR_FIXED_H;
  }
  else
  {
    switch (NesHeader.byInfo1 & 0x09)
    {
    case 0x00:
      Map30_NametableMode = MAP30_MIRROR_FIXED_H;
      break;

    case 0x01:
      Map30_NametableMode = MAP30_MIRROR_FIXED_V;
      break;

    case 0x08:
      Map30_NametableMode = MAP30_MIRROR_ONESCREEN;
      break;

    default:
      Map30_NametableMode = MAP30_MIRROR_FOURSCREEN;
      break;
    }
  }

  InfoNES_MemorySet(Map30_ChrRam, 0, Map30_ChrRamSize);
  InfoNES_MemorySet(Map30_FlashIdBank, 0xff, 0x2000);

#if INFONES_MAPPER30_ENABLE_DIAGNOSTICS
  Map30_DebugReset();
#endif
  Map30_ApplyLatch(0);

  K6502_Set_Int_Wiring(1, 1);
}

/*-------------------------------------------------------------------*/
/*  Mapper 30 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map30_Write(WORD wAddr, BYTE byData)
{
#if INFONES_MAPPER30_ENABLE_DIAGNOSTICS
  Map30_DebugRecord(wAddr, byData);
#endif

  if (Map30_Flashable)
  {
    // submapper 0 with battery / submapper 1/3/4: latch decode at $C000-$FFFF only
    if (wAddr < 0xc000)
    {
      Map30_ProcessFlashWrite(wAddr, byData);
      return;
    }

    Map30_ApplyLatch(byData);
    Map30_LogLatchEvent(wAddr, byData);
    Map30_ProcessFlashWrite(wAddr, byData);
    return;
  }

  // Non-flashable path
  // submapper 1/3/4: latch decode at $C000-$FFFF only
  if ((Map30_Submapper == 1 || Map30_Submapper == 3 || Map30_Submapper == 4)
      && wAddr < 0xC000)
    return;

  // submapper 0 (no battery) / submapper 2: bus conflict
  if (Map30_HasBusConflict())
    byData = Map30_ApplyBusConflict(wAddr, byData);

  Map30_ApplyLatch(byData);
  Map30_LogLatchEvent(wAddr, byData);
}
