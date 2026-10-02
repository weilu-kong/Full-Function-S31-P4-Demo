/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYNTH_WAVE_SIN = 0,
    SYNTH_WAVE_SQR,
    SYNTH_WAVE_SAW,
    SYNTH_WAVE_DRUM,
    SYNTH_WAVE_MAX,
} synth_wave_t;

typedef enum {
    SYNTH_MODE_KEY = 0,
    SYNTH_MODE_BT,
    SYNTH_MODE_WEB,
    SYNTH_MODE_MAX,
} synth_mode_t;

typedef enum {
    SYNTH_SFX_CLICK, SYNTH_SFX_CONFIRM, SYNTH_SFX_BACK, SYNTH_SFX_ERROR,
} synth_sfx_t;

typedef struct {
    uint64_t bt_rx_bytes;
    uint64_t bt_played_bytes; /* input-format bytes consumed into successful DAC frames */
    uint64_t bt_dropped_bytes;
    uint64_t bt_flushed_bytes; /* stale FIFO PCM drained on a stream/config transition */
    uint32_t bt_overflow_count;
    uint32_t bt_underflow_count;
    uint32_t bt_short_read_count;
    uint32_t bt_input_rate;
    uint32_t bt_fifo_fill_bytes;
    uint32_t bt_fifo_high_watermark;
    uint32_t codec_write_max_us;
    uint32_t codec_write_errors;
    uint32_t bt_asrc_errors;
    uint32_t command_drop_count;
    uint32_t output_peak;
    uint32_t limited_samples;
} synth_audio_stats_t;

void synth_service_get_audio_stats(synth_audio_stats_t *stats);
void synth_service_note_off(float note_freq);
void synth_service_play_sfx(synth_sfx_t sfx);
float synth_service_get_bt_volume(void);

typedef struct {
    float cutoff_hz;     /* Low-pass cutoff frequency in Hz (200.0f .. 8000.0f) */
    float resonance_q;   /* Filter resonance Q (0.5f .. 5.0f) */
    float reverb_room;   /* Reverb room size (0.0f .. 1.0f) */
    float reverb_damp;   /* Reverb damping factor (0.0f .. 1.0f) */
    float reverb_wet_db; /* Reverb wet level in dB (-40.0f .. 0.0f) */
} synth_fx_params_t;

/**
 * @brief Initialize the synthesizer sound engine, Bluetooth A2DP Sink,
 *        and synth-only EQ/Reverb plus final saturating mix.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t synth_service_init(void);

/**
 * @brief Set whether the synthesizer audio engine is active.
 *
 * Controls the synth scene only; Bluetooth and UI SFX continue on all screens.
 */
void synth_service_set_active(bool active);

/**
 * @brief Trigger a note with specified frequency and velocity.
 *
 * @param note_freq Frequency in Hz.
 * @param velocity Velocity/volume factor [0.0, 1.0].
 */
void synth_service_note_on(float note_freq, float velocity);

/**
 * @brief Set the active oscillator waveform.
 */
void synth_service_set_waveform(synth_wave_t wave);

/**
 * @brief Get the active oscillator waveform.
 */
synth_wave_t synth_service_get_waveform(void);

/**
 * @brief Set the synth mode (KEY, BT, WEB).
 */
void synth_service_set_mode(synth_mode_t mode);

/**
 * @brief Get the synth mode.
 */
synth_mode_t synth_service_get_mode(void);

/**
 * @brief Update DSP audio effects parameters in real time.
 */
void synth_service_set_fx(const synth_fx_params_t *params);

/**
 * @brief Query current DSP audio effects parameters.
 */
void synth_service_get_fx(synth_fx_params_t *params);

/**
 * @brief Initialize Bluetooth A2DP Sink accompaniment receiver.
 *
 * Exposes device as "Yokai-Groovebox" for phone / tablet pairing.
 */
/* Shared IN/OUT codec on mono ES8311 boards; synth owns its lifetime. */
void *synth_service_audio_codec(void);
bool synth_service_bt_supported(void);
esp_err_t synth_service_bt_a2dp_init(void);

/**
 * @brief Query whether a Bluetooth phone/device is currently connected.
 */
bool synth_service_is_bt_connected(void);

/**
 * @brief Query whether Bluetooth accompaniment audio stream is currently playing.
 */
bool synth_service_is_bt_streaming(void);

/**
 * @brief Adjust Bluetooth accompaniment mix volume.
 *
 * @param volume Volume factor [0.0, 1.0].
 */
void synth_service_set_bt_volume(float volume);

/**
 * @brief Set hardware master speaker volume (0 .. 100).
 */
void synth_service_set_master_volume(int volume);

/**
 * @brief Get hardware master speaker volume (0 .. 100).
 */
int synth_service_get_master_volume(void);

/**
 * @brief Query whether Bluetooth radio is enabled.
 */
bool synth_service_is_bt_enabled(void);

/**
 * @brief Enable or disable Bluetooth radio.
 */
void synth_service_set_bt_enabled(bool enabled);

/**
 * @brief Query connected Bluetooth device name and address.
 */
bool synth_service_get_bt_device_info(char *dev_name, size_t max_len, char *bda_str, size_t bda_max_len);

/**
 * @brief Disconnect currently connected Bluetooth A2DP device.
 */
void synth_service_bt_disconnect(void);

/** Play a short acknowledgement tone on the existing speaker path. */
void synth_service_play_feedback_tone(void);

#ifdef __cplusplus
}
#endif
