/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "synth_service.h"
#include "voice_service.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_asrc.h"
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
#include "esp_ae_eq.h"
#include "esp_ae_reverb.h"

static const char *TAG = "synth_service";

#define SYNTH_SAMPLE_RATE       44100
#define SYNTH_CHANNELS          2
#define SYNTH_BITS_PER_SAMPLE   16
#define SYNTH_CHUNK_SAMPLES     256
#define SYNTH_MAX_VOICES        4
#define SYNTH_ATTACK_SAMPLES    ((SYNTH_SAMPLE_RATE * 5) / 1000)
#define SYNTH_RELEASE_SAMPLES   ((SYNTH_SAMPLE_RATE * 40) / 1000)
#define BT_RINGBUF_SIZE         (40 * 1024)
#define BT_RESAMPLED_FRAMES     1024
#define AUDIO_COMMAND_COUNT     32

typedef struct {
    bool active;
    bool releasing;
    float release_step;
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
static volatile bool s_bt_enabled = true;
typedef enum { AUDIO_NOTE_ON, AUDIO_NOTE_OFF, AUDIO_SFX } audio_command_type_t;
typedef struct {
    audio_command_type_t type;
    float freq;
    float velocity;
    synth_wave_t wave;
    synth_sfx_t sfx;
} audio_command_t;
static QueueHandle_t s_audio_commands;
static portMUX_TYPE s_bt_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_bt_epoch;
static uint32_t s_bt_input_rate = SYNTH_SAMPLE_RATE;
static uint8_t s_bt_input_channels = 2;
static synth_audio_stats_t s_stats;
/* The following buffers and ASRC state belong exclusively to the audio task. */
static esp_asrc_handle_t s_bt_asrc;
static uint32_t s_bt_consumer_epoch;
static uint32_t s_bt_consumer_rate;
static uint8_t s_bt_consumer_channels;
static bool s_bt_prebuffered;
static bool s_bt_asrc_failed;
static size_t s_bt_input_bytes;
static uint32_t s_bt_output_frames;
static int16_t s_bt_input[SYNTH_CHUNK_SAMPLES * SYNTH_CHANNELS];
static int16_t s_bt_output[BT_RESAMPLED_FRAMES * SYNTH_CHANNELS];
static esp_bd_addr_t s_remote_bda = {0};
static float s_bt_volume = 1.0f;
static int s_master_volume = 80;

static SemaphoreHandle_t s_synth_mutex = NULL;
static RingbufHandle_t s_bt_ringbuf = NULL;

static esp_codec_dev_handle_t s_speaker_dev = NULL;
static esp_ae_eq_handle_t s_eq_handle = NULL;
static esp_ae_reverb_handle_t s_reverb_handle = NULL;

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
static int32_t s_mix_buf[SYNTH_CHUNK_SAMPLES * SYNTH_CHANNELS];
static int16_t s_chunk_buf[SYNTH_CHUNK_SAMPLES * SYNTH_CHANNELS] __attribute__((aligned(16)));

static uint32_t bt_fifo_fill(void);
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

    s_audio_commands = xQueueCreate(AUDIO_COMMAND_COUNT, sizeof(audio_command_t));
    if (s_audio_commands == NULL) return ESP_ERR_NO_MEM;
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

    esp_ae_err_t ae_ret;

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
    if (!s_bt_enabled || s_bt_ringbuf == NULL || data == NULL || len == 0) return;
    /* BYTEBUF is a bounded SPSC byte FIFO. Send is all-or-nothing, preserving
       sample alignment even on overflow; only the consumer assembles frames. */
    bool accepted = xRingbufferSend(s_bt_ringbuf, data, len, 0) == pdTRUE;
    uint32_t fill = bt_fifo_fill();
    portENTER_CRITICAL(&s_bt_lock);
    if (fill > s_stats.bt_fifo_high_watermark) s_stats.bt_fifo_high_watermark = fill;
    s_stats.bt_rx_bytes += len;
    if (!accepted) {
        ++s_stats.bt_overflow_count;
        s_stats.bt_dropped_bytes += len;
    }
    portEXIT_CRITICAL(&s_bt_lock);
}

/* GAP queues asynchronously. If a toggle overtakes this call, enqueue the
 * current setting again; never hold the audio spinlock across a GAP API. */
