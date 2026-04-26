#pragma once

#include "InfoNES_Types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void boko_flash_trace_begin(const char *rom_path);
void boko_flash_trace_record_2c(unsigned long seq,
                                unsigned long frame,
                                unsigned scanline,
                                WORD pc,
                                WORD ret,
                                BYTE prev,
                                BYTE value,
                                DWORD pad1,
                                BYTE r3b,
                                BYTE r3c,
                                BYTE r9d);
void boko_flash_trace_record_state(unsigned long frame,
                                   unsigned scanline,
                                   WORD pc,
                                   WORD addr,
                                   BYTE value,
                                   DWORD pad1,
                                   DWORD pad2,
                                   BYTE r2c,
                                   BYTE r3b,
                                   BYTE r3c,
                                   BYTE r9d);
void boko_flash_trace_record_8827(unsigned long trig,
                                  unsigned window,
                                  unsigned long frame,
                                  unsigned scanline,
                                  BYTE a,
                                  BYTE flags,
                                  BYTE r1b,
                                  BYTE r25,
                                  BYTE r26,
                                  BYTE r2c,
                                  BYTE r3f,
                                  BYTE r48,
                                  BYTE r3b,
                                  BYTE r3c,
                                  BYTE r9d);
void boko_flash_trace_record_888e(unsigned long trig,
                                  unsigned window,
                                  unsigned long frame,
                                  unsigned scanline,
                                  BYTE a,
                                  BYTE flags,
                                  BYTE r1f,
                                  BYTE r25,
                                  BYTE r26,
                                  BYTE r29,
                                  BYTE r2c,
                                  BYTE r3b,
                                  BYTE r3c);
void boko_flash_trace_record_heartbeat(unsigned long seq,
                                       WORD pc,
                                       unsigned scanline,
                                       DWORD pad1,
                                       DWORD pad2,
                                       DWORD sys,
                                       BYTE r2c,
                                       BYTE r3b,
                                       BYTE r3c,
                                       BYTE r9d,
                                       BYTE a);
void boko_flash_trace_record_freeze(unsigned samples,
                                    WORD pc,
                                    BYTE r2c,
                                    BYTE r3b,
                                    BYTE r3c,
                                    BYTE r9d);
void boko_flash_trace_flush(void);
void boko_flash_trace_dump_to_sd(const char *reason);

#ifdef __cplusplus
}
#endif
