/*
 * pwm_audio.c — PicoCalc PWM audio output
 *
 * Drives PicoCalc audio output pins with 8-bit PWM using a DMA paced
 * ping-pong buffer. Samples are pulled from the platform audio ring in
 * half-buffer chunks, and any shortage is filled with centered silence so
 * the output path stays continuous without last-sample hold.
 */

#include "common_types.h"
#include "audio.h"
#include "runtime_log.h"

#ifdef PICO_BUILD

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include <math.h>
#include <stdio.h>

#if defined(NESCO_RUNTIME_LOGS)
#define AUDIO_STEP_LOGF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define AUDIO_STEP_LOGF(fmt, ...) ((void)0)
#endif

enum {
    PICO_AUDIO_PIN_LEFT  = 26,
    PICO_AUDIO_PIN_RIGHT = 27,
    PICO_AUDIO_WRAP      = 255,
    PICO_AUDIO_DMA_HALF_SAMPLES = 128,
    PICO_AUDIO_STARTUP_SILENCE_SAMPLES = 1024,
    PICO_AUDIO_UI_BUSY_TONE_HZ = 880,
    PICO_AUDIO_UI_BUSY_TONE_MS = 70,
    PICO_AUDIO_UI_BUSY_PERIOD_MS = 500,
    PICO_AUDIO_UI_BUSY_AMPLITUDE = 34,
};

static int  s_slice_left   = -1;
static uint s_chan_left    = 0;
static uint s_chan_right   = 0;
static bool s_audio_live   = false;
static volatile bool s_audio_paused = false;
static int s_dma_chan_left = -1;
static int s_dma_timer = -1;
static uint s_dma_active_half = 0u;
static volatile int  s_min_available = AUDIO_RING_SIZE;
static volatile int  s_max_available = 0;
static volatile uint32_t s_available_sum = 0;
static volatile uint32_t s_available_samples = 0;
static volatile uint32_t s_underrun_count = 0;
static volatile uint32_t s_dma_refill_calls = 0;
static volatile uint32_t s_dma_refill_shortage_samples = 0;
static volatile uint32_t s_dma_silence_fill_samples = 0;
static volatile uint32_t s_clipped_samples = 0;
static volatile uint32_t s_output_sum_sq = 0;
static volatile uint32_t s_output_sample_count = 0;
static volatile BYTE s_output_peak = 0;
static uint32_t s_startup_silence_samples = 0;
static uint32_t s_dma_buffer[2][PICO_AUDIO_DMA_HALF_SAMPLES];
static uint64_t s_last_debug_us = 0;
static uint32_t s_sample_rate = 22050u;
static volatile bool s_ui_busy_active = false;
static volatile uint32_t s_ui_busy_pos = 0;

void pwm_audio_close(void);
void pwm_audio_reset_stats(void);

