/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "synth_service.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "bsp/esp32_s31_korvo_1.h"
#include "esp_codec_dev.h"

/* Bluetooth Classic & A2DP Sink headers */
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"

/* ESP-Audio-Effects modules */
#include "esp_ae_mixer.h"
#include "esp_ae_eq.h"
#include "esp_ae_reverb.h"
#include "esp_ae_alc.h"

static const char *TAG = "synth_service";

#define SYNTH_SAMPLE_RATE       44100
#define SYNTH_CHANNELS          2
#define SYNTH_BITS_PER_SAMPLE   16
#define SYNTH_CHUNK_SAMPLES     256
#define SYNTH_MAX_VOICES        4
#define BT_RINGBUF_SIZE         16384

typedef struct {
    bool active;
    float freq;
    float base_freq;
    float phase;
    float env;
    float env_decay_rate;
    float velocity;
    synth_wave_t wave;
    uint32_t sample_index;
} synth_voice_t;

static synth_voice_t s_voices[SYNTH_MAX_VOICES];
static synth_wave_t s_current_wave = SYNTH_WAVE_SIN;
static synth_mode_t s_current_mode = SYNTH_MODE_KEY;
static bool s_active = false;
static bool s_inited = false;
static bool s_bt_inited = false;
static volatile bool s_bt_connected = false;
static volatile bool s_bt_streaming = false;
static float s_bt_volume = 0.75f;

static SemaphoreHandle_t s_synth_mutex = NULL;
static RingbufHandle_t s_bt_ringbuf = NULL;

static esp_codec_dev_handle_t s_speaker_dev = NULL;
static esp_ae_mixer_handle_t s_mixer_handle = NULL;
static esp_ae_eq_handle_t s_eq_handle = NULL;
static esp_ae_reverb_handle_t s_reverb_handle = NULL;
static esp_ae_alc_handle_t s_alc_handle = NULL;

static esp_ae_mixer_info_t s_mixer_info[2];
static esp_ae_eq_filter_para_t s_eq_filters[2];
static synth_fx_params_t s_fx = {
    .cutoff_hz = 2800.0f,
    .resonance_q = 1.2f,
    .reverb_room = 0.55f,
    .reverb_damp = 0.35f,
    .reverb_wet_db = -12.0f,
};

static int16_t s_synth_buf[SYNTH_CHUNK_SAMPLES * SYNTH_CHANNELS] __attribute__((aligned(16)));
static int16_t s_bt_buf[SYNTH_CHUNK_SAMPLES * SYNTH_CHANNELS] __attribute__((aligned(16)));
static int16_t s_chunk_buf[SYNTH_CHUNK_SAMPLES * SYNTH_CHANNELS] __attribute__((aligned(16)));

static void synth_audio_task(void *arg);
static void bt_a2dp_data_cb(const uint8_t *data, uint32_t len);
static void bt_a2dp_event_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

