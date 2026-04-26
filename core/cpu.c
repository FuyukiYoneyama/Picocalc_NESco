/*
 * cpu.c — MOS 6502 CPU emulator (NTSC NES variant: Ricoh 2A03)
 *
 * Design decisions:
 *  - Switch-based dispatch (256 cases): lowest branch-prediction overhead
 *    on Cortex-M0+; compiler can build a jump table.
 *  - Addressing mode helpers are static inline — inlined into each switch
 *    case, avoiding call overhead on the hot scanline path.
 *  - K6502_Step and K6502_Read/Write are RAMFUNC (RAM-resident on RP2040).
 *  - Unofficial opcodes are handled by a separate Flash-resident function,
 *    gated by the 256-byte g_unofficialOpcodeTable RAM lookup.
 *  - PAGE_CROSS macro detects page boundary crossings for +1 cycle penalty.
 *
 * Spec reference: specs/20_cpu.md
 *
 * Part of Picocalc_NESco
 * MIT License
 */

#include "cpu.h"
#include "nes_globals.h"
#include "nes_system.h"
#include "mapper.h"
#include "ppu.h"
#include "apu.h"

#include <string.h>

#if defined(PICO_BUILD)
#include <stdio.h>
#endif

#if defined(PICO_BUILD)
static CpuPerfCounters s_cpu_perf;

#define CPU_PERF_INC(field) do { s_cpu_perf.field++; } while (0)
#define CPU_PERF_STACK_POP() ((void)(s_cpu_perf.stack_ops++))

void CPU_PerfSnapshot(CpuPerfCounters *out) {
    if (out != NULL) {
        *out = s_cpu_perf;
    }
}

void CPU_PerfReset(void) {
    memset(&s_cpu_perf, 0, sizeof(s_cpu_perf));
}
#else
#define CPU_PERF_INC(field) do { } while (0)
#define CPU_PERF_STACK_POP() ((void)0)

void CPU_PerfSnapshot(CpuPerfCounters *out) {
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
}

void CPU_PerfReset(void) {
}
#endif

/* =====================================================================
 *  Forward declarations for PPU / APU register I/O
 *  (implemented in ppu.c / apu.c)
 * ===================================================================== */
extern BYTE PPU_RegisterRead(BYTE reg);
extern void PPU_RegisterWrite(BYTE reg, BYTE data);
extern void PPU_SyncVBlankEdge(void);
extern void APU_RegisterWrite(WORD addr, BYTE data);
extern BYTE APU_RegisterRead(WORD addr);

#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
static BYTE s_logged_unknown_official[256];
static BYTE s_logged_unknown_unofficial[256];

static void diag_log_unknown_opcode(const char *kind, BYTE opcode, WORD pc_before) {
    printf("[CPU] unknown %s opcode=%02X pc=%04X a=%02X x=%02X y=%02X sp=%02X f=%02X\r\n",
           kind, opcode, pc_before, A, X, Y, SP, F);
    fflush(stdout);
}
#endif

#if defined(PICO_BUILD) && defined(NESCO_RUNTIME_LOGS)
static void diag_log_loderunner_value_source(WORD pc_before, BYTE opcode) {
    if (!PPU_DiagSecondWindowActive()) {
        return;
    }
    if (!(pc_before == 0xDD5Au || pc_before == 0xDD5Du ||
          pc_before == 0xDD60u || pc_before == 0xDD63u)) {
        return;
    }
    printf("[CPU_LR] pc=%04X op=%02X a=%02X x=%02X y=%02X p=%02X sp=%02X "
           "addr=%04X temp=%04X latch=%u fineX=%u r1=%02X scan=%d clocks=%u\n",
           pc_before,
           opcode,
           A,
           X,
           Y,
           F,
           SP,
           PPU_Addr & 0x3FFFu,
           PPU_Temp & 0x3FFFu,
           (unsigned int)PPU_Latch,
           (unsigned int)PPU_Scr_H_Bit,
           (unsigned int)PPU_R1,
           g_nScanLine,
           (unsigned int)g_wPassedClocks);
}
#else
static void diag_log_loderunner_value_source(WORD pc_before, BYTE opcode) {
    (void)pc_before;
    (void)opcode;
}
#endif

/* =====================================================================
 *  Internal helpers
 * ===================================================================== */

/* Set N and Z flags from 8-bit result */
#define SET_NZ(v) do { \
    F = (F & ~(BYTE)(FLAG_N | FLAG_Z)) \
        | ((BYTE)(v) & (BYTE)FLAG_N) \
        | ((BYTE)(v) ? 0u : (BYTE)FLAG_Z); \
} while (0)

/* Page-cross detection: true if (base) and (base+idx) are in different pages */
#define PAGE_CROSS(base, idx)  (((WORD)(base) & 0xFF00u) != \
                                 (((WORD)(base) + (WORD)(idx)) & 0xFF00u))

/* Stack helpers */
#define STACK_PUSH(v)  do { \
    CPU_PERF_INC(stack_ops); \
    RAM[0x0100u | (WORD)SP] = (BYTE)(v); \
    SP--; \
} while (0)

#define STACK_POP()  (CPU_PERF_STACK_POP(), SP++, RAM[0x0100u | (WORD)SP])

/* Add cycles to counter */
#define CLOCKS(n)  do { g_wPassedClocks += (WORD)(n); } while(0)

static inline BYTE joypad_latch_read(DWORD latch, BYTE *bit_index) {
    BYTE bit;

    if (*bit_index < 8u) {
        bit = (BYTE)((latch >> *bit_index) & 1u);
        (*bit_index)++;
    } else {
        /* Standard controller returns 1 once the 8 button bits are exhausted. */
        bit = 1u;
    }

    return bit;
}

static inline WORD oam_dma_cycle_cost(void) {
    /* Alignment is determined on the cycle after the $4014 write. */
    return (WORD)(513u + (g_wPassedClocks & 1u));
}

static inline BYTE read_cpu_data(WORD addr);
static inline BYTE read_prg_rom(WORD addr);
static inline BYTE read_zp_ram(BYTE addr);
static inline void write_zp_ram(BYTE addr, BYTE data);
static inline BYTE read_zp_data(BYTE addr);
static inline BYTE read_zp_ptr_data(BYTE addr);
static inline BYTE read_zp_rmw_data(BYTE addr);
static inline WORD fetch_pc_word(void);

/* Read little-endian 16-bit value from address */
static inline WORD read16(WORD addr) {
    if (addr >= 0x8000u && addr < 0xFFFFu) {
        WORD lo = read_prg_rom(addr);
        WORD hi = read_prg_rom((WORD)(addr + 1u));
        return lo | (hi << 8);
    }
    WORD lo = read_cpu_data(addr);
    WORD hi = read_cpu_data(addr + 1u);
    return lo | (hi << 8);
}

