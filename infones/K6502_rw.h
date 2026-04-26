/*===================================================================*/
/*                                                                   */
/*  K6502_RW.h : 6502 Reading/Writing Operation for NES              */
/*               This file is included in K6502.cpp                  */
/*                                                                   */
/*  2000/5/23   InfoNES Project ( based on pNesX )                   */
/*                                                                   */
/*===================================================================*/

#ifndef K6502_RW_H_INCLUDED
#define K6502_RW_H_INCLUDED

/*-------------------------------------------------------------------*/
/*  Include files                                                    */
/*-------------------------------------------------------------------*/

#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_StructuredLog.h"
#include "InfoNES_pAPU.h"
#include "boko_flash_trace.h"
#include <pico.h>
#include <stdio.h>

static inline WORD InfoNES_Palette444ToRgb565(WORD color)
{
  WORD rgb444 = (WORD)(__builtin_bswap16(color) & 0x0FFFu);
  WORD r = (WORD)(((rgb444 >> 8) & 0x0Fu) * 0x11u);
  WORD g = (WORD)(((rgb444 >> 4) & 0x0Fu) * 0x11u);
  WORD b = (WORD)((rgb444 & 0x0Fu) * 0x11u);

  return (WORD)(((r & 0xF8u) << 8) |
                ((g & 0xFCu) << 3) |
                (b >> 3));
}

static inline void InfoNES_IncrementVideoRamAddrDuringRendering()
{
  if ((PPU_Addr & 0x001F) == 0x001F)
  {
    PPU_Addr &= (WORD)~0x001F;
    PPU_Addr ^= 0x0400;
  }
  else
  {
    ++PPU_Addr;
  }

  if ((PPU_Addr & 0x7000) != 0x7000)
  {
    PPU_Addr += 0x1000;
  }
  else
  {
    PPU_Addr &= (WORD)~0x7000;

    WORD y = (PPU_Addr & 0x03E0) >> 5;
    if (y == 29)
    {
      y = 0;
      PPU_Addr ^= 0x0800;
    }
    else if (y == 31)
    {
      y = 0;
    }
    else
    {
      ++y;
    }

    PPU_Addr = (PPU_Addr & (WORD)~0x03E0) | (WORD)(y << 5);
  }
}
/*===================================================================*/
/*                                                                   */
/*            K6502_ReadZp() : Reading from the zero page            */
/*                                                                   */
/*===================================================================*/
static inline bool structured_log_title_scroll_2006_pc(WORD pc)
{
  switch (pc)
  {
  case 0xE22E:
  case 0xE233:
  case 0xE2D8:
  case 0xE2E0:
  case 0xE31E:
  case 0xE323:
  case 0xCDD7:
  case 0xCDDA:
  case 0xC8D2:
  case 0xC8D7:
  case 0xC8F9:
  case 0xC8FE:
    return true;
  default:
    return false;
  }
}

static inline bool structured_log_title_scroll_2007_pc(WORD pc)
{
  switch (pc)
  {
  case 0xE23C:
  case 0xE2E7:
  case 0xE32C:
  case 0xCDE8:
    return true;
  default:
    return false;
  }
}

static inline BYTE K6502_ReadZp(BYTE byAddr)
{
  /*
   *  Reading from the zero page
   *
   *  Parameters
   *    BYTE byAddr              (Read)
   *      An address inside the zero page
   *
   *  Return values
   *    Read Data
   */

  return RAM[byAddr];
}

static inline bool structured_log_watch_ram_addr(WORD addr)
{
  switch (addr)
  {
  case 0x0041:
  case 0x0042:
  case 0x0066:
  case 0x0067:
  case 0x00A5:
  case 0x00B1:
  case 0x061B:
  case 0x061C:
  case 0x062D:
  case 0x0674:
  case 0x0697:
    return true;
  default:
    return false;
  }
}

static inline bool structured_log_watch_apu_addr(WORD addr)
{
  return addr == 0x4015;
}

#if INFONES_ENABLE_BOKOSUKA_STATE_LOG
static BYTE g_bokosuka_last_003b = 0xff;
static BYTE g_bokosuka_last_003c = 0xff;
static BYTE g_bokosuka_last_009d = 0xff;
static BYTE g_bokosuka_last_002c = 0xff;
static unsigned long g_bokosuka_2c_seq = 0;
extern unsigned g_bokosuka_uart_window;
extern unsigned long g_bokosuka_uart_window_seq;