esp_err_t synth_service_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    s_synth_mutex = xSemaphoreCreateMutex();
    if (s_synth_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create synth mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(s_voices, 0, sizeof(s_voices));

    /* 1. Initialize speaker codec device from BSP at 44.1 kHz */
    i2s_std_config_t i2s_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SYNTH_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    (void)bsp_i2c_init();
    (void)bsp_audio_init(&i2s_cfg);

    s_speaker_dev = bsp_audio_codec_speaker_init();
    if (s_speaker_dev != NULL) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = SYNTH_BITS_PER_SAMPLE,
            .channel = SYNTH_CHANNELS,
            .sample_rate = SYNTH_SAMPLE_RATE,
        };
        esp_err_t ret = esp_codec_dev_open(s_speaker_dev, &fs);
        if (ret == ESP_CODEC_DEV_OK) {
            esp_codec_dev_set_out_vol(s_speaker_dev, 80);
            ESP_LOGI(TAG, "Korvo-1 speaker codec opened @ %d Hz stereo", SYNTH_SAMPLE_RATE);
        } else {
            ESP_LOGW(TAG, "Failed to open speaker codec (err: %d), continuing in software mode", ret);
        }
    } else {
        ESP_LOGW(TAG, "Speaker codec handle NULL, audio will run in headless mode");
    }

    /* 2. Configure and open esp-audio-effects Mixer (Synth + BT Accompaniment) */
    s_mixer_info[0].weight1 = 0.85f;
    s_mixer_info[0].weight2 = 0.85f;
    s_mixer_info[0].transit_time = 20;

    s_mixer_info[1].weight1 = 0.70f;
    s_mixer_info[1].weight2 = 0.70f;
    s_mixer_info[1].transit_time = 20;

    esp_ae_mixer_cfg_t mixer_cfg = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .channel = SYNTH_CHANNELS,
        .bits_per_sample = SYNTH_BITS_PER_SAMPLE,
        .src_num = 2,
        .src_info = s_mixer_info,
    };
    esp_ae_err_t ae_ret = esp_ae_mixer_open(&mixer_cfg, &s_mixer_handle);
    if (ae_ret == ESP_AE_ERR_OK) {
        ESP_LOGI(TAG, "esp_audio_effects Mixer initialized");
    } else {
        ESP_LOGE(TAG, "Failed to init esp_audio_effects Mixer: %d", ae_ret);
    }

    /* 3. Configure and open esp-audio-effects Equalizer (LPF Cutoff + Timbre Peak) */
    s_eq_filters[0].filter_type = ESP_AE_EQ_FILTER_LOW_PASS;
    s_eq_filters[0].fc = (uint32_t)s_fx.cutoff_hz;
    s_eq_filters[0].q = s_fx.resonance_q;
    s_eq_filters[0].gain = 0.0f;

    s_eq_filters[1].filter_type = ESP_AE_EQ_FILTER_PEAK;
    s_eq_filters[1].fc = 1200;
    s_eq_filters[1].q = 1.0f;
    s_eq_filters[1].gain = 2.0f;

    esp_ae_eq_cfg_t eq_cfg = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .channel = SYNTH_CHANNELS,
        .bits_per_sample = SYNTH_BITS_PER_SAMPLE,
        .filter_num = 2,
        .para = s_eq_filters,
    };
    ae_ret = esp_ae_eq_open(&eq_cfg, &s_eq_handle);
    if (ae_ret == ESP_AE_ERR_OK) {
        ESP_LOGI(TAG, "esp_audio_effects EQ initialized (LPF fc=%d Hz, q=%.1f)",
                 (int)s_fx.cutoff_hz, s_fx.resonance_q);
    } else {
        ESP_LOGE(TAG, "Failed to init esp_audio_effects EQ: %d", ae_ret);
    }

    /* 4. Configure and open esp-audio-effects Reverb */
    esp_ae_reverb_para_t rev_para = {
        .room_size = s_fx.reverb_room,
        .damping = s_fx.reverb_damp,
        .wet_level = s_fx.reverb_wet_db,
        .dry_level = 0.0f,
        .pre_delay_ms = 25,
    };
    esp_ae_reverb_cfg_t rev_cfg = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .channel = SYNTH_CHANNELS,
        .bits_per_sample = SYNTH_BITS_PER_SAMPLE,
        .reverb_para = rev_para,
    };
    ae_ret = esp_ae_reverb_open(&rev_cfg, &s_reverb_handle);
    if (ae_ret == ESP_AE_ERR_OK) {
        ESP_LOGI(TAG, "esp_audio_effects Reverb initialized (room=%.2f, wet=%.1fdB)",
                 s_fx.reverb_room, s_fx.reverb_wet_db);
    } else {
        ESP_LOGE(TAG, "Failed to init esp_audio_effects Reverb: %d", ae_ret);
    }

    /* 5. Configure and open esp-audio-effects Automatic Level Control (ALC) */
    esp_ae_alc_cfg_t alc_cfg = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .channel = SYNTH_CHANNELS,
        .bits_per_sample = SYNTH_BITS_PER_SAMPLE,
    };
    ae_ret = esp_ae_alc_open(&alc_cfg, &s_alc_handle);
    if (ae_ret == ESP_AE_ERR_OK) {
        esp_ae_alc_set_gain(s_alc_handle, 0, 0);
        esp_ae_alc_set_gain(s_alc_handle, 1, 0);
        esp_ae_alc_set_transit_time(s_alc_handle, 15);
        ESP_LOGI(TAG, "esp_audio_effects ALC limiter initialized");
    } else {
        ESP_LOGE(TAG, "Failed to init esp_audio_effects ALC: %d", ae_ret);
    }

    /* 6. Initialize Bluetooth A2DP Sink Accompaniment */
    (void)synth_service_bt_a2dp_init();

    /* 7. Start audio synthesis & mixing thread */
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        synth_audio_task,
        "synth_audio",
        4096,
        NULL,
        10,
        NULL,
        1
    );
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create synth audio task");
        return ESP_FAIL;
    }

    s_inited = true;
    ESP_LOGI(TAG, "Yokai Pocket Synth & Groovebox engine ready");
    return ESP_OK;
}