static inline BYTE read_prg_rom(WORD addr) {
    CPU_PERF_INC(prg_fast_reads);
    return ROMBANK[(addr - 0x8000u) >> 13][addr & 0x1FFFu];
}

static inline BYTE read_zp_ram(BYTE addr) {
    return RAM[addr];
}

static inline void write_zp_ram(BYTE addr, BYTE data) {
    CPU_PERF_INC(zp_writes);
    RAM[addr] = data;
}

static inline BYTE read_zp_data(BYTE addr) {
    CPU_PERF_INC(zp_reads);
    return read_zp_ram(addr);
}

static inline BYTE read_zp_ptr_data(BYTE addr) {
    CPU_PERF_INC(zp_ptr_reads);
    return read_zp_ram(addr);
}

static inline BYTE read_zp_rmw_data(BYTE addr) {
    CPU_PERF_INC(zp_rmw_reads);
    return read_zp_ram(addr);
}

static inline BYTE read_cpu_data(WORD addr) {
    if (addr >= 0x8000u) {
        return read_prg_rom(addr);
    }
    return K6502_Read(addr);
}

static inline BYTE fetch_pc_byte(void) {
    WORD addr = PC++;
    if (addr >= 0x8000u) {
        return read_prg_rom(addr);
    }
    return read_cpu_data(addr);
}

static inline WORD fetch_pc_word(void) {
    WORD addr = PC;

    if (addr >= 0x8000u && addr < 0xFFFFu) {
        WORD lo = read_prg_rom(addr);
        WORD hi = read_prg_rom((WORD)(addr + 1u));
        PC = (WORD)(addr + 2u);
        return lo | (hi << 8);
    }

    {
        WORD lo = fetch_pc_byte();
        WORD hi = fetch_pc_byte();
        return lo | (hi << 8);
    }
}

/* Read 16-bit value with page-wrap bug (only for JMP indirect) */
static inline WORD read16_wrap(WORD addr) {
    WORD hi_addr = (addr & 0xFF00u) | ((addr + 1u) & 0x00FFu);

    if (addr >= 0x8000u && hi_addr >= 0x8000u) {
        WORD lo = read_prg_rom(addr);
        WORD hi = read_prg_rom(hi_addr);
        return lo | (hi << 8);
    }

    WORD lo = K6502_Read(addr);
    /* High byte wraps within same page (hardware bug) */
    WORD hi = K6502_Read(hi_addr);
    return lo | (hi << 8);
}

/* =====================================================================
 *  Addressing mode effective-address calculators
 *  Each function reads the operand byte(s) from PC and advances PC.
 * ===================================================================== */

/* Immediate: return value directly */
static inline BYTE EA_IMM(void)  { return fetch_pc_byte(); }

/* Zero Page */
static inline WORD EA_ZP(void)   { return fetch_pc_byte(); }
static inline WORD EA_ZPX(void)  { return (WORD)(fetch_pc_byte() + X) & 0xFFu; }
static inline WORD EA_ZPY(void)  { return (WORD)(fetch_pc_byte() + Y) & 0xFFu; }

/* Absolute */
static inline WORD EA_ABS(void) {
    return fetch_pc_word();
}

/* Absolute + X  (sets page_cross flag) */
static inline WORD EA_ABX(int *cross) {
    WORD base = EA_ABS();
    WORD eff  = base + X;
    if (cross) *cross = ((base ^ eff) >> 8) & 1;
    return eff;
}

/* Absolute + Y  (sets page_cross flag) */
static inline WORD EA_ABY(int *cross) {
    WORD base = EA_ABS();
    WORD eff  = base + Y;
    if (cross) *cross = ((base ^ eff) >> 8) & 1;
    return eff;
}

/* (Indirect,X) — (ZP + X) → address */
static inline WORD EA_IZX(void) {
    BYTE zp = (fetch_pc_byte() + X) & 0xFFu;
    WORD lo = read_zp_ptr_data(zp);
    WORD hi = read_zp_ptr_data((BYTE)(zp + 1u));
    return lo | (hi << 8);
}

/* (Indirect),Y — (ZP) → base; base + Y  (sets page_cross flag) */
static inline WORD EA_IZY(int *cross) {
    BYTE zp   = fetch_pc_byte();
    WORD lo   = read_zp_ptr_data(zp);
    WORD hi   = read_zp_ptr_data((BYTE)(zp + 1u));
    WORD base = lo | (hi << 8);
    WORD eff  = base + Y;
    if (cross) *cross = ((base ^ eff) >> 8) & 1;
    return eff;
}

/* =====================================================================
 *  Read wrappers (addressing + mem read in one call)
 * ===================================================================== */
static inline BYTE RD_IMM(void)         { return EA_IMM(); }
static inline BYTE RD_ZP(void)          { return read_zp_data((BYTE)EA_ZP()); }
static inline BYTE RD_ZPX(void)         { return read_zp_data((BYTE)EA_ZPX()); }
static inline BYTE RD_ZPY(void)         { return read_zp_data((BYTE)EA_ZPY()); }
static inline BYTE RD_ABS(void)         { return read_cpu_data(EA_ABS()); }
static inline BYTE RD_ABX(int *cross)   { return read_cpu_data(EA_ABX(cross)); }
static inline BYTE RD_ABY(int *cross)   { return read_cpu_data(EA_ABY(cross)); }
static inline BYTE RD_IZX(void)         { return read_cpu_data(EA_IZX()); }
static inline BYTE RD_IZY(int *cross)   { return read_cpu_data(EA_IZY(cross)); }

/* =====================================================================
 *  K6502_Read — CPU memory read
 * ===================================================================== */
BYTE RAMFUNC(K6502_Read)(WORD addr) {
    if (addr < 0x2000u) {
        /* Internal RAM: 2 KB, mirrored to $1FFF */
        return RAM[addr & 0x07FFu];
    }
    if (addr < 0x4000u) {
        /* PPU registers: $2000–$2007 mirrored every 8 bytes */
        return PPU_RegisterRead(addr & 0x07u);
    }
    if (addr == 0x4015u) {
        /* APU status read */
        return APU_RegisterRead(addr);
    }
    if (addr == 0x4016u) {
        /* Joypad 1 */
        BYTE bit = joypad_latch_read(PAD1_Latch, &PAD1_Bit);
        return (BYTE)(bit | 0x40u);
    }
    if (addr == 0x4017u) {
        /* Joypad 2 */
        BYTE bit = joypad_latch_read(PAD2_Latch, &PAD2_Bit);
        return (BYTE)(bit | 0x40u);
    }
    if (addr < 0x6000u) {
        /* APU / mapper APU range */
        return MapperReadApu(addr);
    }
    if (addr < 0x8000u) {
        /* SRAM / SRAM bank */
        return SRAMBANK[addr & 0x1FFFu];
    }
    /* PRG ROM banks: $8000–$FFFF (4 × 8 KB) */
    return ROMBANK[(addr - 0x8000u) >> 13][addr & 0x1FFFu];
}

