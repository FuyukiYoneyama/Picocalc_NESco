/*
 * ppu.c — NES PPU (2C02) emulator
 *
 * Design:
 *  - Loopy 15-bit v/t address model for scroll (specs/21_ppu.md)
 *  - Background: 32 tiles per scanline fetched from PPUBANK via coarse-X/Y
 *  - Sprites: up to 8 per scanline evaluated from SPRRAM
 *  - Palette: 32-entry RAM → NES 6-bit color → platform RGB444 via extern table
 *  - Frame skip: if g_byFrameSkip active, skip Pre/PostDrawLine
 *  - All hot-path functions are RAMFUNC (RAM-resident on RP2040)
 *
 * Spec reference: specs/21_ppu.md
 *
 * Part of Picocalc_NESco
 * MIT License
 */

#include "ppu.h"
#include "nes_globals.h"
#include "nes_system.h"
#include "mapper.h"
#include "cpu.h"
#include "apu.h"
#include "audio.h"

#include <string.h>

#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
#include "pico/time.h"
#include <stdio.h>
#endif

#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
#include <stdio.h>
#endif

/* =====================================================================
 *  NES palette → RGB444 lookup table
 *  64 entries × 3 nibbles (R, G, B) packed as 16-bit: 0x0RGB
 *  Platform provides this (defined in platform/display.c or display.h).
 *  Extern declaration; actual data in platform layer.
 * ===================================================================== */
extern const WORD g_NesPalette[64];   /* RGB444 colors for NES system palette */

#if defined(PICO_BUILD)
static BYTE s_cpu_scanline_phase;
static BYTE s_ppu_odd_frame;
static BYTE s_ppu_suppress_next_vblank;
static BYTE s_ppu_vblank_pending;
static BYTE s_ppu_nmi_pending;
static BYTE s_ppu_vblank_clear_pending;
#endif

#if defined(PICO_BUILD)
static uint32_t s_perf_scanlines;
static uint32_t s_perf_frames;
static uint64_t s_perf_last_us;
static uint32_t s_perf_cpu_cycles;
static uint64_t s_perf_cpu_us;
static uint64_t s_perf_apu_us;
static uint64_t s_perf_draw_us;
static uint64_t s_perf_tail_us;
static uint64_t s_perf_sprite_comp_us;
static uint64_t s_perf_sprite_overlay_us;
static uint64_t s_perf_sprite_fetch_us;
static uint64_t s_perf_sprite_store_us;
static uint64_t s_perf_sprite_cache_build_us;
static uint64_t s_perf_sprite_mask_probe_us;
static uint64_t s_perf_sprite_resolve_us;
static uint32_t s_perf_visible_lines;
static uint32_t s_perf_border_lines;
static uint32_t s_perf_masked_lines;
static uint32_t s_perf_sprite_enabled_lines;
static uint32_t s_perf_sprite_active_lines;
static uint32_t s_perf_sprite_count_sum;
static uint32_t s_perf_sprite_overlay_tiles;
static uint32_t s_perf_sprite_overlay_pixels;
static uint32_t s_perf_sprite_skip_rows;
static uint32_t s_perf_sprite_written_pixels;
static uint32_t s_perf_sprite_cache_rebuilds;
static uint32_t s_perf_sprite_cache_invalidations;
static uint32_t s_perf_sprite_cache_misses;
static uint32_t s_perf_opaque_match_tiles;
static uint32_t s_perf_opaque_mismatch_tiles;
static uint32_t s_perf_opaque_old_only_tiles;
static uint32_t s_perf_opaque_new_only_tiles;
static uint32_t s_perf_opaque_old_only_zero_tiles;
static uint32_t s_perf_opaque_old_only_partial_tiles;
static uint32_t s_perf_opaque_old_only_front_bits;
static uint32_t s_perf_opaque_old_only_behind_bits;
static uint32_t s_perf_color_match_pixels;
static uint32_t s_perf_color_mismatch_pixels;
static uint32_t s_perf_color_front_mismatch_pixels;
static uint32_t s_perf_color_behind_mismatch_pixels;
static uint32_t s_perf_sprite0_match_lines;
static uint32_t s_perf_sprite0_mismatch_lines;
static uint32_t s_perf_sprite0_old_only_lines;
static uint32_t s_perf_sprite0_new_only_lines;
static uint32_t s_perf_fine_x_lines;
static uint32_t s_perf_bg_clip_lines;
static BYTE s_perf_sprite_count_max;
static CpuPerfCounters s_perf_cpu_detail;
static BYTE s_ppu_log_first_vblank_2002;
static BYTE s_ppu_title_capture_active;
static unsigned s_ppu_title_capture_events;
static unsigned s_ppu_vram_dump_done;
static BYTE s_ppu_title_dump_palette_done;
static BYTE s_ppu_title_dump_fill_early_done;
static BYTE s_ppu_title_dump_frame_on_done;
static BYTE s_ppu_vram_dump_phase;
static BYTE s_ppu_vram_dump_armed_once;
static BYTE s_ppu_vram_dump_mask_zero_count;
static unsigned s_ppu_vram_second_window_writes;
static unsigned s_ppu_vram_second_window_2006_writes;
static BYTE s_ppu_post_window_loopy_lines;
static BYTE s_ppu_post_window_loopy_tile_logs;
static const BYTE s_ppu_enable_title_logs = 0u;
static const BYTE s_ppu_enable_second_window_logs = 0u;
static const BYTE s_ppu_enable_post_window_logs = 0u;
static const BYTE s_ppu_enable_perf_logs = 0u;
static BYTE s_ppu_vblank_side_effects_pending;
static unsigned s_ppu_vram_mid_dump_count;
static BYTE s_ppu_vram_mid_seen[0x40];

typedef struct {
    int  spr_x;
    BYTE plane0;
    BYTE plane1;
    BYTE hflip;
    BYTE behind;
    BYTE pal_sel;
    BYTE is_sprite0;
} SpriteRowInfo;

typedef struct {
    SpriteRowInfo rows[8];
    BYTE row_count;
    BYTE line_sprite_count;
    BYTE sprite_size_16;
    BYTE valid;
    BYTE scanline;
} ScanlineSpriteCache;

static ScanlineSpriteCache s_scanline_sprite_cache;
#endif

static BYTE s_sprite_buf_stamp[256];
static BYTE s_sprite_buf_stamp_gen;
static BYTE s_sprite_tile_mask[33];
static BYTE s_sprite_tile_behind_mask[33];
static BYTE s_sprite_tile_color[33][8];
static BYTE s_sprite0_tile_mask[33];

#if defined(PICO_BUILD)
static inline WORD ppu_next_cpu_clocks(void) {
    static const BYTE pattern[3] = { 113u, 114u, 114u };
    BYTE clocks = pattern[s_cpu_scanline_phase];
    s_cpu_scanline_phase++;
    if (s_cpu_scanline_phase >= 3u) {
        s_cpu_scanline_phase = 0;
    }
    return clocks;
}

static inline WORD ppu_current_scanline_clocks(void) {
    static const BYTE pattern[3] = { 113u, 114u, 114u };
    BYTE phase = s_cpu_scanline_phase;
    phase = (phase == 0u) ? 2u : (BYTE)(phase - 1u);
    return pattern[phase];
}

void RAMFUNC(PPU_SyncVBlankEdge)(void) {
    if (g_nScanLine == (VBLANK_END_SCANLINE - 1) &&
        s_ppu_vblank_clear_pending &&
        g_wPassedClocks + 1u >= ppu_current_scanline_clocks()) {
        s_ppu_vblank_clear_pending = 0;
        PPU_R2 &= ~(BYTE)R2_IN_VBLANK;
        s_ppu_suppress_next_vblank = 0;
    }

    if (g_nScanLine != VBLANK_SCANLINE) {
        return;
    }

    if (s_ppu_vblank_pending && g_wPassedClocks >= 1u) {
        s_ppu_vblank_pending = 0;
        if (s_ppu_suppress_next_vblank) {
            s_ppu_suppress_next_vblank = 0;
            s_ppu_nmi_pending = 0;
        } else {
            PPU_R2 |= R2_IN_VBLANK;
        }
    }

    if (s_ppu_nmi_pending && g_wPassedClocks >= 4u) {
        s_ppu_nmi_pending = 0;
        if ((PPU_R2 & R2_IN_VBLANK) && R0_NMI_VB) {
            NMI_State = (NMI_Wiring == 0) ? 1 : 0;
        }
    }
}

BYTE PPU_DiagSecondWindowActive(void) {
#if defined(NESCO_RUNTIME_LOGS)
    return (BYTE)(s_ppu_vram_dump_phase == 1u);
#else
    return 0u;
#endif
}
#endif

#if defined(PICO_BUILD)
static void ppu_perf_poll(void) {
    if (s_ppu_enable_perf_logs == 0u) {
        return;
    }
    const uint64_t now_us = time_us_64();
    if (s_perf_last_us == 0) {
        s_perf_last_us = now_us;
        return;
    }
    if (now_us - s_perf_last_us >= 1000000u) {
        const uint32_t cpu_hz = s_perf_cpu_cycles;
        const uint32_t cpu_pct = (cpu_hz * 100u) / 1789773u;
        CPU_PerfSnapshot(&s_perf_cpu_detail);
        printf("[PERF] frames=%lu scan=%lu cpu_hz=%lu cpu_pct=%lu%% cpu_us=%llu apu_us=%llu draw_us=%llu tail_us=%llu cpu_prg_fast=%lu cpu_stack=%lu cpu_zp=%lu cpu_zp_w=%lu cpu_zp_ptr=%lu cpu_zp_rmw=%lu spr_comp_us=%llu spr_expand_us=%llu spr_overlay_us=%llu spr_mask_us=%llu spr_resolve_us=%llu spr_cache_us=%llu ovl_tiles=%lu ovl_pixels=%lu spr_cache_miss=%lu opaque_match=%lu opaque_mismatch=%lu opaque_old_only=%lu opaque_new_only=%lu opaque_old_zero=%lu opaque_old_partial=%lu opaque_old_front_bits=%lu opaque_old_behind_bits=%lu color_match=%lu color_mismatch=%lu color_front_mismatch=%lu color_behind_mismatch=%lu spr0_match=%lu spr0_mismatch=%lu spr0_old_only=%lu spr0_new_only=%lu\n",
               (unsigned long)s_perf_frames,
               (unsigned long)s_perf_scanlines,
               (unsigned long)cpu_hz,
               (unsigned long)cpu_pct,
               (unsigned long long)s_perf_cpu_us,
               (unsigned long long)s_perf_apu_us,
               (unsigned long long)s_perf_draw_us,
               (unsigned long long)s_perf_tail_us,
               (unsigned long)s_perf_cpu_detail.prg_fast_reads,
               (unsigned long)s_perf_cpu_detail.stack_ops,
               (unsigned long)s_perf_cpu_detail.zp_reads,
               (unsigned long)s_perf_cpu_detail.zp_writes,
               (unsigned long)s_perf_cpu_detail.zp_ptr_reads,
               (unsigned long)s_perf_cpu_detail.zp_rmw_reads,
               (unsigned long long)s_perf_sprite_comp_us,
               (unsigned long long)s_perf_sprite_store_us,
               (unsigned long long)s_perf_sprite_overlay_us,
               (unsigned long long)s_perf_sprite_mask_probe_us,
               (unsigned long long)s_perf_sprite_resolve_us,
               (unsigned long long)s_perf_sprite_cache_build_us,
               (unsigned long)s_perf_sprite_overlay_tiles,
               (unsigned long)s_perf_sprite_overlay_pixels,
               (unsigned long)s_perf_sprite_cache_misses,
               (unsigned long)s_perf_opaque_match_tiles,
               (unsigned long)s_perf_opaque_mismatch_tiles,
               (unsigned long)s_perf_opaque_old_only_tiles,
               (unsigned long)s_perf_opaque_new_only_tiles,
               (unsigned long)s_perf_opaque_old_only_zero_tiles,
               (unsigned long)s_perf_opaque_old_only_partial_tiles,
               (unsigned long)s_perf_opaque_old_only_front_bits,
               (unsigned long)s_perf_opaque_old_only_behind_bits,
               (unsigned long)s_perf_color_match_pixels,
               (unsigned long)s_perf_color_mismatch_pixels,
               (unsigned long)s_perf_color_front_mismatch_pixels,
               (unsigned long)s_perf_color_behind_mismatch_pixels,
               (unsigned long)s_perf_sprite0_match_lines,
               (unsigned long)s_perf_sprite0_mismatch_lines,
               (unsigned long)s_perf_sprite0_old_only_lines,
               (unsigned long)s_perf_sprite0_new_only_lines);
        fflush(stdout);
        CPU_PerfReset();
        s_perf_frames = 0;
        s_perf_scanlines = 0;
        s_perf_cpu_cycles = 0;
        s_perf_cpu_us = 0;
        s_perf_apu_us = 0;
        s_perf_draw_us = 0;
        s_perf_tail_us = 0;
        s_perf_sprite_comp_us = 0;
        s_perf_sprite_overlay_us = 0;
        s_perf_sprite_fetch_us = 0;
        s_perf_sprite_store_us = 0;
        s_perf_sprite_cache_build_us = 0;
        s_perf_sprite_mask_probe_us = 0;
        s_perf_sprite_resolve_us = 0;
        s_perf_visible_lines = 0;
        s_perf_border_lines = 0;
        s_perf_masked_lines = 0;
        s_perf_sprite_enabled_lines = 0;
        s_perf_sprite_active_lines = 0;
        s_perf_sprite_count_sum = 0;
        s_perf_sprite_overlay_tiles = 0;
        s_perf_sprite_overlay_pixels = 0;
        s_perf_sprite_skip_rows = 0;
        s_perf_sprite_written_pixels = 0;
        s_perf_sprite_cache_rebuilds = 0;
        s_perf_sprite_cache_invalidations = 0;
        s_perf_sprite_cache_misses = 0;
        s_perf_opaque_match_tiles = 0;
        s_perf_opaque_mismatch_tiles = 0;
        s_perf_opaque_old_only_tiles = 0;
        s_perf_opaque_new_only_tiles = 0;
        s_perf_opaque_old_only_zero_tiles = 0;
        s_perf_opaque_old_only_partial_tiles = 0;
        s_perf_opaque_old_only_front_bits = 0;
        s_perf_opaque_old_only_behind_bits = 0;
        s_perf_color_match_pixels = 0;
        s_perf_color_mismatch_pixels = 0;
        s_perf_color_front_mismatch_pixels = 0;
        s_perf_color_behind_mismatch_pixels = 0;
        s_perf_sprite0_match_lines = 0;
        s_perf_sprite0_mismatch_lines = 0;
        s_perf_sprite0_old_only_lines = 0;
        s_perf_sprite0_new_only_lines = 0;
        s_perf_fine_x_lines = 0;
        s_perf_bg_clip_lines = 0;
        s_perf_sprite_count_max = 0;
        memset(&s_perf_cpu_detail, 0, sizeof(s_perf_cpu_detail));
        s_perf_last_us = now_us;
    }
}