static void bt_a2dp_data_cb(const uint8_t *data, uint32_t len)
{
    if (s_bt_ringbuf != NULL && data != NULL && len > 0) {
        xRingbufferSend(s_bt_ringbuf, data, len, 0);
    }
}

static void bt_a2dp_event_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            s_bt_connected = true;
            ESP_LOGI(TAG, "A2DP accompaniment connected from: %02x:%02x:%02x:%02x:%02x:%02x",
                     param->conn_stat.remote_bda[0], param->conn_stat.remote_bda[1],
                     param->conn_stat.remote_bda[2], param->conn_stat.remote_bda[3],
                     param->conn_stat.remote_bda[4], param->conn_stat.remote_bda[5]);
        } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            s_bt_connected = false;
            s_bt_streaming = false;
            ESP_LOGI(TAG, "A2DP accompaniment disconnected");
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        }
        break;

    case ESP_A2D_AUDIO_STATE_EVT:
        s_bt_streaming = (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED);
        ESP_LOGI(TAG, "A2DP audio stream state: %s", s_bt_streaming ? "STARTED" : "SUSPENDED");
        break;

    default:
        break;
    }
}

esp_err_t synth_service_bt_a2dp_init(void)
{
    if (s_bt_inited) {
        return ESP_OK;
    }

    if (s_bt_ringbuf == NULL) {
        s_bt_ringbuf = xRingbufferCreateWithCaps(BT_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_bt_ringbuf == NULL) {
            ESP_LOGE(TAG, "Failed to create BT ring buffer in PSRAM");
            return ESP_ERR_NO_MEM;
        }
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret == ESP_OK) {
        ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "esp_bt_controller_enable: %d", ret);
        }
    } else if (ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_bt_controller_init: %d", ret);
    }

    esp_bluedroid_status_t bstatus = esp_bluedroid_get_status();
    if (bstatus == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        ret = esp_bluedroid_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_bluedroid_init failed: %d", ret);
            return ret;
        }
    }
    bstatus = esp_bluedroid_get_status();
    if (bstatus != ESP_BLUEDROID_STATUS_ENABLED) {
        ret = esp_bluedroid_enable();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_bluedroid_enable failed: %d", ret);
            return ret;
        }
    }

    esp_bt_gap_set_device_name("Yokai-Groovebox");
    esp_a2d_register_callback(bt_a2dp_event_cb);
    esp_a2d_sink_register_data_callback(bt_a2dp_data_cb);
    esp_a2d_sink_init();
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    s_bt_inited = true;
    ESP_LOGI(TAG, "Bluetooth A2DP Sink initialized (device name: Yokai-Groovebox)");
    return ESP_OK;
}

bool synth_service_is_bt_connected(void)
{
    return s_bt_connected;
}

bool synth_service_is_bt_streaming(void)
{
    return s_bt_streaming;
}

void synth_service_set_bt_volume(float volume)
{
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    s_bt_volume = volume;
}

void synth_service_set_active(bool active)
{
    if (s_synth_mutex == NULL) {
        s_active = active;
        return;
    }
    if (xSemaphoreTake(s_synth_mutex, portMAX_DELAY) == pdTRUE) {
        s_active = active;
        if (!active) {
            /* Silence any lingering voice notes when leaving */
            for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
                s_voices[i].active = false;
            }
        }
        xSemaphoreGive(s_synth_mutex);
    }
    ESP_LOGI(TAG, "Synth active state: %s", active ? "ON" : "OFF");
}

void synth_service_note_on(float note_freq, float velocity)
{
    if (note_freq <= 0.0f) {
        return;
    }

    if (xSemaphoreTake(s_synth_mutex, portMAX_DELAY) == pdTRUE) {
        /* Allocate a voice: find idle voice or steal lowest envelope */
        int chosen_idx = 0;
        float min_env = 999.0f;
        for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
            if (!s_voices[i].active) {
                chosen_idx = i;
                break;
            }
            if (s_voices[i].env < min_env) {
                min_env = s_voices[i].env;
                chosen_idx = i;
            }
        }

        synth_voice_t *v = &s_voices[chosen_idx];
        v->active = true;
        v->freq = note_freq;
        v->base_freq = note_freq;
        v->phase = 0.0f;
        v->env = 0.0f; /* 5ms ramp-up prevents click */
        v->velocity = (velocity > 1.0f) ? 1.0f : (velocity < 0.1f ? 0.8f : velocity);
        v->wave = s_current_wave;
        v->sample_index = 0;

        if (v->wave == SYNTH_WAVE_DRUM) {
            /* Yokai Taiko drum: punchy ~300ms decay at 44.1kHz */
            v->env_decay_rate = 0.99967f;
        } else {
            /* Melodic instrument: warm ~650ms decay at 44.1kHz */
            v->env_decay_rate = 0.99987f;
        }

        xSemaphoreGive(s_synth_mutex);
    }
}