/* =====================================================================
 *  K6502_Write — CPU memory write
 * ===================================================================== */
void RAMFUNC(K6502_Write)(WORD addr, BYTE data) {
    if (addr < 0x2000u) {
        RAM[addr & 0x07FFu] = data;
#if defined(PICO_BUILD)
        if ((addr & 0x07FFu) == 0x00F0u) {
            printf("[BLARGG] result=%02X pc=%04X\n", data, PC);
        }
#endif
        return;
    }
    if (addr < 0x4000u) {
        /* PPU registers */
        PPU_RegisterWrite(addr & 0x07u, data);
        return;
    }
    if (addr < 0x4020u) {
        /* APU / IO */
        if (addr == 0x4014u) {
            /* OAM DMA: copy 256 bytes from CPU page to SPRRAM */
            WORD src = (WORD)data << 8;
            for (int i = 0; i < 256; i++) {
                SPRRAM[i] = K6502_Read(src + i);
            }
            PPU_InvalidateSpriteScanlineCache();
            CLOCKS(oam_dma_cycle_cost());
            return;
        }
        if (addr == 0x4016u) {
            /* Joypad strobe: latch button state */
            DWORD pad1, pad2;
            InfoNES_PadState(&pad1, &pad2);
            PAD1_Latch = pad1;
            PAD2_Latch = pad2;
            PAD1_Bit   = 0;
            PAD2_Bit   = 0;
            return;
        }
        APU_RegisterWrite(addr, data);
        return;
    }
    if (addr < 0x6000u) {
        /* Mapper APU-range registers */
        MapperApu(addr, data);
        return;
    }
    if (addr < 0x8000u) {
        /* SRAM write */
        MapperSram(addr, data);
        return;
    }
    /* ROM write: dispatch to mapper bank-switch handler */
    MapperWrite(addr, data);
}

/* =====================================================================
 *  K6502_Set_Int_Wiring
 * ===================================================================== */
void K6502_Set_Int_Wiring(BYTE nmi_wiring, BYTE irq_wiring) {
    NMI_Wiring = nmi_wiring;
    NMI_State  = nmi_wiring;
    IRQ_Wiring = irq_wiring;
    IRQ_State  = irq_wiring;
}

/* =====================================================================
 *  K6502_Reset
 * ===================================================================== */
void K6502_Reset(void) {
    SP = 0xFFu;
    A  = 0;
    X  = 0;
    Y  = 0;
    F  = FLAG_Z | FLAG_R | FLAG_I;
    g_wPassedClocks  = 0;
    g_wCurrentClocks = 0;
    /* Load reset vector */
    PC = read16(0xFFFCu);
}

/* =====================================================================
 *  ADC / SBC helpers (set N, Z, C, V)
 * ===================================================================== */
static inline void op_ADC(BYTE val) {
    WORD result = (WORD)A + (WORD)val + (WORD)(F & FLAG_C ? 1u : 0u);
    /* Overflow: positive + positive = negative, or neg + neg = pos */
    BYTE v = (BYTE)(~(A ^ val) & (A ^ (BYTE)result) & 0x80u);
    F &= ~(BYTE)(FLAG_N | FLAG_Z | FLAG_C | FLAG_V);
    if ((BYTE)result & 0x80u) F |= FLAG_N;
    if ((BYTE)result == 0)    F |= FLAG_Z;
    if (result > 0xFFu)       F |= FLAG_C;
    if (v)                    F |= FLAG_V;
    A = (BYTE)result;
}

static inline void op_SBC(BYTE val) {
    /* SBC = ADC with inverted operand */
    WORD result = (WORD)A - (WORD)val - (WORD)(F & FLAG_C ? 0u : 1u);
    BYTE v = (BYTE)((A ^ val) & (A ^ (BYTE)result) & 0x80u);
    F &= ~(BYTE)(FLAG_N | FLAG_Z | FLAG_C | FLAG_V);
    if ((BYTE)result & 0x80u) F |= FLAG_N;
    if ((BYTE)result == 0)    F |= FLAG_Z;
    if (result < 0x100u)      F |= FLAG_C;   /* Borrow = inverted carry */
    if (v)                    F |= FLAG_V;
    A = (BYTE)result;
}

/* Compare operation: sets N, Z, C; does not change accumulator */
static inline void op_CMP(BYTE reg, BYTE val) {
    WORD diff = (WORD)reg - (WORD)val;
    F &= ~(BYTE)(FLAG_N | FLAG_Z | FLAG_C);
    if ((BYTE)diff & 0x80u) F |= FLAG_N;
    if ((BYTE)diff == 0)    F |= FLAG_Z;
    if (reg >= val)         F |= FLAG_C;
}

/* ASL on accumulator */
static inline void op_ASLA(void) {
    F = (F & ~(BYTE)(FLAG_N | FLAG_Z | FLAG_C))
        | (A & 0x80u ? FLAG_C : 0u);
    A <<= 1;
    SET_NZ(A);
}

/* ASL on memory */
static inline BYTE op_ASL(BYTE m) {
    BYTE c = (m & 0x80u) ? FLAG_C : 0u;
    m <<= 1;
    F = (F & ~(BYTE)(FLAG_N | FLAG_Z | FLAG_C)) | c;
    SET_NZ(m);
    return m;
}

/* LSR on accumulator */
static inline void op_LSRA(void) {
    F = (F & ~(BYTE)(FLAG_N | FLAG_Z | FLAG_C))
        | (A & 0x01u ? FLAG_C : 0u);
    A >>= 1;
    SET_NZ(A);
}

/* LSR on memory */
static inline BYTE op_LSR(BYTE m) {
    BYTE c = (m & 0x01u) ? FLAG_C : 0u;
    m >>= 1;
    F = (F & ~(BYTE)(FLAG_N | FLAG_Z | FLAG_C)) | c;
    SET_NZ(m);
    return m;
}

/* ROL on accumulator */
static inline void op_ROLA(void) {
    BYTE old_c = (F & FLAG_C) ? 1u : 0u;
    F = (F & ~(BYTE)(FLAG_N | FLAG_Z | FLAG_C))
        | (A & 0x80u ? FLAG_C : 0u);
    A = (A << 1) | old_c;
    SET_NZ(A);
}

/* ROL on memory */
static inline BYTE op_ROL(BYTE m) {
    BYTE old_c = (F & FLAG_C) ? 1u : 0u;
    BYTE c     = (m & 0x80u) ? FLAG_C : 0u;
    m = (m << 1) | old_c;
    F = (F & ~(BYTE)(FLAG_N | FLAG_Z | FLAG_C)) | c;
    SET_NZ(m);
    return m;
}