static void ppu_diag_dump_range(const char *tag, WORD start, WORD end) {
    WORD addr = start;
    while (addr <= end) {
        printf("[%s] %04X:", tag, addr);
        for (unsigned i = 0; i < 16u && addr <= end; i++, addr++) {
            printf(" %02X", PPU_VRamRead(addr));
        }
        printf("\n");
    }
}

static void ppu_diag_dump_title_vram(const char *label) {
    if (s_ppu_vram_dump_done >= 1024u) {
        return;
    }
    s_ppu_vram_dump_done++;
    printf("[PPU_DUMP] %s scan=%d r1=%02X addr=%04X temp=%04X latch=%u fineX=%u\n",
           label,
           g_nScanLine,
           (unsigned int)PPU_R1,
           PPU_Addr & 0x3FFFu,
           PPU_Temp & 0x3FFFu,
           (unsigned int)PPU_Latch,
           (unsigned int)(PPU_Scr_H_Bit & 0x07u));
    ppu_diag_dump_range("NT0", 0x2000u, 0x23FFu);
    ppu_diag_dump_range("NT1", 0x2400u, 0x27FFu);
    ppu_diag_dump_range("PAL", 0x3F00u, 0x3F1Fu);
    printf("[PPU_DUMP] %s end\n", label);
    fflush(stdout);
}

static void ppu_log_title_capture_event(const char *tag, BYTE data, BYTE phase) {
    if (!s_ppu_enable_title_logs || !s_ppu_title_capture_active) {
        return;
    }

    s_ppu_title_capture_events++;
    printf("[%s] n=%u pc=%04X data=%02X phase=%u addr=%04X temp=%04X latch=%u r0=%02X r1=%02X scan=%d clocks=%u\n",
           tag,
           s_ppu_title_capture_events,
           PC,
           data,
           (unsigned int)phase,
           PPU_Addr & 0x3FFFu,
           PPU_Temp & 0x3FFFu,
           (unsigned int)PPU_Latch,
           (unsigned int)PPU_R0,
           (unsigned int)PPU_R1,
           g_nScanLine,
           (unsigned int)g_wPassedClocks);
    fflush(stdout);

    if (!s_ppu_title_dump_palette_done && s_ppu_title_capture_events >= 38u) {
        s_ppu_title_dump_palette_done = 1u;
        ppu_diag_dump_title_vram("title_palette_done");
    }
    if (!s_ppu_title_dump_fill_early_done && s_ppu_title_capture_events >= 96u) {
        s_ppu_title_dump_fill_early_done = 1u;
        ppu_diag_dump_title_vram("title_fill_early");
    }
    if (s_ppu_title_capture_events >= 224u) {
        s_ppu_title_capture_active = 0u;
    }
}

static void ppu_log_second_window_event(const char *tag, BYTE data, BYTE phase) {
    if (!s_ppu_enable_second_window_logs ||
        s_ppu_vram_dump_phase != 1u ||
        PPU_R1 != 0x00u) {
        return;
    }

    printf("[%s] n=%u w=%u pc=%04X data=%02X phase=%u addr=%04X temp=%04X latch=%u r0=%02X r1=%02X r2=%02X scan=%d clocks=%u\n",
           tag,
           s_ppu_vram_second_window_2006_writes,
           s_ppu_vram_second_window_writes,
           PC,
           data,
           (unsigned int)phase,
           PPU_Addr & 0x3FFFu,
           PPU_Temp & 0x3FFFu,
           (unsigned int)PPU_Latch,
           (unsigned int)PPU_R0,
           (unsigned int)PPU_R1,
           (unsigned int)PPU_R2,
           g_nScanLine,
           (unsigned int)g_wPassedClocks);
    fflush(stdout);
}

static void ppu_log_post_window_loopy(const char *tag, BYTE phase) {
    if (!s_ppu_enable_post_window_logs ||
        s_ppu_vram_dump_phase != 2u ||
        s_ppu_post_window_loopy_lines == 0u) {
        return;
    }

    printf("[%s] line=%u tile=%u phase=%u addr=%04X temp=%04X latch=%u r0=%02X r1=%02X r2=%02X scan=%d clocks=%u\n",
           tag,
           (unsigned int)s_ppu_post_window_loopy_lines,
           (unsigned int)s_ppu_post_window_loopy_tile_logs,
           (unsigned int)phase,
           PPU_Addr & 0x3FFFu,
           PPU_Temp & 0x3FFFu,
           (unsigned int)PPU_Latch,
           (unsigned int)PPU_R0,
           (unsigned int)PPU_R1,
           (unsigned int)PPU_R2,
           g_nScanLine,
           (unsigned int)g_wPassedClocks);
    fflush(stdout);
}

static void ppu_log_post_window_probe_start(void) {
    if (!s_ppu_enable_post_window_logs ||
        s_ppu_vram_dump_phase != 2u ||
        s_ppu_post_window_loopy_lines == 0u) {
        return;
    }

    printf("[PPU_LOOPY_START] line=%u addr=%04X temp=%04X latch=%u r0=%02X r1=%02X r2=%02X scan=%d clocks=%u\n",
           (unsigned int)s_ppu_post_window_loopy_lines,
           PPU_Addr & 0x3FFFu,
           PPU_Temp & 0x3FFFu,
           (unsigned int)PPU_Latch,
           (unsigned int)PPU_R0,
           (unsigned int)PPU_R1,
           (unsigned int)PPU_R2,
           g_nScanLine,
           (unsigned int)g_wPassedClocks);
    fflush(stdout);
}

static void ppu_log_post_window_drawline(const char *tag, int scanline) {
    if (!s_ppu_enable_post_window_logs ||
        s_ppu_vram_dump_phase != 2u ||
        s_ppu_post_window_loopy_lines == 0u) {
        return;
    }

    printf("[%s] line=%u scan=%d updown=%u render=%u addr=%04X temp=%04X latch=%u r0=%02X r1=%02X r2=%02X clocks=%u\n",
           tag,
           (unsigned int)s_ppu_post_window_loopy_lines,
           scanline,
           (unsigned int)PPU_UpDown_Clip,
           (unsigned int)(R1_RENDERING != 0),
           PPU_Addr & 0x3FFFu,
           PPU_Temp & 0x3FFFu,
           (unsigned int)PPU_Latch,
           (unsigned int)PPU_R0,
           (unsigned int)PPU_R1,
           (unsigned int)PPU_R2,
           (unsigned int)g_wPassedClocks);
    fflush(stdout);
}
#endif


#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
static unsigned s_ppu_log_r0_count;
static unsigned s_ppu_log_2005_count;
static unsigned s_ppu_log_2006_count;
static unsigned s_ppu_log_2007_count;
static BYTE s_ppu_bg_capture_active;
static BYTE s_ppu_bg_capture_done;

static void ppu_diag_begin_bg_capture(void) {
    if (s_ppu_bg_capture_done) {
        return;
    }
    s_ppu_bg_capture_done = 1;
    s_ppu_bg_capture_active = 1;
    s_ppu_log_2005_count = 0;
    s_ppu_log_2006_count = 0;
    s_ppu_log_2007_count = 0;
    printf("[PPU_BG] capture_start scan=%d addr=%04X temp=%04X latch=%u r1=%02X\n",
           g_nScanLine,
           PPU_Addr & 0x3FFFu,
           PPU_Temp & 0x3FFFu,
           (unsigned int)PPU_Latch,
           (unsigned int)PPU_R1);
    fflush(stdout);
}

static void ppu_diag_log_r0(BYTE data) {
    if (s_ppu_log_r0_count++ < 16u) {
        printf("[PPU] $2000=%02X inc=%u nt=%u bg=%u sp=%u nmi=%u\r\n",
               data,
               (unsigned int)((data >> 2) & 1u),
               (unsigned int)(data & 0x03u),
               (unsigned int)((data >> 4) & 1u),
               (unsigned int)((data >> 3) & 1u),
               (unsigned int)((data >> 7) & 1u));
        fflush(stdout);
    }
}

static void ppu_diag_log_2006(BYTE data, BYTE latch_before, WORD temp_after, WORD addr_after) {
    if (s_ppu_bg_capture_active && s_ppu_log_2006_count++ < 64u) {
        printf("[PPU] $2006 phase=%u data=%02X temp=%04X addr=%04X\r\n",
               (unsigned int)latch_before,
               data,
               temp_after & 0x3FFFu,
               addr_after & 0x3FFFu);
        fflush(stdout);
    }
}

static void ppu_diag_log_2005(BYTE data, BYTE latch_before, WORD temp_after, BYTE fine_x_after) {
    if (s_ppu_bg_capture_active && s_ppu_log_2005_count++ < 32u) {
        printf("[PPU] $2005 phase=%u data=%02X fineX=%u coarseX=%u coarseY=%u fineY=%u temp=%04X\r\n",
               (unsigned int)latch_before,
               data,
               (unsigned int)(fine_x_after & 0x07u),
               (unsigned int)(temp_after & 0x001Fu),
               (unsigned int)((temp_after >> 5) & 0x1Fu),
               (unsigned int)((temp_after >> 12) & 0x07u),
               temp_after & 0x3FFFu);
        fflush(stdout);
    }
}

