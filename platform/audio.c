/*
 * audio.c — PWM audio ring buffer and mixing (PicoCalc / RP2040)
 *
 * Mix 5 APU channels (8-bit each, range 0–15 or 0–127) into 8-bit mono.
 * Write into g_audio_ring; platform PWM output drains the ring.
 *
 * Mixing equation (per NES non-linear mix approximation, simplified):
 *   pulse_out   = 95.88 / (8128 / (p1 + p2) + 100)
 *   tnd_out     = 159.79 / (1 / (tri/8227 + noise/12241 + dpcm/22638) + 100)
 * Simplified linear mix used here for speed on Cortex-M0+.
 *
 * Part of Picocalc_NESco
 * MIT License
 */

#include "audio.h"
#include "InfoNES_pAPU.h"
#include "runtime_log.h"

#ifdef PICO_BUILD
#include "pico/time.h"
#endif

#define AUDIO_WAIT_LOOPS_MAX 2000
#define AUDIO_WAIT_SLEEP_US 50
#define AUDIO_MIX_NOISE_WEIGHT 4u
#define AUDIO_MIX_OUTPUT_SCALE 255u
#define AUDIO_MIX_DIVISOR 1120u
#define AUDIO_MIX_ROUND_BIAS (AUDIO_MIX_DIVISOR / 2u)

/* =====================================================================
 *  Ring buffer state
 * ===================================================================== */
BYTE g_audio_ring[AUDIO_RING_SIZE];

static volatile int s_ring_write = 0;  /* Producer (APU side) */
static volatile int s_ring_read  = 0;  /* Consumer (PWM side) */
static int s_mix_dc_estimate = 0;      /* Running DC estimate in Q8 */
static volatile uint32_t s_ring_overrun_count = 0;
static volatile uint32_t s_prod_push_samples = 0;
static volatile uint32_t s_prod_drop_samples = 0;
static volatile uint32_t s_prod_call_count = 0;
static volatile uint32_t s_prod_nch_sum = 0;
static volatile uint32_t s_prod_max_nch = 0;
static int s_open_samples_per_sync = 0;
static int s_open_clock_per_sync = 0;
static int s_audio_hw_sample_rate = 0;
static bool s_audio_hw_ready = false;
static volatile bool s_audio_paused = false;
static BYTE s_mix_peak = 0;
static BYTE s_noise_peak = 0;
static BYTE s_dpcm_peak = 0;
#ifdef PICO_BUILD
static uint64_t s_audio_debug_last_us = 0;
#endif

/* =====================================================================
 *  Platform PWM calls (implement in drivers/pwm_audio.c)
 * ===================================================================== */
extern "C" void pwm_audio_init(int gpio_pin, int sample_rate);
extern "C" void pwm_audio_close(void);
extern "C" void pwm_audio_debug_poll(void);
extern "C" void pwm_audio_reset_stats(void);
extern "C" void pwm_audio_set_paused(int paused);
extern "C" void pwm_audio_ui_busy_start(void);
extern "C" void pwm_audio_ui_busy_stop(void);

static int audio_ring_writable(void) {
    return (AUDIO_RING_SIZE - 1) - audio_ring_available();
}

static void audio_ring_push_ui_sample(BYTE sample)
{
    int next_write = (s_ring_write + 1) & (AUDIO_RING_SIZE - 1);
    if (next_write == s_ring_read) {
        return;
    }
    g_audio_ring[s_ring_write] = sample;
    s_ring_write = next_write;
}

void audio_play_ui_tone(unsigned freq_hz, unsigned duration_ms, BYTE amplitude)
{
    unsigned sample_rate;
    unsigned samples;
    unsigned half_period;

    if (!s_audio_hw_ready || s_audio_hw_sample_rate <= 0 || freq_hz == 0u || duration_ms == 0u) {
        return;
    }

    sample_rate = (unsigned)s_audio_hw_sample_rate;
    samples = (sample_rate * duration_ms) / 1000u;
    half_period = sample_rate / (freq_hz * 2u);
    if (half_period == 0u) {
        half_period = 1u;
    }

    for (unsigned i = 0; i < samples; ++i) {
        BYTE sample = ((i / half_period) & 1u) ? (BYTE)(128u - amplitude) : (BYTE)(128u + amplitude);
        audio_ring_push_ui_sample(sample);
    }
}