/* ROR on accumulator */
static inline void op_RORA(void) {
    BYTE old_c = (F & FLAG_C) ? 0x80u : 0u;
    F = (F & ~(BYTE)(FLAG_N | FLAG_Z | FLAG_C))
        | (A & 0x01u ? FLAG_C : 0u);
    A = (A >> 1) | old_c;
    SET_NZ(A);
}

/* ROR on memory */
static inline BYTE op_ROR(BYTE m) {
    BYTE old_c = (F & FLAG_C) ? 0x80u : 0u;
    BYTE c     = (m & 0x01u) ? FLAG_C : 0u;
    m = (m >> 1) | old_c;
    F = (F & ~(BYTE)(FLAG_N | FLAG_Z | FLAG_C)) | c;
    SET_NZ(m);
    return m;
}

/* BIT test */
static inline void op_BIT(BYTE m) {
    F &= ~(BYTE)(FLAG_N | FLAG_Z | FLAG_V);
    if (!(A & m)) F |= FLAG_Z;
    if (m & 0x80u) F |= FLAG_N;
    if (m & 0x40u) F |= FLAG_V;
}

/* Branch helper: take branch to PC + signed offset */
static inline void do_branch(BYTE cond) {
    SBYTE offset = (SBYTE)fetch_pc_byte();
    if (cond) {
        CLOCKS(1);
        WORD new_pc = (WORD)(PC + offset);
        if ((new_pc ^ PC) & 0xFF00u) CLOCKS(1);  /* +1 if page crosses */
        PC = new_pc;
    }
}

/* Interrupt sequence: push PC+F, load vector */
static inline void do_interrupt(WORD vector, BYTE push_flags) {
    CLOCKS(7);
    STACK_PUSH(PC >> 8);
    STACK_PUSH(PC & 0xFFu);
    STACK_PUSH(push_flags);
    F &= ~(BYTE)FLAG_D;
    F |= FLAG_I;
    PC = read16(vector);
}

/* =====================================================================
 *  K6502_Step — Main execution loop
 *  RAM-resident on RP2040.
 * ===================================================================== */
