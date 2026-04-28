/*===================================================================*/
/*===================================================================*/
/*                                                                   */
/*  InfoNES.cpp : NES Emulator for Win32, Linux(x86), Linux(PS2)     */
/*                                                                   */
/*  2000/05/18  InfoNES Project ( based on pNesX )                   */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------
 * File List :
 *
 * [NES Hardware]
 *   InfoNES.cpp
 *   InfoNES.h
 *   K6502_rw.h
 *
 * [Mapper function]
 *   InfoNES_Mapper.cpp
 *   InfoNES_Mapper.h
 *
 * [The function which depends on a system]
 *   InfoNES_System_ooo.cpp (ooo is a system name. win, ...)
 *   InfoNES_System.h
 *
 * [CPU]
 *   K6502.cpp
 *   K6502.h
 *
 * [Others]
 *   InfoNES_Types.h
 *
 --------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/*  Include files                                                    */
/*-------------------------------------------------------------------*/

#include "InfoNES.h"
#include "InfoNES_System.h"
#include "runtime_log.h"

#include <cstdint>

extern "C" void display_toggle_nes_view_scale(void);
extern "C" int display_get_nes_view_scale(void);
extern "C" void display_perf_reset(void);
extern "C" void display_perf_snapshot(uint64_t *wait_us,
                                      uint64_t *flush_us,
                                      uint64_t *queue_wait_us,
                                      uint32_t *queue_wait_count,
                                      uint64_t *frame_pacing_sleep_us,
                                      uint32_t *frame_pacing_sleep_count);
#include "input.h"
#include "InfoNES_Mapper.h"
#include "InfoNES_StructuredLog.h"
#include "screenshot.h"
#include "InfoNES_pAPU.h"
#include "K6502.h"
#include "audio.h"
#include "sram_store.h"
#include "boko_flash_trace.h"
#include <assert.h>
#include <pico.h>
#include <pico/time.h>
#include <tuple>
#include <cstdio>

//#include <util/work_meter.h>

constexpr uint16_t makeTag(int r, int g, int b)
{
  return (r << 10) | (g << 5) | (b);
}

#if INFONES_ENABLE_BOKOSUKA_STATE_LOG
static unsigned long g_bokosuka_heartbeat_seq = 0;
static bool g_bokosuka_auto_freeze_done = false;
static bool g_bokosuka_stable_state_valid = false;
static unsigned g_bokosuka_stable_state_samples = 0;
static BYTE g_bokosuka_last_r2c = 0;
static BYTE g_bokosuka_last_r3b = 0;
static BYTE g_bokosuka_last_r3c = 0;
static BYTE g_bokosuka_last_r9d = 0;

static void InfoNES_BokosukaMaybeFreezeTrace(void)
{
  const bool same_state =
      g_bokosuka_stable_state_valid &&
      g_bokosuka_last_r2c == RAM[0x002C] &&
      g_bokosuka_last_r3b == RAM[0x003B] &&
      g_bokosuka_last_r3c == RAM[0x003C] &&
      g_bokosuka_last_r9d == RAM[0x009D];

  if (same_state)
  {
    g_bokosuka_stable_state_samples++;
  }
  else
  {
    g_bokosuka_stable_state_valid = true;
    g_bokosuka_stable_state_samples = 1;
    g_bokosuka_last_r2c = RAM[0x002C];
    g_bokosuka_last_r3b = RAM[0x003B];
    g_bokosuka_last_r3c = RAM[0x003C];
    g_bokosuka_last_r9d = RAM[0x009D];
  }

  const bool pc_idle_loop = PC >= 0xE300 && PC <= 0xE3FF;
  const bool active_state = RAM[0x002C] != 0 || RAM[0x009D] != 0;
  if (!g_bokosuka_auto_freeze_done &&
      g_bokosuka_stable_state_samples >= 5u &&
      pc_idle_loop &&
      active_state)
  {
    std::printf("[BOKO_AUTO_FREEZE] t_us=%lu reason=stable-e3xx samples=%u pc=%04X r2c=%02X r3b=%02X r3c=%02X r9d=%02X\n",
                static_cast<unsigned long>(time_us_32()),
                g_bokosuka_stable_state_samples,
                static_cast<unsigned>(PC),
                static_cast<unsigned>(RAM[0x002C]),
                static_cast<unsigned>(RAM[0x003B]),
                static_cast<unsigned>(RAM[0x003C]),
                static_cast<unsigned>(RAM[0x009D]));
    boko_flash_trace_record_freeze(g_bokosuka_stable_state_samples,
                                   PC,
                                   RAM[0x002C],
                                   RAM[0x003B],
                                   RAM[0x003C],
                                   RAM[0x009D]);
    g_bokosuka_auto_freeze_done = true;
  }
}

static void InfoNES_BokosukaHeartbeat(void)
{
  g_bokosuka_heartbeat_seq++;
  if ((g_bokosuka_heartbeat_seq % 60ul) != 0ul && PAD_System == 0)
  {
    return;
  }

  std::printf("[BOKO_HB] t_us=%lu seq=%lu pc=%04X sl=%u p1=%08lX p2=%08lX sys=%08lX r2c=%02X r3b=%02X r3c=%02X r9d=%02X a=%02X\n",
              static_cast<unsigned long>(time_us_32()),
              g_bokosuka_heartbeat_seq,
              static_cast<unsigned>(PC),
              static_cast<unsigned>(PPU_Scanline),
              static_cast<unsigned long>(PAD1_Latch),
              static_cast<unsigned long>(PAD2_Latch),
              static_cast<unsigned long>(PAD_System),
              static_cast<unsigned>(RAM[0x002C]),
              static_cast<unsigned>(RAM[0x003B]),
              static_cast<unsigned>(RAM[0x003C]),
              static_cast<unsigned>(RAM[0x009D]),
              static_cast<unsigned>(A));
  boko_flash_trace_record_heartbeat(g_bokosuka_heartbeat_seq,
                                    PC,
                                    PPU_Scanline,
                                    PAD1_Latch,
                                    PAD2_Latch,
                                    PAD_System,
                                    RAM[0x002C],
                                    RAM[0x003B],
                                    RAM[0x003C],
                                    RAM[0x009D],
                                    A);
  InfoNES_BokosukaMaybeFreezeTrace();
}
#endif
enum
{
  MARKER_START = makeTag(0, 31, 31),
  MARKER_CPU = makeTag(0, 31, 0),
  MARKER_SOUND = makeTag(31, 31, 0),
  MARKER_BG = makeTag(0, 0, 31),
  MARKER_SPRITE = makeTag(31, 0, 0),
};

