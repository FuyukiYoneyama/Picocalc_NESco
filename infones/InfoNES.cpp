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
#include "display.h"
#include "runtime_log.h"

#include <cstdint>

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

#if defined(NESCO_MAPPER19_IRQ_DIAGNOSTICS)
#include <cstring>

static int g_mapper19_irq_fixture_kind = 0;
static bool g_mapper19_irq_fixture_reported = false;

static int mapper19_irq_fixture_kind_for_path(const char *path,
                                              BYTE mapper,
                                              BYTE prg16,
                                              BYTE chr8)
{
  if (mapper != 19 || prg16 != 2 || chr8 != 1 || !path)
  {
    return 0;
  }

  const char *basename = path;
  for (const char *p = path; *p != '\0'; ++p)
  {
    if (*p == '/' || *p == '\\')
    {
      basename = p + 1;
    }
  }

  if (std::strcmp(basename, "n163_irq_cpu_cycle.nes") == 0)
    return 1;
  if (std::strcmp(basename, "n163_irq_post_ack_test.nes") == 0)
    return 2;
  return 0;
}
#endif

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

constexpr int kSpriteActiveListMaxEntries = 64 * 16;
BYTE g_sprite_active_indices[kSpriteActiveListMaxEntries];
uint16_t g_sprite_active_offsets[NES_DISP_HEIGHT + 1];
bool g_sprite_active_list_valid = false;

inline void initBgTileRenderLut()
{
  for (int pl0 = 0; pl0 < 16; ++pl0)
  {
    for (int pl1 = 0; pl1 < 16; ++pl1)
    {
      BYTE packed = 0;
      for (int i = 0; i < 4; ++i)
      {
        const BYTE idx =
            (BYTE)(((pl0 >> (3 - i)) & 1u) | (((pl1 >> (3 - i)) & 1u) << 1));
        packed |= (BYTE)(idx << (6 - (i << 1)));
      }

      const int key = (pl0 << 4) | pl1;
      g_bg_tile_pair_idx4[key] = packed;
    }
  }
}

constexpr bool kPerfLogToSerial =
#if defined(NESCO_CORE1_BASELINE_LOG) || defined(NESCO_BG_TILE_SHARE_LOG)
    true;
#else
    false;
#endif
constexpr bool kBgTileShareTiming =
#if defined(NESCO_BG_TILE_SHARE_LOG)
    true;
#else
    false;
#endif
constexpr bool kDetailedPerfLogToSerial = false;
constexpr bool kSpriteActiveListMetrics =
#if defined(NESCO_SPRITE_ACTIVE_LIST_METRICS)
    true;
#else
    false;
#endif
constexpr bool kSpriteActiveListEnabled =
#if defined(NESCO_SPRITE_ACTIVE_LIST)
    true;
#else
    false;
#endif
constexpr uint64_t kPerfWindowUs = 1000000;
constexpr uint32_t kPerfFrameSampleCapacity = 64;
constexpr int kNesViewScaleStretch320x300 = 1;

uint64_t g_perf_window_start_us = 0;
uint64_t g_perf_last_frame_us = 0;
uint64_t g_perf_frame_us_total = 0;
uint64_t g_perf_frame_us_max = 0;
uint32_t g_perf_frame_samples = 0;
uint32_t g_perf_frame_us_samples[kPerfFrameSampleCapacity];
uint64_t g_perf_last_pad_us = 0;
uint64_t g_perf_pad_interval_us_total = 0;
uint64_t g_perf_pad_interval_us_max = 0;
uint32_t g_perf_pad_interval_samples = 0;
uint32_t g_perf_frames = 0;
#if defined(NESCO_MAPPER4_FRAME_PROGRESS)
static uint32_t g_mapper4_ppu_frames = 0;
#endif

#if defined(NESCO_MAPPER4_FRACTIONAL_TIMING)
/*
 * Mapper 4 diagnostic timing only.  The PPU advances 341 dots per
 * scanline, while the 6502 advances at one CPU cycle per three PPU dots.
 * Keep the dot remainder between scanlines instead of rounding every line
 * to 114 CPU cycles.  Mesen2 starts with an odd NTSC frame, whose rendered
 * pre-render line is one dot shorter.
 */
static uint8_t g_mapper4_ppu_dot_remainder = 0;
static bool g_mapper4_odd_frame = true;
#endif
uint32_t g_perf_scanlines = 0;
uint64_t g_perf_cpu_us = 0;
uint64_t g_perf_apu_us = 0;
uint64_t g_perf_ppu_bg_us = 0;
uint64_t g_perf_ppu_bg_mapper_us = 0;
uint64_t g_perf_ppu_bg_clear_us = 0;
uint64_t g_perf_ppu_bg_setup_us = 0;
uint64_t g_perf_ppu_bg_tile_us = 0;
uint64_t g_perf_ppu_bg_tile_pal_us = 0;
uint64_t g_perf_ppu_bg_tile_build_us = 0;
uint64_t g_perf_ppu_bg_tile_render_us = 0;
uint64_t g_perf_ppu_bg_mapperppu_us = 0;
uint32_t g_perf_ppu_bg_tile_count = 0;
uint32_t g_perf_ppu_bg_tile_full_count = 0;
uint32_t g_perf_ppu_bg_tile_partial_count = 0;
uint64_t g_perf_ppu_bg_clip_us = 0;
uint64_t g_perf_ppu_sprite_us = 0;
uint64_t g_perf_ppu_sprite_mapper_us = 0;
uint64_t g_perf_ppu_sprite_clear_us = 0;
uint64_t g_perf_ppu_sprite_scan_us = 0;
uint64_t g_perf_ppu_sprite_scan_oam_us = 0;
uint64_t g_perf_ppu_sprite_scan_fetch_us = 0;
uint64_t g_perf_ppu_sprite_scan_write_us = 0;
uint32_t g_perf_ppu_sprite_scan_skip_count = 0;
uint64_t g_perf_ppu_sprite_active_build_us = 0;
uint32_t g_perf_ppu_sprite_active_entries = 0;
uint32_t g_perf_ppu_sprite_active_lines = 0;
uint32_t g_perf_ppu_sprite_active_max_per_line = 0;
uint32_t g_perf_sprite_active_list_scanlines = 0;
uint32_t g_perf_sprite_active_list_fallback_scanlines = 0;
uint32_t g_perf_sprite_active_list_candidates = 0;
uint64_t g_perf_ppu_sprite_comp_us = 0;
uint64_t g_perf_ppu_sprite_clip_us = 0;
uint32_t g_perf_ppu_sprite_visible_count = 0;
uint64_t g_perf_mapper_hsync_us = 0;
uint64_t g_perf_mapper_vsync_us = 0;
uint64_t g_perf_load_frame_us = 0;
uint64_t g_perf_tail_us = 0;
uint64_t g_perf_lcd_wait_us = 0;
uint64_t g_perf_lcd_flush_us = 0;
uint64_t g_perf_lcd_queue_wait_us = 0;
uint32_t g_perf_lcd_queue_wait_count = 0;
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
#if defined(NESCO_MAPPER4_FRAME_PROGRESS)
  g_mapper4_ppu_frames = 0;