static inline WORD bokosuka_state_stack_return()
{
  const BYTE lo = RAM[0x0100 + ((SP + 1) & 0xFF)];
  const BYTE hi = RAM[0x0100 + ((SP + 2) & 0xFF)];
  return (WORD)((((WORD)hi << 8) | lo) + 1);
}

static inline bool bokosuka_state_hot_pc(WORD pc)
{
  switch (pc)
  {
  case 0xA6E3:
  case 0xA6E8:
  case 0xA6ED:
  case 0xA63F:
    return true;
  default:
    return false;
  }
}

static inline void bokosuka_state_maybe_log_ram_write(WORD addr, BYTE value)
{
  if (!bokosuka_state_hot_pc(PC))
  {
    return;
  }

  BYTE *last = nullptr;
  switch (addr)
  {
  case 0x003B: last = &g_bokosuka_last_003b; break;
  case 0x003C: last = &g_bokosuka_last_003c; break;
  case 0x009D: last = &g_bokosuka_last_009d; break;
  case 0x002C:
    if (PC != 0xA63F)
    {
      return;
    }
    last = &g_bokosuka_last_002c;
    break;
  default:
    return;
  }

  if (*last == value)
  {
    return;
  }
  const BYTE prev = *last;
  *last = value;

  if (addr == 0x002C)
  {
    g_bokosuka_2c_seq++;
    g_bokosuka_uart_window = 24;
    g_bokosuka_uart_window_seq = g_bokosuka_2c_seq;
    printf("[BOKO_2C] seq=%lu f=%lu sl=%u pc=%04X ret=%04X %02X->%02X p1=%08lX r3b=%02X r3c=%02X r9d=%02X\n",
           g_bokosuka_2c_seq,
           structured_log_current_frame(),
           (unsigned)PPU_Scanline,
           (unsigned)PC,
           (unsigned)bokosuka_state_stack_return(),
           (unsigned)prev,
           (unsigned)value,
           (unsigned long)PAD1_Latch,
           (unsigned)RAM[0x003B],
           (unsigned)RAM[0x003C],
           (unsigned)RAM[0x009D]);
    boko_flash_trace_record_2c(g_bokosuka_2c_seq,
                               structured_log_current_frame(),
                               PPU_Scanline,
                               PC,
                               bokosuka_state_stack_return(),
                               prev,
                               value,
                               PAD1_Latch,
                               RAM[0x003B],
                               RAM[0x003C],
                               RAM[0x009D]);
    return;
  }

  printf("[BOKO_STATE] f=%lu sl=%u pc=%04X ram[%04X]=%02X p1=%08lX p2=%08lX r2c=%02X r3b=%02X r3c=%02X r9d=%02X\n",
         structured_log_current_frame(),
         (unsigned)PPU_Scanline,
         (unsigned)PC,
         (unsigned)addr,
         (unsigned)value,
         (unsigned long)PAD1_Latch,
         (unsigned long)PAD2_Latch,
         (unsigned)RAM[0x002C],
         (unsigned)RAM[0x003B],
         (unsigned)RAM[0x003C],
         (unsigned)RAM[0x009D]);
  boko_flash_trace_record_state(structured_log_current_frame(),
                                PPU_Scanline,
                                PC,
                                addr,
                                value,
                                PAD1_Latch,
                                PAD2_Latch,
                                RAM[0x002C],
                                RAM[0x003B],
                                RAM[0x003C],
                                RAM[0x009D]);
}
#endif

#if INFONES_ENABLE_INPUT_IO_LOG
struct InputIoLogSequence
{
  bool active;
  bool summary_emitted;
  bool extra_emitted;
  unsigned long seq_no;
  unsigned long start_frame;
  unsigned long start_nmi;
  unsigned long start_scanline;
  WORD start_pc;
  WORD read_pc;
  DWORD latch;
  BYTE bits;
  BYTE read_count;
  BYTE extra_reads;
};

struct InputIoLogLastSummary
{
  bool valid;
  unsigned long seq_no;
  WORD read_pc;
  BYTE bits;
  BYTE read_count;
};

static InputIoLogSequence g_input_io_pad1_seq = {};
static InputIoLogSequence g_input_io_pad2_seq = {};
static InputIoLogLastSummary g_input_io_pad1_last = {};
static InputIoLogLastSummary g_input_io_pad2_last = {};

