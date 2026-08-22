/*
 * audio.h — PWM audio output interface (PicoCalc / RP2040)
 *
 * Ring-buffer + PWM audio pipeline:
 *   APU → InfoNES_SoundOutput → g_audio_ring[4096]
 *   DMA IRQ refill → consume ring samples → PWM DMA buffer → PicoCalc audio pins
 *
 * Part of Picocalc_NESco
 * MIT License
 */
#pragma once
#include "InfoNES_Types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NESCO_AUDIO_RING_8192
#define AUDIO_RING_SIZE   8192   /* 8 KB ring buffer */
#else
#define AUDIO_RING_SIZE   4096   /* 4 KB ring buffer */
#endif
#define AUDIO_DMA_CHUNK   512    /* Legacy chunk constant; active PWM DMA half size is driver-local */

extern BYTE g_audio_ring[AUDIO_RING_SIZE];

void audio_init(void);
void audio_close(void);
BYTE audio_ring_pop_sample(void);
int audio_ring_available(void);
void audio_debug_poll(void);
void audio_reset_runtime_state(void);
void audio_pause_for_capture(void);
void audio_resume_after_capture(void);
void audio_play_ui_tone(unsigned freq_hz, unsigned duration_ms, BYTE amplitude);
void audio_play_ui_silence(unsigned duration_ms);
void audio_start_ui_busy_indicator(void);
void audio_stop_ui_busy_indicator(void);
void audio_perf_reset(void);
void audio_perf_snapshot(uint64_t *wait_us, uint32_t *wait_count);

#ifdef PICO_BUILD
void InfoNES_SoundOutputN163(int samples,
                             BYTE *wave1, BYTE *wave2, BYTE *wave3,
                             BYTE *wave4, BYTE *wave5,
                             const int16_t *n163, int n163_samples);
void InfoNES_SoundOutputMMC5(int samples,
                             BYTE *wave1, BYTE *wave2, BYTE *wave3,
                             BYTE *wave4, BYTE *wave5,
                             const int16_t *mmc5, int mmc5_samples);
#endif

#ifdef NESCO_MAPPER19_N163_ONLY_DIAGNOSTIC
/* Generator-only observation point: before the sample reaches the audio ring. */
void audio_n163_diag_reset(void);
void audio_n163_diag_observe(const int16_t *samples, int count);
void audio_n163_diag_snapshot(uint32_t *sample_count,
                              uint32_t *fnv1a_le,
                              int16_t *min_sample,
                              int16_t *max_sample,
                              uint32_t *nonzero_count);
void audio_n163_diag_dump(void);
#endif

#ifdef __cplusplus
}
#endif