static void bt_update_scan_mode(void)
{
    bool enabled;
    do {
        enabled = s_bt_enabled;
        esp_bt_gap_set_scan_mode(enabled ? ESP_BT_CONNECTABLE : ESP_BT_NON_CONNECTABLE,
                                 enabled ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
    } while (enabled != s_bt_enabled);
}

static void bt_a2dp_event_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            portENTER_CRITICAL(&s_bt_lock);
            bool accepted = s_bt_enabled;
            if (accepted) {
                s_bt_connected = true;
                memcpy(s_remote_bda, param->conn_stat.remote_bda, sizeof(esp_bd_addr_t));
            }
            portEXIT_CRITICAL(&s_bt_lock);
            if (!accepted) {
                esp_a2d_sink_disconnect(param->conn_stat.remote_bda);
                break;
            }
            ESP_LOGI(TAG, "A2DP accompaniment connected from: %02x:%02x:%02x:%02x:%02x:%02x",
                     param->conn_stat.remote_bda[0], param->conn_stat.remote_bda[1],
                     param->conn_stat.remote_bda[2], param->conn_stat.remote_bda[3],
                     param->conn_stat.remote_bda[4], param->conn_stat.remote_bda[5]);
        } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            portENTER_CRITICAL(&s_bt_lock);
            s_bt_connected = false;
            s_bt_streaming = false;
            ++s_bt_epoch;
            memset(s_remote_bda, 0, sizeof(esp_bd_addr_t));
            portEXIT_CRITICAL(&s_bt_lock);
            ESP_LOGI(TAG, "A2DP accompaniment disconnected");
            bt_update_scan_mode();
        }
        break;

    case ESP_A2D_AUDIO_STATE_EVT:
        portENTER_CRITICAL(&s_bt_lock);
        s_bt_streaming = s_bt_enabled && s_bt_connected &&
                         param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED;
        ++s_bt_epoch;
        portEXIT_CRITICAL(&s_bt_lock);
        ESP_LOGI(TAG, "A2DP audio stream state: %s", s_bt_streaming ? "STARTED" : "SUSPENDED");
        break;

    case ESP_A2D_AUDIO_CFG_EVT: {
        const esp_a2d_mcc_t *mcc = &param->audio_cfg.mcc;
        uint32_t rate = 0;
        uint8_t channels = 2;
        if (mcc->type == ESP_A2D_MCT_SBC) {
            switch (mcc->cie.sbc_info.samp_freq) {
            case ESP_A2D_SBC_CIE_SF_44K: rate = 44100; break;
            case ESP_A2D_SBC_CIE_SF_48K: rate = 48000; break;
            case ESP_A2D_SBC_CIE_SF_32K: rate = 32000; break;
            case ESP_A2D_SBC_CIE_SF_16K: rate = 16000; break;
            default: break;
            }
            channels = mcc->cie.sbc_info.ch_mode == ESP_A2D_SBC_CIE_CH_MODE_MONO ? 1 : 2;
        }
        portENTER_CRITICAL(&s_bt_lock);
        if (!s_bt_enabled) {
            portEXIT_CRITICAL(&s_bt_lock);
            break;
        }
        s_bt_input_rate = rate;
        s_bt_input_channels = channels;
        ++s_bt_epoch;
        portEXIT_CRITICAL(&s_bt_lock);
        if (rate == 0) {
            ESP_LOGE(TAG, "[AUDIO] Unsupported A2DP codec/config type=%u", mcc->type);
        } else {
            ESP_LOGI(TAG, "[AUDIO] A2DP config codec=SBC rate=%" PRIu32 " channels=%u", rate, channels);
        }
        break;
    }
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
    bt_update_scan_mode();

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
    if (!isfinite(volume)) return;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    s_bt_volume = volume;
}

void synth_service_set_master_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    s_master_volume = volume;
    if (s_speaker_dev != NULL) {
        esp_codec_dev_set_out_vol(s_speaker_dev, volume);
    }
}

int synth_service_get_master_volume(void)
{
    return s_master_volume;
}

bool synth_service_is_bt_enabled(void)
{
    return s_bt_enabled;
}