#endif
  g_perf_ppu_bg_us = 0;
  g_perf_ppu_bg_tile_us = 0;
  g_perf_ppu_sprite_us = 0;
  g_perf_ppu_sprite_active_build_us = 0;
  g_perf_ppu_sprite_active_entries = 0;
  g_perf_ppu_sprite_active_lines = 0;
  g_perf_ppu_sprite_active_max_per_line = 0;
  g_perf_sprite_active_list_scanlines = 0;
  g_perf_sprite_active_list_fallback_scanlines = 0;
  g_perf_sprite_active_list_candidates = 0;
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
    if (g_perf_frame_samples < kPerfFrameSampleCapacity)
    {
      g_perf_frame_us_samples[g_perf_frame_samples] = static_cast<uint32_t>(frame_us);
      ++g_perf_frame_samples;
    }
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

inline void perf_sort_frame_samples()
{
  for (uint32_t i = 1; i < g_perf_frame_samples; ++i)
  {
    const uint32_t value = g_perf_frame_us_samples[i];
    uint32_t j = i;
    while (j > 0 && g_perf_frame_us_samples[j - 1] > value)
    {
      g_perf_frame_us_samples[j] = g_perf_frame_us_samples[j - 1];
      --j;
    }
    g_perf_frame_us_samples[j] = value;
  }
}

inline uint64_t perf_frame_avg_us()
{
  return g_perf_frame_samples != 0
             ? g_perf_frame_us_total / g_perf_frame_samples
             : 0;
}

inline uint64_t perf_frame_median_us()
{
  if (g_perf_frame_samples == 0)
  {
    return 0;
  }

  const uint32_t middle = g_perf_frame_samples / 2;
  if ((g_perf_frame_samples & 1u) != 0)
  {
    return g_perf_frame_us_samples[middle];
  }

  return (static_cast<uint64_t>(g_perf_frame_us_samples[middle - 1]) +
          g_perf_frame_us_samples[middle]) /
         2;
}