static void ppu_diag_log_2007(BYTE data, WORD addr_before, WORD addr_after) {
    if (s_ppu_bg_capture_active && s_ppu_log_2007_count++ < 96u) {
        printf("[PPU] $2007 data=%02X addr=%04X->%04X render=%u scan=%d inc=%u\r\n",
               data,
               addr_before & 0x3FFFu,
               addr_after & 0x3FFFu,
               (unsigned int)((R1_RENDERING != 0) && (g_nScanLine < 240)),
               g_nScanLine,
               (unsigned int)R0_INC_ADDR);
        fflush(stdout);
        if (s_ppu_log_2007_count >= 96u) {
            s_ppu_bg_capture_active = 0;
        }
    }
}
#endif

/* =====================================================================
 *  Palette RAM (32 bytes, managed here)
 * ===================================================================== */
BYTE g_PalRAM[32];

/* =====================================================================
 *  Loopy address model bit fields
 *  PPU_Addr / PPU_Temp layout:
 *    bit [4:0]   = coarse-X
 *    bit [9:5]   = coarse-Y
 *    bit [11:10] = nametable select (NT X = bit10, NT Y = bit11)
 *    bit [14:12] = fine-Y
 * ===================================================================== */
#define LOOPY_COARSE_X(v)   ((v) & 0x001Fu)
#define LOOPY_COARSE_Y(v)   (((v) >> 5) & 0x001Fu)
#define LOOPY_NT(v)         (((v) >> 10) & 0x0003u)
#define LOOPY_FINE_Y(v)     (((v) >> 12) & 0x0007u)

/* =====================================================================
 *  Sprite line buffer (local to DrawLine)
 *  Holds composited sprite pixels for one scanline.
 *  sprite_buf[x] = (palette_color << 2) | priority_flag | transparent_flag
 * ===================================================================== */
#define SPR_TRANSPARENT  0x00u
#define SPR_BEHIND_BG    0x01u    /* bit 0: behind-BG priority */
#define SPR_OPAQUE       0x02u    /* bit 1: pixel is opaque */

/* =====================================================================
 *  PPU_Init
 * ===================================================================== */
void PPU_Init(void) {
    PPU_R0 = 0;
    PPU_R1 = 0;
    PPU_R2 = 0;
    PPU_R3 = 0;
    PPU_R7 = 0;
    PPU_Addr = 0;
    PPU_Temp = 0;
    PPU_Scr_H_Bit = 0;
    PPU_Latch = 0;
    g_nScanLine = 261;
    g_byFrameSkip = 0;
    s_cpu_scanline_phase = 0;
    s_ppu_odd_frame = 0;
    s_ppu_suppress_next_vblank = 0;
    s_ppu_vblank_pending = 0;
    s_ppu_nmi_pending = 0;
    s_ppu_vblank_clear_pending = 0;
#if defined(PICO_BUILD)
    s_perf_scanlines = 0;
    s_perf_frames = 0;
    s_perf_last_us = 0;
    s_perf_cpu_cycles = 0;
    s_perf_cpu_us = 0;
    s_perf_apu_us = 0;
    s_perf_draw_us = 0;
    s_perf_tail_us = 0;
    s_perf_sprite_comp_us = 0;
    s_perf_sprite_overlay_us = 0;
    s_perf_sprite_fetch_us = 0;
    s_perf_sprite_store_us = 0;
    s_perf_sprite_cache_build_us = 0;
    s_perf_sprite_mask_probe_us = 0;
    s_perf_sprite_resolve_us = 0;
    s_perf_visible_lines = 0;
    s_perf_border_lines = 0;
    s_perf_masked_lines = 0;
    s_perf_sprite_enabled_lines = 0;
    s_perf_sprite_active_lines = 0;
    s_perf_sprite_count_sum = 0;
    s_perf_sprite_overlay_tiles = 0;
    s_perf_sprite_skip_rows = 0;
    s_perf_sprite_written_pixels = 0;
    s_perf_sprite_cache_rebuilds = 0;
    s_perf_sprite_cache_invalidations = 0;
    s_perf_sprite_cache_misses = 0;
    s_perf_fine_x_lines = 0;
    s_perf_bg_clip_lines = 0;
    s_perf_sprite_count_max = 0;
    memset(&s_perf_cpu_detail, 0, sizeof(s_perf_cpu_detail));
    CPU_PerfReset();
    s_scanline_sprite_cache.valid = 0u;
    memset(s_sprite_buf_stamp, 0, sizeof(s_sprite_buf_stamp));
    s_sprite_buf_stamp_gen = 0u;
    s_ppu_log_first_vblank_2002 = 1;
    s_ppu_title_capture_active = 0u;
    s_ppu_title_capture_events = 0u;
    s_ppu_vram_dump_done = 0;
    s_ppu_title_dump_palette_done = 0u;
    s_ppu_title_dump_fill_early_done = 0u;
    s_ppu_title_dump_frame_on_done = 0u;
    s_ppu_vram_dump_phase = 0;
    s_ppu_vram_dump_armed_once = 0;
    s_ppu_vram_dump_mask_zero_count = 0;
    s_ppu_vram_second_window_writes = 0u;
    s_ppu_vram_mid_dump_count = 0u;
    s_ppu_post_window_loopy_lines = 0u;
    s_ppu_post_window_loopy_tile_logs = 0u;
    memset(s_ppu_vram_mid_seen, 0, sizeof(s_ppu_vram_mid_seen));
#endif
#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
    s_ppu_log_r0_count = 0;
    s_ppu_log_2005_count = 0;
    s_ppu_log_2006_count = 0;
    s_ppu_log_2007_count = 0;
    s_ppu_bg_capture_active = 0;
    s_ppu_bg_capture_done = 0;
#endif
    memset(PPURAM, 0, sizeof(PPURAM));
    memset(g_PalRAM, 0, sizeof(g_PalRAM));
    memset(SPRRAM, 0, SPRRAM_SIZE);
    memset(PPU_LineBuf, 0, sizeof(PPU_LineBuf));
    g_wScanLine = PPU_LineBuf;
}

/* =====================================================================
 *  PPU VRAM read / write  (through PPUBANK)
 * ===================================================================== */
BYTE PPU_VRamRead(WORD addr) {
    addr &= 0x3FFFu;
    if (addr < 0x2000u) {
        WORD bank = (addr >> 10) & 0x07u;
        return PPUBANK[bank][addr & 0x3FFu];
    }
    if (addr < 0x3000u) {
        WORD nt_idx = ((addr - 0x2000u) >> 10) & 0x03u;
        return PPUBANK[8 + nt_idx][(addr) & 0x3FFu];
    }
    if (addr < 0x3F00u) {
        /* Mirror of $2000–$2EFF */
        return PPU_VRamRead(addr - 0x1000u);
    }
    /* Palette RAM $3F00–$3F1F mirrored to $3FFF */
    addr &= 0x1Fu;
    /* Addresses $3F10, $3F14, $3F18, $3F1C mirror $3F00, $3F04, ... */
    if ((addr & 0x13u) == 0x10u) addr &= 0x0Fu;
    return g_PalRAM[addr];
}

void PPU_VRamWrite(WORD addr, BYTE data) {
    addr &= 0x3FFFu;
    if (addr < 0x2000u) {
        /* CHR write is valid only for CHR-RAM; CHR-ROM writes are ignored. */
        if (NesHeader.byVRomSize > 0 && VROM != NULL) {
            return;
        }
        WORD bank = addr >> 10;
        WORD off  = addr & 0x3FFu;
        ((BYTE *)PPUBANK[bank])[off] = data;
        return;
    }
    if (addr < 0x3000u) {
        WORD nt_idx = ((addr - 0x2000u) >> 10) & 0x03u;
        ((BYTE *)PPUBANK[8 + nt_idx])[addr & 0x3FFu] = data;
        return;
    }
    if (addr < 0x3F00u) {
        PPU_VRamWrite(addr - 0x1000u, data);
        return;
    }
    /* Palette RAM */
    addr &= 0x1Fu;
    if ((addr & 0x13u) == 0x10u) addr &= 0x0Fu;
    g_PalRAM[addr] = data & 0x3Fu;
}

/* =====================================================================
 *  PPU_RegisterRead — CPU read from $2000–$2007 (reg = addr & 7)
 * ===================================================================== */
BYTE RAMFUNC(PPU_RegisterRead)(BYTE reg) {
    switch (reg) {
    case 2: { /* $2002: Status */
        PPU_SyncVBlankEdge();
        BYTE status = PPU_R2;
#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
        if (s_ppu_enable_title_logs &&
            s_ppu_log_first_vblank_2002 &&
            (status & R2_IN_VBLANK)) {
            s_ppu_log_first_vblank_2002 = 0;
            s_ppu_title_capture_active = 1u;
            s_ppu_title_capture_events = 0u;
            printf("[PPU_INIT] first_vblank_$2002=%02X pc=%04X scan=%d clocks=%u addr=%04X temp=%04X latch=%u fineX=%u\r\n",
                   status,
                   PC,
                   g_nScanLine,
                   (unsigned int)g_wPassedClocks,
                   PPU_Addr & 0x3FFFu,
                   PPU_Temp & 0x3FFFu,
                   (unsigned int)PPU_Latch,
                   (unsigned int)(PPU_Scr_H_Bit & 0x07u));
            fflush(stdout);
        }
#endif
        if (g_nScanLine == (VBLANK_SCANLINE - 1) &&
            g_wPassedClocks + 1u == ppu_current_scanline_clocks()) {
            s_ppu_suppress_next_vblank = 1;
        }
        PPU_R2 &= ~(BYTE)R2_IN_VBLANK;  /* Clear VBlank flag on read */
        PPU_Latch = 0;                   /* Reset address latch */
        return status;
    }
    case 4: /* $2004: OAM data read */
        return SPRRAM[PPU_R3++];
    case 7: { /* $2007: VRAM data (buffered) */
        BYTE ret = PPU_R7;
        WORD addr = PPU_Addr & 0x3FFFu;
        if (addr >= 0x3F00u) {
            /* Palette reads are immediate, but still fill the delayed buffer
             * from the underlying nametable address hidden by palette space. */
            ret = PPU_VRamRead(addr);
            PPU_R7 = PPU_VRamRead(addr & 0x2FFFu);
        } else {
            PPU_R7 = PPU_VRamRead(addr);
        }
        /* Increment address */
        PPU_Addr += R0_INC_ADDR ? 32u : 1u;
        return ret;
    }
    default:
        return 0x00u;  /* $2000, $2001, $2003, $2005, $2006 are write-only */
    }
}

/* =====================================================================
 *  PPU_RegisterWrite — CPU write to $2000–$2007
 * ===================================================================== */
