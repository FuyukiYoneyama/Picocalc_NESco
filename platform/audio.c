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
#include <stdio.h>

#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
#include "InfoNES_Mapper.h"
#endif

#ifdef PICO_BUILD
#include "pico/time.h"
#endif

#if defined(PICO_BUILD) && defined(NESCO_AUDIO_RAMFUNC)
#define AUDIO_RAMFUNC(function_name) __not_in_flash_func(function_name)
#else
#define AUDIO_RAMFUNC(function_name) function_name
#endif

#if defined(PICO_BUILD) && (defined(NESCO_AUDIO_RAMFUNC) || defined(NESCO_AUDIO_MIX_RAMFUNC))
#define AUDIO_MIX_RAMFUNC(function_name) __not_in_flash_func(function_name)
#else
#define AUDIO_MIX_RAMFUNC(function_name) function_name
#endif

#define AUDIO_WAIT_LOOPS_MAX 2000
#define AUDIO_WAIT_SLEEP_US 50
#define AUDIO_MIX_NOISE_WEIGHT 4u
#define AUDIO_MIX_OUTPUT_SCALE 255u
#define AUDIO_MIX_DIVISOR 1120u
#define AUDIO_MIX_ROUND_BIAS (AUDIO_MIX_DIVISOR / 2u)
#define AUDIO_MIX_N163_GAIN_NUM 1
#define AUDIO_MIX_N163_GAIN_DEN 2
#define AUDIO_MIX_N163_GAIN_ROUND_BIAS (AUDIO_MIX_N163_GAIN_DEN / 2)

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
#if defined(PICO_BUILD) && defined(NESCO_MAPPER19_AUDIO_COMFORT)
/*
 * Listening-candidate safety stage.  A normal transition smaller than the
 * limit is bit-identical.  Larger final-mix jumps are spread over a fraction
 * of a millisecond, which trades a little transient sharpness for a calmer
 * listening result without inserting silence, repeating a sample, or changing
 * the producer/DMA clock.
 */
#define AUDIO_COMFORT_MAX_STEP 15
static BYTE s_audio_comfort_previous_raw = 128u;
static BYTE s_audio_comfort_pending_raw = 128u;
static BYTE s_audio_comfort_last_sample = 128u;
#endif
static uint64_t s_perf_audio_wait_us = 0;
static uint32_t s_perf_audio_wait_count = 0;
#ifdef PICO_BUILD
static uint64_t s_audio_debug_last_us = 0;
#endif

#if defined(PICO_BUILD) && defined(NESCO_AUDIO_MEASURE)
/* Stage A contract: this interval begins after the first normal producer
 * block following ROM open.  It deliberately excludes prefill and the DMA
 * setup path, while leaving the existing one-second diagnostics unchanged. */
static bool s_audio_measure_pending = false;
static bool s_audio_measure_active = false;
static uint64_t s_audio_measure_begin_us = 0;
static uint64_t s_audio_measure_last_producer_us = 0;
static int s_audio_measure_ring_start = 0;
static uint32_t s_audio_measure_push_samples = 0;
static uint32_t s_audio_measure_drop_samples = 0;
#endif

#if defined(PICO_BUILD) && defined(NESCO_AUDIO_CLOCK_LOCK)
/*
 * Mapper 19's emulation clock and the PWM DMA clock are independent clocks.
 * The source normally stays within a fraction of a percent of the fixed
 * 22050 Hz sink, but a long run can otherwise drain the reserve.  The clock
 * lock emits at most a small bounded number of interpolated samples per
 * source block when the sink deadline is ahead.  It is a timebase converter,
 * not a silence/last-sample underrun fill: the source N163 PCM and its event
 * timing remain unchanged before this final handoff.
 */
#define AUDIO_CLOCK_MAX_EXTRA_PER_BLOCK 8
#define AUDIO_CLOCK_MIX_SCRATCH_SAMPLES 735
#define AUDIO_CLOCK_SOURCE_FIFO_SAMPLES 2048
static bool s_audio_clock_started = false;
static uint64_t s_audio_clock_start_us = 0;
static uint64_t s_audio_clock_emitted_samples = 0;
static uint64_t s_audio_clock_last_elapsed_us = 0;
static uint64_t s_audio_clock_last_target_total = 0;
static BYTE s_audio_clock_last_mix = 128u;
static BYTE s_audio_clock_mix_scratch[AUDIO_CLOCK_MIX_SCRATCH_SAMPLES];
static BYTE s_audio_clock_source_fifo[AUDIO_CLOCK_SOURCE_FIFO_SAMPLES];
static unsigned s_audio_clock_source_fifo_head = 0;
static unsigned s_audio_clock_source_fifo_count = 0;
#endif

