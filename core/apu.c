/*
 * apu.c — NES APU (2A03) synthesizer
 *
 * Implements 5-channel NES audio synthesis:
 *   0: Pulse 1   — variable-duty square wave, envelope, sweep
 *   1: Pulse 2   — variable-duty square wave, envelope, sweep
 *   2: Triangle  — linear-counter gated triangle wave
 *   3: Noise     — LFSR-based noise, envelope
 *   4: DPCM      — delta PCM, CPU memory reads
 *
 * Synthesis model:
 *   - Fixed-point phase accumulators (32-bit) for all channels
 *   - Event queue (APU_QUEUE_SIZE entries per frame) for CPU-write replay
 *   - H-Sync synthesis: generate N samples per scanline; call SoundOutput
 *   - V-Sync: envelope decay, sweep, length counter, linear counter
 *
 * Spec reference: specs/22_apu.md
 * Open Questions: OQ-APU-1 (queue size), OQ-APU-3 (noise envelope),
 *                 OQ-APU-4 (FrameIRQ absent)
 *
 * Part of Picocalc_NESco
 * MIT License
 */

#include "apu.h"
#include "nes_globals.h"
#include "nes_system.h"
#include "cpu.h"
#include "audio.h"

#define APU_CH_DIAG_ENABLED 0

#include <string.h>
#include <stdint.h>
#include <stdio.h>

#ifdef PICO_BUILD
#include "pico/time.h"
#endif

/* =====================================================================
 *  Constants
 * ===================================================================== */
#define APU_QUEUE_SIZE   200        /* Event queue entries (per spec D3 headroom) */
#define APU_SAMPLE_RATE  AUDIO_SAMPLE_RATE
#define APU_CPU_FREQ     1789773    /* NES CPU clock (NTSC, Hz) */

/* Samples per H-Sync (scanline): 44100 / (1789773 / 114) ≈ 2.81 */
/* Accumulated via fixed-point counter to avoid drift */
#define APU_FP_SHIFT     16         /* Fixed-point fraction bits */
#define APU_FP_ONE       (1 << APU_FP_SHIFT)
#define APU_QUARTER_HZ   240
#define APU_HALF_HZ      120