void synth_service_set_bt_enabled(bool enabled)
{
    esp_bd_addr_t remote;
    portENTER_CRITICAL(&s_bt_lock);
    s_bt_enabled = enabled;
    bool disconnect = !enabled && s_bt_connected;
    if (disconnect) memcpy(remote, s_remote_bda, sizeof(remote));
    if (!enabled) {
        s_bt_streaming = false;
        ++s_bt_epoch;
    }
    portEXIT_CRITICAL(&s_bt_lock);
    if (enabled) {
        if (!s_bt_inited) {
            synth_service_bt_a2dp_init();
        } else {
            bt_update_scan_mode();
        }
    } else {
        if (disconnect) esp_a2d_sink_disconnect(remote);
        bt_update_scan_mode();
    }
}

bool synth_service_get_bt_device_info(char *dev_name, size_t max_len, char *bda_str, size_t bda_max_len)
{
    if (!s_bt_connected) {
        return false;
    }
    if (dev_name && max_len > 0) {
        snprintf(dev_name, max_len, "Bluetooth 音声端末");
    }
    if (bda_str && bda_max_len > 0) {
        snprintf(bda_str, bda_max_len, "%02X:%02X:%02X:%02X:%02X:%02X",
                 s_remote_bda[0], s_remote_bda[1], s_remote_bda[2],
                 s_remote_bda[3], s_remote_bda[4], s_remote_bda[5]);
    }
    return true;
}

void synth_service_bt_disconnect(void)
{
    if (s_bt_connected) {
        esp_a2d_sink_disconnect(s_remote_bda);
    }
}

static void queue_audio_command(const audio_command_t *command)
{
    if (s_audio_commands == NULL) return;
    if (xQueueSend(s_audio_commands, command, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_bt_lock);
        ++s_stats.command_drop_count;
        portEXIT_CRITICAL(&s_bt_lock);
    }
}

void synth_service_play_sfx(synth_sfx_t sfx)
{
    if (sfx < SYNTH_SFX_CLICK || sfx > SYNTH_SFX_ERROR) return;
    audio_command_t command = {.type = AUDIO_SFX, .sfx = sfx};
    queue_audio_command(&command);
}

void synth_service_play_feedback_tone(void)
{
    synth_service_play_sfx(SYNTH_SFX_CONFIRM);
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
                s_voices[i].releasing = true;
                s_voices[i].release_step = s_voices[i].env / SYNTH_RELEASE_SAMPLES;
            }
        }
        xSemaphoreGive(s_synth_mutex);
    }
    ESP_LOGI(TAG, "Synth active state: %s", active ? "ON" : "OFF");
}

static void start_note(float note_freq, float velocity, synth_wave_t wave)
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
        v->releasing = false;
        v->release_step = 0.0f;
        v->freq = note_freq;
        v->base_freq = note_freq;
        v->phase = 0.0f;
        v->env = 0.0f; /* 5ms ramp-up prevents click */
        v->velocity = (velocity > 1.0f) ? 1.0f : (velocity < 0.1f ? 0.8f : velocity);
        v->wave = wave;
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

void synth_service_note_on(float note_freq, float velocity)
{
    if (!isfinite(note_freq) || note_freq <= 0.0f || note_freq >= SYNTH_SAMPLE_RATE / 2 ||
        !isfinite(velocity)) return;
    audio_command_t command = {.type = AUDIO_NOTE_ON, .freq = note_freq,
                               .velocity = velocity, .wave = s_current_wave};
    queue_audio_command(&command);
}