#ifdef NESCO_MAPPER19_N163_ONLY_DIAGNOSTIC
#define AUDIO_N163_DIAG_CAPTURE_SAMPLES 4096u
static uint32_t s_n163_diag_sample_count = 0;
static uint32_t s_n163_diag_fnv1a_le = 2166136261u;
static int16_t s_n163_diag_min_sample = 0;
static int16_t s_n163_diag_max_sample = 0;
static uint32_t s_n163_diag_nonzero_count = 0;
static uint8_t s_n163_diag_capture[AUDIO_N163_DIAG_CAPTURE_SAMPLES];
static uint32_t s_n163_diag_capture_count = 0;
#endif

#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
#define M5_AUDIO_DIAG_CAPTURE_SAMPLES 4096u
static bool s_mmc5_diag_seen = false;
static bool s_mmc5_diag_reported = false;
static BYTE s_mmc5_diag_duty_min = 255u;
static BYTE s_mmc5_diag_duty_max = 0u;
static uint32_t s_mmc5_diag_pushed = 0;
static uint32_t s_mmc5_diag_dropped = 0;

static void audio_mmc5_diag_dump(void)
{
    uint32_t generator_samples = 0;
    int16_t generator_min = 0;
    int16_t generator_max = 0;

    Map5_AudioDiagnosticsSnapshot(&generator_samples,
                                  &generator_min,
                                  &generator_max);

    if (!s_mmc5_diag_seen || s_mmc5_diag_reported ||
        generator_samples < M5_AUDIO_DIAG_CAPTURE_SAMPLES) {
        return;
    }

    printf("[M5_AUDIO_DIAG] gen_samples=%lu gen_min=%d gen_max=%d "
           "duty_min=%u duty_max=%u pushed=%lu dropped=%lu\r\n",
           (unsigned long)generator_samples,
           (int)generator_min,
           (int)generator_max,
           (unsigned)s_mmc5_diag_duty_min,
           (unsigned)s_mmc5_diag_duty_max,
           (unsigned long)s_mmc5_diag_pushed,
           (unsigned long)s_mmc5_diag_dropped);
    fflush(stdout);
    s_mmc5_diag_reported = true;
}
#endif

#ifdef NESCO_MAPPER19_N163_ONLY_DIAGNOSTIC
void audio_n163_diag_reset(void)
{
    s_n163_diag_sample_count = 0;
    s_n163_diag_fnv1a_le = 2166136261u;
    s_n163_diag_min_sample = 0;
    s_n163_diag_max_sample = 0;
    s_n163_diag_nonzero_count = 0;
    s_n163_diag_capture_count = 0;
}

void audio_n163_diag_observe(const int16_t *samples, int count)
{
    if (!samples || count <= 0) return;

    for (int i = 0; i < count; ++i) {
        const int16_t sample = samples[i];
        const uint16_t encoded = (uint16_t)sample;
        s_n163_diag_fnv1a_le ^= (uint32_t)(encoded & 0xffu);
        s_n163_diag_fnv1a_le *= 16777619u;
        s_n163_diag_fnv1a_le ^= (uint32_t)(encoded >> 8);
        s_n163_diag_fnv1a_le *= 16777619u;

        if (s_n163_diag_sample_count == 0 || sample < s_n163_diag_min_sample) {
            s_n163_diag_min_sample = sample;
        }
        if (s_n163_diag_sample_count == 0 || sample > s_n163_diag_max_sample) {
            s_n163_diag_max_sample = sample;
        }
        if (sample != 0) ++s_n163_diag_nonzero_count;
        if (s_n163_diag_capture_count < AUDIO_N163_DIAG_CAPTURE_SAMPLES) {
            int encoded_sample = (int)sample + 128;
            if (encoded_sample < 0) encoded_sample = 0;
            if (encoded_sample > 255) encoded_sample = 255;
            s_n163_diag_capture[s_n163_diag_capture_count++] = (uint8_t)encoded_sample;
        }
        ++s_n163_diag_sample_count;
    }
}

void audio_n163_diag_snapshot(uint32_t *sample_count,
                              uint32_t *fnv1a_le,
                              int16_t *min_sample,
                              int16_t *max_sample,
                              uint32_t *nonzero_count)
{
    if (sample_count) *sample_count = s_n163_diag_sample_count;
    if (fnv1a_le) *fnv1a_le = s_n163_diag_fnv1a_le;
    if (min_sample) *min_sample = s_n163_diag_min_sample;
    if (max_sample) *max_sample = s_n163_diag_max_sample;
    if (nonzero_count) *nonzero_count = s_n163_diag_nonzero_count;
}