namespace
{
uint8_t g_bg_tile_pair_idx4[256];
uint8_t g_bg_tile_pair_opaque4[256];

inline void initBgTileRenderLut()
{
  for (int pl0 = 0; pl0 < 16; ++pl0)
  {
    for (int pl1 = 0; pl1 < 16; ++pl1)
    {
      BYTE packed = 0;
      BYTE opaque = 0;
      for (int i = 0; i < 4; ++i)
      {
        const BYTE idx =
            (BYTE)(((pl0 >> (3 - i)) & 1u) | (((pl1 >> (3 - i)) & 1u) << 1));
        packed |= (BYTE)(idx << (6 - (i << 1)));
        opaque |= (BYTE)((idx != 0) << (3 - i));
      }

      const int key = (pl0 << 4) | pl1;
      g_bg_tile_pair_idx4[key] = packed;
      g_bg_tile_pair_opaque4[key] = opaque;
    }
  }
}

constexpr bool kPerfLogToSerial =
#if defined(NESCO_CORE1_BASELINE_LOG)
    true;
#else
    false;
#endif
constexpr uint64_t kPerfWindowUs = 1000000;
constexpr int kNesViewScaleStretch320x300 = 1;

uint64_t g_perf_window_start_us = 0;
uint64_t g_perf_last_frame_us = 0;
uint64_t g_perf_frame_us_total = 0;
uint64_t g_perf_frame_us_max = 0;
uint32_t g_perf_frame_samples = 0;
uint64_t g_perf_last_pad_us = 0;
uint64_t g_perf_pad_interval_us_total = 0;
uint64_t g_perf_pad_interval_us_max = 0;
uint32_t g_perf_pad_interval_samples = 0;
uint32_t g_perf_frames = 0;
uint32_t g_perf_scanlines = 0;
uint64_t g_perf_cpu_us = 0;
uint64_t g_perf_apu_us = 0;
uint64_t g_perf_draw_us = 0;
uint64_t g_perf_ppu_bg_us = 0;
uint64_t g_perf_ppu_bg_mapper_us = 0;
uint64_t g_perf_ppu_bg_clear_us = 0;
uint64_t g_perf_ppu_bg_setup_us = 0;
uint64_t g_perf_ppu_bg_tile_us = 0;
uint64_t g_perf_ppu_bg_clip_us = 0;
uint64_t g_perf_ppu_sprite_us = 0;
uint64_t g_perf_mapper_hsync_us = 0;
uint64_t g_perf_mapper_vsync_us = 0;
uint64_t g_perf_load_frame_us = 0;
uint64_t g_perf_tail_us = 0;
uint64_t g_perf_lcd_wait_us = 0;
uint64_t g_perf_lcd_flush_us = 0;
uint64_t g_perf_lcd_queue_wait_us = 0;
uint32_t g_perf_lcd_queue_wait_count = 0;
uint64_t g_perf_frame_pacing_sleep_us = 0;
uint32_t g_perf_frame_pacing_sleep_count = 0;
uint64_t g_perf_audio_wait_us = 0;
uint32_t g_perf_audio_wait_count = 0;

inline void perf_reset()
{
  g_perf_window_start_us = time_us_64();
  g_perf_last_frame_us = 0;
  g_perf_frame_us_total = 0;
  g_perf_frame_us_max = 0;
  g_perf_frame_samples = 0;
  g_perf_last_pad_us = 0;
  g_perf_pad_interval_us_total = 0;
  g_perf_pad_interval_us_max = 0;
  g_perf_pad_interval_samples = 0;
  g_perf_frames = 0;
  g_perf_scanlines = 0;
  g_perf_cpu_us = 0;
  g_perf_apu_us = 0;
  g_perf_draw_us = 0;
  g_perf_ppu_bg_us = 0;
  g_perf_ppu_bg_mapper_us = 0;
  g_perf_ppu_bg_clear_us = 0;
  g_perf_ppu_bg_setup_us = 0;
  g_perf_ppu_bg_tile_us = 0;
  g_perf_ppu_bg_clip_us = 0;
  g_perf_ppu_sprite_us = 0;
  g_perf_mapper_hsync_us = 0;
  g_perf_mapper_vsync_us = 0;
  g_perf_load_frame_us = 0;
  g_perf_tail_us = 0;
  g_perf_lcd_wait_us = 0;
  g_perf_lcd_flush_us = 0;
  g_perf_lcd_queue_wait_us = 0;
  g_perf_lcd_queue_wait_count = 0;
  g_perf_frame_pacing_sleep_us = 0;
  g_perf_frame_pacing_sleep_count = 0;
  g_perf_audio_wait_us = 0;
  g_perf_audio_wait_count = 0;
  (void)input_consume_event_count();
  display_perf_reset();
  audio_perf_reset();
}

inline void perf_note_frame(uint64_t now_us)
{
  if (g_perf_last_frame_us != 0)
  {
    const uint64_t frame_us = now_us - g_perf_last_frame_us;
    g_perf_frame_us_total += frame_us;
    if (frame_us > g_perf_frame_us_max)
    {
      g_perf_frame_us_max = frame_us;
    }
    ++g_perf_frame_samples;
  }
  g_perf_last_frame_us = now_us;
}

inline void perf_note_pad_poll(uint64_t now_us)
{
  if (g_perf_last_pad_us != 0)
  {
    const uint64_t interval_us = now_us - g_perf_last_pad_us;
    g_perf_pad_interval_us_total += interval_us;
    if (interval_us > g_perf_pad_interval_us_max)
    {
      g_perf_pad_interval_us_max = interval_us;
    }
    ++g_perf_pad_interval_samples;
  }
  g_perf_last_pad_us = now_us;
}

inline void perf_log_if_due(uint64_t now_us)
{
  if (!kPerfLogToSerial)
    return;

  if (g_perf_window_start_us == 0)
  {
    perf_reset();
    return;
  }

  const uint64_t elapsed_us = now_us - g_perf_window_start_us;
  if (elapsed_us < kPerfWindowUs)
    return;

  display_perf_snapshot(&g_perf_lcd_wait_us,
                        &g_perf_lcd_flush_us,
                        &g_perf_lcd_queue_wait_us,
                        &g_perf_lcd_queue_wait_count,
                        &g_perf_frame_pacing_sleep_us,
                        &g_perf_frame_pacing_sleep_count);
  audio_perf_snapshot(&g_perf_audio_wait_us, &g_perf_audio_wait_count);
  const uint64_t fps_x100 =
      elapsed_us != 0
          ? (static_cast<uint64_t>(g_perf_frames) * 100ull * 1000000ull) / elapsed_us
          : 0;
  const uint64_t frame_us_avg =
      g_perf_frame_samples != 0
          ? g_perf_frame_us_total / g_perf_frame_samples
          : 0;
  const uint64_t pad_interval_us_avg =
      g_perf_pad_interval_samples != 0
          ? g_perf_pad_interval_us_total / g_perf_pad_interval_samples
          : 0;
  const unsigned input_events = input_consume_event_count();
  const char *view_mode =
      display_get_nes_view_scale() == kNesViewScaleStretch320x300
          ? "stretch"
          : "normal";

  printf("[CORE1_BASE] t_us=%llu frames=%lu fps_x100=%llu frame_us_avg=%llu frame_us_max=%llu cpu_us=%llu apu_us=%llu draw_us=%llu ppu_bg_us=%llu ppu_bg_mapper_us=%llu ppu_bg_clear_us=%llu ppu_bg_setup_us=%llu ppu_bg_tile_us=%llu ppu_bg_clip_us=%llu ppu_sprite_us=%llu mapper_hsync_us=%llu mapper_vsync_us=%llu load_frame_us=%llu tail_us=%llu lcd_wait_us=%llu lcd_flush_us=%llu lcd_queue_wait_us=%llu lcd_queue_wait_count=%lu frame_pacing_sleep_us=%llu frame_pacing_sleep_count=%lu audio_wait_us=%llu audio_wait_count=%lu pad_interval_us_avg=%llu pad_interval_us_max=%llu input_events=%u view_mode=%s\n",
         static_cast<unsigned long long>(now_us),
         static_cast<unsigned long>(g_perf_frames),
         static_cast<unsigned long long>(fps_x100),
         static_cast<unsigned long long>(frame_us_avg),
         static_cast<unsigned long long>(g_perf_frame_us_max),
         static_cast<unsigned long long>(g_perf_cpu_us),
         static_cast<unsigned long long>(g_perf_apu_us),
         static_cast<unsigned long long>(g_perf_draw_us),
         static_cast<unsigned long long>(g_perf_ppu_bg_us),
         static_cast<unsigned long long>(g_perf_ppu_bg_mapper_us),
         static_cast<unsigned long long>(g_perf_ppu_bg_clear_us),
         static_cast<unsigned long long>(g_perf_ppu_bg_setup_us),
         static_cast<unsigned long long>(g_perf_ppu_bg_tile_us),
         static_cast<unsigned long long>(g_perf_ppu_bg_clip_us),
         static_cast<unsigned long long>(g_perf_ppu_sprite_us),
         static_cast<unsigned long long>(g_perf_mapper_hsync_us),
         static_cast<unsigned long long>(g_perf_mapper_vsync_us),
         static_cast<unsigned long long>(g_perf_load_frame_us),
         static_cast<unsigned long long>(g_perf_tail_us),
         static_cast<unsigned long long>(g_perf_lcd_wait_us),
         static_cast<unsigned long long>(g_perf_lcd_flush_us),
         static_cast<unsigned long long>(g_perf_lcd_queue_wait_us),
         static_cast<unsigned long>(g_perf_lcd_queue_wait_count),
         static_cast<unsigned long long>(g_perf_frame_pacing_sleep_us),
         static_cast<unsigned long>(g_perf_frame_pacing_sleep_count),
         static_cast<unsigned long long>(g_perf_audio_wait_us),
         static_cast<unsigned long>(g_perf_audio_wait_count),
         static_cast<unsigned long long>(pad_interval_us_avg),
         static_cast<unsigned long long>(g_perf_pad_interval_us_max),
         input_events,
         view_mode);
  fflush(stdout);

  perf_reset();
}
}

/*-------------------------------------------------------------------*/
/*  NES resources                                                    */
/*-------------------------------------------------------------------*/

#pragma region buffers
/* RAM */
BYTE RAM[RAM_SIZE];
// Share with romselect.cpp
void *InfoNes_GetRAM(size_t *size)
{
  NESCO_LOGF("Acquired RAM Buffer from emulator: %d bytes\n", RAM_SIZE);
  *size = RAM_SIZE;
  return SRAM;
}
/* SRAM */
BYTE SRAM[SRAM_SIZE];

/* Character Buffer */
BYTE ChrBuf[CHRBUF_SIZE];

// Share with romselect.cpp
void *InfoNes_GetChrBuf(size_t *size)
{
  NESCO_LOGF("Acquired ChrBuf Buffer from emulator: %d bytes\n", CHRBUF_SIZE);
  *size = CHRBUF_SIZE;
  return ChrBuf;
}
/* PPU RAM */
BYTE PPURAM[PPURAM_SIZE];
// Share with romselect.cpp
void *InfoNes_GetPPURAM(size_t *size)
{
  NESCO_LOGF("Acquired PPURAM Buffer from emulator: %d bytes\n", PPURAM_SIZE);
  *size = PPURAM_SIZE;
  return PPURAM;
}
/* PPU BANK ( 1Kb * 16 ) */
BYTE *PPUBANK[16];
/* Sprite RAM */
BYTE SPRRAM[SPRRAM_SIZE];
// Share with romselect.cpp
void *InfoNes_GetSPRRAM(size_t *size)
{
  NESCO_LOGF("Acquired SPRRAM Buffer from emulator: %d bytes\n", SPRRAM_SIZE);
  *size = SPRRAM_SIZE;
  return SPRRAM;
}
/* Scanline Table */
BYTE PPU_ScanTable[263];
#pragma endregion

bool SRAMwritten = false;

/* ROM */
BYTE *ROM;

/* SRAM BANK ( 8Kb ) */
BYTE *SRAMBANK;