inline uint64_t perf_frame_p95_us()
{
  if (g_perf_frame_samples == 0)
  {
    return 0;
  }

  const uint32_t rank = (g_perf_frame_samples * 95u + 99u) / 100u;
  return g_perf_frame_us_samples[rank - 1u];
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

  const uint64_t fps_x100 =
      elapsed_us != 0
          ? (static_cast<uint64_t>(g_perf_frames) * 100ull * 1000000ull) / elapsed_us
          : 0;
  const char *view_mode =
      display_get_nes_view_scale() == kNesViewScaleStretch320x300
          ? "stretch"
          : "normal";

  perf_sort_frame_samples();

  display_perf_window_t display_window = {};
  display_perf_take_window(&display_window);

  const uint64_t pad_interval_us_avg =
      g_perf_pad_interval_samples != 0
          ? g_perf_pad_interval_us_total / g_perf_pad_interval_samples
          : 0;
  const unsigned input_events = input_consume_event_count();

  NESCO_LOG_PERF("[CORE1_BASE] t_us=%llu frames=%lu fps_x100=%llu frame_us_avg=%llu frame_us_max=%llu lcd_wait_us=%llu lcd_flush_us=%llu lcd_queue_wait_us=%llu lcd_queue_wait_count=%lu lcd_empty_polls=%lu palette_protocol_faults=%lu pad_interval_us_avg=%llu pad_interval_us_max=%llu input_events=%u view_mode=%s lcd_queue_wait_episodes=%lu frame_pacing_sleep_us=%llu frame_pacing_sleep_count=%lu lcd_dma_wait_us=%llu lcd_dma_wait_count=%lu lcd_window_set_us=%llu lcd_window_set_count=%lu\n",
                 static_cast<unsigned long long>(now_us),
                 static_cast<unsigned long>(g_perf_frames),
                 static_cast<unsigned long long>(fps_x100),
                 static_cast<unsigned long long>(perf_frame_avg_us()),
                 static_cast<unsigned long long>(g_perf_frame_us_max),
                 static_cast<unsigned long long>(display_window.lcd_wait_us),
                 static_cast<unsigned long long>(display_window.lcd_flush_us),
                 static_cast<unsigned long long>(display_window.lcd_queue_wait_us),
                 static_cast<unsigned long>(display_window.lcd_queue_wait_count),
                 static_cast<unsigned long>(display_window.lcd_empty_polls),
                 static_cast<unsigned long>(display_window.palette_protocol_faults),
                 static_cast<unsigned long long>(pad_interval_us_avg),
                 static_cast<unsigned long long>(g_perf_pad_interval_us_max),
                 input_events,
                 view_mode,
                 static_cast<unsigned long>(display_window.lcd_queue_wait_episodes),
                 static_cast<unsigned long long>(display_window.frame_pacing_sleep_us),
                 static_cast<unsigned long>(display_window.frame_pacing_sleep_count),
                 static_cast<unsigned long long>(display_window.lcd_dma_wait_us),
                 static_cast<unsigned long>(display_window.lcd_dma_wait_count),
                 static_cast<unsigned long long>(display_window.lcd_window_set_us),
                 static_cast<unsigned long>(display_window.lcd_window_set_count));

#if defined(NESCO_PALETTE_SNAPSHOT_LOG)
  NESCO_LOG_PERF("[PALETTE_SNAPSHOT] frames=%lu line_items=%lu snapshots=%lu forced=%lu applied=%lu protocol_faults=%lu version=%u\n",
                 static_cast<unsigned long>(g_perf_frames),
                 static_cast<unsigned long>(display_window.palette_line_items),
                 static_cast<unsigned long>(display_window.palette_snapshots),
                 static_cast<unsigned long>(display_window.palette_forced),
                 static_cast<unsigned long>(display_window.palette_applied),
                 static_cast<unsigned long>(display_window.palette_protocol_faults),
                 static_cast<unsigned>(display_window.palette_version));
#endif

  NESCO_LOG_PERF("[FRAME_STATS] avg_us=%llu median_us=%llu p95_us=%llu max_us=%llu\n",
                 static_cast<unsigned long long>(perf_frame_avg_us()),
                 static_cast<unsigned long long>(perf_frame_median_us()),
                 static_cast<unsigned long long>(perf_frame_p95_us()),
                 static_cast<unsigned long long>(g_perf_frame_us_max));

  if constexpr (kBgTileShareTiming)
  {
    const uint64_t bg_tile_us_per_frame =
        g_perf_frames != 0 ? g_perf_ppu_bg_tile_us / g_perf_frames : 0;
    const uint64_t bg_tile_pct_x100 =
        g_perf_frame_us_total != 0
            ? (g_perf_ppu_bg_tile_us * 10000ull) / g_perf_frame_us_total
            : 0;
    NESCO_LOG_PERF("[BG_SHARE] frames=%lu bg_tile_us=%llu bg_us=%llu sprite_us=%llu bg_tile_us_per_frame=%llu bg_tile_pct_x100=%llu view_mode=%s\n",
                   static_cast<unsigned long>(g_perf_frames),
                   static_cast<unsigned long long>(g_perf_ppu_bg_tile_us),
                   static_cast<unsigned long long>(g_perf_ppu_bg_us),
                   static_cast<unsigned long long>(g_perf_ppu_sprite_us),
                   static_cast<unsigned long long>(bg_tile_us_per_frame),
                   static_cast<unsigned long long>(bg_tile_pct_x100),
                   view_mode);
  }

  NESCO_LOG_PERF("[FPS_SUMMARY] t_us=%llu frames=%lu fps_x100=%llu view_mode=%s\n",
                 static_cast<unsigned long long>(now_us),
                 static_cast<unsigned long>(g_perf_frames),
                 static_cast<unsigned long long>(fps_x100),
                 view_mode);

  if constexpr (kSpriteActiveListMetrics)
  {
    NESCO_LOG_PERF("[SPR_ACTIVE] build_us=%llu entries=%lu active_lines=%lu max_per_line=%lu list_scanlines=%lu fallback_scanlines=%lu candidates=%lu\n",
                   static_cast<unsigned long long>(g_perf_ppu_sprite_active_build_us),
                   static_cast<unsigned long>(g_perf_ppu_sprite_active_entries),
                   static_cast<unsigned long>(g_perf_ppu_sprite_active_lines),
                   static_cast<unsigned long>(g_perf_ppu_sprite_active_max_per_line),
                   static_cast<unsigned long>(g_perf_sprite_active_list_scanlines),
                   static_cast<unsigned long>(g_perf_sprite_active_list_fallback_scanlines),
                   static_cast<unsigned long>(g_perf_sprite_active_list_candidates));
  }

  std::fflush(stdout);
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
BYTE *WorkLine = nullptr;
void __not_in_flash_func(InfoNES_SetLineBuffer)(BYTE *p, WORD size)
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
  display_perf_reset();
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

#if defined(NESCO_MAPPER19_IRQ_DIAGNOSTICS)
  g_mapper19_irq_fixture_kind = 0;
  g_mapper19_irq_fixture_reported = false;
#endif
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

#if defined(NESCO_MAPPER19_IRQ_DIAGNOSTICS)
  g_mapper19_irq_fixture_kind = 0;
  g_mapper19_irq_fixture_reported = false;
#endif

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

#if defined(NESCO_MAPPER19_IRQ_DIAGNOSTICS)
  g_mapper19_irq_fixture_kind = mapper19_irq_fixture_kind_for_path(
      pszFileName,
      MapperNo,
      NesHeader.byRomSize,
      NesHeader.byVRomSize);
#endif

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
  display_perf_reset();
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
  display_lcd_worker_palette_force_snapshot();

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
  K6502_Set_CpuCycleCallback(nullptr);
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
  InfoNES_InvalidateSpriteActiveList();

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

#if defined(NESCO_MAPPER4_FRACTIONAL_TIMING)
  if (MapperNo == 4)
  {
    g_mapper4_ppu_dot_remainder = 0;
    g_mapper4_odd_frame = true;
  }
#endif

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
          int scanline_clocks = STEP_PER_SCANLINE;

#if defined(NESCO_MAPPER4_FRACTIONAL_TIMING)
          if (MapperNo == 4)
          {
              const bool odd_frame_pre_render =
                  g_mapper4_odd_frame &&
                  PPU_Scanline == SCAN_VBLANK_END &&
                  (PPU_R1 & (R1_SHOW_SCR | R1_SHOW_SP));
              const int scanline_dots = odd_frame_pre_render ? 340 : 341;
              const int dots_with_remainder =
                  scanline_dots + g_mapper4_ppu_dot_remainder;
              scanline_clocks = dots_with_remainder / 3;
              g_mapper4_ppu_dot_remainder = dots_with_remainder % 3;

#if defined(NESCO_MAPPER4_TIMING_TRACE)
              static unsigned mapper4_clock_trace_count;
              if (mapper4_clock_trace_count < 8u)
              {
                  printf("[M4_CLOCK] n=%u sl=%u dots=%d clocks=%d rem=%u odd=%u r1=%02X\n",
                         mapper4_clock_trace_count,
                         (unsigned)PPU_Scanline,
                         scanline_dots,
                         scanline_clocks,
                         (unsigned)g_mapper4_ppu_dot_remainder,
                         odd_frame_pre_render ? 1u : 0u,
                         (unsigned)PPU_R1);
                  fflush(stdout);
                  ++mapper4_clock_trace_count;
              }
#endif
          }
#endif

#if defined(NESCO_MAPPER4_TIMING_TRACE)
          static unsigned mapper4_cycle_trace_count;
          static unsigned mapper4_state_trace_count;
          if (MapperNo == 4 && mapper4_state_trace_count < 8u)
          {
              printf("[M4_STATE] n=%u sl=%u fs=%u r0=%02X r1=%02X frame=%u cnt=%u pc=%04X\n",
                     mapper4_state_trace_count,
                     (unsigned)PPU_Scanline,
                     (unsigned)FrameStep,
                     (unsigned)PPU_R0,
                     (unsigned)PPU_R1,
                     (unsigned)FrameCnt,
                     (unsigned)g_perf_frames,
                     (unsigned)PC);
              fflush(stdout);
              ++mapper4_state_trace_count;
          }
#endif

          const bool mapper4_scanline_irq =
              MapperNo == 4 &&
              (PPU_R1 & (R1_SHOW_SCR | R1_SHOW_SP)) &&
              (PPU_Scanline < SCAN_UNKNOWN_START ||
               PPU_Scanline == SCAN_VBLANK_END);
#if defined(NESCO_MAPPER4_A12_SINGLE_SOURCE)
          /*
           * Mesen2 observes the MMC3 A12 edge at PPU cycle 261 on the
           * rendering scanline.  This is a request position, not the later
           * CPU IRQ acceptance point.
           */
          constexpr int mapper4_event_offset = 87;
#else
          const int mapper4_event_offset =
              (PPU_Scanline == SCAN_VBLANK_END)
                  ? scanline_clocks - 23
                  : ((PPU_R0 & R0_SP_ADDR) ? 87 : 0);
#endif
          int mapper4_elapsed = 0;
          bool mapper4_event_done = false;
          auto step_scanline_part = [&](int clocks)
          {
              if (mapper4_scanline_irq &&
                  !mapper4_event_done &&
                  mapper4_event_offset >= mapper4_elapsed &&
                  mapper4_event_offset <= mapper4_elapsed + clocks)
              {
#if defined(NESCO_MAPPER4_TIMING_TRACE)
                  if (mapper4_cycle_trace_count < 4u)
                  {
                      printf("[M4_EDGE] sl=%u fs=%u r0=%02X r1=%02X offset=%d elapsed=%d clocks=%d pc=%04X cpu=%d\n",
                             (unsigned)PPU_Scanline,
                             (unsigned)FrameStep,
                             (unsigned)PPU_R0,
                             (unsigned)PPU_R1,
                             mapper4_event_offset,
                             mapper4_elapsed,
                             clocks,
                             (unsigned)PC,
                             getCurrentClocks32());
                      fflush(stdout);
                      ++mapper4_cycle_trace_count;
                  }
#endif
                  const int before_event = mapper4_event_offset - mapper4_elapsed;
                  if (before_event > 0)
                      K6502_Step(before_event);

                  /* One PPU A12 high phase for this scanline. */
#if defined(NESCO_MAPPER4_A12_SINGLE_SOURCE)
                  MapperPPU(0x1000);
#else
                  /* One PPU A12 low-to-high transition for this scanline. */
                  MapperPPU(0x0000);
                  MapperPPU(0x1000);
#endif
                  /* Let a newly asserted mapper IRQ be observed at this edge. */
#if defined(NESCO_MAPPER4_A12_SINGLE_SOURCE)
                  /*
                   * The A12 edge is the IRQ request.  Mesen2 accepts it at
                   * the following CPU instruction boundary; do not call
                   * procNMI immediately at the request point.
                   */
                  constexpr int mapper4_irq_accept_delay = 4;
#else
                  constexpr int mapper4_irq_accept_delay = 0;
#endif
                  mapper4_event_done = true;

                  const int after_event = clocks - before_event;
                  if (after_event > mapper4_irq_accept_delay)
                  {
                      if (mapper4_irq_accept_delay > 0)
                          K6502_Step_NoInterrupt(mapper4_irq_accept_delay);
                      K6502_Step(after_event - mapper4_irq_accept_delay);
                  }
                  else if (after_event > 0)
                  {
                      K6502_Step(after_event);
                  }
              }
              else
              {
                  K6502_Step(clocks);
              }
              mapper4_elapsed += clocks;
          };

#if defined(NESCO_MAPPER4_A12_SINGLE_SOURCE)
          if (mapper4_scanline_irq)
          {
              /* Start the low interval at the beginning of this scanline. */
              MapperPPU(0x0000);
          }
#endif

          // Set a flag if a scanning line is a hit in the sprite #0
          if (SpriteJustHit == PPU_Scanline &&
              PPU_ScanTable[PPU_Scanline] == SCAN_ON_SCREEN)
          {
              // # of Steps to execute before sprite #0 hit
              int nStep = SPRRAM[SPR_X] * scanline_clocks / NES_DISP_WIDTH;

              // Execute instructions
              step_scanline_part(nStep);

              // Set a sprite hit flag
              if ((PPU_R1 & R1_SHOW_SP) && (PPU_R1 & R1_SHOW_SCR))
                  PPU_R2 |= R2_HIT_SP;

              // Sprite-0 hit only updates PPUSTATUS.  PPUCTRL bit 6 is the
              // master/slave select and is not an NMI enable bit; the NES
              // generates NMI at VBlank only.

              // Execute instructions
              step_scanline_part(scanline_clocks - nStep);
          }
          else
          {
              // Execute instructions
              step_scanline_part(scanline_clocks);
          }

          // Frame IRQ in H-Sync
          FrameStep += scanline_clocks;
          if (FrameStep > STEP_PER_FRAME && FrameIRQ_Enable)
          {
              FrameStep %= STEP_PER_FRAME;
              IRQ_REQ;
              APU_Reg[0x15] |= 0x40;
          }

          //util::WorkMeterMark(MARKER_CPU);
      }
    // A mapper function in H-Sync
    MapperHSync();

    // A function in H-Sync
    if (InfoNES_HSync() == -1) //quit was called
      return; // To the menu screen

    // HSYNC Wait
    InfoNES_Wait();
  }
}