void audio_n163_diag_dump(void)
{
    printf("[M19_N163_PRE_RING_DATA_BEGIN] samples=%lu captured=%lu\n",
           (unsigned long)s_n163_diag_sample_count,
           (unsigned long)s_n163_diag_capture_count);
    for (uint32_t i = 0; i < s_n163_diag_capture_count; ++i) {
        printf("%02X", (unsigned)s_n163_diag_capture[i]);
        if ((i & 31u) == 31u) {
            printf("\n");
        } else {
            printf(" ");
        }
    }
    if ((s_n163_diag_capture_count & 31u) != 0u) printf("\n");
    printf("[M19_N163_PRE_RING_DATA_END]\n");
}
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

#if defined(PICO_BUILD) && defined(NESCO_MAPPER19_AUDIO_COMFORT)
static BYTE AUDIO_MIX_RAMFUNC(audio_comfort_filter_sample)(BYTE incoming)
{
    const BYTE a = s_audio_comfort_previous_raw;
    const BYTE b = s_audio_comfort_pending_raw;
    const BYTE c = incoming;
    BYTE median;

    if (a < b) {
        median = b < c ? b : (a < c ? c : a);
    } else {
        median = a < c ? a : (b < c ? c : b);
    }

    s_audio_comfort_previous_raw = b;
    s_audio_comfort_pending_raw = c;

    int next = (int)median;
    const int previous = (int)s_audio_comfort_last_sample;
    const int delta = next - previous;

    if (delta > AUDIO_COMFORT_MAX_STEP) {
        next = previous + AUDIO_COMFORT_MAX_STEP;
    } else if (delta < -AUDIO_COMFORT_MAX_STEP) {
        next = previous - AUDIO_COMFORT_MAX_STEP;
    }

    s_audio_comfort_last_sample = (BYTE)next;
    return s_audio_comfort_last_sample;
}
#endif

#if defined(PICO_BUILD) && defined(NESCO_AUDIO_CLOCK_LOCK)
static int AUDIO_MIX_RAMFUNC(audio_ring_push_clock_sample)(BYTE sample)
{
    int next_write = (s_ring_write + 1) & (AUDIO_RING_SIZE - 1);
    if (next_write == s_ring_read) {
        s_ring_overrun_count++;
        s_prod_drop_samples++;
#if defined(NESCO_AUDIO_MEASURE)
        if (s_audio_measure_active) {
            s_audio_measure_drop_samples++;
        }
#endif
        return 0;
    }

    g_audio_ring[s_ring_write] = sample;
    s_ring_write = next_write;
    s_prod_push_samples++;
#if defined(NESCO_AUDIO_MEASURE)
    if (s_audio_measure_active) {
        s_audio_measure_push_samples++;
    }
#endif
    return 1;
}

static BYTE AUDIO_MIX_RAMFUNC(audio_clock_fifo_peek)(unsigned index)
{
    return s_audio_clock_source_fifo[
        (s_audio_clock_source_fifo_head + index) &
        (AUDIO_CLOCK_SOURCE_FIFO_SAMPLES - 1u)];
}
#endif

static void audio_ring_push_ui_sample(BYTE sample)
{
    int next_write = (s_ring_write + 1) & (AUDIO_RING_SIZE - 1);
    if (next_write == s_ring_read) {
        return;
    }
    g_audio_ring[s_ring_write] = sample;
    s_ring_write = next_write;
}

#ifdef NESCO_AUDIO_PREFILL
#define AUDIO_PREFILL_SAMPLES 2048u

static void audio_prefill_silence(void)
{
    for (unsigned i = 0; i < AUDIO_PREFILL_SAMPLES; ++i) {
        audio_ring_push_ui_sample(128u);
    }
}
#endif

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
#if defined(PICO_BUILD) && defined(NESCO_MAPPER19_AUDIO_COMFORT)
    s_audio_comfort_previous_raw = 128u;
    s_audio_comfort_pending_raw = 128u;
    s_audio_comfort_last_sample = 128u;
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
#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
    s_mmc5_diag_seen = false;
    s_mmc5_diag_reported = false;
    s_mmc5_diag_duty_min = 255u;
    s_mmc5_diag_duty_max = 0u;
    s_mmc5_diag_pushed = 0;
    s_mmc5_diag_dropped = 0;
#endif
#ifdef PICO_BUILD
    s_audio_debug_last_us = time_us_64();