static uint32_t pwm_audio_gcd_u32(uint32_t a, uint32_t b) {
    while (b != 0u) {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static void pwm_audio_compute_dma_fraction(uint32_t sample_rate, uint32_t clk_hz, uint16_t *numerator, uint16_t *denominator) {
    uint32_t num = sample_rate;
    uint32_t den = clk_hz;
    uint32_t gcd = pwm_audio_gcd_u32(num, den);
    if (gcd != 0u) {
        num /= gcd;
        den /= gcd;
    }

    if (den > 65535u || num > 65535u) {
        uint32_t best_num = 1u;
        uint32_t best_den = 1u;
        uint64_t best_err_num = UINT64_MAX;
        uint64_t best_err_den = 1u;

        for (uint32_t cand_den = 1u; cand_den <= 65535u; cand_den++) {
            uint64_t scaled = (uint64_t)sample_rate * (uint64_t)cand_den;
            uint32_t cand_num = (uint32_t)((scaled + (clk_hz / 2u)) / clk_hz);
            if (cand_num == 0u) {
                cand_num = 1u;
            }
            if (cand_num > cand_den || cand_num > 65535u) {
                continue;
            }

            uint64_t actual = (uint64_t)clk_hz * (uint64_t)cand_num;
            uint64_t target = (uint64_t)sample_rate * (uint64_t)cand_den;
            uint64_t err = (actual > target) ? (actual - target) : (target - actual);

            if (best_err_num == UINT64_MAX ||
                err * best_err_den < best_err_num * (uint64_t)cand_den ||
                (err * best_err_den == best_err_num * (uint64_t)cand_den && cand_den > best_den)) {
                best_num = cand_num;
                best_den = cand_den;
                best_err_num = err;
                best_err_den = cand_den;
            }
        }

        num = best_num;
        den = best_den;
    }

    *numerator = (uint16_t)num;
    *denominator = (uint16_t)den;
}

static inline uint32_t pwm_audio_pack_sample(BYTE sample) {
    return (uint32_t)sample | ((uint32_t)sample << 16);
}

static BYTE pwm_audio_ui_busy_sample(void) {
    uint32_t sample_rate = s_sample_rate;
    uint32_t period_samples = (sample_rate * PICO_AUDIO_UI_BUSY_PERIOD_MS) / 1000u;
    uint32_t tone_samples = (sample_rate * PICO_AUDIO_UI_BUSY_TONE_MS) / 1000u;
    uint32_t half_period = sample_rate / (PICO_AUDIO_UI_BUSY_TONE_HZ * 2u);
    uint32_t pos = s_ui_busy_pos++;

    if (period_samples == 0u) {
        period_samples = 1u;
    }
    if (tone_samples == 0u) {
        tone_samples = 1u;
    }
    if (half_period == 0u) {
        half_period = 1u;
    }

    if ((pos % period_samples) >= tone_samples) {
        return 128u;
    }
    return (((pos / half_period) & 1u) != 0u)
               ? (BYTE)(128u - PICO_AUDIO_UI_BUSY_AMPLITUDE)
               : (BYTE)(128u + PICO_AUDIO_UI_BUSY_AMPLITUDE);
}

static void pwm_audio_refill_half(uint half_index) {
    if (s_ui_busy_active) {
        for (int i = 0; i < PICO_AUDIO_DMA_HALF_SAMPLES; i++) {
            s_dma_buffer[half_index][i] = pwm_audio_pack_sample(pwm_audio_ui_busy_sample());
        }
        s_dma_refill_calls++;
        return;
    }

    if (s_audio_paused) {
        uint32_t packed_silence = pwm_audio_pack_sample(128u);
        for (int i = 0; i < PICO_AUDIO_DMA_HALF_SAMPLES; i++) {
            s_dma_buffer[half_index][i] = packed_silence;
        }
        s_dma_refill_calls++;
        s_dma_silence_fill_samples += PICO_AUDIO_DMA_HALF_SAMPLES;
        return;
    }

    if (s_startup_silence_samples > 0u) {
        uint32_t packed_silence = pwm_audio_pack_sample(128u);
        for (int i = 0; i < PICO_AUDIO_DMA_HALF_SAMPLES; i++) {
            s_dma_buffer[half_index][i] = packed_silence;
        }
        if (s_startup_silence_samples > PICO_AUDIO_DMA_HALF_SAMPLES) {
            s_startup_silence_samples -= PICO_AUDIO_DMA_HALF_SAMPLES;
        } else {
            s_startup_silence_samples = 0u;
        }
        s_dma_refill_calls++;
        s_dma_silence_fill_samples += PICO_AUDIO_DMA_HALF_SAMPLES;
        return;
    }

    int available = audio_ring_available();
    if (available < s_min_available) s_min_available = available;
    if (available > s_max_available) s_max_available = available;
    s_available_sum += (uint32_t)available;
    s_available_samples++;
    s_dma_refill_calls++;

    int count = available;
    if (count > PICO_AUDIO_DMA_HALF_SAMPLES) {
        count = PICO_AUDIO_DMA_HALF_SAMPLES;
    }

    for (int i = 0; i < count; i++) {
        BYTE sample = audio_ring_pop_sample();
        s_dma_buffer[half_index][i] = pwm_audio_pack_sample(sample);
        int centered = (int)sample - 128;
        uint32_t mag = (uint32_t)((centered < 0) ? -centered : centered);
        if (mag > s_output_peak) {
            s_output_peak = (BYTE)mag;
        }
        s_output_sum_sq += (uint32_t)(centered * centered);
        s_output_sample_count++;
        if (sample == 0u || sample == 255u) {
            s_clipped_samples++;
        }
    }

    if (count < PICO_AUDIO_DMA_HALF_SAMPLES) {
        uint32_t packed_silence = pwm_audio_pack_sample(128u);
        uint32_t shortage = (uint32_t)(PICO_AUDIO_DMA_HALF_SAMPLES - count);
        s_underrun_count += shortage;
        s_dma_refill_shortage_samples += shortage;
        s_dma_silence_fill_samples += shortage;
        for (int i = count; i < PICO_AUDIO_DMA_HALF_SAMPLES; i++) {
            s_dma_buffer[half_index][i] = packed_silence;
        }
    }
}

static void pwm_audio_start_half(uint half_index) {
    dma_channel_set_read_addr((uint)s_dma_chan_left, s_dma_buffer[half_index], false);
    dma_channel_set_transfer_count((uint)s_dma_chan_left, PICO_AUDIO_DMA_HALF_SAMPLES, false);
    dma_start_channel_mask(1u << s_dma_chan_left);
    s_dma_active_half = half_index;
}

static void RAMFUNC(pwm_audio_dma_irq0_handler)(void) {
    if (dma_channel_get_irq0_status((uint)s_dma_chan_left)) {
        dma_channel_acknowledge_irq0((uint)s_dma_chan_left);
    }

    if (!s_audio_live) {
        return;
    }

    uint completed_half = s_dma_active_half;
    uint next_half = completed_half ^ 1u;
    pwm_audio_refill_half(completed_half);
    pwm_audio_start_half(next_half);
}

void pwm_audio_init(int gpio_pin, int sample_rate) {
    UNUSED(gpio_pin);

    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] begin clk_sys_khz=%lu\r\n",
                     (unsigned long)(clock_get_hz(clk_sys) / 1000u));
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_close\r\n");
    pwm_audio_close();
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_close\r\n");

    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_gpio_pwm\r\n");
    gpio_set_function(PICO_AUDIO_PIN_LEFT, GPIO_FUNC_PWM);
    gpio_set_function(PICO_AUDIO_PIN_RIGHT, GPIO_FUNC_PWM);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_gpio_pwm\r\n");

    s_slice_left   = pwm_gpio_to_slice_num(PICO_AUDIO_PIN_LEFT);
    s_chan_left    = pwm_gpio_to_channel(PICO_AUDIO_PIN_LEFT);
    s_chan_right   = pwm_gpio_to_channel(PICO_AUDIO_PIN_RIGHT);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] slice=%d chan_l=%u chan_r=%u\r\n",
                     s_slice_left,
                     (unsigned int)s_chan_left,
                     (unsigned int)s_chan_right);

    if (pwm_gpio_to_slice_num(PICO_AUDIO_PIN_RIGHT) != (uint)s_slice_left) {
        NESCO_LOGF("[AUDIO] pin pair mismatch L=%d R=%d\r\n", PICO_AUDIO_PIN_LEFT, PICO_AUDIO_PIN_RIGHT);
        return;
    }

    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_pwm_init\r\n");
    pwm_config config = pwm_get_default_config();
    float clkdiv = 1.0f;
    pwm_config_set_clkdiv(&config, clkdiv);
    pwm_config_set_wrap(&config, PICO_AUDIO_WRAP);

    pwm_init((uint)s_slice_left, &config, false);

    pwm_set_counter((uint)s_slice_left, 0);

    pwm_set_chan_level((uint)s_slice_left, s_chan_left, 128u);
    pwm_set_chan_level((uint)s_slice_left, s_chan_right, 128u);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_pwm_init\r\n");
    s_min_available = AUDIO_RING_SIZE;
    s_max_available = 0;
    s_available_sum = 0;
    s_available_samples = 0;
    s_underrun_count = 0;
    s_dma_refill_calls = 0;
    s_dma_refill_shortage_samples = 0;
    s_dma_silence_fill_samples = 0;
    s_clipped_samples = 0;
    s_output_sum_sq = 0;
    s_output_sample_count = 0;
    s_output_peak = 0;
    s_startup_silence_samples = PICO_AUDIO_STARTUP_SILENCE_SAMPLES;
    s_sample_rate = (uint32_t)sample_rate;
    s_ui_busy_active = false;
    s_ui_busy_pos = 0;
    s_audio_paused = false;
    s_last_debug_us = time_us_64();

    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_dma_claim_channel\r\n");
    s_dma_chan_left = dma_claim_unused_channel(true);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_dma_claim_channel chan=%d\r\n", s_dma_chan_left);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_dma_claim_timer\r\n");
    s_dma_timer = dma_claim_unused_timer(true);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_dma_claim_timer timer=%d\r\n", s_dma_timer);
    uint16_t dma_num = 0u;
    uint16_t dma_den = 0u;
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_dma_fraction\r\n");
    pwm_audio_compute_dma_fraction((uint32_t)sample_rate, clock_get_hz(clk_sys), &dma_num, &dma_den);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_dma_fraction num=%u den=%u\r\n",
                     (unsigned int)dma_num,
                     (unsigned int)dma_den);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_dma_timer_fraction\r\n");
    dma_timer_set_fraction((uint)s_dma_timer, dma_num, dma_den);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_dma_timer_fraction\r\n");

    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_dma_config\r\n");
    dma_channel_config cfg_left = dma_channel_get_default_config((uint)s_dma_chan_left);
    channel_config_set_transfer_data_size(&cfg_left, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg_left, true);
    channel_config_set_write_increment(&cfg_left, false);
    channel_config_set_dreq(&cfg_left, dma_get_timer_dreq((uint)s_dma_timer));

    dma_channel_configure((uint)s_dma_chan_left,
                          &cfg_left,
                          &pwm_hw->slice[s_slice_left].cc,
                          s_dma_buffer[0],
                          PICO_AUDIO_DMA_HALF_SAMPLES,
                          false);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_dma_config\r\n");

    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_refill\r\n");
    pwm_audio_refill_half(0u);
    pwm_audio_refill_half(1u);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_refill\r\n");

    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_irq_clear\r\n");
    dma_channel_acknowledge_irq0((uint)s_dma_chan_left);
    irq_clear(DMA_IRQ_0);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_irq_clear\r\n");

    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_dma_irq0_enable\r\n");
    dma_channel_set_irq0_enabled((uint)s_dma_chan_left, true);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_dma_irq0_enable\r\n");
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_irq_set_exclusive_handler\r\n");
    irq_set_exclusive_handler(DMA_IRQ_0, pwm_audio_dma_irq0_handler);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_irq_set_exclusive_handler\r\n");
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_irq_set_enabled\r\n");
    s_audio_live = true;
    irq_set_enabled(DMA_IRQ_0, true);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_irq_set_enabled\r\n");

    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_pwm_enable\r\n");
    pwm_set_enabled((uint)s_slice_left, true);

    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] before_start_half\r\n");
    pwm_audio_start_half(0u);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] after_start_half\r\n");

    NESCO_LOGF("[AUDIO] pwm init L=%d R=%d rate=%d wrap=%d clkdiv=%.3f carrier=%lu dma_half=%d dma_timer=%d dma_frac=%u/%u\r\n",
               PICO_AUDIO_PIN_LEFT,
               PICO_AUDIO_PIN_RIGHT,
               sample_rate,
               PICO_AUDIO_WRAP,
               (double)clkdiv,
               (unsigned long)(clock_get_hz(clk_sys) / (PICO_AUDIO_WRAP + 1u)),
               PICO_AUDIO_DMA_HALF_SAMPLES,
               s_dma_timer,
               (unsigned int)dma_num,
               (unsigned int)dma_den);
    AUDIO_STEP_LOGF("[AUDIO_PWM_STEP] end\r\n");
}