/* ROM BANK ( 8Kb * 4 ) */
BYTE *ROMBANK[4];
// BYTE *ROMBANK0;
// BYTE *ROMBANK1;
// BYTE *ROMBANK2;
// BYTE *ROMBANK3;

/*-------------------------------------------------------------------*/
/*  PPU resources                                                    */
/*-------------------------------------------------------------------*/

/* VROM */
BYTE *VROM;

// BYTE *SPRRAM;
/* PPU Register */
BYTE PPU_R0;
BYTE PPU_R1;
BYTE PPU_R2;
BYTE PPU_R3;
BYTE PPU_R7;

/* Vertical scroll value */
BYTE PPU_Scr_V;
BYTE PPU_Scr_V_Next;
BYTE PPU_Scr_V_Byte;
BYTE PPU_Scr_V_Byte_Next;
BYTE PPU_Scr_V_Bit;
BYTE PPU_Scr_V_Bit_Next;

/* Horizontal scroll value */
BYTE PPU_Scr_H;
BYTE PPU_Scr_H_Next;
BYTE PPU_Scr_H_Byte;
BYTE PPU_Scr_H_Byte_Next;
BYTE PPU_Scr_H_Bit;
BYTE PPU_Scr_H_Bit_Next;

/* PPU Address */
WORD PPU_Addr;

/* PPU Address */
WORD PPU_Temp;

/* The increase value of the PPU Address */
WORD PPU_Increment;

/* Current Scanline */
WORD PPU_Scanline;

/* Name Table Bank */
BYTE PPU_NameTableBank;

/* BG Base Address */
BYTE *PPU_BG_Base;

/* Sprite Base Address */
BYTE *PPU_SP_Base;

/* Sprite Height */
WORD PPU_SP_Height;

/* Sprite #0 Scanline Hit Position */
int SpriteJustHit;

/* VRAM Write Enable ( 0: Disable, 1: Enable ) */
BYTE byVramWriteEnable;

/* PPU Address and Scroll Latch Flag*/
BYTE PPU_Latch_Flag;

/* Up and Down Clipping Flag ( 0: non-clip, 1: clip ) */
BYTE PPU_UpDown_Clip;

/* Frame IRQ ( 0: Disabled, 1: Enabled )*/
BYTE FrameIRQ_Enable;
WORD FrameStep;

/*-------------------------------------------------------------------*/
/*  Display and Others resouces                                      */
/*-------------------------------------------------------------------*/

/* Frame Skip */
WORD FrameSkip;
WORD FrameCnt;

/* Display Buffer */
#if 0
WORD DoubleFrame[ 2 ][ NES_DISP_WIDTH * NES_DISP_HEIGHT ];
WORD *WorkFrame;
WORD WorkFrameIdx;
#else
// WORD WorkFrame[ NES_DISP_WIDTH * NES_DISP_HEIGHT ];
WORD *WorkLine = nullptr;
BYTE BackgroundOpaqueLine[NES_DISP_WIDTH];
void __not_in_flash_func(InfoNES_SetLineBuffer)(WORD *p, WORD size)
{
  assert(size >= NES_DISP_WIDTH);
  WorkLine = p;
}
#endif

/* Update flag for ChrBuf */
BYTE ChrBufUpdate;

/* Palette Table */
WORD PalTable[32];

/* Table for Mirroring */
BYTE PPU_MirrorTable[][4] =
    {
        {NAME_TABLE0, NAME_TABLE0, NAME_TABLE1, NAME_TABLE1},
        {NAME_TABLE0, NAME_TABLE1, NAME_TABLE0, NAME_TABLE1},
        {NAME_TABLE1, NAME_TABLE1, NAME_TABLE1, NAME_TABLE1},
        {NAME_TABLE0, NAME_TABLE0, NAME_TABLE0, NAME_TABLE0},
        {NAME_TABLE0, NAME_TABLE1, NAME_TABLE2, NAME_TABLE3},
        {NAME_TABLE0, NAME_TABLE0, NAME_TABLE0, NAME_TABLE1}};

/*-------------------------------------------------------------------*/
/*  APU and Pad resources                                            */
/*-------------------------------------------------------------------*/

/* APU Register */
BYTE APU_Reg[0x18];

/* APU Mute ( 0:OFF, 1:ON ) */
int APU_Mute = 0;

/* Pad data */
DWORD PAD1_Latch;
DWORD PAD2_Latch;
DWORD PAD_System;
DWORD PAD1_Bit;
DWORD PAD2_Bit;

/*-------------------------------------------------------------------*/
/*  Mapper Function                                                  */
/*-------------------------------------------------------------------*/

/* Initialize Mapper */
void (*MapperInit)();
/* Write to Mapper */
void (*MapperWrite)(WORD wAddr, BYTE byData);
/* Write to SRAM */
void (*MapperSram)(WORD wAddr, BYTE byData);
/* Write to Apu */
void (*MapperApu)(WORD wAddr, BYTE byData);
/* Read from Apu */
BYTE(*MapperReadApu)
(WORD wAddr);
/* Callback at VSync */
void (*MapperVSync)();
/* Callback at HSync */
void (*MapperHSync)();
/* Callback at PPU read/write */
void (*MapperPPU)(WORD wAddr); // mapper 96だけ？
/* Callback at Rendering Screen 1:BG, 0:Sprite */
void (*MapperRenderScreen)(BYTE byMode);

/*-------------------------------------------------------------------*/
/*  ROM information                                                  */
/*-------------------------------------------------------------------*/

/* .nes File Header */
struct NesHeader_tag NesHeader;

/* Mapper Number */
BYTE MapperNo;

/* Mirroring 0:Horizontal 1:Vertical */
BYTE ROM_Mirroring;
/* It has SRAM */
BYTE ROM_SRAM;
/* It has Trainer */
BYTE ROM_Trainer;
/* Four screen VRAM  */
BYTE ROM_FourScr;

/*===================================================================*/
/*                                                                   */
/*                InfoNES_Init() : Initialize InfoNES                */
/*                                                                   */
/*===================================================================*/
void InfoNES_Init()
{
  /*
   *  Initialize InfoNES
   *
   *  Remarks
   *    Initialize K6502 and Scanline Table.
   */
  int nIdx;

  // Initialize 6502
  K6502_Init();

  // Initialize Scanline Table
  for (nIdx = 0; nIdx < 263; ++nIdx)
  {
    if (nIdx < SCAN_ON_SCREEN_START)
      PPU_ScanTable[nIdx] = SCAN_ON_SCREEN;
    else if (nIdx < SCAN_BOTTOM_OFF_SCREEN_START)
      PPU_ScanTable[nIdx] = SCAN_ON_SCREEN;
    else if (nIdx < SCAN_UNKNOWN_START)
      PPU_ScanTable[nIdx] = SCAN_ON_SCREEN;
    else if (nIdx < SCAN_VBLANK_START)
      PPU_ScanTable[nIdx] = SCAN_UNKNOWN;
    else
      PPU_ScanTable[nIdx] = SCAN_VBLANK;
  }

  initBgTileRenderLut();
  perf_reset();
}

/*===================================================================*/
/*                                                                   */
/*                InfoNES_Fin() : Completion treatment               */
/*                                                                   */
/*===================================================================*/
void InfoNES_Fin()
{
  /*
   *  Completion treatment
   *
   *  Remarks
   *    Release resources
   */
  // Finalize pAPU
  InfoNES_pAPUDone();

  // Release a memory for ROM
  InfoNES_ReleaseRom();
}

/*===================================================================*/
/*                                                                   */
/*                  InfoNES_Load() : Load a cassette                 */
/*                                                                   */
/*===================================================================*/
int InfoNES_Load(const char *pszFileName)
{
  /*
   *  Load a cassette
   *
   *  Parameters
   *    const char *pszFileName            (Read)
   *      File name of ROM image
   *
   *  Return values
   *     0 : It was finished normally.
   *    -1 : An error occurred.
   *
   *  Remarks
   *    Read a ROM image in the memory.
   *    Reset InfoNES.
   */

  // Release a memory for ROM
  InfoNES_ReleaseRom();

  sram_store_begin_rom(pszFileName);

  // Read a ROM image in the memory
  if (InfoNES_ReadRom(pszFileName) < 0)
  {
    sram_store_clear_session();
    return -1;
  }

#if INFONES_ENABLE_BOKOSUKA_STATE_LOG
  boko_flash_trace_begin(pszFileName);
#endif

  // Reset InfoNES
  if (InfoNES_Reset() < 0)
  {
    sram_store_clear_session();
    return -1;
  }

  sram_store_restore_for_current_rom();

  // Successful
  return 0;
}