void synth_service_set_waveform(synth_wave_t wave)
{
    if (wave >= SYNTH_WAVE_MAX) {
        return;
    }
    if (xSemaphoreTake(s_synth_mutex, portMAX_DELAY) == pdTRUE) {
        s_current_wave = wave;
        xSemaphoreGive(s_synth_mutex);
    }
    ESP_LOGI(TAG, "Synth waveform switched to: %d", (int)wave);
}

synth_wave_t synth_service_get_waveform(void)
{
    return s_current_wave;
}

void synth_service_set_mode(synth_mode_t mode)
{
    if (mode >= SYNTH_MODE_MAX) {
        return;
    }
    s_current_mode = mode;
    ESP_LOGI(TAG, "Synth mode switched to: %d", (int)mode);
}

synth_mode_t synth_service_get_mode(void)
{
    return s_current_mode;
}

void synth_service_set_fx(const synth_fx_params_t *params)
{
    if (params == NULL) {
        return;
    }
    if (xSemaphoreTake(s_synth_mutex, portMAX_DELAY) == pdTRUE) {
        s_fx = *params;

        if (s_eq_handle) {
            esp_ae_eq_filter_para_t para = {
                .filter_type = ESP_AE_EQ_FILTER_LOW_PASS,
                .fc = (uint32_t)s_fx.cutoff_hz,
                .q = s_fx.resonance_q,
                .gain = 0.0f,
            };
            esp_ae_eq_set_filter_para(s_eq_handle, 0, &para);
        }

        if (s_reverb_handle) {
            esp_ae_reverb_set_room_size(s_reverb_handle, s_fx.reverb_room);
            esp_ae_reverb_set_damping(s_reverb_handle, s_fx.reverb_damp);
        }

        xSemaphoreGive(s_synth_mutex);
    }
}

void synth_service_get_fx(synth_fx_params_t *params)
{
    if (params != NULL) {
        *params = s_fx;
    }
}

static inline float synth_render_sample(synth_voice_t *v)
{
    /* 5ms (~220 samples @ 44.1kHz) linear attack to eliminate transients */
    if (v->sample_index < 220) {
        v->env = (float)v->sample_index / 220.0f;
    } else {
        v->env *= v->env_decay_rate;
    }

    if (v->env < 0.001f) {
        v->active = false;
        return 0.0f;
    }

    float out = 0.0f;
    switch (v->wave) {
    case SYNTH_WAVE_SIN:
        out = sinf(v->phase * 2.0f * (float)M_PI);
        break;

    case SYNTH_WAVE_SQR:
        out = (v->phase < 0.5f) ? 0.75f : -0.75f;
        break;

    case SYNTH_WAVE_SAW:
        out = 1.6f * (v->phase - 0.5f);
        break;

    case SYNTH_WAVE_DRUM: {
        /* Yokai Taiko Drum: exponential pitch drop from base down to 45Hz sub */
        float cur_freq = 45.0f + (v->base_freq - 45.0f) * expf(-(float)v->sample_index * 0.002f);
        v->phase += cur_freq / (float)SYNTH_SAMPLE_RATE;
        if (v->phase >= 1.0f) {
            v->phase -= 1.0f;
        }

        float body = sinf(v->phase * 2.0f * (float)M_PI);
        float transient = 0.0f;
        if (v->sample_index < 500) {
            float noise = ((float)(rand() % 2000 - 1000) / 1000.0f);
            transient = noise * (1.0f - (float)v->sample_index / 500.0f) * 0.65f;
        }
        out = (body * 1.2f + transient);
        v->sample_index++;
        return out * v->env * v->velocity;
    }

    default:
        out = sinf(v->phase * 2.0f * (float)M_PI);
        break;
    }

    v->phase += v->freq / (float)SYNTH_SAMPLE_RATE;
    if (v->phase >= 1.0f) {
        v->phase -= 1.0f;
    }
    v->sample_index++;

    return out * v->env * v->velocity;
}