void RAMFUNC(PPU_RegisterWrite)(BYTE reg, BYTE data) {
    switch (reg) {
    case 0: /* $2000: Control 1 */
        {
        const BYTE old_r0 = PPU_R0;
        PPU_R0 = data;
        /* Update PPU_Temp nametable bits [11:10] from R0 bits [1:0] */
        PPU_Temp = (PPU_Temp & ~0x0C00u) | ((WORD)(data & 0x03u) << 10);
        if ((old_r0 ^ data) & R0_SP_SIZE) {
            PPU_InvalidateSpriteScanlineCache();
        }
        if (!(old_r0 & 0x80u) && (data & 0x80u) && (PPU_R2 & R2_IN_VBLANK)) {
            s_ppu_nmi_pending = 1u;
        }
#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
        ppu_log_title_capture_event("PPU_TITLE_W2000", data, 0u);
        ppu_log_second_window_event("PPU_WIN_W2000", data, 0u);
#endif
#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
        ppu_diag_log_r0(data);
#endif
        break;
        }

    case 1: /* $2001: Control 2 / Mask */
        PPU_R1 = data;
#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
        ppu_log_title_capture_event("PPU_TITLE_W2001", data, 0u);
        if (s_ppu_enable_title_logs &&
            !s_ppu_title_dump_frame_on_done &&
            s_ppu_log_first_vblank_2002 == 0u &&
            data != 0x00u) {
            s_ppu_title_dump_frame_on_done = 1u;
            ppu_diag_dump_title_vram("frame_on");
        }
#endif
#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
        if (s_ppu_enable_second_window_logs &&
            data == 0x00u &&
            s_ppu_vram_dump_phase == 0u &&
            s_ppu_vram_dump_armed_once == 0u) {
            s_ppu_vram_dump_mask_zero_count++;
            if (s_ppu_vram_dump_mask_zero_count >= 2u) {
                s_ppu_vram_dump_phase = 1u;
                s_ppu_vram_dump_done = 0u;
                s_ppu_vram_dump_armed_once = 1u;
                s_ppu_vram_second_window_writes = 0u;
                s_ppu_vram_second_window_2006_writes = 0u;
                s_ppu_vram_mid_dump_count = 0u;
                memset(s_ppu_vram_mid_seen, 0, sizeof(s_ppu_vram_mid_seen));
            }
        } else if (s_ppu_enable_post_window_logs &&
                   data != 0x00u &&
                   s_ppu_vram_dump_phase == 1u) {
            ppu_diag_dump_title_vram("end_window");
            s_ppu_vram_dump_phase = 2u;
            s_ppu_post_window_loopy_lines = 32u;
            s_ppu_post_window_loopy_tile_logs = 0u;
            ppu_log_post_window_probe_start();
        }
#endif
#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
        if (data == 0x00u) {
            ppu_diag_begin_bg_capture();
        }
#endif
        break;

    case 2: /* $2002: read-only; write ignored */
        break;

    case 3: /* $2003: OAM address */
        PPU_R3 = data;
        break;

    case 4: /* $2004: OAM data write */
        SPRRAM[PPU_R3++] = data;
        PPU_InvalidateSpriteScanlineCache();
        break;

    case 5: /* $2005: Scroll */
        if (PPU_Latch == 0) {
            /* First write: horizontal scroll */
            PPU_Temp = (PPU_Temp & ~0x001Fu) | ((WORD)(data >> 3) & 0x001Fu);
            PPU_Scr_H_Bit = data & 0x07u;
#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
            ppu_log_title_capture_event("PPU_TITLE_W2005", data, 0u);
            ppu_log_second_window_event("PPU_WIN_W2005", data, 0u);
#endif
#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
            ppu_diag_log_2005(data, PPU_Latch, PPU_Temp, PPU_Scr_H_Bit);
#endif
            PPU_Latch = 1;
        } else {
            /* Second write: vertical scroll */
            PPU_Temp = (PPU_Temp & ~0x73E0u)
                     | ((WORD)(data & 0x07u) << 12)   /* fine-Y  [14:12] */
                     | ((WORD)(data >> 3)    << 5);    /* coarse-Y [9:5]  */
#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
            ppu_log_title_capture_event("PPU_TITLE_W2005", data, 1u);
            ppu_log_second_window_event("PPU_WIN_W2005", data, 1u);
#endif
#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
            ppu_diag_log_2005(data, PPU_Latch, PPU_Temp, PPU_Scr_H_Bit);
#endif
            PPU_Latch = 0;
        }
        break;

    case 6: /* $2006: VRAM address */
        if (PPU_Latch == 0) {
            /* First write: high 6 bits of address into PPU_Temp [13:8] */
            PPU_Temp = (PPU_Temp & 0x00FFu) | ((WORD)(data & 0x3Fu) << 8);
#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
            ppu_log_title_capture_event("PPU_TITLE_W2006", data, 0u);
#endif
#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
            if (s_ppu_vram_dump_phase == 1u && PPU_R1 == 0x00u) {
                s_ppu_vram_second_window_2006_writes++;
                printf("[PPU_W2006] n=%u w=%u pc=%04X phase=0 data=%02X temp=%04X addr=%04X r1=%02X scan=%d clocks=%u\n",
                       s_ppu_vram_second_window_2006_writes,
                       s_ppu_vram_second_window_writes,
                       PC,
                       data,
                       PPU_Temp & 0x3FFFu,
                       PPU_Addr & 0x3FFFu,
                       (unsigned int)PPU_R1,
                       g_nScanLine,
                       (unsigned int)g_wPassedClocks);
            }
#endif
#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
            ppu_diag_log_2006(data, PPU_Latch, PPU_Temp, PPU_Addr);
#endif
            PPU_Latch = 1;
        } else {
            /* Second write: low 8 bits; copy PPU_Temp → PPU_Addr */
            PPU_Temp = (PPU_Temp & 0xFF00u) | (WORD)data;
            PPU_Addr = PPU_Temp;
            if ((PPU_Addr & 0x3FFFu) < 0x2000u) {
                /* Match the observed InfoNES/fixNES behavior: pattern-table
                 * addresses prime the PPUDATA read buffer immediately after the
                 * second $2006 write so the next $2007 access is aligned. */
                PPU_R7 = PPU_VRamRead(PPU_Addr);
            }
            InfoNES_SetupScr();
#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
            ppu_log_title_capture_event("PPU_TITLE_W2006", data, 1u);
            if (s_ppu_vram_dump_phase == 1u && PPU_R1 == 0x00u) {
                s_ppu_vram_second_window_2006_writes++;
                printf("[PPU_W2006] n=%u w=%u pc=%04X phase=1 data=%02X temp=%04X addr=%04X r1=%02X scan=%d clocks=%u\n",
                       s_ppu_vram_second_window_2006_writes,
                       s_ppu_vram_second_window_writes,
                       PC,
                       data,
                       PPU_Temp & 0x3FFFu,
                       PPU_Addr & 0x3FFFu,
                       (unsigned int)PPU_R1,
                       g_nScanLine,
                       (unsigned int)g_wPassedClocks);
            }
            if (s_ppu_vram_dump_phase == 1u &&
                PPU_R1 == 0x00u &&
                (PPU_Addr & 0x3FFFu) >= 0x2180u &&
                (PPU_Addr & 0x3FFFu) <= 0x21BFu &&
                s_ppu_vram_mid_dump_count < 1000u) {
                unsigned idx = (unsigned)((PPU_Addr & 0x3FFFu) - 0x2180u);
                if (s_ppu_vram_mid_seen[idx] == 0u) {
                    char label[40];
                    s_ppu_vram_mid_seen[idx] = 1u;
                    s_ppu_vram_mid_dump_count++;
                    snprintf(label, sizeof(label), "mid_%02u_w%u_a%04X",
                             s_ppu_vram_mid_dump_count,
                             s_ppu_vram_second_window_writes,
                             PPU_Addr & 0x3FFFu);
                    ppu_diag_dump_title_vram(label);
                }
            }
#endif
#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
            ppu_diag_log_2006(data, PPU_Latch, PPU_Temp, PPU_Addr);
#endif
            PPU_Latch = 0;
        }
        break;

    case 7: /* $2007: VRAM write */
        WORD addr_before = PPU_Addr;
#if !defined(NESCO_RUNTIME_LOGS) && !defined(NESCO_DIAGNOSTICS)
        (void)addr_before;
#endif
        PPU_VRamWrite(PPU_Addr, data);
#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
        ppu_log_title_capture_event("PPU_TITLE_W2007", data, 0u);
#endif
#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
        if (s_ppu_vram_dump_phase == 1u &&
            PPU_R1 == 0x00u &&
            ((addr_before & 0x3FFFu) >= 0x2000u) &&
            ((addr_before & 0x3FFFu) <= 0x27FFu)) {
            unsigned write_index = s_ppu_vram_second_window_writes + 1u;
            unsigned inc = R0_INC_ADDR ? 32u : 1u;
            WORD addr_after = (WORD)((addr_before + inc) & 0x3FFFu);
            s_ppu_vram_second_window_writes = write_index;
            printf("[PPU_W2007] w=%u pc=%04X val=%02X before=%04X after=%04X inc=%u r1=%02X scan=%d clocks=%u\n",
                   write_index,
                   PC,
                   data,
                   addr_before & 0x3FFFu,
                   addr_after & 0x3FFFu,
                   inc,
                   (unsigned int)PPU_R1,
                   g_nScanLine,
                   (unsigned int)g_wPassedClocks);
        }
#endif
        /*
         * Lode Runner and fixNES both expect PPUDATA writes to advance with the
         * control-register increment amount, even when rendering is enabled.
         * The earlier loopy-style rendering increment corrupted mid-frame bulk
         * nametable fills and produced stable title/background corruption.
         */
        PPU_Addr += R0_INC_ADDR ? 32u : 1u;
#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
        ppu_diag_log_2007(data, addr_before, PPU_Addr);
#endif
        break;
    }
}

/* =====================================================================
 *  Horizontal scroll advance after each tile (coarse-X increment + NT wrap)
 * ===================================================================== */
static inline void loopy_coarse_x_inc(void) {
    if ((PPU_Addr & 0x001Fu) == 31u) {
        PPU_Addr &= ~0x001Fu;   /* coarse-X = 0 */
        PPU_Addr ^=  0x0400u;   /* Flip NT-X */
    } else {
        PPU_Addr++;
    }
}

/* Vertical scroll advance at end of scanline */
static inline void loopy_fine_y_inc(void) {
    if ((PPU_Addr & 0x7000u) != 0x7000u) {
        PPU_Addr += 0x1000u;  /* Increment fine-Y */
    } else {
        PPU_Addr &= ~0x7000u;
        WORD coarse_y = (PPU_Addr >> 5) & 0x1Fu;
        if (coarse_y == 29u) {
            coarse_y = 0;
            PPU_Addr ^= 0x0800u;  /* Flip NT-Y */
        } else if (coarse_y == 31u) {
            coarse_y = 0;  /* Wrap without flip (out-of-range) */
        } else {
            coarse_y++;
        }
        PPU_Addr = (PPU_Addr & ~0x03E0u) | (coarse_y << 5);
    }
}

/* Reload horizontal position from PPU_Temp at start of visible scanline */
static inline void loopy_reload_x(void) {
    /* coarse-X and NT-X from t → v */
    PPU_Addr = (PPU_Addr & ~0x041Fu) | (PPU_Temp & 0x041Fu);
}

/* =====================================================================
 *  Sprite composite for one scanline
 *  Fills sprite_buf[256] with composited sprite pixels.
 *  Returns sprite-0 hit X coordinate (-1 if no hit).
 * ===================================================================== */
void PPU_InvalidateSpriteScanlineCache(void) {
    s_scanline_sprite_cache.valid = 0u;
#if defined(PICO_BUILD)
    s_perf_sprite_cache_invalidations++;
#endif
}