/*===================================================================*/
/*                                                                   */
/*                 InfoNES_Reset() : Reset InfoNES                   */
/*                                                                   */
/*===================================================================*/
int InfoNES_Reset()
{
  /*
   *  Reset InfoNES
   *
   *  Return values
   *     0 : Normally
   *    -1 : Non support mapper
   *
   *  Remarks
   *    Initialize Resources, PPU and Mapper.
   *    Reset CPU.
   */

  int nIdx;

  /*-------------------------------------------------------------------*/
  /*  Get information on the cassette                                  */
  /*-------------------------------------------------------------------*/

  // boot_menu.cpp already normalizes legacy iNES garbage, so mapper detection
  // can safely use the standard lower+upper nibble combination for both
  // classic iNES and NES 2.0 headers.
  MapperNo = (NesHeader.byInfo1 >> 4) | (NesHeader.byInfo2 & 0xf0);

  // Get information on the ROM
  ROM_Mirroring = NesHeader.byInfo1 & 1;
  ROM_SRAM = NesHeader.byInfo1 & 2;
  ROM_Trainer = NesHeader.byInfo1 & 4;
  ROM_FourScr = NesHeader.byInfo1 & 8;

  /*-------------------------------------------------------------------*/
  /*  Initialize resources                                             */
  /*-------------------------------------------------------------------*/

  // Clear RAM
  InfoNES_MemorySet(RAM, 0, RAM_SIZE);

  // Reset frame skip and frame count
  FrameSkip = 0;
  FrameCnt = 0;
  perf_reset();

#if 0
  // Reset work frame
  WorkFrame = DoubleFrame[ 0 ];
  WorkFrameIdx = 0;
#endif

  // Reset update flag of ChrBuf
  ChrBufUpdate = 0xff;

  // Reset palette table
  InfoNES_MemorySet(PalTable, 0, sizeof PalTable);

  // Reset APU register
  InfoNES_MemorySet(APU_Reg, 0, sizeof APU_Reg);

  // Reset joypad
  PAD1_Latch = PAD2_Latch = PAD_System = 0;
  PAD1_Bit = PAD2_Bit = 0;

  /*-------------------------------------------------------------------*/
  /*  Initialize PPU                                                   */
  /*-------------------------------------------------------------------*/

  InfoNES_SetupPPU();

  /*-------------------------------------------------------------------*/
  /*  Initialize pAPU                                                  */
  /*-------------------------------------------------------------------*/

  InfoNES_pAPUInit();

  /*-------------------------------------------------------------------*/
  /*  Initialize Mapper                                                */
  /*-------------------------------------------------------------------*/
  InfoNES_MessageBox("Using Mapper #%d\n", MapperNo);
  // Get Mapper Table Index
  for (nIdx = 0; MapperTable[nIdx].nMapperNo != -1; ++nIdx)
  {
    if (MapperTable[nIdx].nMapperNo == MapperNo)
      break;
  }

  if (MapperTable[nIdx].nMapperNo == -1)
  {
    // Non support mapper
    InfoNES_Error("Mapper #%d is unsupported.", MapperNo);
    return -1;
  }

  // Set up a mapper initialization function
  MapperTable[nIdx].pMapperInit();

  /*-------------------------------------------------------------------*/
  /*  Reset CPU                                                        */
  /*-------------------------------------------------------------------*/

  K6502_Reset();

  // Successful
  return 0;
}

/*===================================================================*/
/*                                                                   */
/*                InfoNES_SetupPPU() : Initialize PPU                */
/*                                                                   */
/*===================================================================*/
void InfoNES_SetupPPU()
{
  /*
   *  Initialize PPU
   *
   */
  int nPage;

  // Clear PPU and Sprite Memory
  InfoNES_MemorySet(PPURAM, 0, PPURAM_SIZE);
  InfoNES_MemorySet(SPRRAM, 0, SPRRAM_SIZE);

  // Reset PPU Register
  PPU_R0 = PPU_R1 = PPU_R2 = PPU_R3 = PPU_R7 = 0;

  // Reset latch flag
  PPU_Latch_Flag = 0;

  // Reset up and down clipping flag
  PPU_UpDown_Clip = 0;

  FrameStep = 0;
  FrameIRQ_Enable = 0;

  // Reset only horizontal current scroll state for the next DART/TOWER split
  // test. Keep vertical current and all *_Next values untouched so we can
  // isolate whether PPU_Scr_H alone is driving the regression.
  PPU_Scr_H = 0;
  PPU_Scr_H_Byte = 0;
  PPU_Scr_H_Bit = 0;

  // Reset PPU address
  PPU_Addr = 0;
  PPU_Temp = 0;

  // Reset scanline
  PPU_Scanline = 0;

  // Reset hit position of sprite #0
  SpriteJustHit = 0;

  // Reset information on PPU_R0
  PPU_Increment = 1;
  PPU_NameTableBank = NAME_TABLE0;
  PPU_BG_Base = ChrBuf;
  PPU_SP_Base = ChrBuf + 256 * 64;
  PPU_SP_Height = 8;

  // Reset PPU banks
  for (nPage = 0; nPage < 16; ++nPage)
    PPUBANK[nPage] = &PPURAM[nPage * 0x400];

  /* Mirroring of Name Table */
  InfoNES_Mirroring(ROM_Mirroring);

  /* Reset VRAM Write Enable */
  byVramWriteEnable = (NesHeader.byVRomSize == 0) ? 1 : 0;
}

/*===================================================================*/
/*                                                                   */
/*       InfoNES_Mirroring() : Set up a Mirroring of Name Table      */
/*                                                                   */
/*===================================================================*/
void InfoNES_Mirroring(int nType)
{
  /*
   *  Set up a Mirroring of Name Table
   *
   *  Parameters
   *    int nType          (Read)
   *      Mirroring Type
   *        0 : Horizontal
   *        1 : Vertical
   *        2 : One Screen 0x2400
   *        3 : One Screen 0x2000
   *        4 : Four Screen
   *        5 : Special for Mapper #233
   */

  PPUBANK[NAME_TABLE0] = &PPURAM[PPU_MirrorTable[nType][0] * 0x400];
  PPUBANK[NAME_TABLE1] = &PPURAM[PPU_MirrorTable[nType][1] * 0x400];
  PPUBANK[NAME_TABLE2] = &PPURAM[PPU_MirrorTable[nType][2] * 0x400];
  PPUBANK[NAME_TABLE3] = &PPURAM[PPU_MirrorTable[nType][3] * 0x400];
}

/*===================================================================*/
/*                                                                   */
/*              InfoNES_Main() : The main loop of InfoNES            */
/*                                                                   */
/*===================================================================*/
void InfoNES_Main()
{
  /*
   *  The main loop of InfoNES
   *
   */

  // Initialize InfoNES
  InfoNES_Init();

  // Main loop
  // while (1)
  // {
  /*-------------------------------------------------------------------*/
  /*  To the menu screen                                               */
  /*-------------------------------------------------------------------*/
  if (InfoNES_Menu() == 0 )
  {
    
    /*-------------------------------------------------------------------*/
    /*  Start a NES emulation                                            */
    /*-------------------------------------------------------------------*/
    InfoNES_Cycle();
  }
  //}

  // Completion treatment
  InfoNES_Fin();
}

/*===================================================================*/
/*                                                                   */
/*              InfoNES_Cycle() : The loop of emulation              */
/*                                                                   */
/*===================================================================*/
void __not_in_flash_func(InfoNES_Cycle)()
{
  /*
   *  The loop of emulation
   *
   */

  // Set the PPU adress to the buffered value
  // if ((PPU_R1 & R1_SHOW_SP) || (PPU_R1 & R1_SHOW_SCR))
  //   PPU_Addr = PPU_Temp;

  // Emulation loop
  for (;;)
  {
    //util::WorkMeterMark(MARKER_START);
      if (!micromenu)
      {
          // Set a flag if a scanning line is a hit in the sprite #0
          if (SpriteJustHit == PPU_Scanline &&
              PPU_ScanTable[PPU_Scanline] == SCAN_ON_SCREEN)
          {
              const uint64_t cpu_start_us = time_us_64();
              // # of Steps to execute before sprite #0 hit
              int nStep = SPRRAM[SPR_X] * STEP_PER_SCANLINE / NES_DISP_WIDTH;

              // Execute instructions
              K6502_Step(nStep);

              // Set a sprite hit flag
              if ((PPU_R1 & R1_SHOW_SP) && (PPU_R1 & R1_SHOW_SCR))
                  PPU_R2 |= R2_HIT_SP;

              // NMI is required if there is necessity
              if ((PPU_R0 & R0_NMI_SP) && (PPU_R1 & R1_SHOW_SP))
                  NMI_REQ;

              // Execute instructions
              K6502_Step(STEP_PER_SCANLINE - nStep);
              g_perf_cpu_us += time_us_64() - cpu_start_us;
          }
          else
          {
              // Execute instructions
              const uint64_t cpu_start_us = time_us_64();
              K6502_Step(STEP_PER_SCANLINE);
              g_perf_cpu_us += time_us_64() - cpu_start_us;
          }

          // Frame IRQ in H-Sync
          FrameStep += STEP_PER_SCANLINE;
          if (FrameStep > STEP_PER_FRAME && FrameIRQ_Enable)
          {
              FrameStep %= STEP_PER_FRAME;
              IRQ_REQ;
              APU_Reg[0x15] |= 0x40;
          }

          //util::WorkMeterMark(MARKER_CPU);
      }
    // A mapper function in H-Sync
    const uint64_t mapper_hsync_start_us = time_us_64();
    MapperHSync();
    g_perf_mapper_hsync_us += time_us_64() - mapper_hsync_start_us;

    // A function in H-Sync
    if (InfoNES_HSync() == -1) //quit was called
      return; // To the menu screen

    // HSYNC Wait
    InfoNES_Wait();
  }
}