/* Length counter table (64 entries, indexed by bits [7:3] of length load) */
static const BYTE s_LenTable[32] = {
    10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

/* Pulse duty cycle waveforms (8 steps each) */
static const BYTE s_PulseDuty[4][8] = {
    { 0, 1, 0, 0, 0, 0, 0, 0 },   /* 12.5% */
    { 0, 1, 1, 0, 0, 0, 0, 0 },   /* 25%   */
    { 0, 1, 1, 1, 1, 0, 0, 0 },   /* 50%   */
    { 1, 0, 0, 1, 1, 1, 1, 1 },   /* 75%   */
};

/* Noise period table (NTSC) */
static const WORD s_NoisePeriod[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

/* DPCM rate table (NTSC): CPU cycles per sample */
static const WORD s_DpcmRate[16] = {
    428, 380, 340, 320, 286, 254, 226, 214,
    190, 160, 142, 128, 106,  84,  72,  54
};

/* =====================================================================
 *  APU event queue
 *  Stores (timestamp, addr, data) tuples for CPU register writes.
 *  Replayed during H-Sync synthesis at the correct sample offset.
 * ===================================================================== */
typedef struct {
    WORD  clocks;   /* CPU clock at write time */
    WORD  addr;     /* $4000–$4017 */
    BYTE  data;
} ApuEvent;

static ApuEvent s_Queue[APU_QUEUE_SIZE];
static int      s_QueueHead = 0;
static int      s_QueueCount = 0;

/* =====================================================================
 *  Pulse channel state
 * ===================================================================== */
typedef struct {
    /* Registers */
    BYTE  duty;           /* Duty cycle index [0..3] */
    BYTE  env_loop;       /* Envelope loop / length counter halt */
    BYTE  env_const;      /* Constant volume flag */
    BYTE  env_vol;        /* Volume / envelope period */
    BYTE  sweep_en;       /* Sweep enable */
    BYTE  sweep_period;   /* Sweep period */
    BYTE  sweep_neg;      /* Sweep negate */
    BYTE  sweep_shift;    /* Sweep shift amount */
    WORD  timer_reload;   /* Timer period (11-bit) */
    BYTE  len_load;       /* Length counter load index */
    /* Runtime */
    BYTE  len_cnt;        /* Length counter */
    BYTE  env_cnt;        /* Envelope counter */
    BYTE  env_decay;      /* Envelope decay level */
    BYTE  env_start;      /* Envelope restart flag */
    BYTE  sweep_cnt;      /* Sweep divider counter */
    BYTE  sweep_reload;   /* Sweep reload flag */
    WORD  timer;          /* Timer current value */
    BYTE  seq_pos;        /* Sequencer position [0..7] */
    BYTE  enabled;        /* Channel enabled flag */
    /* Fixed-point phase for sample generation */
    DWORD phase_acc;
    DWORD phase_inc;
} PulseState;

/* =====================================================================
 *  Triangle channel state
 * ===================================================================== */
typedef struct {
    BYTE  ctrl_flag;      /* Linear counter control / length halt */
    BYTE  lin_load;       /* Linear counter reload value */
    WORD  timer_reload;
    BYTE  len_load;
    /* Runtime */
    BYTE  len_cnt;
    BYTE  lin_cnt;
    BYTE  lin_reload;     /* Linear counter reload flag */
    WORD  timer;
    BYTE  seq_pos;        /* Sequencer position [0..31] */
    BYTE  enabled;
    DWORD phase_acc;
    DWORD phase_inc;
} TriState;

/* =====================================================================
 *  Noise channel state
 * ===================================================================== */
typedef struct {
    BYTE  env_loop;
    BYTE  env_const;
    BYTE  env_vol;
    BYTE  mode;           /* Short mode (bit 4 of $400E) */
    BYTE  period_idx;
    BYTE  len_load;
    /* Runtime */
    BYTE  len_cnt;
    BYTE  env_cnt;
    BYTE  env_decay;
    BYTE  env_start;
    WORD  lfsr;           /* LFSR state (15-bit, init = 1) */
    WORD  timer;
    WORD  timer_reload;
    BYTE  enabled;
} NoiseState;

/* =====================================================================
 *  DPCM channel state
 * ===================================================================== */
typedef struct {
    BYTE  irq_en;
    BYTE  loop_en;
    BYTE  rate_idx;
    BYTE  direct;         /* Direct load value */
    WORD  sample_addr;
    WORD  sample_len;
    /* Runtime */
    BYTE  output;         /* Current output level [0..127] */
    WORD  cur_addr;
    WORD  bytes_left;
    BYTE  shift_reg;
    BYTE  bits_rem;
    WORD  timer;
    WORD  timer_reload;
    BYTE  enabled;
    BYTE  silence;
} DpcmState;

/* =====================================================================
 *  Global APU state
 * ===================================================================== */
static PulseState  s_Pulse[2];
static TriState    s_Tri;
static NoiseState  s_Noise;
static DpcmState   s_Dpcm;

/* Frame sequencer */
static BYTE  s_FrameSeqMode;   /* 0 = 4-step, 1 = 5-step */
static BYTE  s_FrameSeqStep;
static BYTE  s_FrameIRQ_en;

/* Sample rate accumulator (fixed-point, per scanline) */
static DWORD s_SampleAccum;    /* Fractional samples carried between scanlines */
static DWORD s_SamplePerLine;  /* Fixed-point samples per scanline */
static DWORD s_CpuCycleAccum;      /* Fractional CPU cycles carried between samples */
static DWORD s_CpuCyclesPerSample; /* Fixed-point CPU cycles per output sample */
static DWORD s_FrameQuarterAccum;
static DWORD s_FrameHalfAccum;
static DWORD s_FrameQuarterStep;
static DWORD s_FrameHalfStep;
static uint32_t s_FrameQuarterCount;
static uint32_t s_FrameHalfCount;
static uint32_t s_SampleCount;
#if defined(PICO_BUILD) && APU_CH_DIAG_ENABLED
static uint64_t s_ApuDebugLastUs;
static uint32_t s_ApuNoiseNonzeroSamples;
static uint32_t s_ApuDpcmNonzeroSamples;
static uint32_t s_ApuNoiseEnabledLines;
static uint32_t s_ApuDpcmActiveLines;
static uint32_t s_ApuDpcmBytesLeftMax;
#endif

/* Output buffer pointers into g_ApuWave */
static BYTE *s_WaveBuf[5];

/* =====================================================================
 *  Helpers
 * ===================================================================== */

/* Compute phase increment for a given timer period (in CPU clocks) */
static inline DWORD pulse_phase_inc(WORD period) {
    /* Period in CPU cycles: (period + 1) * 2
     * Frequency: APU_CPU_FREQ / ((period+1)*2)
     * Phase inc per sample: freq / APU_SAMPLE_RATE * (1<<32) / 8
     * But we advance by sequencer step directly, so keep it simple. */
    /* We don't use fixed-point pitch for pulse; use timer countdown instead. */
    return 0; /* placeholder */
}

static inline WORD apu_next_sample_cycles(void) {
    s_CpuCycleAccum += s_CpuCyclesPerSample;
    WORD cycles = (WORD)(s_CpuCycleAccum >> APU_FP_SHIFT);
    s_CpuCycleAccum &= (APU_FP_ONE - 1);
    return cycles;
}

static inline void apu_advance_timer(WORD *timer, WORD period, WORD cycles, BYTE *seq_pos, BYTE seq_mask) {
    if (period == 0) {
        *timer = 0;
        return;
    }

    if (*timer == 0 || *timer > period) {
        *timer = period;
    }

    while (cycles >= *timer) {
        cycles -= *timer;
        *timer = period;
        if (seq_pos) {
            *seq_pos = (BYTE)((*seq_pos + 1) & seq_mask);
        }
    }

    *timer = (WORD)(*timer - cycles);
}

static void apu_apply_register_write(WORD addr, BYTE data) {
    switch (addr) {
    /* ---- Pulse 1 ($4000–$4003) ---- */
    case 0x4000:
        s_Pulse[0].duty      = (data >> 6) & 3;
        s_Pulse[0].env_loop  = (data >> 5) & 1;
        s_Pulse[0].env_const = (data >> 4) & 1;
        s_Pulse[0].env_vol   = data & 0x0F;
        break;
    case 0x4001:
        s_Pulse[0].sweep_en     = (data >> 7) & 1;
        s_Pulse[0].sweep_period = (data >> 4) & 7;
        s_Pulse[0].sweep_neg    = (data >> 3) & 1;
        s_Pulse[0].sweep_shift  = data & 7;
        s_Pulse[0].sweep_reload = 1;
        break;
    case 0x4002:
        s_Pulse[0].timer_reload = (s_Pulse[0].timer_reload & 0x700) | data;
        break;
    case 0x4003:
        s_Pulse[0].timer_reload = (s_Pulse[0].timer_reload & 0x0FF) | ((WORD)(data & 7) << 8);
        s_Pulse[0].len_cnt      = s_LenTable[(data >> 3) & 0x1F];
        s_Pulse[0].env_start    = 1;
        s_Pulse[0].seq_pos      = 0;
        break;

    /* ---- Pulse 2 ($4004–$4007) ---- */
    case 0x4004:
        s_Pulse[1].duty      = (data >> 6) & 3;
        s_Pulse[1].env_loop  = (data >> 5) & 1;
        s_Pulse[1].env_const = (data >> 4) & 1;
        s_Pulse[1].env_vol   = data & 0x0F;
        break;
    case 0x4005:
        s_Pulse[1].sweep_en     = (data >> 7) & 1;
        s_Pulse[1].sweep_period = (data >> 4) & 7;
        s_Pulse[1].sweep_neg    = (data >> 3) & 1;
        s_Pulse[1].sweep_shift  = data & 7;
        s_Pulse[1].sweep_reload = 1;
        break;
    case 0x4006:
        s_Pulse[1].timer_reload = (s_Pulse[1].timer_reload & 0x700) | data;
        break;
    case 0x4007:
        s_Pulse[1].timer_reload = (s_Pulse[1].timer_reload & 0x0FF) | ((WORD)(data & 7) << 8);
        s_Pulse[1].len_cnt      = s_LenTable[(data >> 3) & 0x1F];
        s_Pulse[1].env_start    = 1;
        s_Pulse[1].seq_pos      = 0;
        break;

    /* ---- Triangle ($4008–$400B) ---- */
    case 0x4008:
        s_Tri.ctrl_flag = (data >> 7) & 1;
        s_Tri.lin_load  = data & 0x7F;
        break;
    case 0x400A:
        s_Tri.timer_reload = (s_Tri.timer_reload & 0x700) | data;
        break;
    case 0x400B:
        s_Tri.timer_reload = (s_Tri.timer_reload & 0x0FF) | ((WORD)(data & 7) << 8);
        s_Tri.len_cnt      = s_LenTable[(data >> 3) & 0x1F];
        s_Tri.lin_reload   = 1;
        break;

    /* ---- Noise ($400C–$400F) ---- */
    case 0x400C:
        s_Noise.env_loop  = (data >> 5) & 1;
        s_Noise.env_const = (data >> 4) & 1;
        s_Noise.env_vol   = data & 0x0F;
        break;
    case 0x400E:
        s_Noise.mode         = (data >> 7) & 1;
        s_Noise.period_idx   = data & 0x0F;
        s_Noise.timer_reload = s_NoisePeriod[s_Noise.period_idx];
        break;
    case 0x400F:
        s_Noise.len_cnt   = s_LenTable[(data >> 3) & 0x1F];
        s_Noise.env_start = 1;
        break;

    /* ---- DPCM ($4010–$4013) ---- */
    case 0x4010:
        s_Dpcm.irq_en       = (data >> 7) & 1;
        s_Dpcm.loop_en      = (data >> 6) & 1;
        s_Dpcm.rate_idx     = data & 0x0F;
        s_Dpcm.timer_reload = s_DpcmRate[s_Dpcm.rate_idx];
        break;
    case 0x4011:
        s_Dpcm.output = data & 0x7F;
        break;
    case 0x4012:
        s_Dpcm.sample_addr = 0xC000 + ((WORD)data << 6);
        break;
    case 0x4013:
        s_Dpcm.sample_len = ((WORD)data << 4) + 1;
        break;

    /* ---- Channel enable ($4015) ---- */
    case 0x4015:
        s_Pulse[0].enabled = (data >> 0) & 1;
        s_Pulse[1].enabled = (data >> 1) & 1;
        s_Tri.enabled      = (data >> 2) & 1;
        s_Noise.enabled    = (data >> 3) & 1;
        s_Dpcm.enabled     = (data >> 4) & 1;
        if (!s_Pulse[0].enabled) s_Pulse[0].len_cnt = 0;
        if (!s_Pulse[1].enabled) s_Pulse[1].len_cnt = 0;
        if (!s_Tri.enabled)      s_Tri.len_cnt = 0;
        if (!s_Noise.enabled)    s_Noise.len_cnt = 0;
        if (!s_Dpcm.enabled) {
            s_Dpcm.bytes_left = 0;
        } else if (s_Dpcm.bytes_left == 0) {
            s_Dpcm.cur_addr   = s_Dpcm.sample_addr;
            s_Dpcm.bytes_left = s_Dpcm.sample_len;
            s_Dpcm.silence    = 0;
        }
        break;

    /* ---- Frame counter ($4017) ---- */
    case 0x4017:
        s_FrameSeqMode = (data >> 7) & 1;
        s_FrameIRQ_en  = !((data >> 6) & 1);
        s_FrameSeqStep = 0;
        break;

    default:
        break;
    }
}

static void apu_apply_queued_events_until(WORD clock_limit) {
    while (s_QueueCount > 0) {
        ApuEvent *ev = &s_Queue[s_QueueHead];
        if (ev->clocks > clock_limit) {
            break;
        }
        apu_apply_register_write(ev->addr, ev->data);
        s_QueueHead = (s_QueueHead + 1) % APU_QUEUE_SIZE;
        s_QueueCount--;
    }
}

static void apu_rebase_queue_for_next_scanline(void) {
    for (int i = 0; i < s_QueueCount; i++) {
        const int idx = (s_QueueHead + i) % APU_QUEUE_SIZE;
        if (s_Queue[idx].clocks >= STEP_PER_SCANLINE) {
            s_Queue[idx].clocks -= STEP_PER_SCANLINE;
        } else {
            s_Queue[idx].clocks = 0;
        }
    }
}

/* =====================================================================
 *  APU_Init
 * ===================================================================== */
void APU_Init(void) {
    memset(&s_Pulse, 0, sizeof(s_Pulse));
    memset(&s_Tri,   0, sizeof(s_Tri));
    memset(&s_Noise, 0, sizeof(s_Noise));
    memset(&s_Dpcm,  0, sizeof(s_Dpcm));

    s_Noise.lfsr       = 1;
    s_Noise.timer      = 0;
    s_Noise.timer_reload = s_NoisePeriod[0];

    s_FrameSeqMode = 0;
    s_FrameSeqStep = 0;
    s_FrameIRQ_en  = 1;
    s_QueueHead    = 0;
    s_QueueCount   = 0;

    /* Compute fixed-point samples-per-scanline:
     * APU_SAMPLE_RATE * (114 cycles/line) / APU_CPU_FREQ
     * = 44100 * 114 / 1789773 ≈ 2.812 samples per scanline */
    s_SamplePerLine = (DWORD)((uint64_t)APU_SAMPLE_RATE * STEP_PER_SCANLINE
                              * APU_FP_ONE / APU_CPU_FREQ);
    s_SampleAccum   = 0;
    s_CpuCyclesPerSample = (DWORD)(((uint64_t)APU_CPU_FREQ << APU_FP_SHIFT)
                                   / APU_SAMPLE_RATE);
    s_CpuCycleAccum = 0;
    s_FrameQuarterStep = (DWORD)(((uint64_t)APU_CPU_FREQ << APU_FP_SHIFT) / APU_QUARTER_HZ);
    s_FrameHalfStep    = (DWORD)(((uint64_t)APU_CPU_FREQ << APU_FP_SHIFT) / APU_HALF_HZ);
    s_FrameQuarterAccum = 0;
    s_FrameHalfAccum    = 0;
    s_FrameQuarterCount = 0;
    s_FrameHalfCount    = 0;
    s_SampleCount       = 0;
#if defined(PICO_BUILD) && APU_CH_DIAG_ENABLED
    s_ApuDebugLastUs    = time_us_64();
    s_ApuNoiseNonzeroSamples = 0;
    s_ApuDpcmNonzeroSamples = 0;
    s_ApuNoiseEnabledLines = 0;
    s_ApuDpcmActiveLines = 0;
    s_ApuDpcmBytesLeftMax = 0;
#endif

    for (int i = 0; i < 5; i++) s_WaveBuf[i] = g_ApuWave[i];

}

/* =====================================================================
 *  APU_RegisterWrite — queue CPU register write events
 * ===================================================================== */
void APU_RegisterWrite(WORD addr, BYTE data) {
    if (s_QueueCount < APU_QUEUE_SIZE) {
        s_Queue[(s_QueueHead + s_QueueCount) % APU_QUEUE_SIZE] =
            (ApuEvent){ .clocks = (WORD)g_wPassedClocks, .addr = addr, .data = data };
        s_QueueCount++;
    }
}

/* =====================================================================
 *  APU_RegisterRead — $4015 status read
 * ===================================================================== */
BYTE APU_RegisterRead(WORD addr) {
    if (addr == 0x4015) {
        BYTE status = 0;
        if (s_Pulse[0].len_cnt > 0) status |= 0x01;
        if (s_Pulse[1].len_cnt > 0) status |= 0x02;
        if (s_Tri.len_cnt > 0)      status |= 0x04;
        if (s_Noise.len_cnt > 0)    status |= 0x08;
        if (s_Dpcm.bytes_left > 0)  status |= 0x10;
        return status;
    }
    return (BYTE)(addr >> 8);
}

/* =====================================================================
 *  V-Sync: envelope decay, sweep, linear counter, length counter
 * ===================================================================== */

static void clock_envelope(BYTE *decay, BYTE *cnt, BYTE *start, BYTE loop, BYTE const_vol, BYTE vol) {
    if (*start) {
        *start = 0;
        *decay = 15;
        *cnt = vol;
        return;
    }
    if (*cnt == 0) {
        *cnt = vol;
        if (*decay == 0) {
            if (loop) *decay = 15;
        } else {
            (*decay)--;
        }
    } else {
        (*cnt)--;
    }
}

static void clock_sweep(PulseState *p, int channel) {
    if (p->sweep_reload) {
        p->sweep_cnt    = p->sweep_period;
        p->sweep_reload = 0;
        return;
    }
    if (p->sweep_cnt == 0) {
        p->sweep_cnt = p->sweep_period;
        if (p->sweep_en && p->sweep_shift > 0) {
            WORD delta = p->timer_reload >> p->sweep_shift;
            if (p->sweep_neg) {
                p->timer_reload -= delta;
                if (channel == 0) p->timer_reload--; /* Pulse 1 negate: subtract 1 extra */
            } else {
                p->timer_reload += delta;
            }
        }
    } else {
        p->sweep_cnt--;
    }
}

static void apu_clock_quarter_frame(void) {
    /* Envelope clocks */
    clock_envelope(&s_Pulse[0].env_decay, &s_Pulse[0].env_cnt, &s_Pulse[0].env_start,
                   s_Pulse[0].env_loop, s_Pulse[0].env_const, s_Pulse[0].env_vol);
    clock_envelope(&s_Pulse[1].env_decay, &s_Pulse[1].env_cnt, &s_Pulse[1].env_start,
                   s_Pulse[1].env_loop, s_Pulse[1].env_const, s_Pulse[1].env_vol);
    clock_envelope(&s_Noise.env_decay, &s_Noise.env_cnt, &s_Noise.env_start,
                   s_Noise.env_loop, s_Noise.env_const, s_Noise.env_vol);

    /* Sweep */
    clock_sweep(&s_Pulse[0], 0);
    clock_sweep(&s_Pulse[1], 1);

    /* Linear counter */
    if (s_Tri.lin_reload) {
        s_Tri.lin_cnt = s_Tri.lin_load;
        if (!s_Tri.ctrl_flag) s_Tri.lin_reload = 0;
    } else if (s_Tri.lin_cnt > 0) {
        s_Tri.lin_cnt--;
    }
}

static void apu_clock_half_frame(void) {
    /* Sweep */
    clock_sweep(&s_Pulse[0], 0);
    clock_sweep(&s_Pulse[1], 1);

    /* Length counters */
    if (!s_Pulse[0].env_loop && s_Pulse[0].len_cnt > 0) s_Pulse[0].len_cnt--;
    if (!s_Pulse[1].env_loop && s_Pulse[1].len_cnt > 0) s_Pulse[1].len_cnt--;
    if (!s_Tri.ctrl_flag    && s_Tri.len_cnt > 0)       s_Tri.len_cnt--;
    if (!s_Noise.env_loop   && s_Noise.len_cnt > 0)     s_Noise.len_cnt--;
}

void InfoNES_pAPUVsync(void) {
    apu_clock_quarter_frame();
    apu_clock_half_frame();
}

/* =====================================================================
 *  Sample generation helpers
 * ===================================================================== */

/* Get current pulse channel volume (0 if muted) */
static inline BYTE pulse_volume(const PulseState *p) {
    if (!p->enabled || p->len_cnt == 0) return 0;
    if (p->timer_reload < 8 || p->timer_reload > 0x7FF) return 0;  /* Sweep mute */
    return p->env_const ? p->env_vol : p->env_decay;
}

/* Triangle sequencer waveform: 0..15..0 (32-step, output range 0..15) */
static const BYTE s_TriSeq[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
     0,  1,  2,  3,  4,  5, 6, 7, 8, 9,10,11,12,13,14,15
};

/* =====================================================================
 *  InfoNES_pAPUHsync — synthesize one scanline worth of samples
 *  RAM-resident on RP2040.
 * ===================================================================== */
void RAMFUNC(InfoNES_pAPUHsync)(int render_enabled) {
    UNUSED(render_enabled);

    s_FrameQuarterAccum += ((DWORD)STEP_PER_SCANLINE << APU_FP_SHIFT);
    s_FrameHalfAccum    += ((DWORD)STEP_PER_SCANLINE << APU_FP_SHIFT);
    while (s_FrameQuarterAccum >= s_FrameQuarterStep) {
        s_FrameQuarterAccum -= s_FrameQuarterStep;
        apu_clock_quarter_frame();
        s_FrameQuarterCount++;
    }
    while (s_FrameHalfAccum >= s_FrameHalfStep) {
        s_FrameHalfAccum -= s_FrameHalfStep;
        apu_clock_half_frame();
        s_FrameHalfCount++;
    }

    /* Determine how many samples to generate this scanline */
    s_SampleAccum += s_SamplePerLine;
    int n_samples = (int)(s_SampleAccum >> APU_FP_SHIFT);
    s_SampleAccum &= (APU_FP_ONE - 1);

    if (n_samples <= 0) return;
    if (n_samples > APU_WAVE_BUF_SIZE) n_samples = APU_WAVE_BUF_SIZE;
    s_SampleCount += (uint32_t)n_samples;

#if defined(PICO_BUILD) && APU_CH_DIAG_ENABLED
    if (s_Noise.enabled && s_Noise.len_cnt > 0) {
        s_ApuNoiseEnabledLines++;
    }
    if (s_Dpcm.bytes_left > 0) {
        s_ApuDpcmActiveLines++;
        if (s_Dpcm.bytes_left > s_ApuDpcmBytesLeftMax) {
            s_ApuDpcmBytesLeftMax = s_Dpcm.bytes_left;
        }
    }
#endif

    WORD elapsed_cycles = 0;
    for (int i = 0; i < n_samples; i++) {
        WORD cycles = apu_next_sample_cycles();
        elapsed_cycles = (WORD)(elapsed_cycles + cycles);
        apu_apply_queued_events_until(elapsed_cycles);

        /* ---- Pulse 1 ---- */
        {
            PulseState *p = &s_Pulse[0];
            BYTE vol = pulse_volume(p);
            BYTE out = 0;
            if (vol > 0) {
                WORD period = (WORD)(((DWORD)p->timer_reload + 1u) * 2u);
                apu_advance_timer(&p->timer, period, cycles, &p->seq_pos, 7u);
                out = s_PulseDuty[p->duty][p->seq_pos] ? vol : 0;
            }
            s_WaveBuf[0][i] = out;
        }

        /* ---- Pulse 2 ---- */
        {
            PulseState *p = &s_Pulse[1];
            BYTE vol = pulse_volume(p);
            BYTE out = 0;
            if (vol > 0) {
                WORD period = (WORD)(((DWORD)p->timer_reload + 1u) * 2u);
                apu_advance_timer(&p->timer, period, cycles, &p->seq_pos, 7u);
                out = s_PulseDuty[p->duty][p->seq_pos] ? vol : 0;
            }
            s_WaveBuf[1][i] = out;
        }

        /* ---- Triangle ---- */
        {
            TriState *t = &s_Tri;
            BYTE active = t->enabled && t->len_cnt > 0 && t->lin_cnt > 0;
            BYTE out = 0;
            if (active) {
                WORD period = (WORD)(t->timer_reload + 1u);
                apu_advance_timer(&t->timer, period, cycles, &t->seq_pos, 31u);
                out = s_TriSeq[t->seq_pos];
            }
            s_WaveBuf[2][i] = out;
        }

        /* ---- Noise ---- */
        {
            NoiseState *n = &s_Noise;
            BYTE vol = (n->enabled && n->len_cnt > 0) ?
                       (n->env_const ? n->env_vol : n->env_decay) : 0;
            BYTE out = 0;
            if (vol > 0) {
                WORD noise_cycles = cycles;
                WORD period = n->timer_reload ? n->timer_reload : 1u;
                if (n->timer == 0 || n->timer > period) {
                    n->timer = period;
                }
                while (noise_cycles >= n->timer) {
                    noise_cycles -= n->timer;
                    n->timer = period;
                    WORD feedback;
                    if (n->mode) {
                        feedback = (n->lfsr & 1u) ^ ((n->lfsr >> 6) & 1u);
                    } else {
                        feedback = (n->lfsr & 1u) ^ ((n->lfsr >> 1) & 1u);
                    }
                    n->lfsr = (WORD)((n->lfsr >> 1) | (feedback << 14));
                }
                n->timer = (WORD)(n->timer - noise_cycles);
                out = (n->lfsr & 1u) ? 0u : vol;
            }
            s_WaveBuf[3][i] = out;
#if defined(PICO_BUILD) && APU_CH_DIAG_ENABLED
            if (out > 0) {
                s_ApuNoiseNonzeroSamples++;
            }
#endif
        }

        /* ---- DPCM ---- */
        {
            DpcmState *d = &s_Dpcm;
            WORD dpcm_cycles = cycles;
            WORD period = d->timer_reload ? d->timer_reload : 1u;
            if (d->timer == 0 || d->timer > period) {
                d->timer = period;
            }
            while (dpcm_cycles >= d->timer) {
                dpcm_cycles -= d->timer;
                d->timer = period;
                if (!d->silence) {
                    if (d->bits_rem == 0) {
                        if (d->bytes_left > 0) {
                            d->shift_reg = K6502_Read(d->cur_addr);
                            d->cur_addr++;
                            if (d->cur_addr == 0x0000u) d->cur_addr = 0x8000u;
                            d->bytes_left--;
                            d->bits_rem = 8;
                            if (d->bytes_left == 0) {
                                if (d->loop_en) {
                                    d->cur_addr   = d->sample_addr;
                                    d->bytes_left = d->sample_len;
                                } else {
                                    d->silence = 1;
                                }
                            }
                        } else {
                            d->silence = 1;
                        }
                    }
                    if (!d->silence && d->bits_rem > 0) {
                        if (d->shift_reg & 1u) {
                            if (d->output <= 125) d->output += 2;
                        } else {
                            if (d->output >= 2) d->output -= 2;
                        }
                        d->shift_reg >>= 1;
                        d->bits_rem--;
                    }
                }
            }
            d->timer = (WORD)(d->timer - dpcm_cycles);
            s_WaveBuf[4][i] = d->output;
#if defined(PICO_BUILD) && APU_CH_DIAG_ENABLED
            if (d->output > 0) {
                s_ApuDpcmNonzeroSamples++;
            }
#endif
        }
    }

    apu_apply_queued_events_until(STEP_PER_SCANLINE);
    apu_rebase_queue_for_next_scanline();

    /* Send to platform mix */
    InfoNES_SoundOutput(n_samples,
                        s_WaveBuf[0], s_WaveBuf[1],
                        s_WaveBuf[2], s_WaveBuf[3],
                        s_WaveBuf[4]);

#if defined(PICO_BUILD) && APU_CH_DIAG_ENABLED
    {
        uint64_t now = time_us_64();
        if ((now - s_ApuDebugLastUs) >= 1000000ull) {
            printf("[APU_CH] noise_nz=%lu dpcm_nz=%lu noise_lines=%lu dpcm_lines=%lu dpcm_bytes_max=%lu\r\n",
                   (unsigned long)s_ApuNoiseNonzeroSamples,
                   (unsigned long)s_ApuDpcmNonzeroSamples,
                   (unsigned long)s_ApuNoiseEnabledLines,
                   (unsigned long)s_ApuDpcmActiveLines,
                   (unsigned long)s_ApuDpcmBytesLeftMax);
            s_ApuNoiseNonzeroSamples = 0;
            s_ApuDpcmNonzeroSamples = 0;
            s_ApuNoiseEnabledLines = 0;
            s_ApuDpcmActiveLines = 0;
            s_ApuDpcmBytesLeftMax = 0;
            s_ApuDebugLastUs = now;
        }
    }
#endif

}
