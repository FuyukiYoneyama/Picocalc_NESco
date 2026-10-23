// Exercise the production pacing and mixer with a deterministic 22050 Hz sink.
// LCD/CPU costs are injected; this is not a hardware performance measurement.
#include <cassert>
#include <cstdio>

#include "display.c"
#include "audio.c"

WORD FrameSkip;
WORD FrameCnt;
BYTE MapperNo;

static uint64_t test_now_us;
static uint64_t test_consumed;
static uint64_t test_shortage;

uint64_t time_us_64(void) { return test_now_us; }

void sleep_us(uint64_t delay_us)
{
    test_now_us += delay_us;
    const uint64_t due = (test_now_us * 22050u / 1000000u / 32u) * 32u;
    while (test_consumed < due) {
        if (audio_ring_available() > 0) {
            audio_ring_pop_sample();
        } else {
            ++test_shortage;
        }
        ++test_consumed;
    }
}

static bool run_case(const char *name, BYTE mapper, bool stretch, bool fixed,
                     unsigned render_us, unsigned skip_us, bool recover = false)
{
    test_now_us = 1;
    test_consumed = 0;
    test_shortage = 0;
    audio_reset_runtime_state();
    s_audio_hw_sample_rate = 22050;
    s_audio_paused = false;
    audio_prefill_silence();
    display_reset_frame_pacing();
    s_nes_view_scale = stretch ? NES_VIEW_SCALE_STRETCH_320X300 : NES_VIEW_SCALE_NORMAL;
    s_stretch_fixed_frame_skip = fixed;
    MapperNo = mapper;

    FrameCnt = 0;
    unsigned rendered = 0;
    unsigned max_skip = 0;
    unsigned final_rendered = 0;
    unsigned sample_fraction = 0;
    BYTE wave[64];
    int16_t expansion[64] = {};
    memset(wave, 8, sizeof(wave));
    unsigned block_samples = 0;
    for (unsigned frame = 0; frame < 3600; ++frame) {
        if (recover && frame == 1800) {
            s_nes_view_scale = NES_VIEW_SCALE_NORMAL;
            display_reset_frame_pacing();
            render_us = 15000;
            skip_us = 5000;
        }
        const bool draw = FrameCnt == 0;
        const unsigned frame_cost = draw ? render_us : skip_us;
        rendered += draw;
        if (frame >= 3000) final_rendered += draw;
        for (unsigned line = 0; line < 262; ++line) {
            sleep_us(frame_cost * (line + 1u) / 262u - frame_cost * line / 262u);
            sample_fraction += 91926u; // InfoNES_pAPU.cpp, 22050 Hz quality.
            const unsigned samples = sample_fraction >> 16;
            sample_fraction &= 65535u;
            if (mapper == 19) {
                block_samples += samples;
                if (block_samples >= 64) {
                    InfoNES_SoundOutputN163(64, wave, wave, wave, wave, wave,
                                           expansion, 64);
                    block_samples -= 64;
                }
            } else if (mapper == 5) {
                InfoNES_SoundOutputMMC5(samples, wave, wave, wave, wave, wave,
                                       expansion, samples);
            } else {
                InfoNES_SoundOutput(samples, wave, wave, wave, wave, wave);
            }
            if (line + 1u == SCAN_UNKNOWN_START) {
                InfoNES_LoadFrame();
                if (FrameSkip > max_skip) max_skip = FrameSkip;
            }
            if (line + 1u == SCAN_VBLANK_START) {
                FrameCnt = FrameCnt >= FrameSkip ? 0 : FrameCnt + 1;
            }
        }
    }
    const double emulated_fps = 3600.0 * 1000000.0 / test_now_us;
    std::printf("%s: emulated_fps=%.3f rendered=%u max_skip=%u shortage=%llu drops=%lu ring=%d\n",
                name, emulated_fps, rendered, max_skip,
                (unsigned long long)test_shortage,
                (unsigned long)s_prod_drop_samples, audio_ring_available());
    return test_shortage == 0 && s_prod_drop_samples == 0 &&
           emulated_fps > 59.0 && emulated_fps < 61.0 && rendered > 600 &&
           (!recover || final_rendered == 600);
}

static void test_sleep_budget()
{
    audio_reset_runtime_state();
    s_audio_hw_sample_rate = 0;
    assert(audio_frame_sleep_budget_us(1024) == UINT32_MAX);
    s_audio_hw_sample_rate = 22050;
    s_audio_paused = false;
    assert(audio_frame_sleep_budget_us(1024) == 0);
    audio_prefill_silence();
    assert(audio_frame_sleep_budget_us(1024) == 1024u * 1000000ull / 22050u);
    s_audio_paused = true;
    assert(audio_frame_sleep_budget_us(1024) == UINT32_MAX);
    s_audio_paused = false;
#ifdef NESCO_AUDIO_FRAME_ADMISSION
    s_audio_frame_admission_skip = true;
    display_reset_frame_pacing();
    assert(!s_audio_frame_admission_skip);
#endif
}

int main()
{
    test_sleep_budget();
    bool passed = true;
    passed &= run_case("normal", 0, false, true, 15000, 5000);
    passed &= run_case("stretch fixed light", 0, true, true, 26000, 5000);
    passed &= run_case("stretch fixed heavy", 0, true, true, 31000, 8000);
    passed &= run_case("stretch fixed MMC5", 5, true, true, 35000, 10000);
    passed &= run_case("stretch fixed N163", 19, true, true, 35000, 10000);
    passed &= run_case("stretch adaptive", 0, true, false, 31000, 8000);
    passed &= run_case("normal MMC5", 5, false, true, 15000, 5000);
    passed &= run_case("normal N163", 19, false, true, 15000, 5000);
    passed &= run_case("stretch to normal recovery", 0, true, true, 35000, 10000, true);
    return passed ? 0 : 1;
}