/*===================================================================*/
/*                                                                   */
/*              InfoNES_HSync() : A function in H-Sync               */
/*                                                                   */
/*===================================================================*/
int __not_in_flash_func(InfoNES_HSync)()
{
  /*
   *  A function in H-Sync
   *
   *  Return values
   *    0 : Normally
   *   -1 : Exit an emulation
   */

  const uint64_t hsync_start_us = time_us_64();
  uint64_t apu_us = 0;
  uint64_t draw_us = 0;

  const uint64_t apu_start_us = time_us_64();
  InfoNES_pAPUHsync(!APU_Mute);
  apu_us = time_us_64() - apu_start_us;
  g_perf_apu_us += apu_us;
  //util::WorkMeterMark(MARKER_SOUND);

  // int tmpv = (PPU_Addr >> 12) + ((PPU_Addr >> 5) << 3);
  // tmpv -= PPU_Scanline >= 240 ? 0 : PPU_Scanline;
  // PPU_Scr_V_Bit = tmpv & 7;
  // PPU_Scr_V_Byte = (tmpv >> 3) & 31;
  PPU_Scr_H_Byte = PPU_Addr & 31;
  PPU_NameTableBank = NAME_TABLE0 + ((PPU_Addr >> 10) & 3);
  /*-------------------------------------------------------------------*/
  /*  Render a scanline                                                */
  /*-------------------------------------------------------------------*/
  if (FrameCnt == 0 &&
      PPU_ScanTable[PPU_Scanline] == SCAN_ON_SCREEN)
  {
      const uint64_t draw_start_us = time_us_64();
      InfoNES_PreDrawLine(PPU_Scanline);
    if (PPU_Scanline >= 4 && PPU_Scanline < 240 - 4)
    {
    
      InfoNES_DrawLine();
     
    } else {
      // Array out of bounds, WorkLine size is equals to NES_DISP_WIDTH << 1 = 512
      //InfoNES_MemorySet(WorkLine, 0, 640);
      InfoNES_MemorySet(WorkLine, 0, NES_DISP_WIDTH << 1);
    }
     InfoNES_PostDrawLine(PPU_Scanline, false);
     draw_us = time_us_64() - draw_start_us;
     g_perf_draw_us += draw_us;
    //  if (PPU_Scanline >=240) {
    //   printf("hello");
    //  }
    // todo: 描画しないラインにもスプライトオーバーレジスタとかは反映する必要がある
  }

  //util::WorkMeterReset(); // 計測起点はここ

  /*-------------------------------------------------------------------*/
  /*  Set new scroll values                                            */
  /*-------------------------------------------------------------------*/

  //  PPU_Scr_V = PPU_Scr_V_Next;
  // PPU_Scr_V_Byte = PPU_Scr_V_Byte_Next;
  // PPU_Scr_V_Bit = PPU_Scr_V_Bit_Next;

  //  PPU_Scr_H = PPU_Scr_H_Next;
  // PPU_Scr_H_Byte = PPU_Scr_H_Byte_Next;
  // PPU_Scr_H_Bit = PPU_Scr_H_Bit_Next;

  if ((PPU_R1 & R1_SHOW_SP) || (PPU_R1 & R1_SHOW_SCR))
  {
    if (PPU_Scanline == SCAN_VBLANK_END)
    {
      PPU_Addr = PPU_Temp;
    }
    else if (PPU_Scanline < SCAN_UNKNOWN_START)
    {
      PPU_Addr = (PPU_Addr & ~0b10000011111) |
                 (PPU_Temp & 0b10000011111);

      int v = (PPU_Addr >> 12) | ((PPU_Addr >> 2) & (31 << 3));
      if (v == 29 * 8 + 7)
      {
        v = 0;
        PPU_Addr ^= 0x800;
      }
      else if (v == 31 * 8 + 7)
      {
        v = 0;
      }
      else
        ++v;
      PPU_Addr = (PPU_Addr & ~0b111001111100000) |
                 ((v & 7) << 12) | (((v >> 3) & 31) << 5);
    }
  }

  /*-------------------------------------------------------------------*/
  /*  Next Scanline                                                    */
  /*-------------------------------------------------------------------*/
  PPU_Scanline = (PPU_Scanline == SCAN_VBLANK_END) ? 0 : PPU_Scanline + 1;

  /*-------------------------------------------------------------------*/
  /*  Operation in the specific scanning line                          */
  /*-------------------------------------------------------------------*/
  switch (PPU_Scanline)
  {
  case SCAN_TOP_OFF_SCREEN:
    // Reset a PPU status
    PPU_R2 = 0;
    // Set up a character data
    if (NesHeader.byVRomSize == 0 && FrameCnt == 0)
      InfoNES_SetupChr();

    // Get position of sprite #0
    InfoNES_GetSprHitY();
    break;

  case SCAN_UNKNOWN_START:
    if (FrameCnt == 0)
    {
      // Transfer the contents of work frame on the screen
      const uint64_t load_frame_start_us = time_us_64();
      InfoNES_LoadFrame();
      g_perf_load_frame_us += time_us_64() - load_frame_start_us;
      perf_note_frame(time_us_64());
      ++g_perf_frames;

#if 0
        // Switching of the double buffer
        WorkFrameIdx = 1 - WorkFrameIdx;
        WorkFrame = DoubleFrame[ WorkFrameIdx ];
#endif
    }
    break;

  case SCAN_VBLANK_START:
    // FrameCnt + 1
    FrameCnt = (FrameCnt >= FrameSkip) ? 0 : FrameCnt + 1;

    // Set a V-Blank flag
    PPU_R2 |= R2_IN_VBLANK;
    // printf("vb : pc %04x, r2 %02x\n", PC, PPU_R2);

    // Reset latch flag
    // PPU_Latch_Flag = 0;

    // pAPU Sound function in V-Sync
    // if (!APU_Mute)
    InfoNES_pAPUVsync();

    // A mapper function in V-Sync
    const uint64_t mapper_vsync_start_us = time_us_64();
    MapperVSync();
    g_perf_mapper_vsync_us += time_us_64() - mapper_vsync_start_us;

    // Get the condition of the joypad
    perf_note_pad_poll(time_us_64());
    InfoNES_PadState(&PAD1_Latch, &PAD2_Latch, &PAD_System);
#if INFONES_ENABLE_BOKOSUKA_STATE_LOG
    InfoNES_BokosukaHeartbeat();
#endif

    if (PAD_PUSH(PAD_System, PAD_SYS_SCREENSHOT))
    {
      nesco_request_screenshot();
    }

    nesco_maybe_start_screenshot_on_vblank();

    // NMI on V-Blank
    if (PPU_R0 & R0_NMI_VB)
    {
      //      printf("nmi %04x %02x\n", PC, PPU_R0);
      structured_log_note_nmi_request();
      NMI_REQ;
    }

    // Exit an emulation if a QUIT button is pushed
    
    if (PAD_PUSH(PAD_System, PAD_SYS_QUIT))
    {
#if INFONES_ENABLE_BOKOSUKA_STATE_LOG
      boko_flash_trace_dump_to_sd("esc");
#endif
      return -1; // Exit an emulation
    }

    if (PAD_PUSH(PAD_System, PAD_SYS_RESET))
    {
      InfoNES_Reset();
      return 0;
    }

    if (PAD_PUSH(PAD_System, PAD_SYS_VIEW_TOGGLE))
    {
      display_toggle_nes_view_scale();
    }

    break;
  }

  const uint64_t hsync_end_us = time_us_64();
  g_perf_tail_us += (hsync_end_us - hsync_start_us) - apu_us - draw_us;
  ++g_perf_scanlines;
  audio_debug_poll();
  perf_log_if_due(hsync_end_us);

  // Successful
  return 0;
}

//#pragma GCC optimize("O2")

namespace
{
  struct BgTileDescriptor
  {
    const BYTE *pattern_row;
    const WORD *pal;
    WORD *dst;
    BYTE *dst_opaque;
    BYTE clip_left;
    BYTE clip_right;
  };

  static inline void __not_in_flash_func(renderPacked4)(WORD *dst,
                                                        BYTE *dst_opaque,
                                                        const WORD *pal,
                                                        BYTE packed,
                                                        BYTE opaque_mask,
                                                        int base_sx,
                                                        int clip_left,
                                                        int clip_right,
                                                        int *out)
  {
    const int start = clip_left > base_sx ? clip_left : base_sx;
    const int end = clip_right < (base_sx + 4) ? clip_right : (base_sx + 4);

    for (int sx = start; sx < end; ++sx, ++(*out))
    {
      const int local = sx - base_sx;
      const int idx_shift = 6 - (local << 1);
      const int opaque_shift = 3 - local;
      const BYTE idx = (BYTE)((packed >> idx_shift) & 0x03u);
      dst[*out] = pal[idx];
      dst_opaque[*out] = (BYTE)((opaque_mask >> opaque_shift) & 0x01u);
    }
  }