void audio_play_ui_silence(unsigned duration_ms)
{
    unsigned sample_rate;
    unsigned samples;

    if (!s_audio_hw_ready || s_audio_hw_sample_rate <= 0 || duration_ms == 0u) {
        return;
    }

    sample_rate = (unsigned)s_audio_hw_sample_rate;
    samples = (sample_rate * duration_ms) / 1000u;
    for (unsigned i = 0; i < samples; ++i) {
        audio_ring_push_ui_sample(128u);
    }
}

void audio_start_ui_busy_indicator(void)
{
    if (s_audio_hw_ready) {
        pwm_audio_ui_busy_start();
    }
}

void audio_stop_ui_busy_indicator(void)
{
    pwm_audio_ui_busy_stop();
}

/* =====================================================================
 *  audio_init / audio_close
 * ===================================================================== */
void audio_reset_runtime_state(void) {
    s_ring_write = 0;
    s_ring_read  = 0;
    s_mix_dc_estimate = 0;
    s_ring_overrun_count = 0;
    s_prod_push_samples = 0;
    s_prod_drop_samples = 0;
    s_prod_call_count = 0;
    s_prod_nch_sum = 0;
    s_prod_max_nch = 0;
    s_mix_peak = 0;
    s_noise_peak = 0;
    s_dpcm_peak = 0;
#ifdef PICO_BUILD
    s_audio_debug_last_us = time_us_64();
#endif
}

void audio_init(void) {
    audio_reset_runtime_state();
    s_audio_paused = false;
    pwm_audio_init(28, INFONES_AUDIO_DEFAULT_SAMPLE_RATE);   /* PicoCalc audio out: L=28, R=27 */
    s_audio_hw_sample_rate = INFONES_AUDIO_DEFAULT_SAMPLE_RATE;
    s_audio_hw_ready = true;
}

void audio_close(void) {
    s_audio_paused = false;
    pwm_audio_close();
    s_audio_hw_sample_rate = 0;
    s_audio_hw_ready = false;
}

void audio_debug_poll(void) {
    pwm_audio_debug_poll();
#ifdef PICO_BUILD
    uint64_t now = time_us_64();
    if ((now - s_audio_debug_last_us) < 1000000ull) {
        return;
    }

#if defined(NESCO_RUNTIME_LOGS)
    uint32_t prod_avg_nch = 0;
    if (s_prod_call_count > 0u) {
        prod_avg_nch = s_prod_nch_sum / s_prod_call_count;
    }
    NESCO_LOGF("[AUDIO_MIX] overrun=%lu mix_peak=%u noise_peak=%u dpcm_peak=%u push_samples=%lu drop_samples=%lu calls=%lu nch_sum=%lu nch_avg=%lu max_nch=%lu open_sps=%d open_cps=%d\r\n",
               (unsigned long)s_ring_overrun_count,
               s_mix_peak,
               s_noise_peak,
               s_dpcm_peak,
               (unsigned long)s_prod_push_samples,
               (unsigned long)s_prod_drop_samples,
               (unsigned long)s_prod_call_count,
               (unsigned long)s_prod_nch_sum,
               (unsigned long)prod_avg_nch,
               (unsigned long)s_prod_max_nch,
               s_open_samples_per_sync,
               s_open_clock_per_sync);
#endif

    s_ring_overrun_count = 0;
    s_prod_push_samples = 0;
    s_prod_drop_samples = 0;
    s_prod_call_count = 0;
    s_prod_nch_sum = 0;
    s_prod_max_nch = 0;
    s_mix_peak = 0;
    s_noise_peak = 0;
    s_dpcm_peak = 0;
    s_audio_debug_last_us = now;
#endif
}

/* =====================================================================
 *  InfoNES_SoundInit / Open / Close
 * ===================================================================== */
void InfoNES_SoundInit(void) {
    audio_reset_runtime_state();
    s_audio_paused = false;
}

int InfoNES_SoundOpen(int samples_per_sync, int sample_rate) {
    s_open_samples_per_sync = samples_per_sync;
    s_open_clock_per_sync = sample_rate;
    audio_reset_runtime_state();
    s_audio_paused = false;
    /* Keep PWM/DMA alive across ROM starts; re-init only if the hardware
     * sample rate really changes. */
    if (!s_audio_hw_ready || s_audio_hw_sample_rate != sample_rate) {
        pwm_audio_init(28, sample_rate);
        s_audio_hw_sample_rate = sample_rate;
        s_audio_hw_ready = true;
    }
    /* Reset driver-side counters here so each ROM start gets a fresh
     * comparison window without tearing audio hardware down. */
    pwm_audio_reset_stats();
    return 0;
}

void InfoNES_SoundClose(void) {
    audio_reset_runtime_state();
    /* Close stays silent-idle; stats are reset so menu->game / game->menu
     * comparisons stay local to the current session window. */
    pwm_audio_reset_stats();
}