static inline void input_io_log_emit_summary(unsigned pad,
                                             const InputIoLogSequence *seq,
                                             InputIoLogLastSummary *last,
                                             const char *reason)
{
  const bool changed = !last->valid ||
                       last->read_pc != seq->read_pc ||
                       last->bits != seq->bits ||
                       last->read_count != seq->read_count ||
                       (seq->seq_no - last->seq_no) >= 64 ||
                       seq->extra_reads != 0;
  if (!changed)
  {
    return;
  }

  printf("[IN_SUM] pad=%u seq=%lu f=%lu sl=%lu wpc=%04X rpc=%04X latch=%08lX bits=%02X cnt=%u extra=%u reason=%s\n",
         pad,
         seq->seq_no,
         seq->start_frame,
         seq->start_scanline,
         (unsigned)seq->start_pc,
         (unsigned)seq->read_pc,
         (unsigned long)seq->latch,
         (unsigned)seq->bits,
         (unsigned)seq->read_count,
         (unsigned)seq->extra_reads,
         reason);

  last->valid = true;
  last->seq_no = seq->seq_no;
  last->read_pc = seq->read_pc;
  last->bits = seq->bits;
  last->read_count = seq->read_count;
}

static inline void input_io_log_begin_sequence(InputIoLogSequence *seq,
                                               InputIoLogLastSummary *last,
                                               unsigned pad,
                                               DWORD latch)
{
  if (seq->active && !seq->summary_emitted)
  {
    input_io_log_emit_summary(pad, seq, last, "restart");
  }

  seq->active = true;
  seq->summary_emitted = false;
  seq->extra_emitted = false;
  seq->seq_no++;
  seq->start_frame = structured_log_current_frame();
  seq->start_nmi = structured_log_nmi_count();
  seq->start_scanline = (unsigned long)PPU_Scanline;
  seq->start_pc = PC;
  seq->read_pc = 0;
  seq->latch = latch;
  seq->bits = 0;
  seq->read_count = 0;
  seq->extra_reads = 0;
}

static inline void input_io_log_note_read(InputIoLogSequence *seq,
                                          InputIoLogLastSummary *last,
                                          unsigned pad,
                                          BYTE value)
{
  if (!seq->active)
  {
    input_io_log_begin_sequence(seq, last, pad, seq->latch);
    seq->start_pc = PC;
  }

  if (seq->read_count == 0)
  {
    seq->read_pc = PC;
  }

  if (seq->read_count < 8)
  {
    seq->bits = (BYTE)(seq->bits | ((value & 1u) << seq->read_count));
    seq->read_count++;
    if (seq->read_count == 8 && !seq->summary_emitted)
    {
      input_io_log_emit_summary(pad, seq, last, "done");
      seq->summary_emitted = true;
    }
  }
  else
  {
    if (seq->extra_reads < 0xff)
    {
      seq->extra_reads++;
    }
    if (!seq->extra_emitted)
    {
      input_io_log_emit_summary(pad, seq, last, "extra");
      seq->extra_emitted = true;
    }
  }
}
#endif