  static inline void __not_in_flash_func(renderBgTile)(const BgTileDescriptor &desc)
  {
    const BYTE pl0 = desc.pattern_row[0];
    const BYTE pl1 = desc.pattern_row[8];
    WORD *dst = desc.dst;
    BYTE *dst_opaque = desc.dst_opaque;
    const BYTE packed_hi = g_bg_tile_pair_idx4[((pl0 & 0xF0u)) | (pl1 >> 4)];
    const BYTE opaque_hi = g_bg_tile_pair_opaque4[((pl0 & 0xF0u)) | (pl1 >> 4)];
    const BYTE packed_lo = g_bg_tile_pair_idx4[((pl0 & 0x0Fu) << 4) | (pl1 & 0x0Fu)];
    const BYTE opaque_lo = g_bg_tile_pair_opaque4[((pl0 & 0x0Fu) << 4) | (pl1 & 0x0Fu)];

    if (desc.clip_left == 0 && desc.clip_right == 8)
    {
      dst[0] = desc.pal[(packed_hi >> 6) & 0x03u];
      dst[1] = desc.pal[(packed_hi >> 4) & 0x03u];
      dst[2] = desc.pal[(packed_hi >> 2) & 0x03u];
      dst[3] = desc.pal[packed_hi & 0x03u];
      dst[4] = desc.pal[(packed_lo >> 6) & 0x03u];
      dst[5] = desc.pal[(packed_lo >> 4) & 0x03u];
      dst[6] = desc.pal[(packed_lo >> 2) & 0x03u];
      dst[7] = desc.pal[packed_lo & 0x03u];

      dst_opaque[0] = (BYTE)((opaque_hi >> 3) & 0x01u);
      dst_opaque[1] = (BYTE)((opaque_hi >> 2) & 0x01u);
      dst_opaque[2] = (BYTE)((opaque_hi >> 1) & 0x01u);
      dst_opaque[3] = (BYTE)(opaque_hi & 0x01u);
      dst_opaque[4] = (BYTE)((opaque_lo >> 3) & 0x01u);
      dst_opaque[5] = (BYTE)((opaque_lo >> 2) & 0x01u);
      dst_opaque[6] = (BYTE)((opaque_lo >> 1) & 0x01u);
      dst_opaque[7] = (BYTE)(opaque_lo & 0x01u);
      return;
    }

    int out = 0;
    renderPacked4(dst,
                  dst_opaque,
                  desc.pal,
                  packed_hi,
                  opaque_hi,
                  0,
                  desc.clip_left,
                  desc.clip_right,
                  &out);
    renderPacked4(dst,
                  dst_opaque,
                  desc.pal,
                  packed_lo,
                  opaque_lo,
                  4,
                  desc.clip_left,
                  desc.clip_right,
                  &out);
  }

  void __not_in_flash_func(compositeSprite)(const uint16_t *pal,
                                            const uint8_t *spr,
                                            const uint8_t *bgOpaque,
                                            uint16_t *buf)
  {
    auto sprEnd = spr + NES_DISP_WIDTH;
    do
    {
      auto proc = [=](int i) __attribute__((always_inline))
      {
        int v = spr[i];
        if (v && ((v >> 7) || !bgOpaque[i]))
        {
          buf[i] = pal[v & 0xf];
        }
      };

#if 1
      proc(0);
      proc(1);
      proc(2);
      proc(3);
      buf += 4;
      spr += 4;
      bgOpaque += 4;
#else
      proc(0);
      buf += 1;
      spr += 1;
      bgOpaque += 1;
#endif
    } while (spr < sprEnd);
  }
}

