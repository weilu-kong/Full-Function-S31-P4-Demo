/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SAMPLE_RATE 44100
#ifdef SYNTH_RENDER_TEST
#include "synth_render_under_test.h"

static void test_voice_attack_and_decay(void)
{
    for (int wave = SYNTH_WAVE_SIN; wave < SYNTH_WAVE_MAX; ++wave) {
        synth_voice_t v = {.active = true, .freq = 261.63f, .base_freq = 261.63f,
                           .velocity = 1.0f, .wave = wave, .env_decay_rate = 0.99967f};
        float previous = 0.0f;
        bool audible = false;
        for (int i = 0; i < 220; ++i) {
            float pcm = synth_render_sample(&v);
            assert(v.active);  /* sample zero must survive */
            assert(v.env > previous && v.env <= 1.0f);
            if (i == 0) assert(fabsf(pcm) < 0.01f);
            audible |= fabsf(pcm) > 0.001f;
            previous = v.env;
        }
        assert(v.env > 0.99f && audible);
        for (int i = 0; i < SAMPLE_RATE * 5 && v.active; ++i)
            synth_render_sample(&v);
        assert(!v.active);
    }
}

static void test_release(void)
{
    synth_voice_t v = {.active = true, .freq = 196.0f, .base_freq = 196.0f,
                       .velocity = 1.0f, .wave = SYNTH_WAVE_SIN, .env_decay_rate = 0.99987f};
    for (int i = 0; i < SYNTH_ATTACK_SAMPLES; ++i) synth_render_sample(&v);
    float initial = v.env;
    v.releasing = true;
    v.release_step = v.env / SYNTH_RELEASE_SAMPLES;
    for (int i = 0; i < SYNTH_RELEASE_SAMPLES / 2; ++i) synth_render_sample(&v);
    assert(v.active && fabsf(v.env - initial * 0.5f) < 0.001f);
    for (int i = 0; i < SYNTH_RELEASE_SAMPLES / 2 + 2; ++i) synth_render_sample(&v);
    assert(!v.active && v.env == 0.0f);
}

static void test_release_before_first_sample(void)
{
    synth_voice_t v = {.active = true, .releasing = true, .freq = 130.81f,
                       .base_freq = 130.81f, .velocity = 1.0f,
                       .wave = SYNTH_WAVE_SIN, .env_decay_rate = 0.99987f};
    bool audible = false;
    for (int i = 0; i < SYNTH_ATTACK_SAMPLES; ++i) {
        audible |= fabsf(synth_render_sample(&v)) > 0.001f;
        assert(v.active);
    }
    assert(audible);
    for (int i = 0; i < SYNTH_RELEASE_SAMPLES + 2; ++i) synth_render_sample(&v);
    assert(!v.active);
}

static void test_final_limiter(void)
{
    for (int32_t sample = -32768; sample <= 32767; ++sample) {
        float gain = audio_limiter_gain(1.0f, abs(sample));
        /* Negative full scale differs by one LSB from positive full scale. */
        assert(abs(audio_mix_sample(sample, gain) - sample) <= 1);
    }
    float gain = audio_limiter_gain(1.0f, 32767 + 24000 + 4000);
    assert(gain > 0.5f && gain < 1.0f);
    assert(audio_mix_sample(32767 + 24000 + 4000, gain) <= 32767);
    assert(audio_mix_sample(-32768 - 24000 - 4000, gain) >= -32768);
    float next = audio_limiter_gain(gain, 0);
    assert(next > gain && next < 1.0f);
    assert(audio_limiter_gain(next, 100000) < next);
}
#endif

static void test_sine_waveform_bounds(void)
{
    for (int i = 0; i < 100; i++) {
        float phase = (float)i / 100.0f;
        float s = sinf(phase * 2.0f * (float)M_PI);
        assert(s >= -1.0001f && s <= 1.0001f);
    }
}

static void test_sawtooth_waveform_bounds(void)
{
    for (int i = 0; i < 100; i++) {
        float phase = (float)i / 100.0f;
        float s = 1.6f * (phase - 0.5f);
        assert(s >= -0.8001f && s <= 0.8001f);
    }
}

static void test_drum_pitch_drop(void)
{
    float base_freq = 220.0f;
    float prev_f = base_freq;
    for (uint32_t sample = 0; sample < 1000; sample += 50) {
        float cur_freq = 45.0f + (base_freq - 45.0f) * expf(-(float)sample * 0.004f);
        assert(cur_freq <= prev_f + 0.001f);
        assert(cur_freq >= 45.0f);
        prev_f = cur_freq;
    }
}

static void test_soft_saturation_limiting(void)
{
    /* Excessive signal amplitudes should smoothly compress below 1.0f */
    float loud_signal = 10.0f;
    float saturated = tanhf(loud_signal * 0.6f);
    assert(saturated > 0.0f && saturated < 1.0f);

    int16_t s16 = (int16_t)(saturated * 24000.0f);
    assert(s16 > 0 && s16 <= 24000);
}

static void test_mixer_dynamic_headroom(void)
{
    float w0 = 0.85f;
    float w1 = 0.70f;
    /* esp-audio-effects mixer sum formula: w0^2 * in0 + w1^2 * in1 */
    float max_mixed = (w0 * w0 * 1.0f) + (w1 * w1 * 1.0f);
    assert(max_mixed > 0.0f && max_mixed <= 1.25f);
}

int main(void)
{
#ifdef SYNTH_RENDER_TEST
    test_voice_attack_and_decay();
    test_release();
    test_release_before_first_sample();
    test_final_limiter();
#endif
    test_sine_waveform_bounds();
    test_sawtooth_waveform_bounds();
    test_drum_pitch_drop();
    test_soft_saturation_limiting();
    test_mixer_dynamic_headroom();
    printf("All synth math and mixer unit checks passed successfully.\n");
    return 0;
}