static void RAMFUNC(ppu_build_sprite_scanline_cache)(int scanline) {
    int sprite_height = R0_SP_SIZE ? 16 : 8;
    int n_rendered = 0;
    int row_count = 0;
    bool clip_left = (R1_CLIP_SP != 0);
    ScanlineSpriteCache *cache = &s_scanline_sprite_cache;

    cache->row_count = 0u;
    cache->line_sprite_count = 0u;
    cache->sprite_size_16 = (BYTE)(R0_SP_SIZE ? 1u : 0u);
    cache->scanline = (BYTE)scanline;

    for (int s = 0; s < 64; s++) {
        const BYTE *spr = &SPRRAM[s * 4];
        int spr_y = spr[0] + 1;
        int row = scanline - spr_y;
        if (row < 0 || row >= sprite_height) continue;

        int spr_x = spr[3];
        if (clip_left && spr_x < 8) continue;

        if (n_rendered >= 8) {
            PPU_R2 |= R2_SP_OVER;
            break;
        }
        n_rendered++;

        BYTE attr = spr[2];
        BYTE vflip = (attr >> 7) & 1u;
        BYTE tile = spr[1];
        int draw_row = vflip ? (sprite_height - 1 - row) : row;
        WORD pat_addr;
#if defined(PICO_BUILD)
        const uint64_t sprite_fetch_start = time_us_64();
#endif

        if (sprite_height == 8) {
            pat_addr = ((WORD)R0_SP_BASE << 12) | ((WORD)tile << 4) | draw_row;
        } else {
            WORD bank = (tile & 0x01u) ? 0x1000u : 0x0000u;
            BYTE base_tile = tile & 0xFEu;
            if (draw_row >= 8) {
                base_tile++;
                draw_row -= 8;
            }
            pat_addr = bank | ((WORD)base_tile << 4) | draw_row;
        }

        BYTE plane0 = PPUBANK[pat_addr >> 10][pat_addr & 0x3FFu];
        BYTE plane1 = PPUBANK[(pat_addr + 8u) >> 10][(pat_addr + 8u) & 0x3FFu];
#if defined(PICO_BUILD)
        s_perf_sprite_fetch_us += (time_us_64() - sprite_fetch_start);
#endif
        if ((plane0 | plane1) == 0u) {
#if defined(PICO_BUILD)
            s_perf_sprite_skip_rows++;
#endif
            continue;
        }

        cache->rows[row_count].spr_x = spr_x;
        cache->rows[row_count].plane0 = plane0;
        cache->rows[row_count].plane1 = plane1;
        cache->rows[row_count].hflip = (BYTE)((attr >> 6) & 1u);
        cache->rows[row_count].behind = (BYTE)((attr >> 5) & 1u);
        cache->rows[row_count].pal_sel = (BYTE)(attr & 0x03u);
        cache->rows[row_count].is_sprite0 = (BYTE)(s == 0);
        row_count++;
    }

    cache->row_count = (BYTE)row_count;
    cache->line_sprite_count = (BYTE)n_rendered;
    cache->valid = 1u;
}

static void RAMFUNC(ppu_prepare_sprite_scanline_cache)(int scanline) {
    ScanlineSpriteCache *cache = &s_scanline_sprite_cache;
    if (cache->valid &&
        cache->scanline == (BYTE)scanline &&
        cache->sprite_size_16 == (BYTE)(R0_SP_SIZE ? 1u : 0u)) {
        return;
    }
#if defined(PICO_BUILD)
    const uint64_t cache_build_start = time_us_64();
#endif
    ppu_build_sprite_scanline_cache(scanline);
#if defined(PICO_BUILD)
    s_perf_sprite_cache_build_us += (time_us_64() - cache_build_start);
    s_perf_sprite_cache_rebuilds++;
#endif
}

static inline void sprite_stamp_begin_scanline(void) {
    s_sprite_buf_stamp_gen++;
    if (s_sprite_buf_stamp_gen == 0u) {
        memset(s_sprite_buf_stamp, 0, sizeof(s_sprite_buf_stamp));
        s_sprite_buf_stamp_gen = 1u;
    }
}

static inline bool sprite_pixel_has_opaque(const BYTE *sprite_buf, int pixel_x) {
    return (s_sprite_buf_stamp[pixel_x] == s_sprite_buf_stamp_gen) &&
           ((sprite_buf[pixel_x] & SPR_OPAQUE) != 0u);
}

static inline BYTE sprite_tile_opaque_mask(const BYTE *sprite_buf, int pixel_x) {
    BYTE mask = 0u;

    if (sprite_pixel_has_opaque(sprite_buf, pixel_x + 0)) mask |= 0x01u;
    if (sprite_pixel_has_opaque(sprite_buf, pixel_x + 1)) mask |= 0x02u;
    if (sprite_pixel_has_opaque(sprite_buf, pixel_x + 2)) mask |= 0x04u;
    if (sprite_pixel_has_opaque(sprite_buf, pixel_x + 3)) mask |= 0x08u;
    if (sprite_pixel_has_opaque(sprite_buf, pixel_x + 4)) mask |= 0x10u;
    if (sprite_pixel_has_opaque(sprite_buf, pixel_x + 5)) mask |= 0x20u;
    if (sprite_pixel_has_opaque(sprite_buf, pixel_x + 6)) mask |= 0x40u;
    if (sprite_pixel_has_opaque(sprite_buf, pixel_x + 7)) mask |= 0x80u;

    return mask;
}

static inline BYTE sprite_tile_window_mask(const BYTE *tile_masks, int pixel_x) {
    BYTE tile_shift = (BYTE)(pixel_x & 7);
    if (tile_shift == 0u) {
        return tile_masks[pixel_x >> 3];
    }

    {
        BYTE tile_index = (BYTE)(pixel_x >> 3);
        BYTE next_shift = (BYTE)(8u - tile_shift);
        return (BYTE)((tile_masks[tile_index] >> tile_shift) |
                      (tile_masks[tile_index + 1] << next_shift));
    }
}

static int RAMFUNC(compositeSprites)(BYTE *sprite_buf, int scanline, bool *has_sprites, BYTE *line_sprite_count, int *sprite_min_x, int *sprite_max_x) {
    int hit_x = -1;
    BYTE sprite_palette[16];
    const ScanlineSpriteCache *cache;
    const bool clip_left = (R1_CLIP_SP != 0);

    *has_sprites = false;
    *line_sprite_count = 0u;
    *sprite_min_x = 256;
    *sprite_max_x = 0;

    if (!(s_scanline_sprite_cache.valid &&
          s_scanline_sprite_cache.scanline == (BYTE)scanline &&
          s_scanline_sprite_cache.sprite_size_16 == (BYTE)(R0_SP_SIZE ? 1u : 0u))) {
#if defined(PICO_BUILD)
        s_perf_sprite_cache_misses++;
#endif
        ppu_prepare_sprite_scanline_cache(scanline);
    }
    cache = &s_scanline_sprite_cache;

    if (cache->row_count == 0u) {
        *line_sprite_count = cache->line_sprite_count;
        return hit_x;
    }

    for (int i = 0; i < 16; i++) {
        sprite_palette[i] = (BYTE)((g_PalRAM[0x10u + i] & 0x3Fu) << 2);
    }
    sprite_stamp_begin_scanline();
    memset(s_sprite_tile_mask, 0, sizeof(s_sprite_tile_mask));
    memset(s_sprite_tile_behind_mask, 0, sizeof(s_sprite_tile_behind_mask));
    memset(s_sprite_tile_color, 0, sizeof(s_sprite_tile_color));
    memset(s_sprite0_tile_mask, 0, sizeof(s_sprite0_tile_mask));
    *has_sprites = true;

#if defined(PICO_BUILD)
    const uint64_t sprite_store_start = time_us_64();
#endif
    for (int r = 0; r < cache->row_count; r++) {
        const SpriteRowInfo *info = &cache->rows[r];
        int px_start = 0;
        int px_end = 8;
        BYTE behind_flag = info->behind ? SPR_BEHIND_BG : 0u;
        BYTE pal_base = (BYTE)(info->pal_sel << 2);

        if (info->spr_x < 0) {
            px_start = -info->spr_x;
        }
        if (info->spr_x + px_end > 256) {
            px_end = 256 - info->spr_x;
        }
        if (clip_left && info->spr_x < 8) {
            int clip_px = 8 - info->spr_x;
            if (clip_px > px_start) {
                px_start = clip_px;
            }
        }
        if (px_start >= px_end) {
            continue;
        }

        if (!info->hflip) {
            for (int px = px_start; px < px_end; px++) {
                int scr_x = info->spr_x + px;
                int bit = 7 - px;
                BYTE lo = (info->plane0 >> bit) & 1u;
                BYTE hi = (info->plane1 >> bit) & 1u;
                BYTE color = lo | (hi << 1);
                if (color == 0) continue;

                if (scr_x < *sprite_min_x) *sprite_min_x = scr_x;
                if (scr_x + 1 > *sprite_max_x) *sprite_max_x = scr_x + 1;
                if (info->is_sprite0) {
                    BYTE tile_index = (BYTE)(scr_x >> 3);
                    BYTE tile_bit = (BYTE)(1u << (scr_x & 7));
                    s_sprite0_tile_mask[tile_index] |= tile_bit;
                }

                if (s_sprite_buf_stamp[scr_x] != s_sprite_buf_stamp_gen) {
                    BYTE pal_idx = (BYTE)(pal_base | color);
                    BYTE tile_index = (BYTE)(scr_x >> 3);
                    BYTE tile_bit = (BYTE)(1u << (scr_x & 7));
                    sprite_buf[scr_x] = (BYTE)(sprite_palette[pal_idx]
                                               | behind_flag
                                               | SPR_OPAQUE);
                    s_sprite_buf_stamp[scr_x] = s_sprite_buf_stamp_gen;
                    s_sprite_tile_mask[tile_index] |= tile_bit;
                    if (behind_flag) {
                        s_sprite_tile_behind_mask[tile_index] |= tile_bit;
                    }
                    s_sprite_tile_color[tile_index][scr_x & 7] = (BYTE)(sprite_palette[pal_idx] >> 2);
#if defined(PICO_BUILD)
                    s_perf_sprite_written_pixels++;
#endif
                }

                if (info->is_sprite0 && hit_x < 0) {
                    hit_x = scr_x;
                }
            }
        } else {
            for (int px = px_start; px < px_end; px++) {
                int scr_x = info->spr_x + px;
                int bit = px;
                BYTE lo = (info->plane0 >> bit) & 1u;
                BYTE hi = (info->plane1 >> bit) & 1u;
                BYTE color = lo | (hi << 1);
                if (color == 0) continue;

                if (scr_x < *sprite_min_x) *sprite_min_x = scr_x;
                if (scr_x + 1 > *sprite_max_x) *sprite_max_x = scr_x + 1;
                if (info->is_sprite0) {
                    BYTE tile_index = (BYTE)(scr_x >> 3);
                    BYTE tile_bit = (BYTE)(1u << (scr_x & 7));
                    s_sprite0_tile_mask[tile_index] |= tile_bit;
                }

                if (s_sprite_buf_stamp[scr_x] != s_sprite_buf_stamp_gen) {
                    BYTE pal_idx = (BYTE)(pal_base | color);
                    BYTE tile_index = (BYTE)(scr_x >> 3);
                    BYTE tile_bit = (BYTE)(1u << (scr_x & 7));
                    sprite_buf[scr_x] = (BYTE)(sprite_palette[pal_idx]
                                               | behind_flag
                                               | SPR_OPAQUE);
                    s_sprite_buf_stamp[scr_x] = s_sprite_buf_stamp_gen;
                    s_sprite_tile_mask[tile_index] |= tile_bit;
                    if (behind_flag) {
                        s_sprite_tile_behind_mask[tile_index] |= tile_bit;
                    }
                    s_sprite_tile_color[tile_index][scr_x & 7] = (BYTE)(sprite_palette[pal_idx] >> 2);
#if defined(PICO_BUILD)
                    s_perf_sprite_written_pixels++;
#endif
                }

                if (info->is_sprite0 && hit_x < 0) {
                    hit_x = scr_x;
                }
            }
        }
    }
#if defined(PICO_BUILD)
    s_perf_sprite_store_us += (time_us_64() - sprite_store_start);
#endif
    *line_sprite_count = cache->line_sprite_count;
    return hit_x;
}

static inline bool sprite_tile_has_opaque(const BYTE *sprite_buf, int pixel_x) {
    return sprite_tile_opaque_mask(sprite_buf, pixel_x) != 0u;
}

/* =====================================================================
 *  PPU_DrawLine — render one visible scanline
 *  RAM-resident on RP2040.
 * ===================================================================== */