/*===================================================================*/
/*                                                                   */
/*              InfoNES_DrawLine() : Render a scanline               */
/*                                                                   */
/*===================================================================*/
void __not_in_flash_func(InfoNES_DrawLine)()
{
  /*
   *  Render a scanline
   *
   */

  int nX;
  int nY;
  int nY4;
  int nYBit;
  WORD *pPalTbl;
  BYTE *pAttrBase;
  WORD *pPoint;
  BYTE *pOpaquePoint;
  int nNameTable;
  BYTE *pbyNameTable;
  BYTE *pbyChrData;
  BYTE *pSPRRAM;
  int nAttr;
  int nSprCnt;
  int nIdx;
  int nSprData;
  BYTE bySprCol;
  BYTE pSprBuf[NES_DISP_WIDTH + 7];
  uint64_t bg_start_us = 0;
  uint64_t bg_mapper_start_us = 0;
  uint64_t bg_clear_start_us = 0;
  uint64_t bg_setup_start_us = 0;
  uint64_t bg_tile_start_us = 0;
  uint64_t bg_clip_start_us = 0;
  uint64_t sprite_start_us = 0;

  /*-------------------------------------------------------------------*/
  /*  Render Background                                                */
  /*-------------------------------------------------------------------*/

  if constexpr (kPerfLogToSerial)
  {
    bg_start_us = time_us_64();
  }

  /* MMC5 VROM switch */
  if constexpr (kPerfLogToSerial)
  {
    bg_mapper_start_us = time_us_64();
  }
  MapperRenderScreen(1);
  if constexpr (kPerfLogToSerial)
  {
    g_perf_ppu_bg_mapper_us += time_us_64() - bg_mapper_start_us;
  }

  // Pointer to the render position
  //  pPoint = &WorkFrame[PPU_Scanline * NES_DISP_WIDTH];
  assert(WorkLine);
  pPoint = WorkLine;
  pOpaquePoint = BackgroundOpaqueLine;
  if constexpr (kPerfLogToSerial)
  {
    bg_clear_start_us = time_us_64();
  }
  InfoNES_MemorySet(BackgroundOpaqueLine, 0, NES_DISP_WIDTH);
  if constexpr (kPerfLogToSerial)
  {
    g_perf_ppu_bg_clear_us += time_us_64() - bg_clear_start_us;
  }

  // Clear a scanline if screen is off
  if (!(PPU_R1 & R1_SHOW_SCR))
  {
    if constexpr (kPerfLogToSerial)
    {
      bg_clear_start_us = time_us_64();
    }
    InfoNES_MemorySet(pPoint, 0, NES_DISP_WIDTH << 1);
    if constexpr (kPerfLogToSerial)
    {
      g_perf_ppu_bg_clear_us += time_us_64() - bg_clear_start_us;
    }
  }
  else
  {
    if constexpr (kPerfLogToSerial)
    {
      bg_setup_start_us = time_us_64();
    }
    nNameTable = PPU_NameTableBank;

#if 0
    nY = PPU_Scr_V_Byte + (PPU_Scanline >> 3);
    nYBit = PPU_Scr_V_Bit + (PPU_Scanline & 7);

    if (nYBit > 7)
    {
      ++nY;
      nYBit &= 7;
    }
    const int yOfsModBG = nYBit;
    nYBit <<= 3;

    if (nY > 29)
    {
      // Next NameTable (An up-down direction)
      nNameTable ^= NAME_TABLE_V_MASK;
      nY -= 30;
    }
#else
    nY = (PPU_Addr >> 5) & 31;
    const int yOfsModBG = PPU_Addr >> 12;
    nYBit = yOfsModBG << 3;
#endif

    nX = PPU_Scr_H_Byte;

    nY4 = ((nY & 2) << 1);

    //
    const int patternTableIdBG = PPU_R0 & R0_BG_ADDR ? 1 : 0;
    const int bankOfsBG = patternTableIdBG << 2;
    BYTE *pCachedAttrBase = nullptr;
    int nCachedAttrGroup = -1;
    int nCachedAttrHalf = -1;
    pPalTbl = nullptr;

    auto resolveBgPal = [&](BYTE *attrBase,
                            int tileX) -> WORD *
    {
      const int attrGroup = tileX >> 2;
      const int attrHalf = tileX & 2;
      if (attrBase != pCachedAttrBase ||
          attrGroup != nCachedAttrGroup ||
          attrHalf != nCachedAttrHalf)
      {
        pCachedAttrBase = attrBase;
        nCachedAttrGroup = attrGroup;
        nCachedAttrHalf = attrHalf;
        pPalTbl = &PalTable[(((attrBase[attrGroup] >> (attrHalf + nY4)) & 3) << 2)];
      }
      return pPalTbl;
    };

    auto buildBgTile = [&](BYTE *nameTablePtr,
                           WORD *pal,
                           WORD *dst,
                           BYTE *dstOpaque,
                           int clipLeft,
                           int clipRight) -> BgTileDescriptor
    {
      BgTileDescriptor desc;
      const int ch = *nameTablePtr;
      const int bank = (ch >> 6) + bankOfsBG;
      const int addrOfs = ((ch & 63) << 4) + yOfsModBG;

      desc.pattern_row = PPUBANK[bank] + addrOfs;
      desc.pal = pal;
      desc.dst = dst;
      desc.dst_opaque = dstOpaque;
      desc.clip_left = (BYTE)clipLeft;
      desc.clip_right = (BYTE)clipRight;
      return desc;
    };

    auto emitBgTile = [&](BYTE *nameTablePtr,
                          BYTE *attrBase,
                          int tileX,
                          WORD *dst,
                          BYTE *dstOpaque,
                          int clipLeft,
                          int clipRight)
    {
      WORD *pal = resolveBgPal(attrBase, tileX);
      BgTileDescriptor desc =
          buildBgTile(nameTablePtr, pal, dst, dstOpaque, clipLeft, clipRight);
      renderBgTile(desc);
      pbyChrData = const_cast<BYTE *>(desc.pattern_row);
      MapperPPU(PATTBL(pbyChrData));
    };

    /*-------------------------------------------------------------------*/
    /*  Rendering of the block of the left end                           */
    /*-------------------------------------------------------------------*/

    pbyNameTable = PPUBANK[nNameTable] + nY * 32 + nX;
    pAttrBase = PPUBANK[nNameTable] + 0x3c0 + (nY / 4) * 8;
    if constexpr (kPerfLogToSerial)
    {
      g_perf_ppu_bg_setup_us += time_us_64() - bg_setup_start_us;
      bg_tile_start_us = time_us_64();
    }
    emitBgTile(pbyNameTable,
               pAttrBase,
               nX,
               pPoint,
               pOpaquePoint,
               PPU_Scr_H_Bit,
               8);
    pPoint += 8 - PPU_Scr_H_Bit;
    pOpaquePoint += 8 - PPU_Scr_H_Bit;

    ++nX;
    ++pbyNameTable;

    /*-------------------------------------------------------------------*/
    /*  Rendering of the left table                                      */
    /*-------------------------------------------------------------------*/

    for (; nX < 32; ++nX)
    {
      emitBgTile(pbyNameTable,
                 pAttrBase,
                 nX,
                 pPoint,
                 pOpaquePoint,
                 0,
                 8);
      pPoint += 8;
      pOpaquePoint += 8;

      ++pbyNameTable;
    }

    // Holizontal Mirror
    nNameTable ^= NAME_TABLE_H_MASK;

    pbyNameTable = PPUBANK[nNameTable] + nY * 32;
    pAttrBase = PPUBANK[nNameTable] + 0x3c0 + (nY / 4) * 8;
    pCachedAttrBase = nullptr;
    nCachedAttrGroup = -1;
    nCachedAttrHalf = -1;
    pPalTbl = nullptr;

    /*-------------------------------------------------------------------*/
    /*  Rendering of the right table                                     */
    /*-------------------------------------------------------------------*/

    for (nX = 0; nX < PPU_Scr_H_Byte; ++nX)
    {
      emitBgTile(pbyNameTable,
                 pAttrBase,
                 nX,
                 pPoint,
                 pOpaquePoint,
                 0,
                 8);
      pPoint += 8;
      pOpaquePoint += 8;

      ++pbyNameTable;
    }

    /*-------------------------------------------------------------------*/
    /*  Rendering of the block of the right end                          */
    /*-------------------------------------------------------------------*/

    emitBgTile(pbyNameTable,
               pAttrBase,
               nX,
               pPoint,
               pOpaquePoint,
               0,
               PPU_Scr_H_Bit);
    if constexpr (kPerfLogToSerial)
    {
      g_perf_ppu_bg_tile_us += time_us_64() - bg_tile_start_us;
      bg_clip_start_us = time_us_64();
    }

    /*-------------------------------------------------------------------*/
    /*  Backgroud Clipping                                               */
    /*-------------------------------------------------------------------*/
    if (!(PPU_R1 & R1_CLIP_BG))
    {
      WORD *pPointTop;

      // pPointTop = &WorkFrame[PPU_Scanline * NES_DISP_WIDTH];
      pPointTop = WorkLine;
      InfoNES_MemorySet(pPointTop, 0, 8 << 1);
      InfoNES_MemorySet(BackgroundOpaqueLine, 0, 8);
    }

    /*-------------------------------------------------------------------*/
    /*  Clear a scanline if up and down clipping flag is set             */
    /*-------------------------------------------------------------------*/
    if (PPU_UpDown_Clip &&
        (SCAN_ON_SCREEN_START > PPU_Scanline || PPU_Scanline > SCAN_BOTTOM_OFF_SCREEN_START))
    {
      WORD *pPointTop;

      // pPointTop = &WorkFrame[PPU_Scanline * NES_DISP_WIDTH];
      pPointTop = WorkLine;
      InfoNES_MemorySet(pPointTop, 0, NES_DISP_WIDTH << 1);
      InfoNES_MemorySet(BackgroundOpaqueLine, 0, NES_DISP_WIDTH);
    }
    if constexpr (kPerfLogToSerial)
    {
      g_perf_ppu_bg_clip_us += time_us_64() - bg_clip_start_us;
    }
  }

  //util::WorkMeterMark(MARKER_BG);
  if constexpr (kPerfLogToSerial)
  {
    g_perf_ppu_bg_us += time_us_64() - bg_start_us;
    sprite_start_us = time_us_64();
  }

  /*-------------------------------------------------------------------*/
  /*  Render a sprite                                                  */
  /*-------------------------------------------------------------------*/

  /* MMC5 VROM switch */
  MapperRenderScreen(0);

  if (PPU_R1 & R1_SHOW_SP)
  {
    // Reset Scanline Sprite Count
    PPU_R2 &= ~R2_MAX_SP;

    // Reset sprite buffer
    InfoNES_MemorySet(pSprBuf, 0, sizeof pSprBuf);

    const int patternTableIdSP88 = PPU_R0 & R0_SP_ADDR ? 1 : 0;
    const int bankOfsSP88 = patternTableIdSP88 << 2;

    // Render a sprite to the sprite buffer
    nSprCnt = 0;
    for (pSPRRAM = SPRRAM + (63 << 2); pSPRRAM >= SPRRAM; pSPRRAM -= 4)
    {
      nY = pSPRRAM[SPR_Y] + 1;
      if (nY > PPU_Scanline || nY + PPU_SP_Height <= PPU_Scanline)
        continue; // Next sprite

      /*-------------------------------------------------------------------*/
      /*  A sprite in scanning line                                        */
      /*-------------------------------------------------------------------*/

      // Holizontal Sprite Count +1
      ++nSprCnt;

      nAttr = pSPRRAM[SPR_ATTR];
      nYBit = PPU_Scanline - nY;
      nYBit = (nAttr & SPR_ATTR_V_FLIP) ? (PPU_SP_Height - nYBit - 1) : nYBit;
      const int yOfsModSP = nYBit;
      nYBit <<= 3;

#if 0
      if (PPU_R0 & R0_SP_SIZE)
      {
        // Sprite size 8x16
        if (pSPRRAM[SPR_CHR] & 1)
        {
          pbyChrData = ChrBuf + 256 * 64 + ((pSPRRAM[SPR_CHR] & 0xfe) << 6) + nYBit;
        }
        else
        {
          pbyChrData = ChrBuf + ((pSPRRAM[SPR_CHR] & 0xfe) << 6) + nYBit;
        }
      }
      else
      {
        // Sprite size 8x8
        pbyChrData = PPU_SP_Base + (pSPRRAM[SPR_CHR] << 6) + nYBit;
      }

      nAttr ^= SPR_ATTR_PRI;
      bySprCol = (nAttr & (SPR_ATTR_COLOR | SPR_ATTR_PRI)) << 2;
      nX = pSPRRAM[SPR_X];

      if (nAttr & SPR_ATTR_H_FLIP)
      {
        // Horizontal flip
        if (pbyChrData[7])
          pSprBuf[nX] = bySprCol | pbyChrData[7];
        if (pbyChrData[6])
          pSprBuf[nX + 1] = bySprCol | pbyChrData[6];
        if (pbyChrData[5])
          pSprBuf[nX + 2] = bySprCol | pbyChrData[5];
        if (pbyChrData[4])
          pSprBuf[nX + 3] = bySprCol | pbyChrData[4];
        if (pbyChrData[3])
          pSprBuf[nX + 4] = bySprCol | pbyChrData[3];
        if (pbyChrData[2])
          pSprBuf[nX + 5] = bySprCol | pbyChrData[2];
        if (pbyChrData[1])
          pSprBuf[nX + 6] = bySprCol | pbyChrData[1];
        if (pbyChrData[0])
          pSprBuf[nX + 7] = bySprCol | pbyChrData[0];
      }
      else
      {
        // Non flip
        if (pbyChrData[0])
          pSprBuf[nX] = bySprCol | pbyChrData[0];
        if (pbyChrData[1])
          pSprBuf[nX + 1] = bySprCol | pbyChrData[1];
        if (pbyChrData[2])
          pSprBuf[nX + 2] = bySprCol | pbyChrData[2];
        if (pbyChrData[3])
          pSprBuf[nX + 3] = bySprCol | pbyChrData[3];
        if (pbyChrData[4])
          pSprBuf[nX + 4] = bySprCol | pbyChrData[4];
        if (pbyChrData[5])
          pSprBuf[nX + 5] = bySprCol | pbyChrData[5];
        if (pbyChrData[6])
          pSprBuf[nX + 6] = bySprCol | pbyChrData[6];
        if (pbyChrData[7])
          pSprBuf[nX + 7] = bySprCol | pbyChrData[7];
      }
#else
      int ch = pSPRRAM[SPR_CHR];

      int bankOfs;
      if (PPU_R0 & R0_SP_SIZE)
      {
        // 8x16
        bankOfs = (ch & 1) << 2;
        ch &= 0xfe;
      }
      else
      {
        // 8x8
        bankOfs = bankOfsSP88;
      }

      const int bank = (ch >> 6) + bankOfs;
      const int addrOfs = ((ch & 63) << 4) + ((yOfsModSP & 8) << 1) + (yOfsModSP & 7);
      const auto data = PPUBANK[bank] + addrOfs;
      const uint32_t pl0 = data[0];
      const uint32_t pl1 = data[8];
      const auto pat0 = ((pl0 & 0x55) << 24) | ((pl1 & 0x55) << 25);
      const auto pat1 = ((pl0 & 0xaa) << 23) | ((pl1 & 0xaa) << 24);

      nAttr ^= SPR_ATTR_PRI;
      bySprCol = (nAttr & (SPR_ATTR_COLOR | SPR_ATTR_PRI)) << 2;
      nX = pSPRRAM[SPR_X];
      const auto dst = pSprBuf + nX;

      if (nAttr & SPR_ATTR_H_FLIP)
      {
        // h flip
        if (int v = (pat1 << 0) >> 30)
        {
          dst[7] = bySprCol | v;
        }
        if (int v = (pat0 << 0) >> 30)
        {
          dst[6] = bySprCol | v;
        }
        if (int v = (pat1 << 2) >> 30)
        {
          dst[5] = bySprCol | v;
        }
        if (int v = (pat0 << 2) >> 30)
        {
          dst[4] = bySprCol | v;
        }
        if (int v = (pat1 << 4) >> 30)
        {
          dst[3] = bySprCol | v;
        }
        if (int v = (pat0 << 4) >> 30)
        {
          dst[2] = bySprCol | v;
        }
        if (int v = (pat1 << 6) >> 30)
        {
          dst[1] = bySprCol | v;
        }
        if (int v = (pat0 << 6) >> 30)
        {
          dst[0] = bySprCol | v;
        }
      }
      else
      {
        // non flip
        if (int v = (pat1 << 0) >> 30)
        {
          dst[0] = bySprCol | v;
        }
        if (int v = (pat0 << 0) >> 30)
        {
          dst[1] = bySprCol | v;
        }
        if (int v = (pat1 << 2) >> 30)
        {
          dst[2] = bySprCol | v;
        }
        if (int v = (pat0 << 2) >> 30)
        {
          dst[3] = bySprCol | v;
        }
        if (int v = (pat1 << 4) >> 30)
        {
          dst[4] = bySprCol | v;
        }
        if (int v = (pat0 << 4) >> 30)
        {
          dst[5] = bySprCol | v;
        }
        if (int v = (pat1 << 6) >> 30)
        {
          dst[6] = bySprCol | v;
        }
        if (int v = (pat0 << 6) >> 30)
        {
          dst[7] = bySprCol | v;
        }
      }
#endif
    }

    // Rendering sprite
    pPoint = WorkLine;
    //   pPoint -= (NES_DISP_WIDTH - PPU_Scr_H_Bit);

#if 1
    compositeSprite(PalTable + 0x10, pSprBuf, BackgroundOpaqueLine, pPoint);
#else
    {
      const auto *pal = &PalTable[0x10];
      const auto *spr = pSprBuf;
      const auto *sprEnd = spr + NES_DISP_WIDTH;
      // for (nX = 0; nX < NES_DISP_WIDTH; ++nX)
      while (spr != sprEnd)
      {
        // nSprData = pSprBuf[nX];
        auto proc = [=](int i) __attribute__((always_inline))
        {
          int v = spr[i];
          if (v && ((v >> 7) || (pPoint[i] >> 15)))
          {
            pPoint[i] = pal[v & 0xf];
          }
        };

#if 1
        proc(0);
        proc(1);
        proc(2);
        proc(3);
        pPoint += 4;
        spr += 4;
#else
        proc(0);
        pPoint += 1;
        spr += 1;
#endif
      }
    }
#endif

    /*-------------------------------------------------------------------*/
    /*  Sprite Clipping                                                  */
    /*-------------------------------------------------------------------*/
    if (!(PPU_R1 & R1_CLIP_SP))
    {
      WORD *pPointTop;

      // pPointTop = &WorkFrame[PPU_Scanline * NES_DISP_WIDTH];
      pPointTop = WorkLine;
      InfoNES_MemorySet(pPointTop, 0, 8 << 1);
    }

    if (nSprCnt >= 8)
      PPU_R2 |= R2_MAX_SP; // Set a flag of maximum sprites on scanline

    //util::WorkMeterMark(MARKER_SPRITE);
  }

  if constexpr (kPerfLogToSerial)
  {
    g_perf_ppu_sprite_us += time_us_64() - sprite_start_us;
  }
}

