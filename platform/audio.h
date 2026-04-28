/*
 * audio.h — PWM audio output interface (PicoCalc / RP2040)
 *
 * Ring-buffer + PWM audio pipeline:
 *   APU → InfoNES_SoundOutput → g_audio_ring[4096]
 *   PWM interrupt → consume ring samples → PicoCalc audio pins
 *
 * Part of Picocalc_NESco
 * MIT License
 */
#pragma once
#include "InfoNES_Types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RING_SIZE   4096   /* 4 KB ring buffer */
#define AUDIO_DMA_CHUNK   512    /* Samples per DMA transfer */

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

#ifdef __cplusplus
}
#endif