void RAMFUNC(PPU_DrawLine)(void) {
    int scanline = g_nScanLine;
    WORD *out = g_wScanLine;
    WORD bg_palette[16];
    BYTE line_sprite_count = 0u;
    int sprite_min_x = 256;
    int sprite_max_x = 0;
#if defined(PICO_BUILD)
    uint64_t sprite_overlay_us_local = 0;
    uint64_t sprite_mask_probe_us_local = 0;
    uint64_t sprite_resolve_us_local = 0;
    uint32_t sprite_overlay_tiles_local = 0;
    uint32_t sprite_overlay_pixels_local = 0;
#endif

    ppu_log_post_window_drawline("PPU_DRAWLINE_ENTER", scanline);
    s_perf_visible_lines++;

    /* Border lines: clear to background color */
    WORD bg_color = g_NesPalette[g_PalRAM[0] & 0x3Fu];
    if (scanline < PPU_UpDown_Clip || scanline >= (240 - PPU_UpDown_Clip)) {
        s_perf_border_lines++;
        ppu_log_post_window_drawline("PPU_DRAWLINE_BORDER", scanline);
        if (R1_RENDERING) {
            /* Platform-side border clipping must not freeze loopy scroll
             * state. Keep the horizontal reload and end-of-line vertical
             * advance in sync even when we suppress visible output. */
            loopy_reload_x();
            loopy_fine_y_inc();
        }
        for (int i = 0; i < 256; i++) out[i] = bg_color;
        return;
    }

    /* With rendering disabled, the visible line shows the backdrop color and
     * must not perturb Loopy v/t scroll state through scanline fetch logic. */
    if (!R1_RENDERING) {
        s_perf_masked_lines++;
        ppu_log_post_window_drawline("PPU_DRAWLINE_MASKED", scanline);
        for (int i = 0; i < 256; i++) out[i] = bg_color;
        return;
    }

    /* Reload horizontal scroll position from PPU_Temp */
    s_ppu_post_window_loopy_tile_logs = 0u;
    ppu_log_post_window_loopy("PPU_LOOPY_BEFORE_RELOAD", 0u);
    loopy_reload_x();
    ppu_log_post_window_loopy("PPU_LOOPY_AFTER_RELOAD", 1u);

    /* Sprite composite buffer */
    BYTE sprite_buf[256];
    int spr0_hit_x = -1;
    bool has_sprites = false;
    bool spr0_tile_hit = false;
    if (R1_SP_ENABLE) {
        s_perf_sprite_enabled_lines++;
#if defined(PICO_BUILD)
        const uint64_t sprite_comp_start = time_us_64();
#endif
        spr0_hit_x = compositeSprites(sprite_buf, scanline, &has_sprites, &line_sprite_count, &sprite_min_x, &sprite_max_x);
#if defined(PICO_BUILD)
        s_perf_sprite_comp_us += (time_us_64() - sprite_comp_start);
#endif
        if (has_sprites) {
            s_perf_sprite_active_lines++;
            s_perf_sprite_count_sum += line_sprite_count;
            if (line_sprite_count > s_perf_sprite_count_max) {
                s_perf_sprite_count_max = line_sprite_count;
            }
            for (int tile = 0; tile < 33; tile++) {
                if (s_sprite0_tile_mask[tile] != 0u) {
                    spr0_tile_hit = true;
                    break;
                }
            }
        }
    }

#if defined(PICO_BUILD)
    if ((spr0_hit_x >= 0) == spr0_tile_hit) {
        s_perf_sprite0_match_lines++;
    } else {
        s_perf_sprite0_mismatch_lines++;
        if (spr0_hit_x >= 0) {
            s_perf_sprite0_old_only_lines++;
        } else {
            s_perf_sprite0_new_only_lines++;
        }
    }
#endif

    /* Sprite 0 hit */
    if (spr0_tile_hit && R1_BG_ENABLE) {
        PPU_R2 |= R2_SP_HIT;
    }

    bg_palette[0]  = bg_color;
    bg_palette[4]  = bg_color;
    bg_palette[8]  = bg_color;
    bg_palette[12] = bg_color;
    for (int pal = 0; pal < 4; pal++) {
        bg_palette[pal * 4 + 1] = g_NesPalette[g_PalRAM[pal * 4 + 1] & 0x3Fu];
        bg_palette[pal * 4 + 2] = g_NesPalette[g_PalRAM[pal * 4 + 2] & 0x3Fu];
        bg_palette[pal * 4 + 3] = g_NesPalette[g_PalRAM[pal * 4 + 3] & 0x3Fu];
    }

    /* ---- Background rendering ---- */
    /* BG pattern table base: 0x0000 or 0x1000 */
    WORD bg_base = (WORD)R0_BG_BASE << 12;
    WORD fine_y  = LOOPY_FINE_Y(PPU_Addr);
    bool bg_enabled = (R1_BG_ENABLE != 0);
    bool bg_clip = (R1_CLIP_BG != 0);
    bool bg_fast_path = bg_enabled;
    if (PPU_Scr_H_Bit != 0) s_perf_fine_x_lines++;
    if (bg_enabled && bg_clip) s_perf_bg_clip_lines++;

    /* Pixel loop: 256 pixels wide, fine-X offset within first tile */
    int pixel_x = 0;
    int tile_start = 0;

    if (PPU_Scr_H_Bit != 0) {
        WORD coarse_x = LOOPY_COARSE_X(PPU_Addr);
        WORD coarse_y = LOOPY_COARSE_Y(PPU_Addr);
        WORD nt_sel   = LOOPY_NT(PPU_Addr);
        const BYTE *nt_page = PPUBANK[8 + nt_sel];
        BYTE tile_idx = nt_page[(coarse_y << 5) | coarse_x];
        BYTE attr = nt_page[0x03C0u | ((coarse_y >> 2) << 3) | (coarse_x >> 2)];
        BYTE quadrant = (BYTE)(((coarse_y & 2u) << 1) | (coarse_x & 2u));
        BYTE pal_hi   = (attr >> quadrant) & 0x03u;
        const WORD *tile_palette = &bg_palette[pal_hi << 2];
        WORD pat_addr = bg_base | ((WORD)tile_idx << 4) | fine_y;
        BYTE plane0   = PPUBANK[pat_addr >> 10][pat_addr & 0x3FFu];
        BYTE plane1   = PPUBANK[(pat_addr + 8u) >> 10][(pat_addr + 8u) & 0x3FFu];

        if (!has_sprites) {
            for (int b = (int)PPU_Scr_H_Bit; b < 8 && pixel_x < 256; b++, pixel_x++) {
                BYTE bg_px;
                if (!bg_enabled || (bg_clip && pixel_x < 8)) {
                    bg_px = 0;
                } else {
                    int bit = 7 - b;
                    BYTE lo = (plane0 >> bit) & 1u;
                    BYTE hi = (plane1 >> bit) & 1u;
                    bg_px = lo | (hi << 1);
                }
                out[pixel_x] = tile_palette[bg_px];
            }
        } else {
#if defined(PICO_BUILD)
            const uint64_t sprite_overlay_start = time_us_64();
#endif
            sprite_overlay_tiles_local++;
            for (int b = (int)PPU_Scr_H_Bit; b < 8 && pixel_x < 256; b++, pixel_x++) {
                sprite_overlay_pixels_local++;
                BYTE bg_px;
                if (!bg_enabled || (bg_clip && pixel_x < 8)) {
                    bg_px = 0;
                } else {
                    int bit = 7 - b;
                    BYTE lo = (plane0 >> bit) & 1u;
                    BYTE hi = (plane1 >> bit) & 1u;
                    bg_px = lo | (hi << 1);
                }

                WORD color = tile_palette[bg_px];
                if ((pixel_x >= sprite_min_x) && (pixel_x < sprite_max_x) &&
                    sprite_pixel_has_opaque(sprite_buf, pixel_x)) {
                    BYTE spr_pix = sprite_buf[pixel_x];
                    BYTE behind  = spr_pix & SPR_BEHIND_BG;
                    if (!behind || bg_px == 0) {
                        BYTE nes_col = (spr_pix >> 2) & 0x3Fu;
                        color = g_NesPalette[nes_col];
                    }
                }
                out[pixel_x] = color;
            }
#if defined(PICO_BUILD)
            sprite_overlay_us_local += (time_us_64() - sprite_overlay_start);
#endif
        }

        loopy_coarse_x_inc();
        if (s_ppu_post_window_loopy_tile_logs < 6u) {
            s_ppu_post_window_loopy_tile_logs++;
            ppu_log_post_window_loopy("PPU_LOOPY_TILE_INC", 0u);
        }
        tile_start = 1;
    }

    if (bg_enabled && bg_clip && pixel_x < 8) {
        WORD coarse_x = LOOPY_COARSE_X(PPU_Addr);
        WORD coarse_y = LOOPY_COARSE_Y(PPU_Addr);
        WORD nt_sel   = LOOPY_NT(PPU_Addr);
        const BYTE *nt_page = PPUBANK[8 + nt_sel];
        BYTE tile_idx = nt_page[(coarse_y << 5) | coarse_x];
        BYTE attr = nt_page[0x03C0u | ((coarse_y >> 2) << 3) | (coarse_x >> 2)];
        BYTE quadrant = (BYTE)(((coarse_y & 2u) << 1) | (coarse_x & 2u));
        BYTE pal_hi   = (attr >> quadrant) & 0x03u;
        const WORD *tile_palette = &bg_palette[pal_hi << 2];
        WORD pat_addr = bg_base | ((WORD)tile_idx << 4) | fine_y;
        BYTE plane0   = PPUBANK[pat_addr >> 10][pat_addr & 0x3FFu];
        BYTE plane1   = PPUBANK[(pat_addr + 8u) >> 10][(pat_addr + 8u) & 0x3FFu];

        if (!has_sprites) {
            for (int b = 0; b < 8 && pixel_x < 256; b++, pixel_x++) {
                BYTE bg_px;
                if (pixel_x < 8) {
                    bg_px = 0;
                } else {
                    int bit = 7 - b;
                    BYTE lo = (plane0 >> bit) & 1u;
                    BYTE hi = (plane1 >> bit) & 1u;
                    bg_px = lo | (hi << 1);
                }
                out[pixel_x] = tile_palette[bg_px];
            }
        } else {
#if defined(PICO_BUILD)
            const uint64_t sprite_overlay_start = time_us_64();
#endif
            sprite_overlay_tiles_local++;
            for (int b = 0; b < 8 && pixel_x < 256; b++, pixel_x++) {
                sprite_overlay_pixels_local++;
                BYTE bg_px;
                if (pixel_x < 8) {
                    bg_px = 0;
                } else {
                    int bit = 7 - b;
                    BYTE lo = (plane0 >> bit) & 1u;
                    BYTE hi = (plane1 >> bit) & 1u;
                    bg_px = lo | (hi << 1);
                }

                WORD color = tile_palette[bg_px];
                if ((pixel_x >= sprite_min_x) && (pixel_x < sprite_max_x) &&
                    sprite_pixel_has_opaque(sprite_buf, pixel_x)) {
                    BYTE spr_pix = sprite_buf[pixel_x];
                    BYTE behind  = spr_pix & SPR_BEHIND_BG;
                    if (!behind || bg_px == 0) {
                        BYTE nes_col = (spr_pix >> 2) & 0x3Fu;
                        color = g_NesPalette[nes_col];
                    }
                }
                out[pixel_x] = color;
            }
#if defined(PICO_BUILD)
            sprite_overlay_us_local += (time_us_64() - sprite_overlay_start);
#endif
        }

        loopy_coarse_x_inc();
        if (s_ppu_post_window_loopy_tile_logs < 6u) {
            s_ppu_post_window_loopy_tile_logs++;
            ppu_log_post_window_loopy("PPU_LOOPY_TILE_INC", 0u);
        }
        tile_start++;
    }

    for (int tile = tile_start; tile < 33; tile++) {
        /* Nametable address for current coarse-X, coarse-Y, NT */
        WORD coarse_x = LOOPY_COARSE_X(PPU_Addr);
        WORD coarse_y = LOOPY_COARSE_Y(PPU_Addr);
        WORD nt_sel   = LOOPY_NT(PPU_Addr);

        /* Nametable byte and attribute byte live in the same 1 KB page. */
        const BYTE *nt_page = PPUBANK[8 + nt_sel];
        BYTE tile_idx = nt_page[(coarse_y << 5) | coarse_x];
        BYTE attr = nt_page[0x03C0u | ((coarse_y >> 2) << 3) | (coarse_x >> 2)];

        /* Extract 2-bit palette select for this 2×2 tile sub-block */
        BYTE quadrant = (BYTE)(((coarse_y & 2u) << 1) | (coarse_x & 2u));
        BYTE pal_hi   = (attr >> quadrant) & 0x03u;
        const WORD *tile_palette = &bg_palette[pal_hi << 2];

        /* Pattern table row */
        WORD pat_addr = bg_base | ((WORD)tile_idx << 4) | fine_y;
        BYTE plane0   = PPUBANK[pat_addr >> 10][pat_addr & 0x3FFu];
        BYTE plane1   = PPUBANK[(pat_addr + 8u) >> 10][(pat_addr + 8u) & 0x3FFu];

        if (!has_sprites) {
            if (bg_fast_path && (pixel_x >= 8) && (pixel_x <= 248)) {
                out[pixel_x + 0] = tile_palette[((plane0 >> 7) & 1u) | (((plane1 >> 7) & 1u) << 1)];
                out[pixel_x + 1] = tile_palette[((plane0 >> 6) & 1u) | (((plane1 >> 6) & 1u) << 1)];
                out[pixel_x + 2] = tile_palette[((plane0 >> 5) & 1u) | (((plane1 >> 5) & 1u) << 1)];
                out[pixel_x + 3] = tile_palette[((plane0 >> 4) & 1u) | (((plane1 >> 4) & 1u) << 1)];
                out[pixel_x + 4] = tile_palette[((plane0 >> 3) & 1u) | (((plane1 >> 3) & 1u) << 1)];
                out[pixel_x + 5] = tile_palette[((plane0 >> 2) & 1u) | (((plane1 >> 2) & 1u) << 1)];
                out[pixel_x + 6] = tile_palette[((plane0 >> 1) & 1u) | (((plane1 >> 1) & 1u) << 1)];
                out[pixel_x + 7] = tile_palette[((plane0 >> 0) & 1u) | (((plane1 >> 0) & 1u) << 1)];
                pixel_x += 8;
            } else {
                for (int b = 0; b < 8 && pixel_x < 256; b++, pixel_x++) {
                    BYTE bg_px;
                    if (!bg_enabled) {
                        bg_px = 0;
                    } else {
                        int bit = 7 - b;
                        BYTE lo = (plane0 >> bit) & 1u;
                        BYTE hi = (plane1 >> bit) & 1u;
                        bg_px = lo | (hi << 1);
                    }

                    out[pixel_x] = tile_palette[bg_px];
                }
            }
        } else {
            const bool fast_sprite_tile = bg_fast_path && (pixel_x >= 8) && (pixel_x <= 248);
            bool tile_has_sprite = false;

            if (fast_sprite_tile &&
                ((pixel_x + 8) <= sprite_min_x || pixel_x >= sprite_max_x)) {
                out[pixel_x + 0] = tile_palette[((plane0 >> 7) & 1u) | (((plane1 >> 7) & 1u) << 1)];
                out[pixel_x + 1] = tile_palette[((plane0 >> 6) & 1u) | (((plane1 >> 6) & 1u) << 1)];
                out[pixel_x + 2] = tile_palette[((plane0 >> 5) & 1u) | (((plane1 >> 5) & 1u) << 1)];
                out[pixel_x + 3] = tile_palette[((plane0 >> 4) & 1u) | (((plane1 >> 4) & 1u) << 1)];
                out[pixel_x + 4] = tile_palette[((plane0 >> 3) & 1u) | (((plane1 >> 3) & 1u) << 1)];
                out[pixel_x + 5] = tile_palette[((plane0 >> 2) & 1u) | (((plane1 >> 2) & 1u) << 1)];
                out[pixel_x + 6] = tile_palette[((plane0 >> 1) & 1u) | (((plane1 >> 1) & 1u) << 1)];
                out[pixel_x + 7] = tile_palette[((plane0 >> 0) & 1u) | (((plane1 >> 0) & 1u) << 1)];
                pixel_x += 8;
            } else {
#if defined(PICO_BUILD)
                if (fast_sprite_tile) {
                    const uint64_t sprite_mask_start = time_us_64();
                    tile_has_sprite = (sprite_tile_window_mask(s_sprite_tile_mask, pixel_x) != 0u);
                    sprite_mask_probe_us_local += (time_us_64() - sprite_mask_start);
                }
#else
                if (fast_sprite_tile) {
                    tile_has_sprite = (sprite_tile_window_mask(s_sprite_tile_mask, pixel_x) != 0u);
                }
#endif
                if (fast_sprite_tile && !tile_has_sprite) {
                out[pixel_x + 0] = tile_palette[((plane0 >> 7) & 1u) | (((plane1 >> 7) & 1u) << 1)];
                out[pixel_x + 1] = tile_palette[((plane0 >> 6) & 1u) | (((plane1 >> 6) & 1u) << 1)];
                out[pixel_x + 2] = tile_palette[((plane0 >> 5) & 1u) | (((plane1 >> 5) & 1u) << 1)];
                out[pixel_x + 3] = tile_palette[((plane0 >> 4) & 1u) | (((plane1 >> 4) & 1u) << 1)];
                out[pixel_x + 4] = tile_palette[((plane0 >> 3) & 1u) | (((plane1 >> 3) & 1u) << 1)];
                out[pixel_x + 5] = tile_palette[((plane0 >> 2) & 1u) | (((plane1 >> 2) & 1u) << 1)];
                out[pixel_x + 6] = tile_palette[((plane0 >> 1) & 1u) | (((plane1 >> 1) & 1u) << 1)];
                out[pixel_x + 7] = tile_palette[((plane0 >> 0) & 1u) | (((plane1 >> 0) & 1u) << 1)];
                pixel_x += 8;
                } else if (fast_sprite_tile) {
#if defined(PICO_BUILD)
                const uint64_t sprite_overlay_start = time_us_64();
#endif
                BYTE bg_px[8];
                WORD colors[8];
                BYTE opaque_mask;
                BYTE old_opaque_mask;
                BYTE tile_mask_new;
                BYTE behind_mask;
                BYTE old_only_mask;
                sprite_overlay_tiles_local++;
                sprite_overlay_pixels_local += 8u;

                bg_px[0] = ((plane0 >> 7) & 1u) | (((plane1 >> 7) & 1u) << 1);
                bg_px[1] = ((plane0 >> 6) & 1u) | (((plane1 >> 6) & 1u) << 1);
                bg_px[2] = ((plane0 >> 5) & 1u) | (((plane1 >> 5) & 1u) << 1);
                bg_px[3] = ((plane0 >> 4) & 1u) | (((plane1 >> 4) & 1u) << 1);
                bg_px[4] = ((plane0 >> 3) & 1u) | (((plane1 >> 3) & 1u) << 1);
                bg_px[5] = ((plane0 >> 2) & 1u) | (((plane1 >> 2) & 1u) << 1);
                bg_px[6] = ((plane0 >> 1) & 1u) | (((plane1 >> 1) & 1u) << 1);
                bg_px[7] = ((plane0 >> 0) & 1u) | (((plane1 >> 0) & 1u) << 1);

                colors[0] = tile_palette[bg_px[0]];
                colors[1] = tile_palette[bg_px[1]];
                colors[2] = tile_palette[bg_px[2]];
                colors[3] = tile_palette[bg_px[3]];
                colors[4] = tile_palette[bg_px[4]];
                colors[5] = tile_palette[bg_px[5]];
                colors[6] = tile_palette[bg_px[6]];
                colors[7] = tile_palette[bg_px[7]];

                old_opaque_mask = sprite_tile_opaque_mask(sprite_buf, pixel_x);
                tile_mask_new = sprite_tile_window_mask(s_sprite_tile_mask, pixel_x);
                behind_mask = sprite_tile_window_mask(s_sprite_tile_behind_mask, pixel_x);
                opaque_mask = tile_mask_new;

#if defined(PICO_BUILD)
                if (old_opaque_mask == tile_mask_new) {
                    s_perf_opaque_match_tiles++;
                } else {
                    s_perf_opaque_mismatch_tiles++;
                    old_only_mask = (BYTE)(old_opaque_mask & (BYTE)~tile_mask_new);
                    if (old_opaque_mask != 0u && tile_mask_new == 0u) {
                        s_perf_opaque_old_only_tiles++;
                        s_perf_opaque_old_only_zero_tiles++;
                    } else if (old_opaque_mask == 0u && tile_mask_new != 0u) {
                        s_perf_opaque_new_only_tiles++;
                    } else if (old_only_mask != 0u) {
                        s_perf_opaque_old_only_partial_tiles++;
                    }
                    if (old_only_mask != 0u) {
                        s_perf_opaque_old_only_front_bits += (uint32_t)__builtin_popcount((unsigned int)(old_only_mask & (BYTE)~behind_mask));
                        s_perf_opaque_old_only_behind_bits += (uint32_t)__builtin_popcount((unsigned int)(old_only_mask & behind_mask));
                    }
                }
#endif

                if (opaque_mask & 0x01u) {
                    BYTE spr_pix = sprite_buf[pixel_x + 0];
                    BYTE old_nes_col = (BYTE)((spr_pix >> 2) & 0x3Fu);
                    BYTE new_nes_col = s_sprite_tile_color[(pixel_x + 0) >> 3][(pixel_x + 0) & 7];
                    BYTE behind = behind_mask & 0x01u;
#if defined(PICO_BUILD)
                    if (old_nes_col == new_nes_col) {
                        s_perf_color_match_pixels++;
                    } else {
                        s_perf_color_mismatch_pixels++;
                        if (behind) s_perf_color_behind_mismatch_pixels++;
                        else s_perf_color_front_mismatch_pixels++;
                    }
#endif
                    if (!behind || bg_px[0] == 0) {
                        colors[0] = g_NesPalette[new_nes_col];
                    }
                }
                if (opaque_mask & 0x02u) {
                    BYTE spr_pix = sprite_buf[pixel_x + 1];
                    BYTE old_nes_col = (BYTE)((spr_pix >> 2) & 0x3Fu);
                    BYTE new_nes_col = s_sprite_tile_color[(pixel_x + 1) >> 3][(pixel_x + 1) & 7];
                    BYTE behind = behind_mask & 0x02u;
#if defined(PICO_BUILD)
                    if (old_nes_col == new_nes_col) {
                        s_perf_color_match_pixels++;
                    } else {
                        s_perf_color_mismatch_pixels++;
                        if (behind) s_perf_color_behind_mismatch_pixels++;
                        else s_perf_color_front_mismatch_pixels++;
                    }
#endif
                    if (!behind || bg_px[1] == 0) {
                        colors[1] = g_NesPalette[new_nes_col];
                    }
                }
                if (opaque_mask & 0x04u) {
                    BYTE spr_pix = sprite_buf[pixel_x + 2];
                    BYTE old_nes_col = (BYTE)((spr_pix >> 2) & 0x3Fu);
                    BYTE new_nes_col = s_sprite_tile_color[(pixel_x + 2) >> 3][(pixel_x + 2) & 7];
                    BYTE behind = behind_mask & 0x04u;
#if defined(PICO_BUILD)
                    if (old_nes_col == new_nes_col) {
                        s_perf_color_match_pixels++;
                    } else {
                        s_perf_color_mismatch_pixels++;
                        if (behind) s_perf_color_behind_mismatch_pixels++;
                        else s_perf_color_front_mismatch_pixels++;
                    }
#endif
                    if (!behind || bg_px[2] == 0) {
                        colors[2] = g_NesPalette[new_nes_col];
                    }
                }
                if (opaque_mask & 0x08u) {
                    BYTE spr_pix = sprite_buf[pixel_x + 3];
                    BYTE old_nes_col = (BYTE)((spr_pix >> 2) & 0x3Fu);
                    BYTE new_nes_col = s_sprite_tile_color[(pixel_x + 3) >> 3][(pixel_x + 3) & 7];
                    BYTE behind = behind_mask & 0x08u;
#if defined(PICO_BUILD)
                    if (old_nes_col == new_nes_col) {
                        s_perf_color_match_pixels++;
                    } else {
                        s_perf_color_mismatch_pixels++;
                        if (behind) s_perf_color_behind_mismatch_pixels++;
                        else s_perf_color_front_mismatch_pixels++;
                    }
#endif
                    if (!behind || bg_px[3] == 0) {
                        colors[3] = g_NesPalette[new_nes_col];
                    }
                }
                if (opaque_mask & 0x10u) {
                    BYTE spr_pix = sprite_buf[pixel_x + 4];
                    BYTE old_nes_col = (BYTE)((spr_pix >> 2) & 0x3Fu);
                    BYTE new_nes_col = s_sprite_tile_color[(pixel_x + 4) >> 3][(pixel_x + 4) & 7];
                    BYTE behind = behind_mask & 0x10u;
#if defined(PICO_BUILD)
                    if (old_nes_col == new_nes_col) {
                        s_perf_color_match_pixels++;
                    } else {
                        s_perf_color_mismatch_pixels++;
                        if (behind) s_perf_color_behind_mismatch_pixels++;
                        else s_perf_color_front_mismatch_pixels++;
                    }
#endif
                    if (!behind || bg_px[4] == 0) {
                        colors[4] = g_NesPalette[new_nes_col];
                    }
                }
                if (opaque_mask & 0x20u) {
                    BYTE spr_pix = sprite_buf[pixel_x + 5];
                    BYTE old_nes_col = (BYTE)((spr_pix >> 2) & 0x3Fu);
                    BYTE new_nes_col = s_sprite_tile_color[(pixel_x + 5) >> 3][(pixel_x + 5) & 7];
                    BYTE behind = behind_mask & 0x20u;
#if defined(PICO_BUILD)
                    if (old_nes_col == new_nes_col) {
                        s_perf_color_match_pixels++;
                    } else {
                        s_perf_color_mismatch_pixels++;
                        if (behind) s_perf_color_behind_mismatch_pixels++;
                        else s_perf_color_front_mismatch_pixels++;
                    }
#endif
                    if (!behind || bg_px[5] == 0) {
                        colors[5] = g_NesPalette[new_nes_col];
                    }
                }
                if (opaque_mask & 0x40u) {
                    BYTE spr_pix = sprite_buf[pixel_x + 6];
                    BYTE old_nes_col = (BYTE)((spr_pix >> 2) & 0x3Fu);
                    BYTE new_nes_col = s_sprite_tile_color[(pixel_x + 6) >> 3][(pixel_x + 6) & 7];
                    BYTE behind = behind_mask & 0x40u;
#if defined(PICO_BUILD)
                    if (old_nes_col == new_nes_col) {
                        s_perf_color_match_pixels++;
                    } else {
                        s_perf_color_mismatch_pixels++;
                        if (behind) s_perf_color_behind_mismatch_pixels++;
                        else s_perf_color_front_mismatch_pixels++;
                    }
#endif
                    if (!behind || bg_px[6] == 0) {
                        colors[6] = g_NesPalette[new_nes_col];
                    }
                }
                if (opaque_mask & 0x80u) {
                    BYTE spr_pix = sprite_buf[pixel_x + 7];
                    BYTE old_nes_col = (BYTE)((spr_pix >> 2) & 0x3Fu);
                    BYTE new_nes_col = s_sprite_tile_color[(pixel_x + 7) >> 3][(pixel_x + 7) & 7];
                    BYTE behind = behind_mask & 0x80u;
#if defined(PICO_BUILD)
                    if (old_nes_col == new_nes_col) {
                        s_perf_color_match_pixels++;
                    } else {
                        s_perf_color_mismatch_pixels++;
                        if (behind) s_perf_color_behind_mismatch_pixels++;
                        else s_perf_color_front_mismatch_pixels++;
                    }
#endif
                    if (!behind || bg_px[7] == 0) {
                        colors[7] = g_NesPalette[new_nes_col];
                    }
                }

                out[pixel_x + 0] = colors[0];
                out[pixel_x + 1] = colors[1];
                out[pixel_x + 2] = colors[2];
                out[pixel_x + 3] = colors[3];
                out[pixel_x + 4] = colors[4];
                out[pixel_x + 5] = colors[5];
                out[pixel_x + 6] = colors[6];
                out[pixel_x + 7] = colors[7];
                pixel_x += 8;
#if defined(PICO_BUILD)
                sprite_overlay_us_local += (time_us_64() - sprite_overlay_start);
                sprite_resolve_us_local += (time_us_64() - sprite_overlay_start);
#endif
                } else {
#if defined(PICO_BUILD)
                const uint64_t sprite_overlay_start = time_us_64();
#endif
                sprite_overlay_tiles_local++;
                for (int b = 0; b < 8 && pixel_x < 256; b++, pixel_x++) {
                    sprite_overlay_pixels_local++;
                    BYTE bg_px;
                    if (!bg_enabled) {
                        bg_px = 0;
                    } else {
                        int bit = 7 - b;
                        BYTE lo = (plane0 >> bit) & 1u;
                        BYTE hi = (plane1 >> bit) & 1u;
                        bg_px = lo | (hi << 1);
                    }

                    WORD color = tile_palette[bg_px];

                    if ((pixel_x >= sprite_min_x) && (pixel_x < sprite_max_x) &&
                        sprite_pixel_has_opaque(sprite_buf, pixel_x)) {
                        BYTE spr_pix = sprite_buf[pixel_x];
                        BYTE behind  = spr_pix & SPR_BEHIND_BG;
                        if (!behind || bg_px == 0) {
                            BYTE nes_col = (spr_pix >> 2) & 0x3Fu;
                            color = g_NesPalette[nes_col];
                        }
                    }

                    out[pixel_x] = color;
                }
#if defined(PICO_BUILD)
                sprite_overlay_us_local += (time_us_64() - sprite_overlay_start);
                sprite_resolve_us_local += (time_us_64() - sprite_overlay_start);
#endif
                }
            }
        }

        /* Advance coarse-X / NT-X after each tile */
        loopy_coarse_x_inc();
        if (s_ppu_post_window_loopy_tile_logs < 6u) {
            s_ppu_post_window_loopy_tile_logs++;
            ppu_log_post_window_loopy("PPU_LOOPY_TILE_INC", 0u);
        }
    }

    /* Advance fine-Y at end of scanline */
    ppu_log_post_window_loopy("PPU_LOOPY_BEFORE_FINEY", 0u);
    loopy_fine_y_inc();
    ppu_log_post_window_loopy("PPU_LOOPY_AFTER_FINEY", 1u);
#if defined(PICO_BUILD)
    s_perf_sprite_overlay_us += sprite_overlay_us_local;
    s_perf_sprite_mask_probe_us += sprite_mask_probe_us_local;
    s_perf_sprite_resolve_us += sprite_resolve_us_local;
    s_perf_sprite_overlay_tiles += sprite_overlay_tiles_local;
    s_perf_sprite_overlay_pixels += sprite_overlay_pixels_local;
#endif
    if (s_ppu_vram_dump_phase == 2u && s_ppu_post_window_loopy_lines > 0u) {
        s_ppu_post_window_loopy_lines--;
        if (s_ppu_post_window_loopy_lines == 0u) {
            s_ppu_vram_dump_phase = 0u;
        }
    }
}

