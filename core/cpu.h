/*
 * cpu.h — 6502 CPU interface
 *
 * Part of Picocalc_NESco
 * MIT License
 */
#pragma once

#include "nes_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CpuPerfCountersTag {
    DWORD prg_fast_reads;
    DWORD stack_ops;
    DWORD zp_reads;
    DWORD zp_writes;
    DWORD zp_ptr_reads;
    DWORD zp_rmw_reads;
} CpuPerfCounters;

/**
 * K6502_Reset() — Reset the CPU to power-on state.
 *
 * Loads PC from reset vector at $FFFC/$FFFD.
 * Sets SP=$FF, A/X/Y=0, F = FLAG_Z|FLAG_R|FLAG_I.
 * Clears interrupt wiring (NMI_Wiring=NMI_State=IRQ_Wiring=IRQ_State=0;
 * K6502_Set_Int_Wiring is then called separately by the mapper).
 */
void K6502_Reset(void);

/**
 * K6502_Step(wClocks) — Execute instructions until clock budget is consumed.
 *
 * Exits when g_wPassedClocks >= wClocks.
 * On return: g_wPassedClocks -= wClocks  (excess carries into next call).
 * g_wCurrentClocks accumulates monotonically.
 *
 * Called once per scanline with wClocks = STEP_PER_SCANLINE.
 *
 * RAM-resident (RAMFUNC) on RP2040.
 */
void K6502_Step(WORD wClocks);

/**
 * K6502_Read(addr) — Read one byte from the CPU address space.
 *
 * Dispatches to RAM, PPU regs, APU/IO, SRAM, or ROM bank.
 * RAM-resident on RP2040.
 */
BYTE K6502_Read(WORD addr);

/**
 * K6502_Write(addr, data) — Write one byte to the CPU address space.
 *
 * Dispatches to RAM, PPU regs, APU/IO, SRAM, mapper registers.
 * RAM-resident on RP2040.
 */
void K6502_Write(WORD addr, BYTE data);

/**
 * K6502_RunUnofficial(opcode) — Execute one unofficial opcode.
 *
 * Called from K6502_Step when g_unofficialOpcodeTable[opcode] != 0.
 * Flash-resident (not RAMFUNC); handles the stable unofficial group
 * defined in specs/20_cpu.md.
 */
void K6502_RunUnofficial(BYTE opcode);

/**
 * K6502_Set_Int_Wiring(nmi_wiring, irq_wiring) — Set interrupt baseline.
 */
void K6502_Set_Int_Wiring(BYTE nmi_wiring, BYTE irq_wiring);

void CPU_PerfSnapshot(CpuPerfCounters *out);
void CPU_PerfReset(void);

#ifdef __cplusplus
}
#endif