void pwm_audio_ui_busy_start(void) {
    s_ui_busy_pos = 0;
    s_ui_busy_active = true;
}

void pwm_audio_ui_busy_stop(void) {
    s_ui_busy_active = false;
}

void pwm_audio_close(void) {
    if (!s_audio_live) {
        return;
    }

    irq_set_enabled(DMA_IRQ_0, false);
    dma_channel_set_irq0_enabled((uint)s_dma_chan_left, false);
    dma_channel_abort((uint)s_dma_chan_left);
    dma_channel_acknowledge_irq0((uint)s_dma_chan_left);
    dma_channel_unclaim((uint)s_dma_chan_left);
    dma_timer_unclaim((uint)s_dma_timer);

    pwm_set_enabled((uint)s_slice_left, false);

    pwm_set_chan_level((uint)s_slice_left, s_chan_left, 128u);
    pwm_set_chan_level((uint)s_slice_left, s_chan_right, 128u);

    s_slice_left = -1;
    s_dma_chan_left = -1;
    s_dma_timer = -1;
    s_audio_live = false;
    s_audio_paused = false;
    s_dma_active_half = 0u;
}

void pwm_audio_reset_stats(void) {
    s_min_available = AUDIO_RING_SIZE;
    s_max_available = 0;
    s_available_sum = 0;
    s_available_samples = 0;
    s_underrun_count = 0;
    s_dma_refill_calls = 0;
    s_dma_refill_shortage_samples = 0;
    s_dma_silence_fill_samples = 0;
    s_clipped_samples = 0;
    s_output_sum_sq = 0;
    s_output_sample_count = 0;
    s_output_peak = 0;
    s_last_debug_us = time_us_64();
}