/* =====================================================================
 *  PPU_HSync — per-scanline VBlank / scroll / advance
 *  RAM-resident on RP2040.
 * ===================================================================== */
static inline void ppu_begin_scanline(int sl) {
    if (sl == VBLANK_SCANLINE) {
        /* Start VBlank before the CPU executes this scanline so polling
         * loops get the full VRAM upload window on title/menu screens. */
        s_ppu_vblank_pending = 1;
        s_ppu_nmi_pending = R0_NMI_VB ? 1u : 0u;
        s_ppu_vblank_side_effects_pending = 1u;
    } else if (sl == (VBLANK_END_SCANLINE - 1)) {
        s_ppu_vblank_clear_pending = 1;
    } else if (sl == VBLANK_END_SCANLINE) {
        /* Clear VBlank at pre-render start, but only apply the loopy-style
         * vertical reload when background or sprite rendering is enabled. */
        PPU_R2 &= ~(BYTE)(R2_SP_HIT | R2_SP_OVER);
        s_ppu_vblank_pending = 0;
        s_ppu_nmi_pending = 0;
        s_ppu_vblank_clear_pending = 0;
        s_ppu_suppress_next_vblank = 0;
        s_ppu_vblank_side_effects_pending = 0;
        if (R1_RENDERING) {
            PPU_Addr = (PPU_Addr & 0x041Fu) | (PPU_Temp & ~0x041Fu);
        }
    }
}