void RAMFUNC(K6502_Step)(WORD wClocks) {
    /* NMI pre-step: if NMI is pending, consume 7 clocks before dispatch */
    if (NMI_State != NMI_Wiring) {
        CLOCKS(7);
    }

    while (g_wPassedClocks < wClocks) {
        PPU_SyncVBlankEdge();

        /* ---- NMI check ---- */
        if (NMI_State != NMI_Wiring) {
            NMI_State = NMI_Wiring;
            do_interrupt(0xFFFAu, F & ~(BYTE)FLAG_B);
            continue;
        }

        /* ---- IRQ check ---- */
        if ((IRQ_State != IRQ_Wiring) && !(F & FLAG_I)) {
            do_interrupt(0xFFFEu, F & ~(BYTE)FLAG_B);
            continue;
        }

        /* ---- Fetch opcode ---- */
        WORD pc_before = PC;
        BYTE opcode = fetch_pc_byte();

#if defined(PICO_BUILD)
        diag_log_loderunner_value_source(pc_before, opcode);
#endif

        /* ---- Unofficial opcode gate ---- */
        if (g_unofficialOpcodeTable[opcode]) {
            K6502_RunUnofficial(opcode);
            continue;
        }

        /* ---- Official opcode dispatch ---- */
        int cross = 0;
        WORD ea;
        BYTE m;

        switch (opcode) {

        /* ----------------------------------------------------------------
         *  Load / Store
         * -------------------------------------------------------------- */
        /* LDA */
        case 0xA9: A = RD_IMM();       CLOCKS(2); SET_NZ(A); break;
        case 0xA5: A = RD_ZP();        CLOCKS(3); SET_NZ(A); break;
        case 0xB5: A = RD_ZPX();       CLOCKS(4); SET_NZ(A); break;
        case 0xAD: A = RD_ABS();       CLOCKS(4); SET_NZ(A); break;
        case 0xBD: A = RD_ABX(&cross); CLOCKS(4 + cross); SET_NZ(A); break;
        case 0xB9: A = RD_ABY(&cross); CLOCKS(4 + cross); SET_NZ(A); break;
        case 0xA1: A = RD_IZX();       CLOCKS(6); SET_NZ(A); break;
        case 0xB1: A = RD_IZY(&cross); CLOCKS(5 + cross); SET_NZ(A); break;

        /* LDX */
        case 0xA2: X = RD_IMM();       CLOCKS(2); SET_NZ(X); break;
        case 0xA6: X = RD_ZP();        CLOCKS(3); SET_NZ(X); break;
        case 0xB6: X = RD_ZPY();       CLOCKS(4); SET_NZ(X); break;
        case 0xAE: X = RD_ABS();       CLOCKS(4); SET_NZ(X); break;
        case 0xBE: X = RD_ABY(&cross); CLOCKS(4 + cross); SET_NZ(X); break;

        /* LDY */
        case 0xA0: Y = RD_IMM();       CLOCKS(2); SET_NZ(Y); break;
        case 0xA4: Y = RD_ZP();        CLOCKS(3); SET_NZ(Y); break;
        case 0xB4: Y = RD_ZPX();       CLOCKS(4); SET_NZ(Y); break;
        case 0xAC: Y = RD_ABS();       CLOCKS(4); SET_NZ(Y); break;
        case 0xBC: Y = RD_ABX(&cross); CLOCKS(4 + cross); SET_NZ(Y); break;

        /* STA */
        case 0x85: write_zp_ram((BYTE)EA_ZP(),  A); CLOCKS(3); break;
        case 0x95: write_zp_ram((BYTE)EA_ZPX(), A); CLOCKS(4); break;
        case 0x8D: K6502_Write(EA_ABS(), A); CLOCKS(4); break;
        case 0x9D: K6502_Write(EA_ABX(NULL), A); CLOCKS(5); break;
        case 0x99: K6502_Write(EA_ABY(NULL), A); CLOCKS(5); break;
        case 0x81: K6502_Write(EA_IZX(),      A); CLOCKS(6); break;
        case 0x91: K6502_Write(EA_IZY(NULL),  A); CLOCKS(6); break;

        /* STX */
        case 0x86: write_zp_ram((BYTE)EA_ZP(),  X); CLOCKS(3); break;
        case 0x96: write_zp_ram((BYTE)EA_ZPY(), X); CLOCKS(4); break;
        case 0x8E: K6502_Write(EA_ABS(), X); CLOCKS(4); break;

        /* STY */
        case 0x84: write_zp_ram((BYTE)EA_ZP(),  Y); CLOCKS(3); break;
        case 0x94: write_zp_ram((BYTE)EA_ZPX(), Y); CLOCKS(4); break;
        case 0x8C: K6502_Write(EA_ABS(), Y); CLOCKS(4); break;

        /* ----------------------------------------------------------------
         *  Register transfers
         * -------------------------------------------------------------- */
        case 0xAA: X = A; CLOCKS(2); SET_NZ(X); break;  /* TAX */
        case 0xA8: Y = A; CLOCKS(2); SET_NZ(Y); break;  /* TAY */
        case 0xBA: X = SP; CLOCKS(2); SET_NZ(X); break; /* TSX */
        case 0x8A: A = X; CLOCKS(2); SET_NZ(A); break;  /* TXA */
        case 0x9A: SP = X; CLOCKS(2); break;             /* TXS (no flags) */
        case 0x98: A = Y; CLOCKS(2); SET_NZ(A); break;  /* TYA */

        /* ----------------------------------------------------------------
         *  Stack
         * -------------------------------------------------------------- */
        case 0x48: STACK_PUSH(A); CLOCKS(3); break;              /* PHA */
        case 0x08: STACK_PUSH(F | FLAG_B | FLAG_R); CLOCKS(3); break; /* PHP */
        case 0x68: A = STACK_POP(); CLOCKS(4); SET_NZ(A); break; /* PLA */
        case 0x28: F = STACK_POP() | FLAG_R; CLOCKS(4); break;   /* PLP */

        /* ----------------------------------------------------------------
         *  Arithmetic
         * -------------------------------------------------------------- */
        /* ADC */
        case 0x69: op_ADC(RD_IMM());       CLOCKS(2); break;
        case 0x65: op_ADC(RD_ZP());        CLOCKS(3); break;
        case 0x75: op_ADC(RD_ZPX());       CLOCKS(4); break;
        case 0x6D: op_ADC(RD_ABS());       CLOCKS(4); break;
        case 0x7D: op_ADC(RD_ABX(&cross)); CLOCKS(4 + cross); break;
        case 0x79: op_ADC(RD_ABY(&cross)); CLOCKS(4 + cross); break;
        case 0x61: op_ADC(RD_IZX());       CLOCKS(6); break;
        case 0x71: op_ADC(RD_IZY(&cross)); CLOCKS(5 + cross); break;

        /* SBC */
        case 0xE9: op_SBC(RD_IMM());       CLOCKS(2); break;
        case 0xE5: op_SBC(RD_ZP());        CLOCKS(3); break;
        case 0xF5: op_SBC(RD_ZPX());       CLOCKS(4); break;
        case 0xED: op_SBC(RD_ABS());       CLOCKS(4); break;
        case 0xFD: op_SBC(RD_ABX(&cross)); CLOCKS(4 + cross); break;
        case 0xF9: op_SBC(RD_ABY(&cross)); CLOCKS(4 + cross); break;
        case 0xE1: op_SBC(RD_IZX());       CLOCKS(6); break;
        case 0xF1: op_SBC(RD_IZY(&cross)); CLOCKS(5 + cross); break;

        /* ----------------------------------------------------------------
         *  Compare
         * -------------------------------------------------------------- */
        /* CMP */
        case 0xC9: op_CMP(A, RD_IMM());       CLOCKS(2); break;
        case 0xC5: op_CMP(A, RD_ZP());        CLOCKS(3); break;
        case 0xD5: op_CMP(A, RD_ZPX());       CLOCKS(4); break;
        case 0xCD: op_CMP(A, RD_ABS());       CLOCKS(4); break;
        case 0xDD: op_CMP(A, RD_ABX(&cross)); CLOCKS(4 + cross); break;
        case 0xD9: op_CMP(A, RD_ABY(&cross)); CLOCKS(4 + cross); break;
        case 0xC1: op_CMP(A, RD_IZX());       CLOCKS(6); break;
        case 0xD1: op_CMP(A, RD_IZY(&cross)); CLOCKS(5 + cross); break;

        /* CPX */
        case 0xE0: op_CMP(X, RD_IMM()); CLOCKS(2); break;
        case 0xE4: op_CMP(X, RD_ZP());  CLOCKS(3); break;
        case 0xEC: op_CMP(X, RD_ABS()); CLOCKS(4); break;

        /* CPY */
        case 0xC0: op_CMP(Y, RD_IMM()); CLOCKS(2); break;
        case 0xC4: op_CMP(Y, RD_ZP());  CLOCKS(3); break;
        case 0xCC: op_CMP(Y, RD_ABS()); CLOCKS(4); break;

        /* ----------------------------------------------------------------
         *  Logical
         * -------------------------------------------------------------- */
        /* AND */
        case 0x29: A &= RD_IMM();       CLOCKS(2); SET_NZ(A); break;
        case 0x25: A &= RD_ZP();        CLOCKS(3); SET_NZ(A); break;
        case 0x35: A &= RD_ZPX();       CLOCKS(4); SET_NZ(A); break;
        case 0x2D: A &= RD_ABS();       CLOCKS(4); SET_NZ(A); break;
        case 0x3D: A &= RD_ABX(&cross); CLOCKS(4 + cross); SET_NZ(A); break;
        case 0x39: A &= RD_ABY(&cross); CLOCKS(4 + cross); SET_NZ(A); break;
        case 0x21: A &= RD_IZX();       CLOCKS(6); SET_NZ(A); break;
        case 0x31: A &= RD_IZY(&cross); CLOCKS(5 + cross); SET_NZ(A); break;

        /* ORA */
        case 0x09: A |= RD_IMM();       CLOCKS(2); SET_NZ(A); break;
        case 0x05: A |= RD_ZP();        CLOCKS(3); SET_NZ(A); break;
        case 0x15: A |= RD_ZPX();       CLOCKS(4); SET_NZ(A); break;
        case 0x0D: A |= RD_ABS();       CLOCKS(4); SET_NZ(A); break;
        case 0x1D: A |= RD_ABX(&cross); CLOCKS(4 + cross); SET_NZ(A); break;
        case 0x19: A |= RD_ABY(&cross); CLOCKS(4 + cross); SET_NZ(A); break;
        case 0x01: A |= RD_IZX();       CLOCKS(6); SET_NZ(A); break;
        case 0x11: A |= RD_IZY(&cross); CLOCKS(5 + cross); SET_NZ(A); break;

        /* EOR */
        case 0x49: A ^= RD_IMM();       CLOCKS(2); SET_NZ(A); break;
        case 0x45: A ^= RD_ZP();        CLOCKS(3); SET_NZ(A); break;
        case 0x55: A ^= RD_ZPX();       CLOCKS(4); SET_NZ(A); break;
        case 0x4D: A ^= RD_ABS();       CLOCKS(4); SET_NZ(A); break;
        case 0x5D: A ^= RD_ABX(&cross); CLOCKS(4 + cross); SET_NZ(A); break;
        case 0x59: A ^= RD_ABY(&cross); CLOCKS(4 + cross); SET_NZ(A); break;
        case 0x41: A ^= RD_IZX();       CLOCKS(6); SET_NZ(A); break;
        case 0x51: A ^= RD_IZY(&cross); CLOCKS(5 + cross); SET_NZ(A); break;

        /* BIT */
        case 0x24: op_BIT(RD_ZP());  CLOCKS(3); break;
        case 0x2C: op_BIT(RD_ABS()); CLOCKS(4); break;

        /* ----------------------------------------------------------------
         *  Shift / Rotate
         * -------------------------------------------------------------- */
        /* ASL */
        case 0x0A: op_ASLA(); CLOCKS(2); break;
        case 0x06: ea = EA_ZP();  m = op_ASL(read_zp_rmw_data((BYTE)ea)); write_zp_ram((BYTE)ea, m); CLOCKS(5); break;
        case 0x16: ea = EA_ZPX(); m = op_ASL(read_zp_rmw_data((BYTE)ea)); write_zp_ram((BYTE)ea, m); CLOCKS(6); break;
        case 0x0E: ea = EA_ABS(); m = op_ASL(K6502_Read(ea)); K6502_Write(ea, m); CLOCKS(6); break;
        case 0x1E: ea = EA_ABX(NULL); m = op_ASL(K6502_Read(ea)); K6502_Write(ea, m); CLOCKS(7); break;

        /* LSR */
        case 0x4A: op_LSRA(); CLOCKS(2); break;
        case 0x46: ea = EA_ZP();  m = op_LSR(read_zp_rmw_data((BYTE)ea)); write_zp_ram((BYTE)ea, m); CLOCKS(5); break;
        case 0x56: ea = EA_ZPX(); m = op_LSR(read_zp_rmw_data((BYTE)ea)); write_zp_ram((BYTE)ea, m); CLOCKS(6); break;
        case 0x4E: ea = EA_ABS(); m = op_LSR(K6502_Read(ea)); K6502_Write(ea, m); CLOCKS(6); break;
        case 0x5E: ea = EA_ABX(NULL); m = op_LSR(K6502_Read(ea)); K6502_Write(ea, m); CLOCKS(7); break;

        /* ROL */
        case 0x2A: op_ROLA(); CLOCKS(2); break;
        case 0x26: ea = EA_ZP();  m = op_ROL(read_zp_rmw_data((BYTE)ea)); write_zp_ram((BYTE)ea, m); CLOCKS(5); break;
        case 0x36: ea = EA_ZPX(); m = op_ROL(read_zp_rmw_data((BYTE)ea)); write_zp_ram((BYTE)ea, m); CLOCKS(6); break;
        case 0x2E: ea = EA_ABS(); m = op_ROL(K6502_Read(ea)); K6502_Write(ea, m); CLOCKS(6); break;
        case 0x3E: ea = EA_ABX(NULL); m = op_ROL(K6502_Read(ea)); K6502_Write(ea, m); CLOCKS(7); break;

        /* ROR */
        case 0x6A: op_RORA(); CLOCKS(2); break;
        case 0x66: ea = EA_ZP();  m = op_ROR(read_zp_rmw_data((BYTE)ea)); write_zp_ram((BYTE)ea, m); CLOCKS(5); break;
        case 0x76: ea = EA_ZPX(); m = op_ROR(read_zp_rmw_data((BYTE)ea)); write_zp_ram((BYTE)ea, m); CLOCKS(6); break;
        case 0x6E: ea = EA_ABS(); m = op_ROR(K6502_Read(ea)); K6502_Write(ea, m); CLOCKS(6); break;
        case 0x7E: ea = EA_ABX(NULL); m = op_ROR(K6502_Read(ea)); K6502_Write(ea, m); CLOCKS(7); break;

        /* ----------------------------------------------------------------
         *  Increment / Decrement
         * -------------------------------------------------------------- */
        /* INC */
        case 0xE6: ea = EA_ZP();  m = read_zp_rmw_data((BYTE)ea)+1; write_zp_ram((BYTE)ea, m); CLOCKS(5); SET_NZ(m); break;
        case 0xF6: ea = EA_ZPX(); m = read_zp_rmw_data((BYTE)ea)+1; write_zp_ram((BYTE)ea, m); CLOCKS(6); SET_NZ(m); break;
        case 0xEE: ea = EA_ABS(); m = K6502_Read(ea)+1; K6502_Write(ea, m); CLOCKS(6); SET_NZ(m); break;
        case 0xFE: ea = EA_ABX(NULL); m = K6502_Read(ea)+1; K6502_Write(ea, m); CLOCKS(7); SET_NZ(m); break;

        /* DEC */
        case 0xC6: ea = EA_ZP();  m = read_zp_rmw_data((BYTE)ea)-1; write_zp_ram((BYTE)ea, m); CLOCKS(5); SET_NZ(m); break;
        case 0xD6: ea = EA_ZPX(); m = read_zp_rmw_data((BYTE)ea)-1; write_zp_ram((BYTE)ea, m); CLOCKS(6); SET_NZ(m); break;
        case 0xCE: ea = EA_ABS(); m = K6502_Read(ea)-1; K6502_Write(ea, m); CLOCKS(6); SET_NZ(m); break;
        case 0xDE: ea = EA_ABX(NULL); m = K6502_Read(ea)-1; K6502_Write(ea, m); CLOCKS(7); SET_NZ(m); break;

        case 0xE8: X++; CLOCKS(2); SET_NZ(X); break;  /* INX */
        case 0xC8: Y++; CLOCKS(2); SET_NZ(Y); break;  /* INY */
        case 0xCA: X--; CLOCKS(2); SET_NZ(X); break;  /* DEX */
        case 0x88: Y--; CLOCKS(2); SET_NZ(Y); break;  /* DEY */

        /* ----------------------------------------------------------------
         *  Control flow
         * -------------------------------------------------------------- */
        /* JMP */
        case 0x4C: PC = EA_ABS();           CLOCKS(3); break;
        case 0x6C: PC = read16_wrap(EA_ABS()); CLOCKS(5); break;

        /* JSR */
        case 0x20:
            ea = EA_ABS();
            STACK_PUSH((PC - 1u) >> 8);
            STACK_PUSH((PC - 1u) & 0xFFu);
            PC = ea;
            CLOCKS(6);
            break;

        /* RTS */
        case 0x60: {
            WORD lo = STACK_POP();
            WORD hi = STACK_POP();
            PC = (lo | (hi << 8)) + 1u;
            CLOCKS(6);
            break;
        }

        /* RTI */
        case 0x40: {
            F  = STACK_POP() | FLAG_R;
            WORD lo = STACK_POP();
            WORD hi = STACK_POP();
            PC = lo | (hi << 8);
            CLOCKS(6);
            break;
        }

        /* BRK */
        case 0x00:
            PC++;  /* Skip padding byte */
            STACK_PUSH(PC >> 8);
            STACK_PUSH(PC & 0xFFu);
            STACK_PUSH(F | FLAG_B | FLAG_R);
            F &= ~(BYTE)FLAG_D;
            F |= FLAG_I;
            PC = read16(0xFFFEu);
            CLOCKS(7);
            break;

        /* ----------------------------------------------------------------
         *  Branches
         * -------------------------------------------------------------- */
        case 0x10: do_branch(!(F & FLAG_N)); CLOCKS(2); break;  /* BPL */
        case 0x30: do_branch( (F & FLAG_N)); CLOCKS(2); break;  /* BMI */
        case 0x50: do_branch(!(F & FLAG_V)); CLOCKS(2); break;  /* BVC */
        case 0x70: do_branch( (F & FLAG_V)); CLOCKS(2); break;  /* BVS */
        case 0x90: do_branch(!(F & FLAG_C)); CLOCKS(2); break;  /* BCC */
        case 0xB0: do_branch( (F & FLAG_C)); CLOCKS(2); break;  /* BCS */
        case 0xD0: do_branch(!(F & FLAG_Z)); CLOCKS(2); break;  /* BNE */
        case 0xF0: do_branch( (F & FLAG_Z)); CLOCKS(2); break;  /* BEQ */

        /* ----------------------------------------------------------------
         *  Flag operations
         * -------------------------------------------------------------- */
        case 0x18: F &= ~(BYTE)FLAG_C; CLOCKS(2); break;  /* CLC */
        case 0x38: F |= FLAG_C;        CLOCKS(2); break;  /* SEC */
        case 0x58: F &= ~(BYTE)FLAG_I; CLOCKS(2); break;  /* CLI */
        case 0x78: F |= FLAG_I;        CLOCKS(2); break;  /* SEI */
        case 0xB8: F &= ~(BYTE)FLAG_V; CLOCKS(2); break;  /* CLV */
        case 0xD8: F &= ~(BYTE)FLAG_D; CLOCKS(2); break;  /* CLD */
        case 0xF8: F |= FLAG_D;        CLOCKS(2); break;  /* SED */

        /* ----------------------------------------------------------------
         *  NOP (official)
         * -------------------------------------------------------------- */
        case 0xEA: CLOCKS(2); break;

        /* ----------------------------------------------------------------
         *  Unofficial NOPs (absorbed — 1/2/3 byte variants)
         *  Handled here rather than K6502_RunUnofficial for speed.
         * -------------------------------------------------------------- */
        /* 1-byte NOPs */
        case 0x1A: case 0x3A: case 0x5A:
        case 0x7A: case 0xDA: case 0xFA:
            CLOCKS(2); break;
        /* 2-byte NOPs (immediate) */
        case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2:
            PC++; CLOCKS(2); break;
        /* 3-byte NOPs (absolute / absolute+X) */
        case 0x0C:
            EA_ABS(); CLOCKS(4); break;
        case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC:
            EA_ABX(&cross); CLOCKS(4 + cross); break;

        /* ----------------------------------------------------------------
         *  Default: treat unknown opcodes as 1-cycle NOP
         * -------------------------------------------------------------- */
        default:
#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
            if (!s_logged_unknown_official[opcode]) {
                s_logged_unknown_official[opcode] = 1;
                diag_log_unknown_opcode("official", opcode, (WORD)(PC - 1u));
            }
#endif
            CLOCKS(2);
            break;

        } /* switch(opcode) */

        /* Maintain always-1 bit */
        F |= FLAG_R;

    } /* while loop */

    /* Carry excess clocks into next call */
    g_wPassedClocks -= wClocks;
}