/*===================================================================*/
/*                                                                   */
/* InfoNES_GetSprHitY() : Get a position of scanline hits sprite #0  */
/*                                                                   */
/*===================================================================*/
void __not_in_flash_func(InfoNES_GetSprHitY)()
{
  /*
   * Get a position of scanline hits sprite #0
   *
   */

#if 0
  int nYBit;
  DWORD *pdwChrData;
  int nOff;

  if (SPRRAM[SPR_ATTR] & SPR_ATTR_V_FLIP)
  {
    // Vertical flip
    nYBit = (PPU_SP_Height - 1) << 3;
    nOff = -2;
  }
  else
  {
    // Non flip
    nYBit = 0;
    nOff = 2;
  }

  if (PPU_R0 & R0_SP_SIZE)
  {
    // Sprite size 8x16
    if (SPRRAM[SPR_CHR] & 1)
    {
      pdwChrData = (DWORD *)(ChrBuf + 256 * 64 + ((SPRRAM[SPR_CHR] & 0xfe) << 6) + nYBit);
    }
    else
    {
      pdwChrData = (DWORD *)(ChrBuf + ((SPRRAM[SPR_CHR] & 0xfe) << 6) + nYBit);
    }
  }
  else
  {
    // Sprite size 8x8
    pdwChrData = (DWORD *)(PPU_SP_Base + (SPRRAM[SPR_CHR] << 6) + nYBit);
  }

  if ((SPRRAM[SPR_Y] + 1 <= SCAN_UNKNOWN_START) && (SPRRAM[SPR_Y] > 0))
  {
    for (int nLine = 0; nLine < PPU_SP_Height; nLine++)
    {
      if (pdwChrData[0] | pdwChrData[1])
      {
        // Scanline hits sprite #0
        SpriteJustHit = SPRRAM[SPR_Y] + 1 + nLine;
        nLine = SCAN_VBLANK_END;
      }
      pdwChrData += nOff;
    }
  }
  else
  {
    // Scanline didn't hit sprite #0
    SpriteJustHit = SCAN_UNKNOWN_START + 1;
  }
#else
  const int patternTableIdSP88 = PPU_R0 & R0_SP_ADDR ? 1 : 0;
  const int bankOfsSP88 = patternTableIdSP88 << 2;

  int yOfsMod;
  int stride;
  if (SPRRAM[SPR_ATTR] & SPR_ATTR_V_FLIP)
  {
    // Vertical flip
    yOfsMod = PPU_SP_Height - 1;
    stride = -1;
  }
  else
  {
    // Non flip
    yOfsMod = 0;
    stride = 1;
  }

  int ch = SPRRAM[SPR_CHR];

  int bankOfs;
  if (PPU_R0 & R0_SP_SIZE)
  {
    // 8x16
    bankOfs = (ch & 1) << 2;
    ch &= 0xfe;
  }
  else
  {
    // 8x8
    bankOfs = bankOfsSP88;
  }

  const int bank = (ch >> 6) + bankOfs;
  const int addrOfs = ((ch & 63) << 4) + ((yOfsMod & 8) << 1) + (yOfsMod & 7);

  auto *data = PPUBANK[bank] + addrOfs;

  if ((SPRRAM[SPR_Y] + 1 <= SCAN_UNKNOWN_START) && (SPRRAM[SPR_Y] > 0))
  {
    for (int nLine = 0; nLine < PPU_SP_Height; nLine++)
    {
      if (data[0] | data[8])
      {
        // Scanline hits sprite #0
        SpriteJustHit = SPRRAM[SPR_Y] + 1 + nLine;
        nLine = SCAN_VBLANK_END;
      }
      data += stride;
    }
  }
  else
  {
    // Scanline didn't hit sprite #0
    SpriteJustHit = SCAN_UNKNOWN_START + 1;
  }

#endif
}

/*===================================================================*/
/*                                                                   */
/*            InfoNES_SetupChr() : Develop character data            */
/*                                                                   */
/*===================================================================*/
void __not_in_flash_func(InfoNES_SetupChr)()
{
  /*
   *  Develop character data
   *
   */

#if 0
  BYTE *pbyBGData;
  BYTE byData1;
  BYTE byData2;
  int nIdx;
  int nY;
  int nOff;
  static BYTE *pbyPrevBank[8];
  int nBank;

  for (nBank = 0; nBank < 8; ++nBank)
  {
    if (pbyPrevBank[nBank] == PPUBANK[nBank] && !((ChrBufUpdate >> nBank) & 1))
      continue; // Next bank

    /*-------------------------------------------------------------------*/
    /*  An address is different from the last time                       */
    /*    or                                                             */
    /*  An update flag is being set                                      */
    /*-------------------------------------------------------------------*/

    for (nIdx = 0; nIdx < 64; ++nIdx)
    {
      nOff = (nBank << 12) + (nIdx << 6);

      for (nY = 0; nY < 8; ++nY)
      {
        pbyBGData = PPUBANK[nBank] + (nIdx << 4) + nY;

        byData1 = ((pbyBGData[0] >> 1) & 0x55) | (pbyBGData[8] & 0xAA);
        byData2 = (pbyBGData[0] & 0x55) | ((pbyBGData[8] << 1) & 0xAA);

        ChrBuf[nOff] = (byData1 >> 6) & 3;
        ChrBuf[nOff + 1] = (byData2 >> 6) & 3;
        ChrBuf[nOff + 2] = (byData1 >> 4) & 3;
        ChrBuf[nOff + 3] = (byData2 >> 4) & 3;
        ChrBuf[nOff + 4] = (byData1 >> 2) & 3;
        ChrBuf[nOff + 5] = (byData2 >> 2) & 3;
        ChrBuf[nOff + 6] = byData1 & 3;
        ChrBuf[nOff + 7] = byData2 & 3;

        nOff += 8;
      }
    }
    // Keep this address
    pbyPrevBank[nBank] = PPUBANK[nBank];
  }

  // Reset update flag
  ChrBufUpdate = 0;
#endif
}