void synth_service_note_off(float note_freq)
{
    if (!isfinite(note_freq) || note_freq <= 0.0f) return;
    audio_command_t command = {.type = AUDIO_NOTE_OFF, .freq = note_freq};
    queue_audio_command(&command);
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
    /* A queued press/release may precede sample zero. Complete the short attack
       even then, so quick taps cannot become silent zero-envelope releases. */
    if (v->sample_index < SYNTH_ATTACK_SAMPLES) {
        v->env = ((float)v->sample_index + 1.0f) / SYNTH_ATTACK_SAMPLES;
        if (v->releasing) v->release_step = v->env / SYNTH_RELEASE_SAMPLES;
    } else if (v->releasing) {
        v->env -= v->release_step;
        if (v->env <= 0.0f) {
            v->env = 0.0f;
            v->active = false;
            return 0.0f;
        }
    } else {
        v->env *= v->env_decay_rate;
        if (v->env < 0.001f) {
            v->active = false;
            return 0.0f;
        }
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

static uint32_t bt_fifo_fill(void)
{
    UBaseType_t bytes = 0;
    if (s_bt_ringbuf) vRingbufferGetInfo(s_bt_ringbuf, NULL, NULL, NULL, NULL, &bytes);
    return bytes;
}

void synth_service_get_audio_stats(synth_audio_stats_t *stats)
{
    if (stats == NULL) return;
    uint32_t fill = bt_fifo_fill();
    portENTER_CRITICAL(&s_bt_lock);
    *stats = s_stats;
    stats->bt_input_rate = s_bt_input_rate;
    stats->bt_fifo_fill_bytes = fill;
    portEXIT_CRITICAL(&s_bt_lock);
}

float synth_service_get_bt_volume(void)
{
    return s_bt_volume;
}

/* Called by the consumer only, including on suspend/config changes. No reset
   of a FreeRTOS buffer while its producer might be inside a send. */
static void bt_reset_consumer(uint32_t epoch, uint32_t rate, uint8_t channels)
{
    if (s_bt_asrc) esp_asrc_close(s_bt_asrc);
    s_bt_asrc = NULL;
    s_bt_consumer_epoch = epoch;
    s_bt_consumer_rate = rate;
    s_bt_consumer_channels = channels;
    s_bt_prebuffered = false;
    s_bt_asrc_failed = false;
    s_bt_input_bytes = 0;
    s_bt_output_frames = 0;
    size_t discard = bt_fifo_fill();
    while (discard) {
        size_t bytes = 0;
        void *item = xRingbufferReceiveUpTo(s_bt_ringbuf, &bytes, 0, discard);
        if (item == NULL) break;
        portENTER_CRITICAL(&s_bt_lock);
        s_stats.bt_flushed_bytes += bytes;
        portEXIT_CRITICAL(&s_bt_lock);
        discard -= bytes;
        vRingbufferReturnItem(s_bt_ringbuf, item);
    }
    if (rate && (rate != SYNTH_SAMPLE_RATE || channels != SYNTH_CHANNELS)) {
        esp_asrc_cfg_t cfg = {
            .src_info = {.sample_rate = rate, .channel = channels, .bits_per_sample = 16},
            .dest_info = {.sample_rate = SYNTH_SAMPLE_RATE, .channel = SYNTH_CHANNELS, .bits_per_sample = 16},
            /* Voice owns both S31 hardware ASRC streams. */
            .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
            .complexity = 3,
        };
        esp_asrc_err_t err = esp_asrc_open(&cfg, &s_bt_asrc);
        if (err != ESP_ASRC_ERR_OK) {
            s_bt_asrc_failed = true;
            portENTER_CRITICAL(&s_bt_lock);
            ++s_stats.bt_asrc_errors;
            portEXIT_CRITICAL(&s_bt_lock);
            ESP_LOGE(TAG, "[AUDIO] BT ASRC open failed err=%d input=%" PRIu32, err, rate);
        } else {
            ESP_LOGI(TAG, "[AUDIO] BT ASRC software %" PRIu32 " Hz/%u ch -> 44100 Hz/stereo", rate, channels);
        }
    }
}

/* Keep incomplete input AND fractional ASRC output across calls. A short
   wrapped byte-buffer read is not an underflow and never pads a PCM frame. */
static bool bt_epoch_matches(uint32_t epoch)
{
    portENTER_CRITICAL(&s_bt_lock);
    bool matches = epoch == s_bt_epoch;
    portEXIT_CRITICAL(&s_bt_lock);
    return matches;
}

static bool bt_render_chunk(void)
{
    uint32_t epoch, rate;
    uint8_t channels;
    portENTER_CRITICAL(&s_bt_lock);
    epoch = s_bt_epoch;
    rate = s_bt_input_rate;
    channels = s_bt_input_channels;
    portEXIT_CRITICAL(&s_bt_lock);
    if (epoch != s_bt_consumer_epoch || rate != s_bt_consumer_rate || channels != s_bt_consumer_channels)
        bt_reset_consumer(epoch, rate, channels);
    if (!s_bt_streaming || !rate || s_bt_asrc_failed) return false;

    uint32_t fill = bt_fifo_fill();
    portENTER_CRITICAL(&s_bt_lock);
    if (fill > s_stats.bt_fifo_high_watermark) s_stats.bt_fifo_high_watermark = fill;
    portEXIT_CRITICAL(&s_bt_lock);
    if (!s_bt_prebuffered) {
        if (fill + s_bt_input_bytes < rate * channels * sizeof(int16_t) * 30 / 1000) return false;
        s_bt_prebuffered = true;
    }

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(12);
    size_t input_needed = SYNTH_CHUNK_SAMPLES * channels * sizeof(int16_t);
    while (s_bt_output_frames < SYNTH_CHUNK_SAMPLES) {
        while (s_bt_input_bytes < input_needed) {
            size_t bytes = 0;
            TickType_t now = xTaskGetTickCount();
            TickType_t wait = (int32_t)(deadline - now) > 0 ? deadline - now : 0;
            size_t needed = input_needed - s_bt_input_bytes;
            uint8_t *item = xRingbufferReceiveUpTo(s_bt_ringbuf, &bytes, wait, needed);
            if (!s_bt_streaming || !bt_epoch_matches(epoch)) {
                if (item) vRingbufferReturnItem(s_bt_ringbuf, item);
                return false;
            }
            if (item == NULL) {
                portENTER_CRITICAL(&s_bt_lock);
                ++s_stats.bt_underflow_count;
                portEXIT_CRITICAL(&s_bt_lock);
                s_bt_prebuffered = false;
                return false;  /* deadline elapsed: silence whole output frame */
            }
            memcpy((uint8_t *)s_bt_input + s_bt_input_bytes, item, bytes);
            s_bt_input_bytes += bytes;
            vRingbufferReturnItem(s_bt_ringbuf, item);
            if (bytes < needed) {
                portENTER_CRITICAL(&s_bt_lock);
                ++s_stats.bt_short_read_count;
                portEXIT_CRITICAL(&s_bt_lock);
            }
        }
        uint32_t frames = BT_RESAMPLED_FRAMES - s_bt_output_frames;
        int16_t *out = s_bt_output + s_bt_output_frames * SYNTH_CHANNELS;
        if (s_bt_asrc) {
            esp_asrc_err_t err = esp_asrc_process(s_bt_asrc, (uint8_t *)s_bt_input,
                SYNTH_CHUNK_SAMPLES, (uint8_t *)out, &frames);
            if (err != ESP_ASRC_ERR_OK) {
                portENTER_CRITICAL(&s_bt_lock);
                ++s_stats.bt_asrc_errors;
                portEXIT_CRITICAL(&s_bt_lock);
                ESP_LOGE(TAG, "[AUDIO] BT ASRC process failed err=%d", err);
                s_bt_asrc_failed = true;
                return false;
            }
        } else {
            frames = SYNTH_CHUNK_SAMPLES;
            memcpy(out, s_bt_input, input_needed);
        }
        s_bt_input_bytes = 0;
        s_bt_output_frames += frames;
    }
    memcpy(s_bt_buf, s_bt_output, sizeof(s_bt_buf));
    s_bt_output_frames -= SYNTH_CHUNK_SAMPLES;
    memmove(s_bt_output, s_bt_output + SYNTH_CHUNK_SAMPLES * SYNTH_CHANNELS,
            s_bt_output_frames * SYNTH_CHANNELS * sizeof(int16_t));
    return true;
}

static inline float audio_limiter_gain(float previous, uint32_t peak)
{
    float ceiling = peak > 32767 ? 32767.0f / peak : 1.0f;
    /* Immediate peak protection, ~200 ms release at 256/44100 frames. */
    return fminf(ceiling, previous + (1.0f - previous) * 0.03f);
}

static inline int16_t audio_mix_sample(int32_t sum, float limiter_gain)
{
    int32_t mixed = (int32_t)(sum * limiter_gain);
    /* Unity BT bypass; S16 clamp is a final rounding guard after peak limiting. */
    return (int16_t)(mixed > 32767 ? 32767 : (mixed < -32768 ? -32768 : mixed));
}

static void synth_audio_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "[AUDIO] system output=44100 Hz synth_gain=0.73 bt_gain=1.00 sfx_peak=0.12");
    const int sfx_ms[] = {18, 45, 30, 60};
    const float sfx_hz[] = {1200.0f, 880.0f, 660.0f, 330.0f};
    synth_sfx_t sfx = SYNTH_SFX_CLICK;
    int sfx_left = 0, sfx_total = 0;
    float sfx_phase = 0.0f;
    audio_command_t commands[AUDIO_COMMAND_COUNT];
    unsigned command_count = 0;
    int synth_tail = 0;
    int64_t next_log = esp_timer_get_time() + 2000000;
    uint64_t played_fraction = 0;
    float limiter_gain = 1.0f;

    while (1) {
        /* Retain SFX in order while a previous sound is playing. The bounded
           service queue never coalesces rapid clicks into one boolean. */
        while (command_count < AUDIO_COMMAND_COUNT &&
               xQueueReceive(s_audio_commands, &commands[command_count], 0) == pdTRUE)
            ++command_count;
        unsigned used = 0, retained = 0;
        while (used < command_count) {
            audio_command_t *c = &commands[used];
            if (c->type == AUDIO_SFX && sfx_left > 0) {
                commands[retained++] = *c;
                ++used;
                continue;
            }
            if (c->type == AUDIO_NOTE_ON && s_active) {
                start_note(c->freq, c->velocity, c->wave);
            } else if (c->type == AUDIO_NOTE_OFF) {
                xSemaphoreTake(s_synth_mutex, portMAX_DELAY);
                for (int i = 0; i < SYNTH_MAX_VOICES; ++i) {
                    synth_voice_t *v = &s_voices[i];
                    if (v->active && fabsf(v->freq - c->freq) < 0.01f && v->wave != SYNTH_WAVE_DRUM) {
                        v->releasing = true;
                        v->release_step = v->env / SYNTH_RELEASE_SAMPLES;
                    }
                }
                xSemaphoreGive(s_synth_mutex);
            } else if (c->type == AUDIO_SFX) {
                sfx = c->sfx;
                sfx_total = sfx_left = SYNTH_SAMPLE_RATE * sfx_ms[sfx] / 1000;
                sfx_phase = 0.0f;
            }
            ++used;
        }
        command_count = retained;

        bool has_voice = false;
        memset(s_synth_buf, 0, sizeof(s_synth_buf));
        xSemaphoreTake(s_synth_mutex, portMAX_DELAY);
        for (int v = 0; v < SYNTH_MAX_VOICES; ++v) has_voice |= s_voices[v].active;
        if (has_voice) {
            for (int frame = 0; frame < SYNTH_CHUNK_SAMPLES; ++frame) {
                float sum = 0.0f;
                for (int v = 0; v < SYNTH_MAX_VOICES; ++v)
                    if (s_voices[v].active) sum += synth_render_sample(&s_voices[v]);
                int16_t sample = (int16_t)(tanhf(sum * 0.6f) * 24000.0f);
                s_synth_buf[frame * 2] = s_synth_buf[frame * 2 + 1] = sample;
            }
            synth_tail = SYNTH_SAMPLE_RATE * 2;
        }
        /* FX state and knob updates share this mutex. Bluetooth and SFX never
           enter either processor, including during tails. */
        if (has_voice || synth_tail > 0) {
            if (s_eq_handle) esp_ae_eq_process(s_eq_handle, SYNTH_CHUNK_SAMPLES, s_synth_buf, s_synth_buf);
            if (s_reverb_handle) esp_ae_reverb_process(s_reverb_handle, SYNTH_CHUNK_SAMPLES, s_synth_buf, s_synth_buf);
            synth_tail -= SYNTH_CHUNK_SAMPLES;
        }
        xSemaphoreGive(s_synth_mutex);

        memset(s_bt_buf, 0, sizeof(s_bt_buf));
        bool has_bt = bt_render_chunk();
        if (!s_active && !s_bt_streaming && !has_voice && synth_tail <= 0 && sfx_left == 0 && command_count == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint32_t peak = 0, mix_peak = 0, limited = 0;
        float bt_gain = s_bt_volume;
        for (int frame = 0; frame < SYNTH_CHUNK_SAMPLES; ++frame) {
            int32_t tone = 0;
            if (sfx_left > 0) {
                int elapsed = sfx_total - sfx_left;
                int ramp = SYNTH_SAMPLE_RATE * 3 / 1000;
                float envelope = fminf(1.0f, fminf((float)elapsed / ramp, (float)sfx_left / ramp));
                float hz = sfx_hz[sfx];
                if (sfx == SYNTH_SFX_ERROR && elapsed >= sfx_total / 2) hz *= 0.75f;
                tone = (int32_t)(sinf(sfx_phase) * 4000.0f * envelope);
                sfx_phase += 2.0f * (float)M_PI * hz / SYNTH_SAMPLE_RATE;
                if (sfx_phase >= 2.0f * (float)M_PI) sfx_phase -= 2.0f * (float)M_PI;
                --sfx_left;
            }
            for (int ch = 0; ch < SYNTH_CHANNELS; ++ch) {
                int i = frame * SYNTH_CHANNELS + ch;
                int32_t sum = (int32_t)((float)s_bt_buf[i] * bt_gain) + s_synth_buf[i] + tone;
                s_mix_buf[i] = sum;
                uint32_t magnitude = abs(sum);
                if (magnitude > mix_peak) mix_peak = magnitude;
            }
        }
        limiter_gain = audio_limiter_gain(limiter_gain, mix_peak);
        for (int i = 0; i < SYNTH_CHUNK_SAMPLES * SYNTH_CHANNELS; ++i) {
            s_chunk_buf[i] = audio_mix_sample(s_mix_buf[i], limiter_gain);
            if (limiter_gain < 0.999f && s_mix_buf[i] != 0) ++limited;
            uint32_t magnitude = abs((int)s_chunk_buf[i]);
            if (magnitude > peak) peak = magnitude;
        }

        int write_result = ESP_CODEC_DEV_OK;
        int64_t begin = esp_timer_get_time();
        if (s_speaker_dev) {
            voice_service_feed_playback(s_chunk_buf, SYNTH_CHUNK_SAMPLES);
            write_result = esp_codec_dev_write(s_speaker_dev, s_chunk_buf, sizeof(s_chunk_buf));
        } else {
            vTaskDelay(pdMS_TO_TICKS(6));
        }
        uint32_t write_us = esp_timer_get_time() - begin;
        portENTER_CRITICAL(&s_bt_lock);
        if (s_speaker_dev && write_us > s_stats.codec_write_max_us) s_stats.codec_write_max_us = write_us;
        if (write_result != ESP_CODEC_DEV_OK) ++s_stats.codec_write_errors;
        if (peak > s_stats.output_peak) s_stats.output_peak = peak;
        s_stats.limited_samples += limited;
        if (has_bt && s_speaker_dev && write_result == ESP_CODEC_DEV_OK) {
            /* input-equivalent played bytes, permitting rx/play comparisons
               even for 48000 -> 44100 conversion. Preserve fractional bytes. */
            played_fraction += (uint64_t)SYNTH_CHUNK_SAMPLES * s_bt_consumer_rate *
                               s_bt_consumer_channels * sizeof(int16_t);
            s_stats.bt_played_bytes += played_fraction / SYNTH_SAMPLE_RATE;
            played_fraction %= SYNTH_SAMPLE_RATE;
        }
        portEXIT_CRITICAL(&s_bt_lock);
        if (write_result != ESP_CODEC_DEV_OK) vTaskDelay(pdMS_TO_TICKS(6));

        int64_t now = esp_timer_get_time();
        if (s_bt_streaming && now >= next_log) {
            synth_audio_stats_t st;
            synth_service_get_audio_stats(&st);
            ESP_LOGI(TAG, "[AUDIO] bt rate=%" PRIu32 " rx=%" PRIu64 " play=%" PRIu64
                     " fifo=%" PRIu32 "/%u high=%" PRIu32 " under=%" PRIu32 " over=%" PRIu32
                     " short=%" PRIu32 " drop=%" PRIu64 " flush=%" PRIu64 " write_max=%" PRIu32 "us write_err=%" PRIu32
                     " asrc_err=%" PRIu32 " cmd_drop=%" PRIu32 " peak=%" PRIu32 " limited=%" PRIu32,
                     st.bt_input_rate, st.bt_rx_bytes, st.bt_played_bytes, st.bt_fifo_fill_bytes,
                     BT_RINGBUF_SIZE, st.bt_fifo_high_watermark, st.bt_underflow_count, st.bt_overflow_count,
                     st.bt_short_read_count, st.bt_dropped_bytes, st.bt_flushed_bytes, st.codec_write_max_us, st.codec_write_errors,
                     st.bt_asrc_errors, st.command_drop_count, st.output_peak, st.limited_samples);
            next_log = now + 2000000;
        }
    }
}