/* =====================================================================
 *  K6502_RunUnofficial — Stable unofficial opcodes
 *  Flash-resident (not RAMFUNC).
 * ===================================================================== */
void K6502_RunUnofficial(BYTE opcode) {
    int cross = 0;
    WORD ea;
    BYTE m, val;

    /* Helper: SLO = ASL mem, then ORA A */
#define DO_SLO(get_ea, clocks) do { \
    ea = (get_ea); \
    m = op_ASL(K6502_Read(ea)); K6502_Write(ea, m); \
    A |= m; SET_NZ(A); CLOCKS(clocks); \
} while(0)

#define DO_SLO_ZP(get_ea, clocks) do { \
    ea = (get_ea); \
    m = op_ASL(read_zp_rmw_data((BYTE)ea)); write_zp_ram((BYTE)ea, m); \
    A |= m; SET_NZ(A); CLOCKS(clocks); \
} while(0)

    /* Helper: RLA = ROL mem, then AND A */
#define DO_RLA(get_ea, clocks) do { \
    ea = (get_ea); \
    m = op_ROL(K6502_Read(ea)); K6502_Write(ea, m); \
    A &= m; SET_NZ(A); CLOCKS(clocks); \
} while(0)

#define DO_RLA_ZP(get_ea, clocks) do { \
    ea = (get_ea); \
    m = op_ROL(read_zp_rmw_data((BYTE)ea)); write_zp_ram((BYTE)ea, m); \
    A &= m; SET_NZ(A); CLOCKS(clocks); \
} while(0)

    /* Helper: SRE = LSR mem, then EOR A */