#endif
#if defined(PICO_BUILD) && defined(NESCO_AUDIO_MEASURE)
    s_audio_measure_pending = false;
    s_audio_measure_active = false;
    s_audio_measure_begin_us = 0;
    s_audio_measure_last_producer_us = 0;
    s_audio_measure_ring_start = 0;
    s_audio_measure_push_samples = 0;
    s_audio_measure_drop_samples = 0;
#endif
#if defined(PICO_BUILD) && defined(NESCO_AUDIO_CLOCK_LOCK)
    s_audio_clock_started = false;
    s_audio_clock_start_us = 0;
    s_audio_clock_emitted_samples = 0;
    s_audio_clock_last_elapsed_us = 0;
    s_audio_clock_last_target_total = 0;
    s_audio_clock_last_mix = 128u;
    s_audio_clock_source_fifo_head = 0;
    s_audio_clock_source_fifo_count = 0;
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
#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
    if ((now - s_audio_debug_last_us) >= 1000000ull) {
        audio_mmc5_diag_dump();
    }
#endif
    if ((now - s_audio_debug_last_us) < 1000000ull) {
        return;
    }

#if defined(NESCO_RUNTIME_LOGS)
    uint32_t prod_avg_nch = 0;
    if (s_prod_call_count > 0u) {
        prod_avg_nch = s_prod_nch_sum / s_prod_call_count;
    }
#if defined(NESCO_AUDIO_MEASURE)
    uint64_t measure_window_us = 0;
    uint32_t measure_push_samples = 0;
    uint32_t measure_drop_samples = 0;
    int measure_ring_end = audio_ring_available();
    if (s_audio_measure_active) {
        const uint64_t producer_end_us =
            s_audio_measure_last_producer_us != 0
                ? s_audio_measure_last_producer_us
                : now;
        measure_window_us = producer_end_us - s_audio_measure_begin_us;
        measure_push_samples = s_audio_measure_push_samples;
        measure_drop_samples = s_audio_measure_drop_samples;
    }
    NESCO_LOGF("[AUDIO_MIX] overrun=%lu mix_peak=%u noise_peak=%u dpcm_peak=%u push_samples=%lu drop_samples=%lu calls=%lu nch_sum=%lu nch_avg=%lu max_nch=%lu open_sps=%d open_cps=%d window_us=%llu cumulative_push=%lu cumulative_drop=%lu ring_start=%d ring_end=%d\r\n",
#else
    NESCO_LOGF("[AUDIO_MIX] overrun=%lu mix_peak=%u noise_peak=%u dpcm_peak=%u push_samples=%lu drop_samples=%lu calls=%lu nch_sum=%lu nch_avg=%lu max_nch=%lu open_sps=%d open_cps=%d\r\n",
#endif
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
               s_open_clock_per_sync
#if defined(NESCO_AUDIO_MEASURE)
               , (unsigned long long)measure_window_us,
               (unsigned long)measure_push_samples,
               (unsigned long)measure_drop_samples,
               s_audio_measure_ring_start,
               measure_ring_end
#endif
               );
#endif
#if defined(NESCO_AUDIO_CLOCK_LOCK)
    NESCO_LOGF("[AUDIO_CLOCK] elapsed_us=%llu target=%llu emitted=%llu\r\n",
               (unsigned long long)s_audio_clock_last_elapsed_us,
               (unsigned long long)s_audio_clock_last_target_total,
               (unsigned long long)s_audio_clock_emitted_samples);
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

#ifdef NESCO_AUDIO_PREFILL
    /* Stop consuming the old menu/session ring while the new ROM is opened.
     * The producer starts only after InfoNES_SoundOpen returns, so this is the
     * only safe point to establish a known startup reserve. */
    s_audio_paused = true;
    pwm_audio_set_paused(1);
#endif

    audio_reset_runtime_state();
    /* Keep PWM/DMA alive across ROM starts; re-init only if the hardware
     * sample rate really changes. */
    if (!s_audio_hw_ready || s_audio_hw_sample_rate != sample_rate) {
        pwm_audio_init(28, sample_rate);
        s_audio_hw_sample_rate = sample_rate;
        s_audio_hw_ready = true;
    }
#ifdef NESCO_AUDIO_PREFILL
    s_audio_paused = true;
    pwm_audio_set_paused(1);
    audio_prefill_silence();
#else
    s_audio_paused = false;
#endif
    /* Reset driver-side counters here so each ROM start gets a fresh
     * comparison window without tearing audio hardware down. */
    pwm_audio_reset_stats();
#ifdef NESCO_AUDIO_PREFILL
    pwm_audio_set_paused(0);
    s_audio_paused = false;
#endif
#if defined(PICO_BUILD) && defined(NESCO_AUDIO_MEASURE)
    s_audio_measure_pending = true;
    s_audio_measure_active = false;
#endif
    return 0;
}