void InfoNES_InvalidateSpriteActiveList(void)
{
  g_sprite_active_list_valid = false;
}

inline void buildSpriteActiveList()
{
  g_sprite_active_list_valid = false;

  BYTE line_counts[NES_DISP_HEIGHT] = {};
  for (int sprite_index = 0; sprite_index < 64; ++sprite_index)
  {
    const int sprite_offset = sprite_index << 2;
    const int y0 = static_cast<int>(SPRRAM[sprite_offset + SPR_Y]) + 1;
    const int y1 = y0 + static_cast<int>(PPU_SP_Height);
    const int begin = y0 < 0 ? 0 : y0;
    const int end = y1 > NES_DISP_HEIGHT ? NES_DISP_HEIGHT : y1;
    for (int y = begin; y < end; ++y)
    {
      ++line_counts[y];
    }
  }

  uint16_t entries = 0;
  for (int y = 0; y < NES_DISP_HEIGHT; ++y)
  {
    g_sprite_active_offsets[y] = entries;
    entries += line_counts[y];
  }
  g_sprite_active_offsets[NES_DISP_HEIGHT] = entries;
  if (entries > kSpriteActiveListMaxEntries)
  {
    return;
  }

  uint16_t write_offsets[NES_DISP_HEIGHT];
  for (int y = 0; y < NES_DISP_HEIGHT; ++y)
  {
    write_offsets[y] = g_sprite_active_offsets[y];
  }

  // Preserve the existing OAM priority order: sprite 63 down to sprite 0.
  for (int sprite_index = 63; sprite_index >= 0; --sprite_index)
  {
    const int sprite_offset = sprite_index << 2;
    const int y0 = static_cast<int>(SPRRAM[sprite_offset + SPR_Y]) + 1;
    const int y1 = y0 + static_cast<int>(PPU_SP_Height);
    const int begin = y0 < 0 ? 0 : y0;
    const int end = y1 > NES_DISP_HEIGHT ? NES_DISP_HEIGHT : y1;
    for (int y = begin; y < end; ++y)
    {
      g_sprite_active_indices[write_offsets[y]++] = static_cast<BYTE>(sprite_index);
    }
  }

  g_sprite_active_list_valid = true;
}