#define DO_SRE(get_ea, clocks) do { \
    ea = (get_ea); \
    m = op_LSR(K6502_Read(ea)); K6502_Write(ea, m); \
    A ^= m; SET_NZ(A); CLOCKS(clocks); \
} while(0)

#define DO_SRE_ZP(get_ea, clocks) do { \
    ea = (get_ea); \
    m = op_LSR(read_zp_rmw_data((BYTE)ea)); write_zp_ram((BYTE)ea, m); \
    A ^= m; SET_NZ(A); CLOCKS(clocks); \
} while(0)

    /* Helper: RRA = ROR mem, then ADC A */
#define DO_RRA(get_ea, clocks) do { \
    ea = (get_ea); \
    m = op_ROR(K6502_Read(ea)); K6502_Write(ea, m); \
    op_ADC(m); CLOCKS(clocks); \
} while(0)

#define DO_RRA_ZP(get_ea, clocks) do { \
    ea = (get_ea); \
    m = op_ROR(read_zp_rmw_data((BYTE)ea)); write_zp_ram((BYTE)ea, m); \
    op_ADC(m); CLOCKS(clocks); \
} while(0)

    /* Helper: DCP = DEC mem, then CMP A */
#define DO_DCP(get_ea, clocks) do { \
    ea = (get_ea); \
    m = K6502_Read(ea) - 1u; K6502_Write(ea, m); \
    op_CMP(A, m); CLOCKS(clocks); \
} while(0)

#define DO_DCP_ZP(get_ea, clocks) do { \
    ea = (get_ea); \
    m = read_zp_rmw_data((BYTE)ea) - 1u; write_zp_ram((BYTE)ea, m); \
    op_CMP(A, m); CLOCKS(clocks); \
} while(0)

    /* Helper: ISB = INC mem, then SBC A */