static void synth_audio_task(void *arg)
{
    ESP_LOGI(TAG, "Groovebox real-time audio task running @ %d Hz", SYNTH_SAMPLE_RATE);

    while (1) {
        if (!s_active) {
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }

        bool has_active_voice = false;

        /* 1. Render synthesizer keyboard / drum notes */
        if (xSemaphoreTake(s_synth_mutex, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
                if (s_voices[i].active) {
                    has_active_voice = true;
                    break;
                }
            }

            if (has_active_voice) {
                for (int s = 0; s < SYNTH_CHUNK_SAMPLES; s++) {
                    float sample_sum = 0.0f;
                    for (int v = 0; v < SYNTH_MAX_VOICES; v++) {
                        if (s_voices[v].active) {
                            sample_sum += synth_render_sample(&s_voices[v]);
                        }
                    }

                    float saturated = tanhf(sample_sum * 0.6f);
                    int16_t sample_s16 = (int16_t)(saturated * 24000.0f);

                    s_synth_buf[s * 2] = sample_s16;
                    s_synth_buf[s * 2 + 1] = sample_s16;
                }
            } else {
                memset(s_synth_buf, 0, sizeof(s_synth_buf));
            }
            xSemaphoreGive(s_synth_mutex);
        }

        /* 2. Retrieve Bluetooth A2DP accompaniment stream from ring buffer */
        bool has_bt_chunk = false;
        if (s_bt_ringbuf != NULL) {
            size_t bytes_needed = SYNTH_CHUNK_SAMPLES * SYNTH_CHANNELS * sizeof(int16_t);
            size_t received_bytes = 0;
            uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(s_bt_ringbuf, &received_bytes, 0, bytes_needed);
            if (item != NULL && received_bytes > 0) {
                memcpy(s_bt_buf, item, received_bytes);
                if (received_bytes < bytes_needed) {
                    memset((uint8_t *)s_bt_buf + received_bytes, 0, bytes_needed - received_bytes);
                }
                vRingbufferReturnItem(s_bt_ringbuf, item);

                /* Apply accompaniment volume scaling */
                if (s_bt_volume < 0.99f) {
                    for (int s = 0; s < SYNTH_CHUNK_SAMPLES * SYNTH_CHANNELS; s++) {
                        s_bt_buf[s] = (int16_t)((float)s_bt_buf[s] * s_bt_volume);
                    }
                }
                has_bt_chunk = true;
            } else {
                memset(s_bt_buf, 0, sizeof(s_bt_buf));
            }
        } else {
            memset(s_bt_buf, 0, sizeof(s_bt_buf));
        }

        /* 3. Mix Synth + Bluetooth accompaniment using esp_ae_mixer */
        if (has_active_voice || has_bt_chunk) {
            esp_ae_sample_t in_ptrs[2] = { s_synth_buf, s_bt_buf };
            if (s_mixer_handle != NULL) {
                esp_ae_mixer_process(s_mixer_handle, SYNTH_CHUNK_SAMPLES, in_ptrs, s_chunk_buf);
            } else {
                for (int s = 0; s < SYNTH_CHUNK_SAMPLES * SYNTH_CHANNELS; s++) {
                    int32_t mixed = (int32_t)s_synth_buf[s] + (int32_t)s_bt_buf[s];
                    if (mixed > 32767) mixed = 32767;
                    else if (mixed < -32768) mixed = -32768;
                    s_chunk_buf[s] = (int16_t)mixed;
                }
            }

            /* 4. Filter & Effects chain: EQ -> Reverb -> ALC Limiter -> DAC */
            if (s_eq_handle != NULL) {
                esp_ae_eq_process(s_eq_handle, SYNTH_CHUNK_SAMPLES, s_chunk_buf, s_chunk_buf);
            }
            if (s_reverb_handle != NULL) {
                esp_ae_reverb_process(s_reverb_handle, SYNTH_CHUNK_SAMPLES, s_chunk_buf, s_chunk_buf);
            }
            if (s_alc_handle != NULL) {
                esp_ae_alc_process(s_alc_handle, SYNTH_CHUNK_SAMPLES, s_chunk_buf, s_chunk_buf);
            }

            if (s_speaker_dev != NULL) {
                esp_codec_dev_write(s_speaker_dev, s_chunk_buf, sizeof(s_chunk_buf));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        } else {
            memset(s_chunk_buf, 0, sizeof(s_chunk_buf));
            if (s_speaker_dev != NULL) {
                esp_codec_dev_write(s_speaker_dev, s_chunk_buf, sizeof(s_chunk_buf));
            } else {
                vTaskDelay(pdMS_TO_TICKS(15));
            }
        }
    }
}