inline bool spriteActiveListAvailableForScanline(int scanline)
{
  return kSpriteActiveListEnabled &&
         g_sprite_active_list_valid &&
         scanline >= 0 &&
         scanline < NES_DISP_HEIGHT;
}

inline void measureSpriteActiveListBuild()
{
  if constexpr (!kSpriteActiveListEnabled)
  {
    g_sprite_active_list_valid = false;
    return;
  }

  if constexpr (kDetailedPerfLogToSerial || kSpriteActiveListMetrics)
  {
    const uint64_t start_us = time_us_64();
    buildSpriteActiveList();
    g_perf_ppu_sprite_active_build_us += time_us_64() - start_us;
    g_perf_ppu_sprite_active_entries += g_sprite_active_offsets[NES_DISP_HEIGHT];
    for (int y = 0; y < NES_DISP_HEIGHT; ++y)
    {
      const uint32_t count = g_sprite_active_offsets[y + 1] - g_sprite_active_offsets[y];
      if (count != 0)
      {
        ++g_perf_ppu_sprite_active_lines;
      }
      if (count > g_perf_ppu_sprite_active_max_per_line)
      {
        g_perf_ppu_sprite_active_max_per_line = count;
      }
    }
    return;
  }

  buildSpriteActiveList();
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

  InfoNES_pAPUHsync(!APU_Mute);
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
      InfoNES_PreDrawLine(PPU_Scanline);
    if (PPU_Scanline >= 4 && PPU_Scanline < 240 - 4)
    {
    
      InfoNES_DrawLine();
     
    } else {
      InfoNES_MemorySet(WorkLine, 0x20, NES_DISP_WIDTH);
    }
     InfoNES_PostDrawLine(PPU_Scanline, false);
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
#if defined(NESCO_MAPPER4_FRACTIONAL_TIMING)
  if (MapperNo == 4 && PPU_Scanline == 0)
  {
    g_mapper4_odd_frame = !g_mapper4_odd_frame;
  }
#endif
  switch (PPU_Scanline)
  {
  case SCAN_TOP_OFF_SCREEN:
    // Reset a PPU status
    PPU_R2 = 0;
    // Set up a character data
    if (NesHeader.byVRomSize == 0 && FrameCnt == 0)
      InfoNES_SetupChr();

    if (FrameCnt == 0 && (PPU_R1 & R1_SHOW_SP))
    {
      measureSpriteActiveListBuild();
    }

    // Get position of sprite #0
    InfoNES_GetSprHitY();
    break;

  case SCAN_UNKNOWN_START:
#if defined(NESCO_MAPPER4_FRAME_PROGRESS)
    ++g_mapper4_ppu_frames;
    if (MapperNo == 4 &&
        (g_mapper4_ppu_frames == 1u || g_mapper4_ppu_frames == 30u ||
         g_mapper4_ppu_frames == 54u || g_mapper4_ppu_frames == 71u ||
         g_mapper4_ppu_frames == 120u))
    {
      printf("[M4_FRAME_PROGRESS] ppu_frame=%lu scanline=%u pc=%04X a=%02X x=%02X y=%02X r1=%02X result_f8=%02X\n",
             static_cast<unsigned long>(g_mapper4_ppu_frames),
             static_cast<unsigned>(PPU_Scanline),
             static_cast<unsigned>(PC),
             static_cast<unsigned>(A),
             static_cast<unsigned>(X),
             static_cast<unsigned>(Y),
             static_cast<unsigned>(PPU_R1),
             static_cast<unsigned>(RAM[0x00f8]));
      fflush(stdout);
    }
#endif
    if (FrameCnt == 0)
    {
      // Transfer the contents of work frame on the screen
      InfoNES_LoadFrame();
      const uint64_t frame_now_us = time_us_64();
      if constexpr (kPerfLogToSerial)
      {
        perf_note_frame(frame_now_us);
      }
      ++g_perf_frames;
      perf_log_if_due(frame_now_us);

#if 0
        // Switching of the double buffer
        WorkFrameIdx = 1 - WorkFrameIdx;
        WorkFrame = DoubleFrame[ WorkFrameIdx ];
#endif
    }
#if defined(NESCO_MAPPER19_IRQ_DIAGNOSTICS)
    if (g_mapper19_irq_fixture_kind != 0 &&
        !g_mapper19_irq_fixture_reported &&
        RAM[0x00f9] != 0)
    {
      g_mapper19_irq_fixture_reported = true;
      if (g_mapper19_irq_fixture_kind == 1)
      {
        std::printf("[M19_IRQ_DIAG] done=%02X f9=%02X f0=%02X f1=%02X f2=%02X f3=%02X "
                    "f4=%02X f5=%02X f6=%02X f7=%02X irq_count=%02X "
                    "irq_mode=%02X marker_after=%02X irq_marker_seen=%02X\n",
                    static_cast<unsigned>(RAM[0x00f9]),
                    static_cast<unsigned>(RAM[0x00f9]),
                    static_cast<unsigned>(RAM[0x00f0]),
                    static_cast<unsigned>(RAM[0x00f1]),
                    static_cast<unsigned>(RAM[0x00f2]),
                    static_cast<unsigned>(RAM[0x00f3]),
                    static_cast<unsigned>(RAM[0x00f4]),
                    static_cast<unsigned>(RAM[0x00f5]),
                    static_cast<unsigned>(RAM[0x00f6]),
                    static_cast<unsigned>(RAM[0x00f7]),
                    static_cast<unsigned>(RAM[0x00fa]),
                    static_cast<unsigned>(RAM[0x00fb]),
                    static_cast<unsigned>(RAM[0x00fc]),
                    static_cast<unsigned>(RAM[0x00fd]));
      }
      else
      {
        std::printf("[M19_POST_ACK_DIAG] done=%02X f9=%02X read_reassert=%02X "
                    "low_ack=%02X high_ack=%02X irq_count=%02X irq_mode=%02X\n",
                    static_cast<unsigned>(RAM[0x00f9]),
                    static_cast<unsigned>(RAM[0x00f9]),
                    static_cast<unsigned>(RAM[0x00f0]),
                    static_cast<unsigned>(RAM[0x00f1]),
                    static_cast<unsigned>(RAM[0x00f2]),
                    static_cast<unsigned>(RAM[0x00fa]),
                    static_cast<unsigned>(RAM[0x00fb]));
      }
      std::fflush(stdout);
    }
#endif
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
    MapperVSync();

    // Get the condition of the joypad
    InfoNES_PadState(&PAD1_Latch, &PAD2_Latch, &PAD_System);
    if constexpr (kPerfLogToSerial)
    {
      perf_note_pad_poll(time_us_64());
    }
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

    if (PAD_PUSH(PAD_System, PAD_SYS_FRAME_POLICY_TOGGLE))
    {
      display_toggle_stretch_frame_policy();
    }

    break;
  }

  audio_debug_poll();

  // Successful
  return 0;
}