#define DO_ISB(get_ea, clocks) do { \
    ea = (get_ea); \
    m = K6502_Read(ea) + 1u; K6502_Write(ea, m); \
    op_SBC(m); CLOCKS(clocks); \
} while(0)

#define DO_ISB_ZP(get_ea, clocks) do { \
    ea = (get_ea); \
    m = read_zp_rmw_data((BYTE)ea) + 1u; write_zp_ram((BYTE)ea, m); \
    op_SBC(m); CLOCKS(clocks); \
} while(0)

    switch (opcode) {
    /* SLO */
    case 0x07: DO_SLO_ZP(EA_ZP(),     5); break;
    case 0x17: DO_SLO_ZP(EA_ZPX(),    6); break;
    case 0x0F: DO_SLO(EA_ABS(),       6); break;
    case 0x1F: DO_SLO(EA_ABX(NULL),   7); break;
    case 0x1B: DO_SLO(EA_ABY(NULL),   7); break;
    case 0x03: DO_SLO(EA_IZX(),       8); break;
    case 0x13: DO_SLO(EA_IZY(NULL),   8); break;

    /* RLA */
    case 0x27: DO_RLA_ZP(EA_ZP(),     5); break;
    case 0x37: DO_RLA_ZP(EA_ZPX(),    6); break;
    case 0x2F: DO_RLA(EA_ABS(),       6); break;
    case 0x3F: DO_RLA(EA_ABX(NULL),   7); break;
    case 0x3B: DO_RLA(EA_ABY(NULL),   7); break;
    case 0x23: DO_RLA(EA_IZX(),       8); break;
    case 0x33: DO_RLA(EA_IZY(NULL),   8); break;

    /* SRE */
    case 0x47: DO_SRE_ZP(EA_ZP(),     5); break;
    case 0x57: DO_SRE_ZP(EA_ZPX(),    6); break;
    case 0x4F: DO_SRE(EA_ABS(),       6); break;
    case 0x5F: DO_SRE(EA_ABX(NULL),   7); break;
    case 0x5B: DO_SRE(EA_ABY(NULL),   7); break;
    case 0x43: DO_SRE(EA_IZX(),       8); break;
    case 0x53: DO_SRE(EA_IZY(NULL),   8); break;

    /* RRA */
    case 0x67: DO_RRA_ZP(EA_ZP(),     5); break;
    case 0x77: DO_RRA_ZP(EA_ZPX(),    6); break;
    case 0x6F: DO_RRA(EA_ABS(),       6); break;
    case 0x7F: DO_RRA(EA_ABX(NULL),   7); break;
    case 0x7B: DO_RRA(EA_ABY(NULL),   7); break;
    case 0x63: DO_RRA(EA_IZX(),       8); break;
    case 0x73: DO_RRA(EA_IZY(NULL),   8); break;

    /* SAX: store A AND X */
    case 0x87: write_zp_ram((BYTE)EA_ZP(),  A & X); CLOCKS(3); break;
    case 0x97: write_zp_ram((BYTE)EA_ZPY(), A & X); CLOCKS(4); break;
    case 0x8F: K6502_Write(EA_ABS(), A & X); CLOCKS(4); break;
    case 0x83: K6502_Write(EA_IZX(), A & X); CLOCKS(6); break;

    /* LAX: load A and X from memory */
    case 0xA7: val = read_zp_data((BYTE)EA_ZP());  A = X = val; SET_NZ(A); CLOCKS(3); break;
    case 0xB7: val = read_zp_data((BYTE)EA_ZPY()); A = X = val; SET_NZ(A); CLOCKS(4); break;
    case 0xAF: val = K6502_Read(EA_ABS()); A = X = val; SET_NZ(A); CLOCKS(4); break;
    case 0xBF: val = K6502_Read(EA_ABY(&cross)); A = X = val; SET_NZ(A); CLOCKS(4 + cross); break;
    case 0xA3: val = K6502_Read(EA_IZX()); A = X = val; SET_NZ(A); CLOCKS(6); break;
    case 0xB3: val = K6502_Read(EA_IZY(&cross)); A = X = val; SET_NZ(A); CLOCKS(5 + cross); break;

    /* DCP */
    case 0xC7: DO_DCP_ZP(EA_ZP(),     5); break;
    case 0xD7: DO_DCP_ZP(EA_ZPX(),    6); break;
    case 0xCF: DO_DCP(EA_ABS(),       6); break;
    case 0xDF: DO_DCP(EA_ABX(NULL),   7); break;
    case 0xDB: DO_DCP(EA_ABY(NULL),   7); break;
    case 0xC3: DO_DCP(EA_IZX(),       8); break;
    case 0xD3: DO_DCP(EA_IZY(NULL),   8); break;

    /* ISB (ISC) */
    case 0xE7: DO_ISB_ZP(EA_ZP(),     5); break;
    case 0xF7: DO_ISB_ZP(EA_ZPX(),    6); break;
    case 0xEF: DO_ISB(EA_ABS(),       6); break;
    case 0xFF: DO_ISB(EA_ABX(NULL),   7); break;
    case 0xFB: DO_ISB(EA_ABY(NULL),   7); break;
    case 0xE3: DO_ISB(EA_IZX(),       8); break;
    case 0xF3: DO_ISB(EA_IZY(NULL),   8); break;

    /* AXS ($CB): A AND X - immediate → X; sets N, Z, C */
    case 0xCB: {
        BYTE imm = fetch_pc_byte();
        BYTE ax  = A & X;
        WORD diff = (WORD)ax - (WORD)imm;
        X = (BYTE)diff;
        F &= ~(BYTE)(FLAG_N | FLAG_Z | FLAG_C);
        if (X & 0x80u) F |= FLAG_N;
        if (X == 0)    F |= FLAG_Z;
        if (ax >= imm) F |= FLAG_C;
        CLOCKS(2);
        break;
    }

    /* EB: unofficial SBC immediate (same as $E9) */
    case 0xEB:
        op_SBC(fetch_pc_byte());
        CLOCKS(2);
        break;

    default:
#if defined(PICO_BUILD) && defined(NESCO_DIAGNOSTICS)
        if (!s_logged_unknown_unofficial[opcode]) {
            s_logged_unknown_unofficial[opcode] = 1;
            diag_log_unknown_opcode("unofficial", opcode, (WORD)(PC - 1u));
        }
#endif
        /* Unrecognised: treat as 2-cycle NOP */
        CLOCKS(2);
        break;
    }

    F |= FLAG_R;  /* Maintain reserved bit */

#undef DO_SLO
#undef DO_SLO_ZP
#undef DO_RLA
#undef DO_RLA_ZP
#undef DO_SRE
#undef DO_SRE_ZP
#undef DO_RRA
#undef DO_RRA_ZP
#undef DO_DCP
#undef DO_DCP_ZP
#undef DO_ISB
#undef DO_ISB_ZP
}