void pwm_audio_debug_poll(void) {
    if (!s_audio_live) {
        return;
    }

    uint64_t now = time_us_64();
    if ((now - s_last_debug_us) < 1000000ull) {
        return;
    }

    int available = audio_ring_available();
#if defined(NESCO_RUNTIME_LOGS)
    uint32_t ring_avg = (s_available_samples > 0u)
        ? (s_available_sum / s_available_samples)
        : (uint32_t)available;
    uint32_t output_rms = 0u;
    if (s_output_sample_count > 0u) {
        output_rms = (uint32_t)(sqrt((double)s_output_sum_sq / (double)s_output_sample_count) + 0.5);
    }
    NESCO_LOGF("[AUDIO] ring avail=%d min=%d max=%d avg=%lu underruns=%lu hold_runs=%u hold_samples=%u out_peak=%u out_rms=%lu clipped=%lu\r\n",
               available,
               s_min_available,
               s_max_available,
               (unsigned long)ring_avg,
               (unsigned long)s_underrun_count,
               0u,
               0u,
               (unsigned int)s_output_peak,
               (unsigned long)output_rms,
               (unsigned long)s_clipped_samples);
    NESCO_LOGF("[AUDIO_DMA] refill_calls=%lu shortage_samples=%lu silence_fill_samples=%lu active_half=%u\r\n",
               (unsigned long)s_dma_refill_calls,
               (unsigned long)s_dma_refill_shortage_samples,
               (unsigned long)s_dma_silence_fill_samples,
               (unsigned int)s_dma_active_half);
#else
    (void)available;
#endif

    s_min_available = AUDIO_RING_SIZE;
    s_max_available = 0;
    s_available_sum = 0;
    s_available_samples = 0;
    s_underrun_count = 0;
    s_dma_refill_calls = 0;
    s_dma_refill_shortage_samples = 0;
    s_dma_silence_fill_samples = 0;
    s_clipped_samples = 0;
    s_output_sum_sq = 0;
    s_output_sample_count = 0;
    s_output_peak = 0;
    s_last_debug_us = now;
}

void pwm_audio_set_paused(int paused) {
    s_audio_paused = (paused != 0);
    if (s_slice_left >= 0) {
        pwm_set_chan_level((uint)s_slice_left, s_chan_left, 128u);
        pwm_set_chan_level((uint)s_slice_left, s_chan_right, 128u);
    }
}

#else

void pwm_audio_init(int gpio_pin, int sample_rate) {
    UNUSED(gpio_pin);
    UNUSED(sample_rate);
}

void pwm_audio_close(void) { }

void pwm_audio_debug_poll(void) { }
void pwm_audio_reset_stats(void) { }
void pwm_audio_set_paused(int paused) { UNUSED(paused); }

#endif
