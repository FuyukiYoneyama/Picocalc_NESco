/*===================================================================*/
/*                                                                   */
/*  InfoNES_StructuredLog.h : InfoNES structured diagnostic logging  */
/*                                                                   */
/*===================================================================*/

#ifndef INFONES_STRUCTURED_LOG_H_INCLUDED
#define INFONES_STRUCTURED_LOG_H_INCLUDED

#include "InfoNES_Types.h"

#ifndef INFONES_ENABLE_PPU2006_EVT_LOG
#define INFONES_ENABLE_PPU2006_EVT_LOG 0
#endif

#ifndef INFONES_ENABLE_INITIAL_SEQUENCE_LOG
#define INFONES_ENABLE_INITIAL_SEQUENCE_LOG 0
#endif

#ifndef INFONES_ENABLE_INPUT_IO_LOG
#define INFONES_ENABLE_INPUT_IO_LOG 0
#endif

#ifndef INFONES_ENABLE_BOKOSUKA_STATE_LOG
#define INFONES_ENABLE_BOKOSUKA_STATE_LOG 0
#endif

#define INFONES_STRUCTURED_LOG_ENABLED \
  (INFONES_ENABLE_PPU2006_EVT_LOG || \
   INFONES_ENABLE_INITIAL_SEQUENCE_LOG || \
   INFONES_ENABLE_INPUT_IO_LOG || \
   INFONES_ENABLE_BOKOSUKA_STATE_LOG)

#ifdef __cplusplus
extern "C" {
#endif

#if INFONES_STRUCTURED_LOG_ENABLED
void InfoNES_StructuredLogReset(void);
void InfoNES_StructuredLogEndFrame(void);
void InfoNES_StructuredLogNotePadTrigger(bool transition_pressed,
                                         BYTE joypad,
                                         DWORD pad1,
                                         bool a_pressed,
                                         bool start_pressed);
#else
static inline void InfoNES_StructuredLogReset(void) {}
static inline void InfoNES_StructuredLogEndFrame(void) {}
static inline void InfoNES_StructuredLogNotePadTrigger(bool transition_pressed,
                                                       BYTE joypad,
                                                       DWORD pad1,
                                                       bool a_pressed,
                                                       bool start_pressed)
{
  (void)transition_pressed;
  (void)joypad;
  (void)pad1;
  (void)a_pressed;
  (void)start_pressed;
}
#endif

#ifdef __cplusplus
}

#if INFONES_STRUCTURED_LOG_ENABLED
bool structured_log_event_enabled();
bool structured_log_early_ppu2007_enabled();
unsigned long structured_log_current_frame();
unsigned long structured_log_nmi_count();
bool structured_log_near_pad_trigger();
void structured_log_note_nmi_request();
void structured_log_arm_start_trigger();
void structured_log_maybe_begin_2006_window(WORD addr, BYTE latch_flag);
void structured_log_open_scroll_window();
void structured_log_note_initial_ppu_write(BYTE reg, WORD addr);
void structured_log_note_initial_a5_exec(WORD pc);
void structured_log_note_initial_a5_code_bytes(WORD pc, BYTE op0, BYTE op1, BYTE op2);
void structured_log_note_initial_a5_ram_write(WORD addr, BYTE value);
void structured_log_note_initial_a5_ppumask(BYTE value);
#else
static inline bool structured_log_event_enabled() { return false; }
static inline bool structured_log_early_ppu2007_enabled() { return false; }
static inline unsigned long structured_log_current_frame() { return 0; }
static inline unsigned long structured_log_nmi_count() { return 0; }
static inline bool structured_log_near_pad_trigger() { return false; }
static inline void structured_log_note_nmi_request() {}
static inline void structured_log_arm_start_trigger() {}
static inline void structured_log_maybe_begin_2006_window(WORD addr, BYTE latch_flag)
{
  (void)addr;
  (void)latch_flag;
}
static inline void structured_log_open_scroll_window() {}
static inline void structured_log_note_initial_ppu_write(BYTE reg, WORD addr)
{
  (void)reg;
  (void)addr;
}
static inline void structured_log_note_initial_a5_exec(WORD pc)
{
  (void)pc;
}
static inline void structured_log_note_initial_a5_code_bytes(WORD pc, BYTE op0, BYTE op1, BYTE op2)
{
  (void)pc;
  (void)op0;
  (void)op1;
  (void)op2;
}
static inline void structured_log_note_initial_a5_ram_write(WORD addr, BYTE value)
{
  (void)addr;
  (void)value;
}
static inline void structured_log_note_initial_a5_ppumask(BYTE value)
{
  (void)value;
}
#endif
#endif

#endif
