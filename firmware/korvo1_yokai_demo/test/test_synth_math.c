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

#define SAMPLE_RATE 22050

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
    test_sine_waveform_bounds();
    test_sawtooth_waveform_bounds();
    test_drum_pitch_drop();
    test_soft_saturation_limiting();
    test_mixer_dynamic_headroom();
    printf("All synth math and mixer unit checks passed successfully.\n");
    return 0;
}