//#pragma GCC optimize("O2")

namespace
{
  struct BgTileDescriptor
  {
    const BYTE *pattern_row;
    WORD ppu_pattern_address;
    BYTE palette_base;
    BYTE *dst;
    BYTE clip_left;
    BYTE clip_right;
  };

  static inline void __not_in_flash_func(renderPacked4)(BYTE *dst,
                                                        BYTE palette_base,
                                                        BYTE packed,
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
      const BYTE idx = (BYTE)((packed >> idx_shift) & 0x03u);
      dst[*out] = (BYTE)(palette_base | idx);
    }
  }

  static inline void __not_in_flash_func(renderBgTileFull)(BYTE palette_base,
                                                           BYTE *dst,
                                                           BYTE packed_hi,
                                                           BYTE packed_lo) __attribute__((always_inline));

  static inline void __not_in_flash_func(renderBgTileFull)(BYTE palette_base,
                                                           BYTE *dst,
                                                           BYTE packed_hi,
                                                           BYTE packed_lo)
  {
    dst[0] = (BYTE)(palette_base | ((packed_hi >> 6) & 0x03u));
    dst[1] = (BYTE)(palette_base | ((packed_hi >> 4) & 0x03u));
    dst[2] = (BYTE)(palette_base | ((packed_hi >> 2) & 0x03u));
    dst[3] = (BYTE)(palette_base | (packed_hi & 0x03u));
    dst[4] = (BYTE)(palette_base | ((packed_lo >> 6) & 0x03u));
    dst[5] = (BYTE)(palette_base | ((packed_lo >> 4) & 0x03u));
    dst[6] = (BYTE)(palette_base | ((packed_lo >> 2) & 0x03u));
    dst[7] = (BYTE)(palette_base | (packed_lo & 0x03u));
  }

  static inline void __not_in_flash_func(renderBgTile)(const BgTileDescriptor &desc)
  {
    const BYTE pl0 = desc.pattern_row[0];
    const BYTE pl1 = desc.pattern_row[8];
    BYTE *dst = desc.dst;
    const BYTE packed_hi = g_bg_tile_pair_idx4[((pl0 & 0xF0u)) | (pl1 >> 4)];
    const BYTE packed_lo = g_bg_tile_pair_idx4[((pl0 & 0x0Fu) << 4) | (pl1 & 0x0Fu)];

    if (desc.clip_left == 0 && desc.clip_right == 8)
    {
      renderBgTileFull(desc.palette_base, dst, packed_hi, packed_lo);
      return;
    }

    int out = 0;
    renderPacked4(dst,
                  desc.palette_base,
                  packed_hi,
                  0,
                  desc.clip_left,
                  desc.clip_right,
                  &out);
    renderPacked4(dst,
                  desc.palette_base,
                  packed_lo,
                  4,
                  desc.clip_left,
                  desc.clip_right,
                  &out);
  }

  void __not_in_flash_func(compositeSpriteRange)(const BYTE *spr,
                                                 BYTE *buf,
                                                 int begin,
                                                 int end)
  {
    if (end <= begin)
      return;

    spr += begin;
    buf += begin;

    auto sprEnd = spr + (end - begin);
    do
    {
      auto proc = [=](int i) __attribute__((always_inline))
      {
        int v = spr[i];
        if (v && ((v & 0x80) != 0 || (buf[i] & 3) == 0))
        {
          buf[i] = (BYTE)(0x10 | (v & 0x0f));
        }
      };

      proc(0);
      proc(1);
      proc(2);
      proc(3);
      buf += 4;
      spr += 4;
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
  BYTE *pAttrBase;
  BYTE *pPoint;
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
  uint64_t sprite_block_start_us = 0;

  /*-------------------------------------------------------------------*/
  /*  Render Background                                                */
  /*-------------------------------------------------------------------*/

  if constexpr (kBgTileShareTiming)
  {
    bg_start_us = time_us_64();
  }

  /* MMC5 VROM switch */
  if constexpr (kDetailedPerfLogToSerial)
  {
    bg_mapper_start_us = time_us_64();
  }
  MapperRenderScreen(1);
  if constexpr (kDetailedPerfLogToSerial)
  {
    g_perf_ppu_bg_mapper_us += time_us_64() - bg_mapper_start_us;
  }

  // Pointer to the render position
  //  pPoint = &WorkFrame[PPU_Scanline * NES_DISP_WIDTH];
  assert(WorkLine);
  pPoint = WorkLine;

  // Clear a scanline if screen is off
  if (!(PPU_R1 & R1_SHOW_SCR))
  {
    if constexpr (kDetailedPerfLogToSerial)
    {
      bg_clear_start_us = time_us_64();
    }
    InfoNES_MemorySet(pPoint, 0x20, NES_DISP_WIDTH);
    if constexpr (kDetailedPerfLogToSerial)
    {
      g_perf_ppu_bg_clear_us += time_us_64() - bg_clear_start_us;
    }
  }
  else
  {
    if constexpr (kDetailedPerfLogToSerial)
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
    BYTE cachedPaletteBase = 0;

    auto resolveBgPaletteBase = [&](BYTE *attrBase,
                                    int tileX) -> BYTE
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
        cachedPaletteBase = (BYTE)(((attrBase[attrGroup] >> (attrHalf + nY4)) & 3) << 2);
      }
      return cachedPaletteBase;
    };

    auto buildBgTile = [&](BYTE *nameTablePtr,
                           BYTE paletteBase,
                           BYTE *dst,
                           int clipLeft,
                           int clipRight) -> BgTileDescriptor
    {
      BgTileDescriptor desc;
      const int ch = *nameTablePtr;
      const int bank = (ch >> 6) + bankOfsBG;
      const int addrOfs = ((ch & 63) << 4) + yOfsModBG;

      desc.pattern_row = PPUBANK[bank] + addrOfs;
      /* Mapper 9/10 latch on the high-plane byte ($FD8/$FE8), which is
       * eight bytes after the low-plane row used by the direct renderer.
       * The PPU address is based on the tile number, not the currently
       * mapped 1 KiB bank selected by the mapper. */
      desc.ppu_pattern_address = static_cast<WORD>((patternTableIdBG << 12) +
                                                    (ch << 4) +
                                                    yOfsModBG + 8);
      desc.palette_base = paletteBase;
      desc.dst = dst;
      desc.clip_left = (BYTE)clipLeft;
      desc.clip_right = (BYTE)clipRight;
      return desc;
    };

    auto emitBgTile = [&](BYTE *nameTablePtr,
                          BYTE *attrBase,
                          int tileX,
                          BYTE *dst,
                          int clipLeft,
                          int clipRight)
    {
      if constexpr (kDetailedPerfLogToSerial)
      {
        ++g_perf_ppu_bg_tile_count;
        if (clipLeft == 0 && clipRight == 8)
        {
          ++g_perf_ppu_bg_tile_full_count;
        }
        else
        {
          ++g_perf_ppu_bg_tile_partial_count;
        }
      }

      const BYTE paletteBase = resolveBgPaletteBase(attrBase, tileX);
      BgTileDescriptor desc = buildBgTile(nameTablePtr, paletteBase, dst, clipLeft, clipRight);

      renderBgTile(desc);

      /*
       * The direct renderer reads the pattern row from VROM.  PATTBL()
       * is only valid for the old ChrBuf-backed renderer, so derive the
       * actual PPU pattern address from the pattern-table and tile instead.
       * Mapper 9/10 use this callback to observe the $FD/$FE latch tiles.
       */
#if defined(NESCO_MAPPER4_A12_SINGLE_SOURCE)
      if (MapperNo != 4)
        MapperPPU(desc.ppu_pattern_address);
#else
      MapperPPU(desc.ppu_pattern_address);
#endif
    };

    /*-------------------------------------------------------------------*/
    /*  Rendering of the block of the left end                           */
    /*-------------------------------------------------------------------*/

    pbyNameTable = PPUBANK[nNameTable] + nY * 32 + nX;
    pAttrBase = PPUBANK[nNameTable] + 0x3c0 + (nY / 4) * 8;
    if constexpr (kDetailedPerfLogToSerial)
    {
      g_perf_ppu_bg_setup_us += time_us_64() - bg_setup_start_us;
    }
    if constexpr (kBgTileShareTiming)
    {
      bg_tile_start_us = time_us_64();
    }
    emitBgTile(pbyNameTable,
               pAttrBase,
               nX,
               pPoint,
               PPU_Scr_H_Bit,
               8);
    pPoint += 8 - PPU_Scr_H_Bit;

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
                 0,
                 8);
      pPoint += 8;

      ++pbyNameTable;
    }

    // Holizontal Mirror
    nNameTable ^= NAME_TABLE_H_MASK;

    pbyNameTable = PPUBANK[nNameTable] + nY * 32;
    pAttrBase = PPUBANK[nNameTable] + 0x3c0 + (nY / 4) * 8;
    pCachedAttrBase = nullptr;
    nCachedAttrGroup = -1;
    nCachedAttrHalf = -1;
    cachedPaletteBase = 0;

    /*-------------------------------------------------------------------*/
    /*  Rendering of the right table                                     */
    /*-------------------------------------------------------------------*/

    for (nX = 0; nX < PPU_Scr_H_Byte; ++nX)
    {
      emitBgTile(pbyNameTable,
                 pAttrBase,
                 nX,
                 pPoint,
                 0,
                 8);
      pPoint += 8;

      ++pbyNameTable;
    }

    /*-------------------------------------------------------------------*/
    /*  Rendering of the block of the right end                          */
    /*-------------------------------------------------------------------*/

    emitBgTile(pbyNameTable,
               pAttrBase,
               nX,
               pPoint,
               0,
               PPU_Scr_H_Bit);
    if constexpr (kBgTileShareTiming)
    {
      g_perf_ppu_bg_tile_us += time_us_64() - bg_tile_start_us;
    }
    if constexpr (kDetailedPerfLogToSerial)
    {
      bg_clip_start_us = time_us_64();
    }

    /*-------------------------------------------------------------------*/
    /*  Backgroud Clipping                                               */
    /*-------------------------------------------------------------------*/
    if (!(PPU_R1 & R1_CLIP_BG))
    {
      BYTE *pPointTop;

      // pPointTop = &WorkFrame[PPU_Scanline * NES_DISP_WIDTH];
      pPointTop = WorkLine;
      InfoNES_MemorySet(pPointTop, 0x20, 8);
    }

    /*-------------------------------------------------------------------*/
    /*  Clear a scanline if up and down clipping flag is set             */
    /*-------------------------------------------------------------------*/
    if (PPU_UpDown_Clip &&
        (SCAN_ON_SCREEN_START > PPU_Scanline || PPU_Scanline > SCAN_BOTTOM_OFF_SCREEN_START))
    {
      BYTE *pPointTop;

      // pPointTop = &WorkFrame[PPU_Scanline * NES_DISP_WIDTH];
      pPointTop = WorkLine;
      InfoNES_MemorySet(pPointTop, 0x20, NES_DISP_WIDTH);
    }
    if constexpr (kDetailedPerfLogToSerial)
    {
      g_perf_ppu_bg_clip_us += time_us_64() - bg_clip_start_us;
    }
  }

  //util::WorkMeterMark(MARKER_BG);
  if constexpr (kBgTileShareTiming)
  {
    g_perf_ppu_bg_us += time_us_64() - bg_start_us;
    sprite_start_us = time_us_64();
  }

  /*-------------------------------------------------------------------*/
  /*  Render a sprite                                                  */
  /*-------------------------------------------------------------------*/

  /* MMC5 VROM switch */
  if constexpr (kDetailedPerfLogToSerial)
  {
    sprite_block_start_us = time_us_64();
  }
  MapperRenderScreen(0);
  if constexpr (kDetailedPerfLogToSerial)
  {
    g_perf_ppu_sprite_mapper_us += time_us_64() - sprite_block_start_us;
  }

  if (PPU_R1 & R1_SHOW_SP)
  {
    // Reset Scanline Sprite Count
    PPU_R2 &= ~R2_MAX_SP;

    // Reset sprite buffer
    if constexpr (kDetailedPerfLogToSerial)
    {
      sprite_block_start_us = time_us_64();
    }
    InfoNES_MemorySet(pSprBuf, 0, sizeof pSprBuf);
    if constexpr (kDetailedPerfLogToSerial)
    {
      g_perf_ppu_sprite_clear_us += time_us_64() - sprite_block_start_us;
      sprite_block_start_us = time_us_64();
    }

    const int patternTableIdSP88 = PPU_R0 & R0_SP_ADDR ? 1 : 0;
    const int bankOfsSP88 = patternTableIdSP88 << 2;

    // Render a sprite to the sprite buffer
    nSprCnt = 0;
    int sprite_min_x = NES_DISP_WIDTH;
    int sprite_max_x_exclusive = 0;
    uint32_t sprite_scan_skip_count = 0;
    const bool use_active_list = spriteActiveListAvailableForScanline(PPU_Scanline);
    const BYTE *active_indices = use_active_list
                                     ? g_sprite_active_indices + g_sprite_active_offsets[PPU_Scanline]
                                     : nullptr;
    const int sprite_count = use_active_list
                                 ? g_sprite_active_offsets[PPU_Scanline + 1] - g_sprite_active_offsets[PPU_Scanline]
                                 : 64;
    if constexpr (kSpriteActiveListMetrics)
    {
      if (use_active_list)
      {
        ++g_perf_sprite_active_list_scanlines;
        g_perf_sprite_active_list_candidates += sprite_count;
      }
      else
      {
        ++g_perf_sprite_active_list_fallback_scanlines;
      }
    }
    for (int sprite_pos = 0; sprite_pos < sprite_count; ++sprite_pos)
    {
      const int sprite_index = active_indices ? active_indices[sprite_pos] : 63 - sprite_pos;
      pSPRRAM = SPRRAM + (sprite_index << 2);
      nY = pSPRRAM[SPR_Y] + 1;
      if (nY > PPU_Scanline || nY + PPU_SP_Height <= PPU_Scanline)
      {
        if constexpr (kDetailedPerfLogToSerial)
        {
          ++sprite_scan_skip_count;
        }
        continue; // Next sprite
      }

      /*-------------------------------------------------------------------*/
      /*  A sprite in scanning line                                        */
      /*-------------------------------------------------------------------*/

      // Holizontal Sprite Count +1
      ++nSprCnt;
      if constexpr (kDetailedPerfLogToSerial)
      {
        ++g_perf_ppu_sprite_visible_count;
      }

      uint64_t sprite_scan_part_start_us = 0;
      if constexpr (kDetailedPerfLogToSerial)
      {
        sprite_scan_part_start_us = time_us_64();
      }

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
      if constexpr (kDetailedPerfLogToSerial)
      {
        const uint64_t now_us = time_us_64();
        g_perf_ppu_sprite_scan_oam_us += now_us - sprite_scan_part_start_us;
        sprite_scan_part_start_us = now_us;
      }

      const auto data = PPUBANK[bank] + addrOfs;
      const uint32_t pl0 = data[0];
      const uint32_t pl1 = data[8];
      const auto pat0 = ((pl0 & 0x55) << 24) | ((pl1 & 0x55) << 25);
      const auto pat1 = ((pl0 & 0xaa) << 23) | ((pl1 & 0xaa) << 24);
      if constexpr (kDetailedPerfLogToSerial)
      {
        const uint64_t now_us = time_us_64();
        g_perf_ppu_sprite_scan_fetch_us += now_us - sprite_scan_part_start_us;
        sprite_scan_part_start_us = now_us;
      }

      nAttr ^= SPR_ATTR_PRI;
      bySprCol = (nAttr & (SPR_ATTR_COLOR | SPR_ATTR_PRI)) << 2;
      nX = pSPRRAM[SPR_X];
      const int sprite_left = nX;
      const int sprite_right = nX + 8;
      if (sprite_left < sprite_min_x)
        sprite_min_x = sprite_left;
      if (sprite_right > sprite_max_x_exclusive)
        sprite_max_x_exclusive = sprite_right;
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
      if constexpr (kDetailedPerfLogToSerial)
      {
        g_perf_ppu_sprite_scan_write_us += time_us_64() - sprite_scan_part_start_us;
      }
#endif
    }
    if constexpr (kDetailedPerfLogToSerial)
    {
      g_perf_ppu_sprite_scan_skip_count += sprite_scan_skip_count;
      g_perf_ppu_sprite_scan_us += time_us_64() - sprite_block_start_us;
      sprite_block_start_us = time_us_64();
    }

    // Rendering sprite
    pPoint = WorkLine;
    //   pPoint -= (NES_DISP_WIDTH - PPU_Scr_H_Bit);

    if (sprite_min_x < 0)
      sprite_min_x = 0;
    if (sprite_max_x_exclusive > NES_DISP_WIDTH)
      sprite_max_x_exclusive = NES_DISP_WIDTH;

    int comp_begin = sprite_min_x & ~3;
    int comp_end = (sprite_max_x_exclusive + 3) & ~3;
    if (comp_begin < 0)
      comp_begin = 0;
    if (comp_end > NES_DISP_WIDTH)
      comp_end = NES_DISP_WIDTH;

    if (comp_begin < comp_end)
    {
      compositeSpriteRange(pSprBuf, pPoint, comp_begin, comp_end);
    }
    if constexpr (kDetailedPerfLogToSerial)
    {
      g_perf_ppu_sprite_comp_us += time_us_64() - sprite_block_start_us;
      sprite_block_start_us = time_us_64();
    }

    /*-------------------------------------------------------------------*/
    /*  Sprite Clipping                                                  */
    /*-------------------------------------------------------------------*/
    if (!(PPU_R1 & R1_CLIP_SP))
    {
      BYTE *pPointTop;

      // pPointTop = &WorkFrame[PPU_Scanline * NES_DISP_WIDTH];
      pPointTop = WorkLine;
      InfoNES_MemorySet(pPointTop, 0x20, 8);
    }

    if (nSprCnt >= 8)
      PPU_R2 |= R2_MAX_SP; // Set a flag of maximum sprites on scanline

    //util::WorkMeterMark(MARKER_SPRITE);
    if constexpr (kDetailedPerfLogToSerial)
    {
      g_perf_ppu_sprite_clip_us += time_us_64() - sprite_block_start_us;
    }
  }

  if constexpr (kBgTileShareTiming)
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