void audio_pause_for_capture(void) {
    s_audio_paused = true;
    audio_reset_runtime_state();
    pwm_audio_set_paused(1);
    pwm_audio_reset_stats();
}

void audio_resume_after_capture(void) {
    audio_reset_runtime_state();
    pwm_audio_reset_stats();
    pwm_audio_set_paused(0);
    s_audio_paused = false;
}

int InfoNES_GetSoundBufferSize(void) {
    return (AUDIO_RING_SIZE - 1) - audio_ring_available();
}

/* =====================================================================
 *  InfoNES_SoundOutput — mix 5 channels into ring buffer
 *
 *  Channel volumes (nominal peak):
 *    Pulse 1/2: 0–15
 *    Triangle:  0–15
 *    Noise:     0–15
 *    DPCM:      0–127
 *
 *  nes1-compatible simple linear mix:
 *    pulse1 + pulse2 + triangle + noise*4 + dpcm
 *    then normalized to 8-bit with named gain constants so we can
 *    compare whole-output gain changes without touching channel balance.
 * ===================================================================== */
void InfoNES_SoundOutput(int nch,
                         BYTE *buf0, BYTE *buf1,
                         BYTE *buf2, BYTE *buf3, BYTE *buf4) {
    if (s_audio_paused) {
        return;
    }

    s_prod_call_count++;
    s_prod_nch_sum += (uint32_t)nch;
    if ((uint32_t)nch > s_prod_max_nch) {
        s_prod_max_nch = (uint32_t)nch;
    }

#ifdef PICO_BUILD
    int wait_loops = 0;
    while (audio_ring_writable() < nch) {
        ++wait_loops;
        sleep_us(AUDIO_WAIT_SLEEP_US);
        if (wait_loops >= AUDIO_WAIT_LOOPS_MAX) {
            s_ring_overrun_count++;
            s_prod_drop_samples += (uint32_t)nch;
            return;
        }
    }
#endif

    for (int i = 0; i < nch; i++) {
        const uint32_t noise = (uint32_t)buf3[i] * AUDIO_MIX_NOISE_WEIGHT;
        const uint32_t mixed =
            (uint32_t)buf0[i] +
            (uint32_t)buf1[i] +
            (uint32_t)buf2[i] +
            noise +
            (uint32_t)buf4[i];
        int mix = (int)(((mixed * AUDIO_MIX_OUTPUT_SCALE) + AUDIO_MIX_ROUND_BIAS) /
                        AUDIO_MIX_DIVISOR);
        if (mix > 255) mix = 255;

        /* Convert the unipolar mix into a PWM-friendly centered signal.
         * A slow DC tracker keeps long-term silence near 128 instead of 0,
         * which reduces large idle bias/noise on the PicoCalc output path. */
        s_mix_dc_estimate += (((mix << 8) - s_mix_dc_estimate) >> 6);
        mix = 128 + mix - (s_mix_dc_estimate >> 8);

        if (mix > 255) mix = 255;
        if (mix < 0)   mix = 0;

        if ((BYTE)mix > s_mix_peak) s_mix_peak = (BYTE)mix;
        if (buf3[i] > s_noise_peak) s_noise_peak = buf3[i];
        if (buf4[i] > s_dpcm_peak) s_dpcm_peak = buf4[i];

        /* Single-producer/single-consumer ring. Keep one slot empty so the
         * producer can detect full without a shared count variable. */
        int next_write = (s_ring_write + 1) & (AUDIO_RING_SIZE - 1);
        if (next_write != s_ring_read) {
            g_audio_ring[s_ring_write] = (BYTE)mix;
            s_ring_write = next_write;
            s_prod_push_samples++;
        } else {
            s_ring_overrun_count++;
            s_prod_drop_samples++;
        }
        /* If ring full: drop sample and count it for diagnostics. */
    }
}

/* =====================================================================
 *  audio_ring_pop_sample — consume one sample for platform audio output
 * ===================================================================== */
BYTE audio_ring_pop_sample(void) {
    if (s_ring_read == s_ring_write) {
        return 128u;
    }

    BYTE sample = g_audio_ring[s_ring_read];
    s_ring_read = (s_ring_read + 1) & (AUDIO_RING_SIZE - 1);
    return sample;
}

int audio_ring_available(void) {
    int write = s_ring_write;
    int read = s_ring_read;
    if (write >= read) {
        return write - read;
    }
    return AUDIO_RING_SIZE - (read - write);
}