/*===================================================================*/
/*                                                                   */
/*               K6502_Read() : Reading operation                    */
/*                                                                   */
/*===================================================================*/
static inline BYTE __not_in_flash_func(K6502_Read)(WORD wAddr)
{
  /*
   *  Reading operation
   *
   *  Parameters
   *    WORD wAddr              (Read)
   *      Address to read
   *
   *  Return values
   *    Read data
   *
   *  Remarks
   *    0x0000 - 0x1fff  RAM ( 0x800 - 0x1fff is mirror of 0x0 - 0x7ff )
   *    0x2000 - 0x3fff  PPU
   *    0x4000 - 0x5fff  Sound
   *    0x6000 - 0x7fff  SRAM ( Battery Backed )
   *    0x8000 - 0xffff  ROM
   *
   */
  BYTE byRet;

  if (wAddr >= 0x8000)
  {
    byRet = ROMBANK[(wAddr - 0x8000) >> 13][wAddr & 0x1fff];
    return byRet;
  }

  switch (wAddr & 0xe000)
  {
  case 0x0000: /* RAM */
    return RAM[wAddr & 0x7ff];

  case 0x2000:                /* PPU */
    if ((wAddr & 0x7) == 0x7) /* PPU Memory */
    {
      WORD addr = PPU_Addr;

      // Increment PPU Address
      PPU_Addr += PPU_Increment;
      addr &= 0x3fff;

      // Set return value;
      byRet = PPU_R7;

      // Read PPU Memory
      PPU_R7 = PPUBANK[addr >> 10][addr & 0x3ff];

      return byRet;
    }
    else if ((wAddr & 0x7) == 0x4) /* SPR_RAM I/O Register */
    {
      return SPRRAM[PPU_R3++];
    }
    else if ((wAddr & 0x7) == 0x2) /* PPU Status */
    {
      // Set return value
      byRet = PPU_R2;
      #if INFONES_ENABLE_PPU2006_EVT_LOG
      if (structured_log_event_enabled())
      {
        if (PC == 0xC9D6)
        {
          printf("[EVT] f=%lu sl=%u cyc=NA $2002=%02X pc=%04X q67=%02X a45=%02X a46=%02X a47=%02X ce=%02X\n",
                 structured_log_current_frame(),
                 (unsigned)PPU_Scanline,
                 (unsigned)byRet,
                 (unsigned)PC,
                 (unsigned)RAM[0x0671],
                 (unsigned)RAM[0x0645],
                 (unsigned)RAM[0x0646],
                 (unsigned)RAM[0x0647],
                 (unsigned)RAM[0x00CE]);
        }
        else
        {
          printf("[EVT] f=%lu sl=%u cyc=NA $2002=%02X pc=%04X\n",
                 structured_log_current_frame(),
                 (unsigned)PPU_Scanline,
                 (unsigned)byRet,
                 (unsigned)PC);
        }
      }
      #endif

      // Reset a V-Blank flag
      PPU_R2 &= ~R2_IN_VBLANK;

      // Reset address latch
      PPU_Latch_Flag = 0;

      return byRet;
    }
    break;

  case 0x4000: /* Sound */
    if (wAddr == 0x4015)
    {
      // APU status: channel bits report active length/DMC state, not enable latch.
      byRet = APU_Reg[0x15] & 0x40;
      if (ApuC1Atl > 0)
        byRet |= (1 << 0);
      if (ApuC2Atl > 0)
        byRet |= (1 << 1);
      if (ApuC3Atl > 0)
        byRet |= (1 << 2);
      if (ApuC4Atl > 0)
        byRet |= (1 << 3);
      if (ApuC5DmaLength > 0)
        byRet |= (1 << 4);

      // FrameIRQ
      APU_Reg[0x15] &= ~0x40;
      return byRet;
    }
    else if (wAddr == 0x4016)
    {
      // Set Joypad1 data
      const DWORD bit = PAD1_Bit;
      const BYTE padBit = (bit < 8) ? (BYTE)((PAD1_Latch >> bit) & 1) : 1;
      byRet = (BYTE)(padBit | 0x40);
      if (PAD1_Bit < 8)
      {
        PAD1_Bit++;
      }
      #if INFONES_ENABLE_INPUT_IO_LOG
      input_io_log_note_read(&g_input_io_pad1_seq,
                             &g_input_io_pad1_last,
                             1,
                             byRet);
      #endif
      return byRet;
    }
    else if (wAddr == 0x4017)
    {
      // Set Joypad2 data
      const DWORD bit = PAD2_Bit;
      const BYTE padBit = (bit < 8) ? (BYTE)((PAD2_Latch >> bit) & 1) : 1;
      byRet = (BYTE)(padBit | 0x40);
      if (PAD2_Bit < 8)
      {
        PAD2_Bit++;
      }
      #if INFONES_ENABLE_INPUT_IO_LOG
      input_io_log_note_read(&g_input_io_pad2_seq,
                             &g_input_io_pad2_last,
                             2,
                             byRet);
      #endif
      return byRet;
    }
    else
    {
      /* Return Mapper Register*/
      return MapperReadApu(wAddr);
    }
    break;
    // The other sound registers are not readable.

  case 0x6000: /* SRAM */
    if (ROM_SRAM)
    {
      return SRAM[wAddr & 0x1fff];
    }
    else
    { /* SRAM BANK */
      return SRAMBANK[wAddr & 0x1fff];
    }

    // case 0x8000: /* ROM BANK 0 */
    //   return ROMBANK0[wAddr & 0x1fff];

    // case 0xa000: /* ROM BANK 1 */
    //   return ROMBANK1[wAddr & 0x1fff];

    // case 0xc000: /* ROM BANK 2 */
    //   return ROMBANK2[wAddr & 0x1fff];

    // case 0xe000: /* ROM BANK 3 */
    //   return ROMBANK3[wAddr & 0x1fff];
  }

  return (wAddr >> 8); /* when a register is not readable the upper half
                            address is returned. */
}