void InfoNES_SoundClose(void) {
#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
    audio_mmc5_diag_dump();
#endif
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

void audio_perf_reset(void) {
    s_perf_audio_wait_us = 0;
    s_perf_audio_wait_count = 0;
}

void audio_perf_snapshot(uint64_t *wait_us, uint32_t *wait_count) {
    if (wait_us) {
        *wait_us = s_perf_audio_wait_us;
    }
    if (wait_count) {
        *wait_count = s_perf_audio_wait_count;
    }
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
static void AUDIO_MIX_RAMFUNC(audio_sound_output_impl)(int nch,
                                                   BYTE *buf0, BYTE *buf1,
                                                   BYTE *buf2, BYTE *buf3, BYTE *buf4,
                                                   const int16_t *n163, int n163_samples,
                                                   const int16_t *mmc5, int mmc5_samples,
                                                   bool is_mmc5) {
    if (s_audio_paused) {
        return;
    }

#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
    if (is_mmc5 && nch > 0) {
        s_mmc5_diag_seen = true;
    }
#endif

#if defined(PICO_BUILD) && defined(NESCO_AUDIO_MEASURE)
    const bool measure_block_active = s_audio_measure_active;
#endif

    s_prod_call_count++;
    s_prod_nch_sum += (uint32_t)nch;
    if ((uint32_t)nch > s_prod_max_nch) {
        s_prod_max_nch = (uint32_t)nch;
    }

    int output_nch = nch;
#if defined(PICO_BUILD) && defined(NESCO_AUDIO_CLOCK_LOCK)
    const bool clock_lock_active =
        n163 != NULL && nch > 0 && nch <= AUDIO_CLOCK_MIX_SCRATCH_SAMPLES;
    bool clock_lock_prime = false;
    int clock_extra = 0;
    if (clock_lock_active) {
        if (!s_audio_clock_started) {
            clock_lock_prime = true;
        }
    }
#endif

#ifdef PICO_BUILD
#if defined(NESCO_AUDIO_CLOCK_LOCK)
    if (!clock_lock_active)
#endif
    {
    int wait_loops = 0;
    uint64_t wait_start_us = 0;
    while (audio_ring_writable() < output_nch) {
        if (wait_loops == 0) {
            wait_start_us = time_us_64();
        }
        ++wait_loops;
        s_perf_audio_wait_count++;
        sleep_us(AUDIO_WAIT_SLEEP_US);
        if (wait_loops >= AUDIO_WAIT_LOOPS_MAX) {
            s_perf_audio_wait_us += time_us_64() - wait_start_us;
            s_ring_overrun_count++;
            s_prod_drop_samples += (uint32_t)output_nch;
#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
            if (is_mmc5) {
                s_mmc5_diag_dropped += (uint32_t)output_nch;
            }
#endif
            return;
        }
    }
    if (wait_loops > 0) {
        s_perf_audio_wait_us += time_us_64() - wait_start_us;
    }
    }
#endif

    for (int i = 0; i < nch; i++) {
#ifdef NESCO_MAPPER19_N163_ONLY_DIAGNOSTIC
        /*
         * Generator-only comparison path. Map19_RenderAudioSlice() produces
         * a signed level in the range [-120, 112] (4-bit wave value centered
         * at zero, multiplied by channel volume). Put that level directly
         * around the PWM midpoint so the existing audio sink can capture it
         * without involving the APU mixer or the DC tracker.
         */
        int mix = 128;
        if (n163 != NULL && i < n163_samples) {
            int raw = (int)n163[i];
            if (raw < -127) raw = -127;
            if (raw > 127) raw = 127;
            mix += raw;
        }
#else
        const int noise = (int)buf3[i] * (int)AUDIO_MIX_NOISE_WEIGHT;
        int n163Mix = 0;
        if (n163 != NULL && i < n163_samples) {
            const int raw = (int)n163[i];
            const int magnitude = raw < 0 ? -raw : raw;
            const int rounded =
                (magnitude * AUDIO_MIX_N163_GAIN_NUM +
                 AUDIO_MIX_N163_GAIN_ROUND_BIAS) /
                AUDIO_MIX_N163_GAIN_DEN;
            n163Mix = raw < 0 ? -rounded : rounded;
        }
#ifdef NESCO_MAPPER19_N163_MUTE_DIAGNOSTIC
        n163Mix = 0;
#endif
        int mmc5Mix = 0;
        if (mmc5 != NULL && i < mmc5_samples) {
            mmc5Mix = (int)mmc5[i];
        }
        const int mixed =
            (int)buf0[i] +
            (int)buf1[i] +
            (int)buf2[i] +
            noise +
            (int)buf4[i] +
            n163Mix +
            mmc5Mix;
        const int mixedMagnitude = mixed < 0 ? -mixed : mixed;
#if defined(NESCO_AUDIO_MIX_FAST_DIVISION)
        /*
         * For the current APU/N163 input bounds mixedMagnitude is <= 300.
         * Since floor(floor(x / 32) / 35) == floor(x / 1120), this is an
         * exact replacement for (magnitude * 255 + 560) / 1120 over that
         * complete domain.  The second reciprocal is exact for the resulting
         * x <= 2408 and keeps the hot path free of __aeabi_uidiv.
         */
        const uint32_t roundedNumerator =
            (uint32_t)mixedMagnitude * 255u + 560u;
        const int scaledMagnitude =
            (int)(((roundedNumerator >> 5) * 1873u) >> 16);
#else
        const int scaledMagnitude =
            (int)(((mixedMagnitude * (int)AUDIO_MIX_OUTPUT_SCALE) +
                   (int)AUDIO_MIX_ROUND_BIAS) /
                  (int)AUDIO_MIX_DIVISOR);
#endif
        int mix = mixed < 0 ? -scaledMagnitude : scaledMagnitude;
        if (mix > 255) mix = 255;

        /* Convert the unipolar mix into a PWM-friendly centered signal.
         * A slow DC tracker keeps long-term silence near 128 instead of 0,
         * which reduces large idle bias/noise on the PicoCalc output path. */
        s_mix_dc_estimate += (((mix * 256) - s_mix_dc_estimate) >> 6);
        mix = 128 + mix - (s_mix_dc_estimate >> 8);

        if (mix > 255) mix = 255;
        if (mix < 0)   mix = 0;
#endif

#if defined(PICO_BUILD) && defined(NESCO_MAPPER19_AUDIO_COMFORT)
        if (n163 != NULL) {
            mix = (int)audio_comfort_filter_sample((BYTE)mix);
        }
#endif

        if ((BYTE)mix > s_mix_peak) s_mix_peak = (BYTE)mix;
        if (buf3[i] > s_noise_peak) s_noise_peak = buf3[i];
        if (buf4[i] > s_dpcm_peak) s_dpcm_peak = buf4[i];
#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
        if (is_mmc5) {
            if ((BYTE)mix < s_mmc5_diag_duty_min) {
                s_mmc5_diag_duty_min = (BYTE)mix;
            }
            if ((BYTE)mix > s_mmc5_diag_duty_max) {
                s_mmc5_diag_duty_max = (BYTE)mix;
            }
        }
#endif

#if defined(PICO_BUILD) && defined(NESCO_AUDIO_CLOCK_LOCK)
        if (clock_lock_active) {
            s_audio_clock_mix_scratch[i] = (BYTE)mix;
        } else
#endif
        {
            /* Single-producer/single-consumer ring. Keep one slot empty so the
             * producer can detect full without a shared count variable. */
            int next_write = (s_ring_write + 1) & (AUDIO_RING_SIZE - 1);
            if (next_write != s_ring_read) {
                g_audio_ring[s_ring_write] = (BYTE)mix;
                s_ring_write = next_write;
                s_prod_push_samples++;
#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
                if (is_mmc5) {
                    s_mmc5_diag_pushed++;
                }
#endif
#if defined(PICO_BUILD) && defined(NESCO_AUDIO_MEASURE)
                if (s_audio_measure_active) {
                    s_audio_measure_push_samples++;
                }
#endif
            } else {
                s_ring_overrun_count++;
                s_prod_drop_samples++;
#ifdef NESCO_MAPPER5_AUDIO_DIAGNOSTICS
                if (is_mmc5) {
                    s_mmc5_diag_dropped++;
                }
#endif
#if defined(PICO_BUILD) && defined(NESCO_AUDIO_MEASURE)
                if (s_audio_measure_active) {
                    s_audio_measure_drop_samples++;
                }
#endif
            }
            /* If ring full: drop sample and count it for diagnostics. */
        }
    }

#if defined(PICO_BUILD) && defined(NESCO_AUDIO_CLOCK_LOCK)
    if (clock_lock_active) {
        int regular_count = nch;
        int interval_count = 0;

        if (!clock_lock_prime) {
            if (s_audio_clock_source_fifo_count + (unsigned)nch >
                AUDIO_CLOCK_SOURCE_FIFO_SAMPLES) {
                s_ring_overrun_count++;
                s_prod_drop_samples += (uint32_t)nch;
#if defined(NESCO_AUDIO_MEASURE)
                if (s_audio_measure_active) {
                    s_audio_measure_drop_samples += (uint32_t)nch;
                }
#endif
                return;
            }
            for (int i = 0; i < nch; ++i) {
                const unsigned fifo_index =
                    (s_audio_clock_source_fifo_head +
                     s_audio_clock_source_fifo_count) &
                    (AUDIO_CLOCK_SOURCE_FIFO_SAMPLES - 1u);
                s_audio_clock_source_fifo[fifo_index] =
                    s_audio_clock_mix_scratch[i];
                s_audio_clock_source_fifo_count++;
            }

            uint64_t clock_origin_us = s_audio_clock_start_us;
#if defined(NESCO_AUDIO_MEASURE)
            if (s_audio_measure_active) {
                clock_origin_us = s_audio_measure_begin_us;
            }
#endif
            const uint64_t elapsed_us = time_us_64() - clock_origin_us;
            const uint64_t target_total =
                (elapsed_us * (uint64_t)s_audio_hw_sample_rate) / 1000000ull;
            s_audio_clock_last_elapsed_us = elapsed_us;
            s_audio_clock_last_target_total = target_total;

            uint64_t output_budget = 0;
            if (target_total > s_audio_clock_emitted_samples) {
                output_budget = target_total - s_audio_clock_emitted_samples;
            }
            if (output_budget < s_audio_clock_source_fifo_count) {
                regular_count = (int)output_budget;
            } else {
                regular_count = (int)s_audio_clock_source_fifo_count;
            }
            if (regular_count > 0) {
                interval_count = regular_count <
                    (int)s_audio_clock_source_fifo_count
                    ? regular_count : regular_count - 1;
            }
            const uint64_t extra_budget =
                output_budget > (uint64_t)regular_count
                    ? output_budget - (uint64_t)regular_count : 0;
            if (interval_count > 0 && extra_budget > 0) {
                uint64_t bounded_extra = extra_budget;
                if (bounded_extra > AUDIO_CLOCK_MAX_EXTRA_PER_BLOCK) {
                    bounded_extra = AUDIO_CLOCK_MAX_EXTRA_PER_BLOCK;
                }
                clock_extra = (int)bounded_extra;
            } else {
                clock_extra = 0;
            }
            output_nch = regular_count + clock_extra;
        } else {
            output_nch = nch;
        }

#ifdef PICO_BUILD
        int wait_loops = 0;
        uint64_t wait_start_us = 0;
        while (audio_ring_writable() < output_nch) {
            if (wait_loops == 0) {
                wait_start_us = time_us_64();
            }
            ++wait_loops;
            s_perf_audio_wait_count++;
            sleep_us(AUDIO_WAIT_SLEEP_US);
            if (wait_loops >= AUDIO_WAIT_LOOPS_MAX) {
                s_perf_audio_wait_us += time_us_64() - wait_start_us;
                s_ring_overrun_count++;
                s_prod_drop_samples += (uint32_t)output_nch;
#if defined(NESCO_AUDIO_MEASURE)
                if (s_audio_measure_active) {
                    s_audio_measure_drop_samples += (uint32_t)output_nch;
                }
#endif
                return;
            }
        }
        if (wait_loops > 0) {
            s_perf_audio_wait_us += time_us_64() - wait_start_us;
        }
#endif

        if (clock_lock_prime) {
            for (int i = 0; i < nch; ++i) {
                audio_ring_push_clock_sample(s_audio_clock_mix_scratch[i]);
            }
            s_audio_clock_last_mix = s_audio_clock_mix_scratch[nch - 1];
        } else {
            int inserted_extra = 0;
            for (int i = 0; i < regular_count; ++i) {
                audio_ring_push_clock_sample(audio_clock_fifo_peek((unsigned)i));
                if (i < interval_count) {
                    const int interval_extra =
                        ((i + 1) * clock_extra) / interval_count -
                        inserted_extra;
                    for (int k = 0; k < interval_extra; ++k) {
                        const int numerator =
                            (int)audio_clock_fifo_peek((unsigned)i) *
                                (interval_extra - k) +
                            (int)audio_clock_fifo_peek((unsigned)(i + 1)) *
                                (k + 1);
                        const BYTE interpolated =
                            (BYTE)(numerator / (interval_extra + 1));
                        audio_ring_push_clock_sample(interpolated);
                    }
                    inserted_extra += interval_extra;
                }
            }
            if (regular_count > 0) {
                s_audio_clock_last_mix =
                    audio_clock_fifo_peek((unsigned)(regular_count - 1));
                s_audio_clock_source_fifo_head =
                    (s_audio_clock_source_fifo_head +
                     (unsigned)regular_count) &
                    (AUDIO_CLOCK_SOURCE_FIFO_SAMPLES - 1u);
                s_audio_clock_source_fifo_count -= (unsigned)regular_count;
            }
        }

        if (clock_lock_prime) {
            s_audio_clock_started = true;
            s_audio_clock_start_us = time_us_64();
            s_audio_clock_emitted_samples = 0;
        } else {
            s_audio_clock_emitted_samples += (uint64_t)output_nch;
        }
    }
#endif

#if defined(PICO_BUILD) && defined(NESCO_AUDIO_MEASURE)
    if (s_audio_measure_pending && nch > 0) {
        /* The first normal block is intentionally excluded from the
         * cumulative interval.  It only establishes a post-prefill reserve;
         * the marker and counters begin immediately after that block. */
        pwm_audio_reset_stats();
        s_audio_measure_ring_start = audio_ring_available();
        s_audio_measure_push_samples = 0;
        s_audio_measure_drop_samples = 0;
        s_audio_measure_begin_us = time_us_64();
        s_audio_measure_last_producer_us = 0;
        s_audio_measure_active = true;
        s_audio_measure_pending = false;
#if defined(NESCO_AUDIO_CLOCK_LOCK)
        /* Restart the final handoff clock at the same marker used by the
         * producer contract. Any pre-marker source block is excluded from
         * both the measured push count and the clock accumulator. */
        if (clock_lock_active) {
            s_audio_clock_started = true;
            s_audio_clock_start_us = s_audio_measure_begin_us;
            s_audio_clock_emitted_samples = 0;
            s_audio_clock_source_fifo_head = 0;
            s_audio_clock_source_fifo_count = 0;
        }
#endif
        NESCO_LOGF("[AUDIO_MEASURE_BEGIN] rate=%d ring_start=%d first_block=%d\r\n",
                   s_open_clock_per_sync,
                   s_audio_measure_ring_start,
                   nch);
    }
#endif

#if defined(PICO_BUILD) && defined(NESCO_AUDIO_MEASURE)
    if (measure_block_active) {
        s_audio_measure_last_producer_us = time_us_64();
    }
#endif
}

void AUDIO_MIX_RAMFUNC(InfoNES_SoundOutput)(int nch,
                                            BYTE *buf0, BYTE *buf1,
                                            BYTE *buf2, BYTE *buf3, BYTE *buf4) {
    audio_sound_output_impl(nch, buf0, buf1, buf2, buf3, buf4,
                            NULL, 0, NULL, 0, false);
}

#ifdef PICO_BUILD
void AUDIO_MIX_RAMFUNC(InfoNES_SoundOutputN163)(int nch,
                                                BYTE *buf0, BYTE *buf1,
                                                BYTE *buf2, BYTE *buf3,
                                                BYTE *buf4,
                                                const int16_t *n163,
                                                int n163_samples) {
    audio_sound_output_impl(nch, buf0, buf1, buf2, buf3, buf4,
                            n163, n163_samples, NULL, 0, false);
}

void AUDIO_MIX_RAMFUNC(InfoNES_SoundOutputMMC5)(int nch,
                                                BYTE *buf0, BYTE *buf1,
                                                BYTE *buf2, BYTE *buf3,
                                                BYTE *buf4,
                                                const int16_t *mmc5,
                                                int mmc5_samples) {
    audio_sound_output_impl(nch, buf0, buf1, buf2, buf3, buf4,
                            NULL, 0, mmc5, mmc5_samples, true);
}
#endif

/* =====================================================================
 *  audio_ring_pop_sample — consume one sample for platform audio output
 * ===================================================================== */
BYTE AUDIO_RAMFUNC(audio_ring_pop_sample)(void) {
    if (s_ring_read == s_ring_write) {
        return 128u;
    }

    BYTE sample = g_audio_ring[s_ring_read];
    s_ring_read = (s_ring_read + 1) & (AUDIO_RING_SIZE - 1);
    return sample;
}

int AUDIO_RAMFUNC(audio_ring_available)(void) {
    int write = s_ring_write;
    int read = s_ring_read;
    if (write >= read) {
        return write - read;
    }
    return AUDIO_RING_SIZE - (read - write);
}