static inline void ppu_run_vblank_side_effects(void) {
    if (!s_ppu_vblank_side_effects_pending) {
        return;
    }

    s_ppu_vblank_side_effects_pending = 0;
    InfoNES_LoadFrame();
    DWORD pad1, pad2;
    InfoNES_PadState(&pad1, &pad2);
    PAD1_Latch = pad1; PAD1_Bit = 0;
    PAD2_Latch = pad2; PAD2_Bit = 0;
    MapperVSync();
}

void RAMFUNC(PPU_HSync)(void) {
    /* Advance scanline counter */
    g_nScanLine++;
    if (g_nScanLine >= TOTAL_SCANLINES) {
        if (s_ppu_odd_frame && R1_RENDERING) {
            /* Rendered odd frames are one PPU dot shorter. Model that by
             * advancing the 113/114/114 CPU-phase once at frame wrap. */
            s_cpu_scanline_phase++;
            if (s_cpu_scanline_phase >= 3u) {
                s_cpu_scanline_phase = 0;
            }
        }
        g_nScanLine = 0;
        s_ppu_odd_frame ^= 1u;
    }
}

/* =====================================================================
 *  PPU_Cycle — one full scanline execution
 *  Called 262 times per frame.
 *  RAM-resident on RP2040.
 * ===================================================================== */
extern void InfoNES_pAPUHsync(int render_enabled);

void RAMFUNC(PPU_Cycle)(void) {
    const WORD cpu_clocks = ppu_next_cpu_clocks();
#if defined(PICO_BUILD)
    uint64_t t0;
    uint64_t t1;
    uint64_t t2;
    uint64_t t3;
    uint64_t t4;
    s_perf_scanlines++;
    s_perf_cpu_cycles += cpu_clocks;
    t0 = time_us_64();
#endif
    ppu_begin_scanline(g_nScanLine);

    /* 1. CPU: execute ~341/3 clocks per scanline on average */
    K6502_Step(cpu_clocks);
#if defined(PICO_BUILD)
    t1 = time_us_64();
    s_perf_cpu_us += (t1 - t0);
#endif

    /* VBlank becomes CPU-visible before this scanline's CPU work, but
     * frame/pad/mapper side effects are deferred until after it. */
    ppu_run_vblank_side_effects();

    /* 2. APU: generate H-Sync samples */
    InfoNES_pAPUHsync(R1_RENDERING ? 1 : 0);
#if defined(PICO_BUILD)
    t2 = time_us_64();
    s_perf_apu_us += (t2 - t1);
#endif

    /* 3. PPU: render scanline (visible lines only) */
    int sl = g_nScanLine;
    bool visible = (sl >= 0 && sl < 240);

    if (visible) {
        /* Frame skip is decided once per frame at VBlank. When active we
         * skip drawing for the whole frame, but still keep CPU/APU/mapper
         * and PPU timing advancing normally. */
        if (g_byFrameSkip == 0) {
            if (R1_SP_ENABLE) {
                ppu_prepare_sprite_scanline_cache(sl);
            }
            InfoNES_PreDrawLine(sl);
            PPU_DrawLine();
            InfoNES_PostDrawLine(sl);
        }
    }
#if defined(PICO_BUILD)
    t3 = time_us_64();
    s_perf_draw_us += (t3 - t2);
#endif

    /* 4. Mapper H-Sync hook */
    MapperHSync();

    /* 5. Advance scanline state */
    PPU_HSync();
#if defined(PICO_BUILD)
    t4 = time_us_64();
    s_perf_tail_us += (t4 - t3);
#endif

#if defined(PICO_BUILD)
    if (g_nScanLine == 0) {
        s_perf_frames++;
    }
    audio_debug_poll();
    ppu_perf_poll();
#endif
}