/*===================================================================*/
/*                                                                   */
/*               K6502_Write() : Writing operation                    */
/*                                                                   */
/*===================================================================*/
static inline void __not_in_flash_func(K6502_Write)(WORD wAddr, BYTE byData)
{
  /*
   *  Writing operation
   *
   *  Parameters
   *    WORD wAddr              (Read)
   *      Address to write
   *
   *    BYTE byData             (Read)
   *      Data to write
   *
   *  Remarks
   *    0x0000 - 0x1fff  RAM ( 0x800 - 0x1fff is mirror of 0x0 - 0x7ff )
   *    0x2000 - 0x3fff  PPU
   *    0x4000 - 0x5fff  Sound
   *    0x6000 - 0x7fff  SRAM ( Battery Backed )
   *    0x8000 - 0xffff  ROM
   *
   */

  switch (wAddr & 0xe000)
  {
  case 0x0000: /* RAM */
  {
    auto addr = wAddr & 0x7ff;
    RAM[addr] = byData;
    structured_log_note_initial_a5_ram_write(addr, byData);
#if INFONES_ENABLE_BOKOSUKA_STATE_LOG
    bokosuka_state_maybe_log_ram_write(addr, byData);
#endif
#if INFONES_ENABLE_PPU2006_EVT_LOG
    if (structured_log_event_enabled() && structured_log_watch_ram_addr(addr))
    {
      printf("[EVT] f=%lu sl=%u nmi=%lu cyc=NA RAM[%04X]=%02X pc=%04X a=%02X x=%02X y=%02X sp=%02X prg=%u r39=%02X r41=%02X r42=%02X r66=%02X r67=%02X a5=%02X rB1=%02X r61b=%02X r61c=%02X r674=%02X r697=%02X\n",
             structured_log_current_frame(),
             (unsigned)PPU_Scanline,
             structured_log_nmi_count(),
             (unsigned)addr,
             (unsigned)byData,
             (unsigned)PC,
             (unsigned)A,
             (unsigned)X,
             (unsigned)Y,
             (unsigned)SP,
             (unsigned)((ROMBANK0 - ROM) >> 14),
             (unsigned)RAM[0x0039],
             (unsigned)RAM[0x0041],
             (unsigned)RAM[0x0042],
             (unsigned)RAM[0x0066],
             (unsigned)RAM[0x0067],
             (unsigned)RAM[0x00A5],
             (unsigned)RAM[0x00B1],
             (unsigned)RAM[0x061B],
             (unsigned)RAM[0x061C],
             (unsigned)RAM[0x0674],
             (unsigned)RAM[0x0697]);
    }
#endif
  }
  break;

  case 0x2000: /* PPU */
    switch (wAddr & 0x7)
    {
    case 0: /* 0x2000 */
      PPU_R0 = byData;
      PPU_Increment = (PPU_R0 & R0_INC_ADDR) ? 32 : 1;
      PPU_NameTableBank = NAME_TABLE0 + (PPU_R0 & R0_NAME_ADDR);
      PPU_BG_Base = (PPU_R0 & R0_BG_ADDR) ? ChrBuf + 256 * 64 : ChrBuf;
      PPU_SP_Base = (PPU_R0 & R0_SP_ADDR) ? ChrBuf + 256 * 64 : ChrBuf;
      PPU_SP_Height = (PPU_R0 & R0_SP_SIZE) ? 16 : 8;

      // Account for Loopy's scrolling discoveries
      PPU_Temp = (PPU_Temp & 0xF3FF) | ((((WORD)byData) & 0x0003) << 10);
      structured_log_note_initial_ppu_write(0, 0);
      #if INFONES_ENABLE_PPU2006_EVT_LOG
      if (structured_log_event_enabled())
      {
        printf("[EVT] f=%lu sl=%u cyc=NA $2000=%02X pc=%04X a=%02X x=%02X y=%02X\n",
               structured_log_current_frame(),
               (unsigned)PPU_Scanline,
               (unsigned)byData,
               (unsigned)PC,
               (unsigned)A,
               (unsigned)X,
               (unsigned)Y);
      }
      #endif
      break;

    case 1: /* 0x2001 */
      PPU_R1 = byData;
      structured_log_note_initial_ppu_write(1, 0);
      structured_log_note_initial_a5_ppumask(byData);
      #if INFONES_ENABLE_PPU2006_EVT_LOG
      if (structured_log_event_enabled())
      {
        printf("[EVT] f=%lu sl=%u nmi=%lu cyc=NA $2001=%02X pc=%04X a=%02X x=%02X y=%02X r61b=%02X a5=%02X\n",
               structured_log_current_frame(),
               (unsigned)PPU_Scanline,
               structured_log_nmi_count(),
               (unsigned)byData,
               (unsigned)PC,
               (unsigned)A,
               (unsigned)X,
               (unsigned)Y,
               (unsigned)RAM[0x061B],
               (unsigned)RAM[0x00A5]);
      }
      #endif
      break;

    case 2: /* 0x2002 */
#if 0	  
          PPU_R2 = byData;     // 0x2002 is not writable
#endif
      break;

    case 3: /* 0x2003 */
      // Sprite RAM Address
      PPU_R3 = byData;
      break;

    case 4: /* 0x2004 */
      // Write data to Sprite RAM
      SPRRAM[PPU_R3++] = byData;
      break;

    case 5: /* 0x2005 */
      // Set Scroll Register
      if (PPU_Latch_Flag)
      {
        // V-Scroll Register
        PPU_Scr_V_Next = (byData > 239) ? byData - 240 : byData;
        PPU_Scr_V_Byte_Next = PPU_Scr_V_Next >> 3;
        PPU_Scr_V_Bit_Next = PPU_Scr_V_Next & 7;
        if (byData > 239)
          PPU_NameTableBank ^= NAME_TABLE_V_MASK;

        // Added : more Loopy Stuff
        PPU_Temp = (PPU_Temp & 0xFC1F) | ((((WORD)byData) & 0xF8) << 2);
        PPU_Temp = (PPU_Temp & 0x8FFF) | ((((WORD)byData) & 0x07) << 12);
      }
      else
      {
        // H-Scroll Register
        PPU_Scr_H_Next = byData;
        PPU_Scr_H_Byte_Next = PPU_Scr_H_Next >> 3;
        PPU_Scr_H_Bit_Next = PPU_Scr_H_Next & 7;
        PPU_Scr_H_Bit = byData & 7;

        // Added : more Loopy Stuff
        PPU_Temp = (PPU_Temp & 0xFFE0) | ((((WORD)byData) & 0xF8) >> 3);
      }
      structured_log_note_initial_ppu_write(5, 0);
      #if INFONES_ENABLE_PPU2006_EVT_LOG
      // Semantic trigger: first CB0B Y-scroll write with non-zero value opens the window
      if (PPU_Latch_Flag && PC == 0xCB0B && byData != 0)
      {
        structured_log_open_scroll_window();
      }
      if (structured_log_event_enabled() && (PC == 0xCB06 || PC == 0xCB0B || PC == 0xCA5B || PC == 0xCA88))
      {
        printf("[EVT] f=%lu sl=%u cyc=NA $2005=%02X axis=%s pc=%04X\n",
               structured_log_current_frame(),
               (unsigned)PPU_Scanline,
               (unsigned)byData,
               PPU_Latch_Flag ? "Y" : "X",
               (unsigned)PC);
      }
      #endif
      PPU_Latch_Flag ^= 1;
      break;

    case 6: /* 0x2006 */
      // Set PPU Address
      if (PPU_Latch_Flag)
      {
        /* Low */
#if 0
            PPU_Addr = ( PPU_Addr & 0xff00 ) | ( (WORD)byData );
#else
        PPU_Temp = (PPU_Temp & 0xFF00) | (((WORD)byData) & 0x00FF);
        PPU_Addr = PPU_Temp;
#endif
        if (!(PPU_R2 & R2_IN_VBLANK))
          InfoNES_SetupScr();
      }
      else
      {
        /* High */
#if 0
            PPU_Addr = ( PPU_Addr & 0x00ff ) | ( (WORD)( byData & 0x3f ) << 8 );
            InfoNES_SetupScr();
#else
        PPU_Temp = (PPU_Temp & 0x00FF) | ((((WORD)byData) & 0x003F) << 8);
#endif
      }
      structured_log_note_initial_ppu_write(6, PPU_Latch_Flag ? PPU_Addr : PPU_Temp);
      // 1.4i: $2006 logging removed; focus on $2000/$2001
      PPU_Latch_Flag ^= 1;
      break;

    case 7: /* 0x2007 */
    {
      WORD addr = PPU_Addr;
      BYTE vramData = byData;
      const bool renderingWrite = (PPU_Scanline < SCAN_VBLANK_START) && (PPU_R1 & (R1_SHOW_SCR | R1_SHOW_SP));
      if constexpr (kPpu2007Mode == PPU2007_MODE_RENDER_ADDR_LOWBYTE ||
                    kPpu2007Mode == PPU2007_MODE_RENDER_LOWBYTE_AND_SCROLL)
      {
        if (renderingWrite)
          vramData = (BYTE)(addr & 0xff);
      }

      if constexpr (kPpu2007Mode == PPU2007_MODE_RENDER_LOWBYTE_AND_SCROLL)
      {
        if (renderingWrite)
          InfoNES_IncrementVideoRamAddrDuringRendering();
        else
          PPU_Addr += PPU_Increment;
      }
      else
      {
        PPU_Addr += PPU_Increment;
      }
      addr &= 0x3fff;
      WORD addr_after = PPU_Addr & 0x3fff;
      const char* area = (addr < 0x2000) ? "PATTERN" : ((addr < 0x3f00) ? "NAMETABLE" : "PALETTE");
      structured_log_note_initial_ppu_write(7, addr);
      #if INFONES_ENABLE_PPU2006_EVT_LOG
      if (structured_log_early_ppu2007_enabled() && addr < 0x3f00 && structured_log_title_scroll_2007_pc(PC))
      {
        printf("[EVT] f=%lu sl=%u cyc=NA $2007=%02X addr_before=%04X addr_after=%04X area=%s pc=%04X\n",
               structured_log_current_frame(),
               (unsigned)PPU_Scanline,
               (unsigned)byData,
               (unsigned)addr,
               (unsigned)addr_after,
               area,
               (unsigned)PC);
      }
      #endif

      // Write to PPU Memory
      if (addr < 0x2000 && byVramWriteEnable)
      {
        // Pattern Data
        ChrBufUpdate |= (1 << (addr >> 10));
        PPUBANK[addr >> 10][addr & 0x3ff] = vramData;
      }
      else if (addr < 0x3f00) /* 0x2000 - 0x3eff */
      {
        // Name Table and mirror
        PPUBANK[addr >> 10][addr & 0x3ff] = vramData;
        PPUBANK[(addr ^ 0x1000) >> 10][addr & 0x3ff] = vramData;
      }
      else if (!(addr & 0xf)) /* 0x3f00 or 0x3f10 */
      {
        vramData &= 0x3f;
        // Palette mirror
        PPURAM[0x3f10] = PPURAM[0x3f14] = PPURAM[0x3f18] = PPURAM[0x3f1c] =
            PPURAM[0x3f00] = PPURAM[0x3f04] = PPURAM[0x3f08] = PPURAM[0x3f0c] = vramData;
        PalTable[0x00] = PalTable[0x04] = PalTable[0x08] = PalTable[0x0c] =
            PalTable[0x10] = PalTable[0x14] = PalTable[0x18] = PalTable[0x1c] =
                InfoNES_Palette444ToRgb565(NesPalette[vramData]);
      }
      else if (addr & 3)
      {
        vramData &= 0x3f;
        // Palette
        PPURAM[addr] = vramData;
        PalTable[addr & 0x1f] = InfoNES_Palette444ToRgb565(NesPalette[vramData]);
      }
    }
    break;

    }
    break;

  case 0x4000: /* Sound */
    switch (wAddr & 0x1f)
    {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0a:
    case 0x0b:
    case 0x0c:
    case 0x0d:
    case 0x0e:
    case 0x0f:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
      // Call Function corresponding to Sound Registers
      if (!APU_Mute)
        pAPUSoundRegs[wAddr & 0x1f](wAddr, byData);
      break;

    case 0x14: /* 0x4014 */
      // Sprite DMA
      switch (byData >> 5)
      {
      case 0x0: /* RAM */
        InfoNES_MemoryCopy(SPRRAM, &RAM[((WORD)byData << 8) & 0x7ff], SPRRAM_SIZE);
        break;

      case 0x3: /* SRAM */
        InfoNES_MemoryCopy(SPRRAM, &SRAM[((WORD)byData << 8) & 0x1fff], SPRRAM_SIZE);
        break;

      case 0x4: /* ROM BANK 0 */
        InfoNES_MemoryCopy(SPRRAM, &ROMBANK0[((WORD)byData << 8) & 0x1fff], SPRRAM_SIZE);
        break;

      case 0x5: /* ROM BANK 1 */
        InfoNES_MemoryCopy(SPRRAM, &ROMBANK1[((WORD)byData << 8) & 0x1fff], SPRRAM_SIZE);
        break;

      case 0x6: /* ROM BANK 2 */
        InfoNES_MemoryCopy(SPRRAM, &ROMBANK2[((WORD)byData << 8) & 0x1fff], SPRRAM_SIZE);
        break;

      case 0x7: /* ROM BANK 3 */
        InfoNES_MemoryCopy(SPRRAM, &ROMBANK3[((WORD)byData << 8) & 0x1fff], SPRRAM_SIZE);
        break;
      }
      break;

    case 0x15: /* 0x4015 */
      InfoNES_pAPUWriteControl(wAddr, byData);
#if 0
          /* Unknown */
          if ( byData & 0x10 ) 
          {
	    byData &= ~0x80;
	  }
#endif
      break;

    case 0x16: /* 0x4016 */
      // Reset joypad
      if (!(APU_Reg[0x16] & 1) && (byData & 1))
      {
        PAD1_Bit = 0;
        PAD2_Bit = 0;
      }
      #if INFONES_ENABLE_INPUT_IO_LOG
      if ((APU_Reg[0x16] & 1) && !(byData & 1))
      {
        input_io_log_begin_sequence(&g_input_io_pad1_seq,
                                    &g_input_io_pad1_last,
                                    1,
                                    PAD1_Latch);
        input_io_log_begin_sequence(&g_input_io_pad2_seq,
                                    &g_input_io_pad2_last,
                                    2,
                                    PAD2_Latch);
      }
      #endif
      break;

    case 0x17: /* 0x4017 */
      // Frame IRQ
      FrameStep = 0;
      if (!(byData & 0xc0))
      {
        FrameIRQ_Enable = 1;
      }
      else
      {
        FrameIRQ_Enable = 0;
      }
      break;
    }

    #if INFONES_ENABLE_PPU2006_EVT_LOG
    if (structured_log_event_enabled() &&
        structured_log_watch_apu_addr(wAddr) &&
        APU_Reg[wAddr & 0x1f] != byData)
    {
      printf("[EVT] f=%lu sl=%u nmi=%lu cyc=NA APU[$%04X]=%02X prev=%02X pc=%04X a=%02X x=%02X y=%02X r66=%02X r67=%02X a5=%02X r61b=%02X r674=%02X r697=%02X\n",
             structured_log_current_frame(),
             (unsigned)PPU_Scanline,
             structured_log_nmi_count(),
             (unsigned)wAddr,
             (unsigned)byData,
             (unsigned)APU_Reg[wAddr & 0x1f],
             (unsigned)PC,
             (unsigned)A,
             (unsigned)X,
             (unsigned)Y,
             (unsigned)RAM[0x0066],
             (unsigned)RAM[0x0067],
             (unsigned)RAM[0x00A5],
             (unsigned)RAM[0x061B],
             (unsigned)RAM[0x0674],
             (unsigned)RAM[0x0697]);
    }
    #endif

    if (wAddr <= 0x4017)
    {
      /* Write to APU Register */
      APU_Reg[wAddr & 0x1f] = byData;
    }
    else
    {
      /* Write to APU */
      MapperApu(wAddr, byData);
    }
    break;

  case 0x6000: /* SRAM */
    SRAM[wAddr & 0x1fff] = byData;
    SRAMwritten = true;

    /* Write to SRAM, when no SRAM */
    if (!ROM_SRAM)
    {
      MapperSram(wAddr, byData);
    }
    break;

  case 0x8000: /* ROM BANK 0 */
  case 0xa000: /* ROM BANK 1 */
  case 0xc000: /* ROM BANK 2 */
  case 0xe000: /* ROM BANK 3 */
    // Write to Mapper
    MapperWrite(wAddr, byData);
    break;
  }
}

// Reading/Writing operation (WORD version)
static inline WORD K6502_ReadW(WORD wAddr) { return K6502_Read(wAddr) | (WORD)K6502_Read(wAddr + 1) << 8; };
static inline void K6502_WriteW(WORD wAddr, WORD wData)
{
  K6502_Write(wAddr, wData & 0xff);
  K6502_Write(wAddr + 1, wData >> 8);
};
static inline WORD K6502_ReadZpW(BYTE byAddr) { return K6502_ReadZp(byAddr) | (K6502_ReadZp(byAddr + 1) << 8); };

// 6502's indirect absolute jmp(opcode: 6C) has a bug (added at 01/08/15 )
static inline WORD K6502_ReadW2(WORD wAddr)
{
  if (0x00ff == (wAddr & 0x00ff))
  {
    return K6502_Read(wAddr) | (WORD)K6502_Read(wAddr - 0x00ff) << 8;
  }
  else
  {
    return K6502_Read(wAddr) | (WORD)K6502_Read(wAddr + 1) << 8;
  }
}

#endif /* !K6502_RW_H_INCLUDED */
