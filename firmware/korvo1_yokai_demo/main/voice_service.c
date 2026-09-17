#include "voice_service.h"

#include <stddef.h>

#define VOICE_MN_JAPANESE_BASE 100

static const voice_command_info_t s_commands[] = {
    {VOICE_COMMAND_SYNTH,      VOICE_TARGET_SYNTH,      "雷神シンセ",     "OPEN THE SYNTHESIZER",    "音楽",           "OHN GAH KOO",       ""},
    {VOICE_COMMAND_WEATHER,    VOICE_TARGET_WEATHER,    "雪女の天気",     "SHOW THE WEATHER",         "天気",           "TEN KEY",           ""},
    {VOICE_COMMAND_VOICE,      VOICE_TARGET_VOICE,      "言霊の社",       "OPEN VOICE CONTROL",       "音声",           "ON SAY",            ""},
    {VOICE_COMMAND_VISION,     VOICE_TARGET_VISION,     "目目連の眼",     "OPEN THE CAMERA",          "画像",           "GAH ZOH",           ""},
    {VOICE_COMMAND_FIREWORKS,  VOICE_TARGET_FIREWORKS,  "夜空の花火",     "SHOW THE FIREWORKS",       "花火",           "HAH NAH BEE",       ""},
    {VOICE_COMMAND_CLOCK,      VOICE_TARGET_CLOCK,      "狸屋の時計",     "OPEN THE CLOCK",           "時計",           "TOE KAY",           ""},
    {VOICE_COMMAND_CALCULATOR, VOICE_TARGET_CALCULATOR, "和風そろばん",   "OPEN THE CALCULATOR",      "計算",           "KAY SAHN",          ""},
    {VOICE_COMMAND_FOOD,       VOICE_TARGET_FOOD,       "妖怪の屋台",     "OPEN THE FOOD APP",        "ごはん",         "GO HAHN",           ""},
    {VOICE_COMMAND_WIFI,       VOICE_TARGET_WIFI,       "Wi-Fi設定",      "OPEN WI FI SETTINGS",      "ワイファイ",     "WHY FIE",           ""},
    {VOICE_COMMAND_BLUETOOTH,  VOICE_TARGET_BLUETOOTH,  "Bluetooth設定",  "OPEN BLUETOOTH SETTINGS",  "ブルートゥース", "BLUE TOOTH",        ""},
    {VOICE_COMMAND_HOME,       VOICE_TARGET_HOME,       "ホームへ戻る",   "GO BACK HOME",             "ホーム",         "HOME",              ""},
    {VOICE_COMMAND_VOLUME_UP,  VOICE_TARGET_VOLUME,     "音量アップ",     "TURN THE VOLUME UP",        "音量アップ",     "ON RYO UP",         ""},
    {VOICE_COMMAND_VOLUME_DOWN,VOICE_TARGET_VOLUME,     "音量ダウン",     "TURN THE VOLUME DOWN",      "ボリュームダウン", "VO RYEWM DOWN",   ""},
    {VOICE_COMMAND_MUTE,       VOICE_TARGET_VOLUME,     "ミュート",       "MUTE THE SOUND",            "ミュート",       "MEW TOE",           ""},
    {VOICE_COMMAND_UNMUTE,     VOICE_TARGET_VOLUME,     "ミュート解除",   "RESTORE THE VOLUME",        "解除",           "KAI JOE",           ""},
};

size_t voice_service_command_count(void)
{
    return sizeof(s_commands) / sizeof(s_commands[0]);
}

const voice_command_info_t *voice_service_command_info(voice_command_t command)
{
    if (command <= VOICE_COMMAND_NONE || command >= VOICE_COMMAND_COUNT) {
        return NULL;
    }
    return &s_commands[(size_t)command - 1];
}

voice_command_t voice_service_normalize_multinet_id(int id, voice_language_t *language)
{
    voice_language_t detected = VOICE_LANGUAGE_ENGLISH;
    if (id >= VOICE_MN_JAPANESE_BASE) {
        detected = VOICE_LANGUAGE_JAPANESE;
        id -= VOICE_MN_JAPANESE_BASE;
    }
    if (language != NULL) {
        *language = detected;
    }
    return id > VOICE_COMMAND_NONE && id < VOICE_COMMAND_COUNT
        ? (voice_command_t)id : VOICE_COMMAND_NONE;
}

static int clamp_volume(int value)
{
    return value < 0 ? 0 : (value > 100 ? 100 : value);
}

int voice_service_apply_volume(voice_command_t command, int current, int *saved_nonzero)
{
    current = clamp_volume(current);
    if (saved_nonzero == NULL) {
        return current;
    }
    switch (command) {
    case VOICE_COMMAND_VOLUME_UP:
        return clamp_volume(current + 10);
    case VOICE_COMMAND_VOLUME_DOWN:
        return clamp_volume(current - 10);
    case VOICE_COMMAND_MUTE:
        if (current > 0) {
            *saved_nonzero = current;
        }
        return 0;
    case VOICE_COMMAND_UNMUTE:
        return clamp_volume(*saved_nonzero > 0 ? *saved_nonzero : 80);
    default:
        return current;
    }
}

#ifndef VOICE_SERVICE_LOGIC_ONLY

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp32_s31_korvo_1.h"
#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_asrc.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "flite_g2p.h"
#include "model_path.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define VOICE_CODEC_RATE       44100
#define VOICE_SR_RATE          16000
#define VOICE_IO_FRAMES        441
#define VOICE_REF_RING_FRAMES  (VOICE_CODEC_RATE / 4)
#define VOICE_AEC_DELAY_FRAMES (VOICE_CODEC_RATE * 40 / 1000)

static const char *TAG = "voice_service";
static float s_mic_select[2] = {1.0f, 0.0f};
static esp_codec_dev_handle_t s_mic_dev;
static esp_asrc_handle_t s_mic_asrc;
static esp_asrc_handle_t s_ref_asrc;
static uint32_t s_asrc_out_capacity;
static srmodel_list_t *s_models;
static const esp_afe_sr_iface_t *s_afe;
static esp_afe_sr_data_t *s_afe_data;
static esp_mn_iface_t *s_mn;
static model_iface_data_t *s_mn_data;
static int16_t *s_ref_ring;
static size_t s_ref_read;
static size_t s_ref_write;
static size_t s_ref_count;
static SemaphoreHandle_t s_ref_mutex;
static QueueHandle_t s_result_queue;
static TaskHandle_t s_feed_task;
static TaskHandle_t s_fetch_task;
static volatile voice_mode_t s_mode = VOICE_MODE_GLOBAL_WAKE;
static volatile bool s_ready;
static char s_error[96] = "Voice service has not started";

typedef struct {
    int16_t *mic_44k;
    int16_t *ref_stereo_44k;
    int16_t *ref_44k;
    int16_t *mic_16k;
    int16_t *ref_16k;
    int16_t *afe_frame;
    uint32_t mic_out_bytes;
    uint32_t ref_out_bytes;
    int afe_frames;
} voice_feed_buffers_t;

static voice_feed_buffers_t s_feed;

static bool fail_start(const char *message)
{
    snprintf(s_error, sizeof(s_error), "%s", message);
    s_ready = false;
    ESP_LOGE(TAG, "%s", s_error);
    return false;
}

static bool open_microphone(void)
{
    s_mic_dev = bsp_audio_codec_microphone_init();
    if (s_mic_dev == NULL) {
        return false;
    }
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = VOICE_CODEC_RATE,
        .channel = 2,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(s_mic_dev, &fs) != ESP_CODEC_DEV_OK) {
        return false;
    }
    esp_codec_dev_set_in_gain(s_mic_dev, 34.0f);
    return true;
}

static bool open_asrc(void)
{
    esp_asrc_cfg_t mic_cfg = {
        .src_info = {.sample_rate = VOICE_CODEC_RATE, .channel = 2, .bits_per_sample = 16},
        .dest_info = {.sample_rate = VOICE_SR_RATE, .channel = 1, .bits_per_sample = 16},
        .weight = s_mic_select,
        .weight_len = 2,
        .perf_type = ESP_ASRC_PERF_TYPE_HW_ONLY,
        .complexity = 1,
        .timeout_ms = 100,
    };
    esp_asrc_cfg_t ref_cfg = {
        .src_info = {.sample_rate = VOICE_CODEC_RATE, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = VOICE_SR_RATE, .channel = 1, .bits_per_sample = 16},
        .perf_type = ESP_ASRC_PERF_TYPE_HW_ONLY,
        .complexity = 1,
        .timeout_ms = 100,
    };
    esp_asrc_err_t mic_err = esp_asrc_open(&mic_cfg, &s_mic_asrc);
    esp_asrc_err_t ref_err = mic_err == ESP_ASRC_ERR_OK
        ? esp_asrc_open(&ref_cfg, &s_ref_asrc) : ESP_ASRC_ERR_FAIL;
    if (mic_err != ESP_ASRC_ERR_OK || ref_err != ESP_ASRC_ERR_OK) {
        ESP_LOGE(TAG, "ASRC open errors: mic=%d ref=%d", mic_err, ref_err);
        return false;
    }
    return esp_asrc_get_out_sample_num(s_mic_asrc, VOICE_IO_FRAMES,
                                       &s_asrc_out_capacity) == ESP_ASRC_ERR_OK;
}

static bool open_speech_models(void)
{
    s_models = esp_srmodel_init("model");
    if (s_models == NULL) {
        return false;
    }
    char *wake_name = esp_srmodel_filter(s_models, ESP_WN_PREFIX, "wn9_hiesp");
    char *mn_name = esp_srmodel_filter(s_models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (wake_name == NULL || mn_name == NULL) {
        return false;
    }
    afe_config_t *cfg = afe_config_init("MR", s_models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (cfg == NULL) {
        return false;
    }
    cfg->wakenet_init = true;
    cfg->wakenet_model_name = wake_name;
    cfg->aec_init = true;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    cfg->afe_perferred_core = 0;
    cfg->afe_perferred_priority = 8;
    cfg->vad_init = true;
    cfg->vad_mode = VAD_MODE_3;
    cfg->vad_min_speech_ms = 80;
    cfg->vad_min_noise_ms = 300;
    s_afe = esp_afe_handle_from_config(cfg);
    s_afe_data = s_afe != NULL ? s_afe->create_from_config(cfg) : NULL;
    afe_config_free(cfg);
    if (s_afe_data == NULL) {
        return false;
    }
    s_mn = esp_mn_handle_from_name(mn_name);
    s_mn_data = s_mn != NULL ? s_mn->create(mn_name, 5000) : NULL;
    if (s_mn_data != NULL && s_mn->set_det_threshold != NULL) {
        s_mn->set_det_threshold(s_mn_data, 0.23f);
        ESP_LOGI(TAG, "MultiNet detection threshold tuned to 0.23, VAD_MODE_3, timeout 5.0s");
    }
    return s_mn_data != NULL;
}

typedef struct {
    voice_command_t command;
    const char *phrase;
} voice_phrase_t;

/* English phrases to register directly (command_id) */
static const voice_phrase_t s_en_phrases[] = {
    /* Primary / formal commands */
    {VOICE_COMMAND_SYNTH,       "OPEN THE SYNTHESIZER"},
    {VOICE_COMMAND_WEATHER,     "SHOW THE WEATHER"},
    {VOICE_COMMAND_VOICE,       "OPEN VOICE CONTROL"},
    {VOICE_COMMAND_VISION,      "OPEN THE CAMERA"},
    {VOICE_COMMAND_FIREWORKS,   "SHOW THE FIREWORKS"},
    {VOICE_COMMAND_CLOCK,       "OPEN THE CLOCK"},
    {VOICE_COMMAND_CALCULATOR,  "OPEN THE CALCULATOR"},
    {VOICE_COMMAND_FOOD,        "OPEN THE FOOD APP"},
    {VOICE_COMMAND_WIFI,        "OPEN WI FI SETTINGS"},
    {VOICE_COMMAND_BLUETOOTH,   "OPEN BLUETOOTH SETTINGS"},
    {VOICE_COMMAND_HOME,        "GO BACK HOME"},
    {VOICE_COMMAND_VOLUME_UP,   "TURN THE VOLUME UP"},
    {VOICE_COMMAND_VOLUME_DOWN, "TURN THE VOLUME DOWN"},
    {VOICE_COMMAND_MUTE,        "MUTE THE SOUND"},
    {VOICE_COMMAND_UNMUTE,      "RESTORE THE VOLUME"},

    /* English spoken aliases */
    {VOICE_COMMAND_SYNTH,       "OPEN SYNTHESIZER"},
    {VOICE_COMMAND_SYNTH,       "PLAY MUSIC"},
    {VOICE_COMMAND_WEATHER,     "OPEN WEATHER"},
    {VOICE_COMMAND_WEATHER,     "WEATHER"},
    {VOICE_COMMAND_VOICE,       "VOICE CONTROL"},
    {VOICE_COMMAND_VOICE,       "VOICE SHRINE"},
    {VOICE_COMMAND_VISION,      "OPEN CAMERA"},
    {VOICE_COMMAND_VISION,      "CAMERA"},
    {VOICE_COMMAND_FIREWORKS,   "FIREWORKS"},
    {VOICE_COMMAND_CLOCK,       "OPEN CLOCK"},
    {VOICE_COMMAND_CLOCK,       "CLOCK"},
    {VOICE_COMMAND_CALCULATOR,  "OPEN CALCULATOR"},
    {VOICE_COMMAND_CALCULATOR,  "CALCULATOR"},
    {VOICE_COMMAND_FOOD,        "OPEN FOOD"},
    {VOICE_COMMAND_FOOD,        "FOOD"},
    {VOICE_COMMAND_WIFI,        "OPEN WIFI"},
    {VOICE_COMMAND_WIFI,        "WIFI SETTINGS"},
    {VOICE_COMMAND_BLUETOOTH,   "OPEN BLUETOOTH"},
    {VOICE_COMMAND_BLUETOOTH,   "BLUETOOTH"},
    {VOICE_COMMAND_HOME,        "HOME"},
    {VOICE_COMMAND_VOLUME_UP,   "VOLUME UP"},
    {VOICE_COMMAND_VOLUME_UP,   "TURN UP THE VOLUME"},
    {VOICE_COMMAND_VOLUME_DOWN, "VOLUME DOWN"},
    {VOICE_COMMAND_VOLUME_DOWN, "TURN DOWN THE VOLUME"},
    {VOICE_COMMAND_MUTE,        "MUTE"},
    {VOICE_COMMAND_MUTE,        "SILENT"},
    {VOICE_COMMAND_UNMUTE,      "UNMUTE"},
    {VOICE_COMMAND_UNMUTE,      "RESTORE VOLUME"},
};

/* Japanese transliterated phrases registered with VOICE_MN_JAPANESE_BASE + command_id.
 * ESP-SR Flite G2P automatically generates valid phonemes from these natural phonetic words! */
static const voice_phrase_t s_ja_phrases[] = {
    /* 音楽 (おんがく) / 雷神合成器 - 支持 ミュージック(myūjikku) / 音楽 / シンセ / 曲 */
    {VOICE_COMMAND_SYNTH,       "MEW JICK"},              /* ミュージック (破擦音 JH, 非 Z) */
    {VOICE_COMMAND_SYNTH,       "MEW JIC"},
    {VOICE_COMMAND_SYNTH,       "MYU JICK"},
    {VOICE_COMMAND_SYNTH,       "MYU JIC"},
    {VOICE_COMMAND_SYNTH,       "MYOO JICK"},
    {VOICE_COMMAND_SYNTH,       "MUSIC"},
    {VOICE_COMMAND_SYNTH,       "ON GAH KOO"},            /* 音楽 (おんがく) */
    {VOICE_COMMAND_SYNTH,       "OHN GAH KOO"},
    {VOICE_COMMAND_SYNTH,       "OWN GAH KOO"},
    {VOICE_COMMAND_SYNTH,       "ONGAKU"},
    {VOICE_COMMAND_SYNTH,       "ON GAH KU"},
    {VOICE_COMMAND_SYNTH,       "ON GAK"},
    {VOICE_COMMAND_SYNTH,       "KYOH KOO"},              /* 曲 (きょく) */
    {VOICE_COMMAND_SYNTH,       "SHIN SEH"},              /* シンセ */
    {VOICE_COMMAND_SYNTH,       "SHIN SAY"},

    /* 天气 (てんき) / 雪女天气 */
    {VOICE_COMMAND_WEATHER,     "TEN KEY"},
    {VOICE_COMMAND_WEATHER,     "TEN KEE"},

    /* 言霊 (ことだま) / 言灵神社 - 支持 ボイス / 音声 / 神社 / ことだま */
    {VOICE_COMMAND_VOICE,       "VOICE"},                 /* ボイス */
    {VOICE_COMMAND_VOICE,       "VOICE CONTROL"},         /* ボイスコントロール */
    {VOICE_COMMAND_VOICE,       "VOICE SHRINE"},
    {VOICE_COMMAND_VOICE,       "BO I SOO"},
    {VOICE_COMMAND_VOICE,       "BO I SU"},
    {VOICE_COMMAND_VOICE,       "ON SAY"},                /* 音声 (おんせい) */
    {VOICE_COMMAND_VOICE,       "OHN SAY"},
    {VOICE_COMMAND_VOICE,       "ON SEI"},
    {VOICE_COMMAND_VOICE,       "JIN JAH"},               /* 神社 (じんじゃ) */
    {VOICE_COMMAND_VOICE,       "JEAN JAH"},
    {VOICE_COMMAND_VOICE,       "JINJA"},
    {VOICE_COMMAND_VOICE,       "KOTO DAMA"},             /* 言霊 (ことだま) */
    {VOICE_COMMAND_VOICE,       "COTO DAMA"},
    {VOICE_COMMAND_VOICE,       "KOH TOH DAH MAH"},
    {VOICE_COMMAND_VOICE,       "COAT DAH MAH"},
    {VOICE_COMMAND_VOICE,       "KOTODAMA"},

    /* 画像 (がぞう) / カメラ / 目目连视觉 - 支持 がぞう / カメラ / 写真 */
    {VOICE_COMMAND_VISION,      "GA ZOH"},                /* 画像 (がぞう) */
    {VOICE_COMMAND_VISION,      "GAH ZOH"},
    {VOICE_COMMAND_VISION,      "GA ZO"},
    {VOICE_COMMAND_VISION,      "GAH ZO"},
    {VOICE_COMMAND_VISION,      "GAZOU"},
    {VOICE_COMMAND_VISION,      "GAZO"},
    {VOICE_COMMAND_VISION,      "GAH ZOH OO"},
    {VOICE_COMMAND_VISION,      "KAH MEH RAH"},           /* 日语标准 kamera [Kn Mf Rn] */
    {VOICE_COMMAND_VISION,      "CAH MEH RAH"},
    {VOICE_COMMAND_VISION,      "KAH MAY RAH"},
    {VOICE_COMMAND_VISION,      "KAH MEH LAH"},
    {VOICE_COMMAND_VISION,      "CAH MEH LAH"},
    {VOICE_COMMAND_VISION,      "CAMELA"},
    {VOICE_COMMAND_VISION,      "KAMELA"},
    {VOICE_COMMAND_VISION,      "CAMERA"},
    {VOICE_COMMAND_VISION,      "CAM LA"},
    {VOICE_COMMAND_VISION,      "SHAH SHEEN"},            /* 写真 (しゃしん) */
    {VOICE_COMMAND_VISION,      "SHAH SHIN"},
    {VOICE_COMMAND_VISION,      "SHA SHIN"},
    {VOICE_COMMAND_VISION,      "PHOTO"},
    {VOICE_COMMAND_VISION,      "PICTURE"},

    /* 花火 (はなび) / 夜空花火 - 覆盖 hanabi / hana bi / hanabee / ha na bee 等多重音标 */
    {VOICE_COMMAND_FIREWORKS,   "HANABI"},
    {VOICE_COMMAND_FIREWORKS,   "HA NA BI"},
    {VOICE_COMMAND_FIREWORKS,   "HANA BI"},
    {VOICE_COMMAND_FIREWORKS,   "HA NA BEE"},
    {VOICE_COMMAND_FIREWORKS,   "HANA BEE"},
    {VOICE_COMMAND_FIREWORKS,   "HAH NAH BEE"},
    {VOICE_COMMAND_FIREWORKS,   "HAH NAH BI"},
    {VOICE_COMMAND_FIREWORKS,   "HAH NAH BIH"},
    {VOICE_COMMAND_FIREWORKS,   "HAH NAH BE"},
    {VOICE_COMMAND_FIREWORKS,   "HAH NAH BEA"},
    {VOICE_COMMAND_FIREWORKS,   "HANABEE"},
    {VOICE_COMMAND_FIREWORKS,   "YO ZOH RAH HAH NAH BEE"},/* 夜空花火 (全名) */
    {VOICE_COMMAND_FIREWORKS,   "HAH NAH BEE TIE KAI"},   /* 花火大会 */
    {VOICE_COMMAND_FIREWORKS,   "TAH MAH YAH"},           /* 玉屋 (たまや) */
    {VOICE_COMMAND_FIREWORKS,   "TA MA YA"},
    {VOICE_COMMAND_FIREWORKS,   "FIRE WORKS"},

    /* 時計 (とけい) / 狸屋时钟 */
    {VOICE_COMMAND_CLOCK,       "TOE KAY"},
    {VOICE_COMMAND_CLOCK,       "TOE KAI"},
    {VOICE_COMMAND_CLOCK,       "EE MAH NAHN JEE"},       /* 今何時 (いまなんじ) */
    {VOICE_COMMAND_CLOCK,       "JEE KAHN"},              /* 時間 (じかん) */

    /* 計算 (けいさん) / 和風算盤 - 支持 電卓 / 計算機 / 算盤 / Calculator */
    {VOICE_COMMAND_CALCULATOR,  "DENTAKU"},               /* 電卓 (でんたく) */
    {VOICE_COMMAND_CALCULATOR,  "DEN TAH KOO"},
    {VOICE_COMMAND_CALCULATOR,  "DEN TAH KU"},
    {VOICE_COMMAND_CALCULATOR,  "DEN TAK"},
    {VOICE_COMMAND_CALCULATOR,  "KAY SAHN"},              /* 計算 (けいさん) */
    {VOICE_COMMAND_CALCULATOR,  "KAY SAHN KEY"},          /* 計算機 (けいさんき) */
    {VOICE_COMMAND_CALCULATOR,  "KAY SAHN KEE"},
    {VOICE_COMMAND_CALCULATOR,  "KEISANKI"},
    {VOICE_COMMAND_CALCULATOR,  "KEISAN"},
    {VOICE_COMMAND_CALCULATOR,  "SO ROH BAHN"},           /* 算盤 (そろばん) */
    {VOICE_COMMAND_CALCULATOR,  "SOROBAN"},
    {VOICE_COMMAND_CALCULATOR,  "SO RO BAHN"},
    {VOICE_COMMAND_CALCULATOR,  "CALCULATOR"},

    /* 料理 (りょうり) / 妖怪屋台 */
    {VOICE_COMMAND_FOOD,        "YAH TIE"},               /* 屋台 (やたい) */
    {VOICE_COMMAND_FOOD,        "YA TAI"},
    {VOICE_COMMAND_FOOD,        "YATAI"},
    {VOICE_COMMAND_FOOD,        "GO HAHN"},               /* ご飯 (ごはん) */
    {VOICE_COMMAND_FOOD,        "GO HAN"},
    {VOICE_COMMAND_FOOD,        "GOHAN"},
    {VOICE_COMMAND_FOOD,        "TAH BEH MOH NOH"},       /* 食べ物 (たべもの) */
    {VOICE_COMMAND_FOOD,        "TABEMONO"},
    {VOICE_COMMAND_FOOD,        "REE YOH REE"},           /* 料理 (りょうり) */
    {VOICE_COMMAND_FOOD,        "RYOH REE"},
    {VOICE_COMMAND_FOOD,        "RYORI"},

    /* ワイファイ / Wi-Fi 设置 */
    {VOICE_COMMAND_WIFI,        "WHY FIE"},
    {VOICE_COMMAND_WIFI,        "WHY FAI"},
    {VOICE_COMMAND_WIFI,        "WI FI"},

    /* ブルートゥース / 蓝牙设置 (日式片假名非咬舌清齿音 S) */
    {VOICE_COMMAND_BLUETOOTH,   "BLUE TOOSE"},
    {VOICE_COMMAND_BLUETOOTH,   "BLOO TOOSE"},
    {VOICE_COMMAND_BLUETOOTH,   "BLUE TOOS"},
    {VOICE_COMMAND_BLUETOOTH,   "BLUE TOOTH"},
    {VOICE_COMMAND_BLUETOOTH,   "BLUETOOTH"},

    /* ホーム / 返回桌面 */
    {VOICE_COMMAND_HOME,        "HOME"},
    {VOICE_COMMAND_HOME,        "HOOM"},
    {VOICE_COMMAND_HOME,        "MOH DOH ROO"},           /* 戻る (もどる) */

    /* 上げて / 音量增加 (覆盖 agete / age / onryou agete / onryou o agete / ookiku shite / volume up) */
    {VOICE_COMMAND_VOLUME_UP,   "AH GAY TAY"},            /* あげて */
    {VOICE_COMMAND_VOLUME_UP,   "AH GHE TAY"},
    {VOICE_COMMAND_VOLUME_UP,   "AH GAY TEH"},
    {VOICE_COMMAND_VOLUME_UP,   "AH GHEH TEH"},
    {VOICE_COMMAND_VOLUME_UP,   "A GAY TAY"},
    {VOICE_COMMAND_VOLUME_UP,   "AH GAY"},                /* あげ (短发音) */
    {VOICE_COMMAND_VOLUME_UP,   "A GAY"},
    {VOICE_COMMAND_VOLUME_UP,   "UP"},                    /* アップ */
    {VOICE_COMMAND_VOLUME_UP,   "VOL UP"},
    {VOICE_COMMAND_VOLUME_UP,   "ON RYO UP"},             /* 音量アップ */
    {VOICE_COMMAND_VOLUME_UP,   "OHN RYOH UP"},
    {VOICE_COMMAND_VOLUME_UP,   "ON RYOH AH GAY TAY"},    /* 音量あげて */
    {VOICE_COMMAND_VOLUME_UP,   "ON RYO AH GAY TAY"},
    {VOICE_COMMAND_VOLUME_UP,   "ON RYO OH AH GAY TAY"},  /* 音量をあげて */
    {VOICE_COMMAND_VOLUME_UP,   "OHN RYOH OH AH GAY TAY"},
    {VOICE_COMMAND_VOLUME_UP,   "ON RYO AH GAY"},         /* 音量あげ */
    {VOICE_COMMAND_VOLUME_UP,   "OH KEE KOO"},            /* 大きく (おおきく) */
    {VOICE_COMMAND_VOLUME_UP,   "OH KEE KOO SHE TAY"},    /* 大きくして */
    {VOICE_COMMAND_VOLUME_UP,   "OH KEE KOO SHE TEH"},
    {VOICE_COMMAND_VOLUME_UP,   "OH TOE OH KEE KOO"},     /* 音大きく */
    {VOICE_COMMAND_VOLUME_UP,   "OH TOH OH KEE KOO"},
    {VOICE_COMMAND_VOLUME_UP,   "OH TOE OH KEE KOO SHE TAY"},/* 音大きくして */
    {VOICE_COMMAND_VOLUME_UP,   "BO RYEW MOO UP"},        /* ボリュームアップ */
    {VOICE_COMMAND_VOLUME_UP,   "BO RYEW MOO AH GAY TAY"},/* ボリューム上げて */
    {VOICE_COMMAND_VOLUME_UP,   "VO RYEWM AH GAY TAY"},
    {VOICE_COMMAND_VOLUME_UP,   "VOLUME UP"},
    {VOICE_COMMAND_VOLUME_UP,   "VO RYEWM UP"},

    /* 下げて / 音量降低 (覆盖 sagete / sage / onryou sagete / onryou o sagete / chiisaku shite / volume down) */
    {VOICE_COMMAND_VOLUME_DOWN, "SAH GAY TAY"},           /* さげて */
    {VOICE_COMMAND_VOLUME_DOWN, "SAH GHE TAY"},
    {VOICE_COMMAND_VOLUME_DOWN, "SAH GAY TEH"},
    {VOICE_COMMAND_VOLUME_DOWN, "SAH GHEH TEH"},
    {VOICE_COMMAND_VOLUME_DOWN, "SA GAY TAY"},
    {VOICE_COMMAND_VOLUME_DOWN, "SAH GAY"},               /* さげ (短发音) */
    {VOICE_COMMAND_VOLUME_DOWN, "SA GAY"},
    {VOICE_COMMAND_VOLUME_DOWN, "DOWN"},                  /* ダウン */
    {VOICE_COMMAND_VOLUME_DOWN, "VOL DOWN"},
    {VOICE_COMMAND_VOLUME_DOWN, "ON RYO DOWN"},           /* 音量ダウン */
    {VOICE_COMMAND_VOLUME_DOWN, "OHN RYOH DOWN"},
    {VOICE_COMMAND_VOLUME_DOWN, "ON RYOH SAH GAY TAY"},   /* 音量さげて */
    {VOICE_COMMAND_VOLUME_DOWN, "ON RYO SAH GAY TAY"},
    {VOICE_COMMAND_VOLUME_DOWN, "ON RYO OH SAH GAY TAY"}, /* 音量をさげて */
    {VOICE_COMMAND_VOLUME_DOWN, "OHN RYOH OH SAH GAY TAY"},
    {VOICE_COMMAND_VOLUME_DOWN, "ON RYO SAH GAY"},        /* 音量さげ */
    {VOICE_COMMAND_VOLUME_DOWN, "CHEE SAH KOO"},          /* 小さく (ちいさく) */
    {VOICE_COMMAND_VOLUME_DOWN, "CHEE SAH KOO SHE TAY"},  /* 小さくして */
    {VOICE_COMMAND_VOLUME_DOWN, "CHEE SAH KOO SHE TEH"},
    {VOICE_COMMAND_VOLUME_DOWN, "OH TOE CHEE SAH KOO"},   /* 音小さく */
    {VOICE_COMMAND_VOLUME_DOWN, "OH TOH CHEE SAH KOO"},
    {VOICE_COMMAND_VOLUME_DOWN, "OH TOE CHEE SAH KOO SHE TAY"},/* 音小さくして */
    {VOICE_COMMAND_VOLUME_DOWN, "BO RYEW MOO DOWN"},      /* ボリュームダウン */
    {VOICE_COMMAND_VOLUME_DOWN, "BO RYEW MOO SAH GAY TAY"},/* ボリューム下げて */
    {VOICE_COMMAND_VOLUME_DOWN, "VO RYEWM SAH GAY TAY"},
    {VOICE_COMMAND_VOLUME_DOWN, "VOLUME DOWN"},
    {VOICE_COMMAND_VOLUME_DOWN, "VO RYEWM DOWN"},

    /* ミュート / 静音 */
    {VOICE_COMMAND_MUTE,        "MEW TOE"},
    {VOICE_COMMAND_MUTE,        "SHEE ZOO KAH NEE"},      /* 静かに (しずかに) */

    /* 戻して / 恢复音量 (覆盖 戻す / 戻して / 解除 / 音出して / 音量戻して) */
    {VOICE_COMMAND_UNMUTE,      "MOH DOH SOO"},           /* 戻す (もどす) */
    {VOICE_COMMAND_UNMUTE,      "MO DOH SOO"},
    {VOICE_COMMAND_UNMUTE,      "MO DOH SHE TAY"},        /* 戻して (もどして) */
    {VOICE_COMMAND_UNMUTE,      "MOH DOH SHE TAY"},
    {VOICE_COMMAND_UNMUTE,      "MO DOH SHE"},
    {VOICE_COMMAND_UNMUTE,      "MOH DOH SHE"},
    {VOICE_COMMAND_UNMUTE,      "MO DOH SHE TEH"},
    {VOICE_COMMAND_UNMUTE,      "MOH DOH SHE TEH"},
    {VOICE_COMMAND_UNMUTE,      "KAI JOE"},               /* 解除 (かいじょ) */
    {VOICE_COMMAND_UNMUTE,      "KAI JO"},
    {VOICE_COMMAND_UNMUTE,      "OH TOE DAH SHE TAY"},    /* 音出して (おとだして) */
    {VOICE_COMMAND_UNMUTE,      "OH TOH DAH SHE TAY"},
    {VOICE_COMMAND_UNMUTE,      "OH TOE DAH SHE"},
    {VOICE_COMMAND_UNMUTE,      "MEW TOE KAI JOE"},       /* ミュート解除 */
    {VOICE_COMMAND_UNMUTE,      "MEW TOE KAI JO"},
    {VOICE_COMMAND_UNMUTE,      "ON RYO MO DOH SHE TAY"}, /* 音量戻して */
    {VOICE_COMMAND_UNMUTE,      "ON RYOH MOH DOH SHE TAY"},
    {VOICE_COMMAND_UNMUTE,      "ON RYO OH MO DOH SHE TAY"},/* 音量をもどして */
    {VOICE_COMMAND_UNMUTE,      "AHN MEW TOE"},           /* アンミュート */
    {VOICE_COMMAND_UNMUTE,      "UNMUTE"},
    {VOICE_COMMAND_UNMUTE,      "RESTORE THE VOLUME"},
    {VOICE_COMMAND_UNMUTE,      "RESTORE VOLUME"},
};

static bool register_commands(void)
{
    if (esp_mn_commands_alloc(s_mn, s_mn_data) != ESP_OK) {
        return false;
    }
    for (size_t i = 0; i < sizeof(s_en_phrases) / sizeof(s_en_phrases[0]); ++i) {
        if (esp_mn_commands_add(s_en_phrases[i].command, s_en_phrases[i].phrase) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to add EN phrase: %s", s_en_phrases[i].phrase);
        }
    }
    for (size_t i = 0; i < sizeof(s_ja_phrases) / sizeof(s_ja_phrases[0]); ++i) {
        char *ph = flite_g2p(s_ja_phrases[i].phrase, 1);
        if (ph != NULL) {
            ESP_LOGI(TAG, "JA_REG[%03d] '%s' -> [%s]",
                     VOICE_MN_JAPANESE_BASE + s_ja_phrases[i].command,
                     s_ja_phrases[i].phrase, ph);
            free(ph);
        }
        if (esp_mn_commands_add(VOICE_MN_JAPANESE_BASE + s_ja_phrases[i].command,
                                s_ja_phrases[i].phrase) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to add JA phrase: %s", s_ja_phrases[i].phrase);
        }
    }
    esp_mn_error_t *errors = esp_mn_commands_update();
    if (errors != NULL && errors->num != 0) {
        return false;
    }
    if (s_mn_data != NULL && s_mn->set_det_threshold != NULL) {
        s_mn->set_det_threshold(s_mn_data, 0.26f);
        ESP_LOGI(TAG, "MultiNet detection threshold verified/re-applied: 0.26");
    }
    esp_mn_active_commands_print();
    return true;
}

static void free_feed_buffers(void)
{
    free(s_feed.mic_44k);
    free(s_feed.ref_stereo_44k);
    free(s_feed.ref_44k);
    free(s_feed.mic_16k);
    free(s_feed.ref_16k);
    free(s_feed.afe_frame);
    memset(&s_feed, 0, sizeof(s_feed));
}

static bool allocate_feed_buffers(void)
{
    esp_asrc_buffer_alignment_t align;
    uint32_t allocated;
    if (esp_asrc_get_buffer_alignment(&align) != ESP_ASRC_ERR_OK) {
        return false;
    }
    s_feed.mic_44k = esp_asrc_align_alloc(VOICE_IO_FRAMES * 4, align.inbuf_addr_align,
                                          align.inbuf_size_align, &allocated);
    s_feed.ref_stereo_44k = esp_asrc_align_alloc(VOICE_IO_FRAMES * 4, align.inbuf_addr_align,
                                                 align.inbuf_size_align, &allocated);
    s_feed.ref_44k = esp_asrc_align_alloc(VOICE_IO_FRAMES * 2, align.inbuf_addr_align,
                                          align.inbuf_size_align, &allocated);
    s_feed.mic_16k = esp_asrc_align_alloc(s_asrc_out_capacity * 2, align.outbuf_addr_align,
                                          align.outbuf_size_align, &s_feed.mic_out_bytes);
    s_feed.ref_16k = esp_asrc_align_alloc(s_asrc_out_capacity * 2, align.outbuf_addr_align,
                                          align.outbuf_size_align, &s_feed.ref_out_bytes);
    s_feed.afe_frames = s_afe->get_feed_chunksize(s_afe_data);
    s_feed.afe_frame = heap_caps_calloc((size_t)s_feed.afe_frames * 2, sizeof(int16_t),
                                        MALLOC_CAP_INTERNAL);
    if (!s_feed.mic_44k || !s_feed.ref_stereo_44k || !s_feed.ref_44k ||
        !s_feed.mic_16k || !s_feed.ref_16k || !s_feed.afe_frame) {
        free_feed_buffers();
        return false;
    }
    return true;
}

void voice_service_feed_playback(const int16_t *stereo, size_t frames)
{
    if (stereo == NULL || frames == 0 || s_ref_ring == NULL || s_ref_mutex == NULL ||
        xSemaphoreTake(s_ref_mutex, 0) != pdTRUE) {
        return;
    }
    for (size_t i = 0; i < frames; ++i) {
        if (s_ref_count == VOICE_REF_RING_FRAMES) {
            s_ref_read = (s_ref_read + 1) % VOICE_REF_RING_FRAMES;
            --s_ref_count;
        }
        s_ref_ring[s_ref_write * 2] = stereo[i * 2];
        s_ref_ring[s_ref_write * 2 + 1] = stereo[i * 2 + 1];
        s_ref_write = (s_ref_write + 1) % VOICE_REF_RING_FRAMES;
        ++s_ref_count;
    }
    xSemaphoreGive(s_ref_mutex);
}

static void read_reference(int16_t *out, size_t frames)
{
    memset(out, 0, frames * 2 * sizeof(*out));
    if (xSemaphoreTake(s_ref_mutex, pdMS_TO_TICKS(2)) != pdTRUE) {
        return;
    }
    size_t target = VOICE_AEC_DELAY_FRAMES + frames;
    while (s_ref_count > target) {
        s_ref_read = (s_ref_read + 1) % VOICE_REF_RING_FRAMES;
        --s_ref_count;
    }
    while (s_ref_count < target) {
        s_ref_ring[s_ref_write * 2] = 0;
        s_ref_ring[s_ref_write * 2 + 1] = 0;
        s_ref_write = (s_ref_write + 1) % VOICE_REF_RING_FRAMES;
        ++s_ref_count;
    }
    for (size_t i = 0; i < frames; ++i) {
        out[i * 2] = s_ref_ring[s_ref_read * 2];
        out[i * 2 + 1] = s_ref_ring[s_ref_read * 2 + 1];
        s_ref_read = (s_ref_read + 1) % VOICE_REF_RING_FRAMES;
        --s_ref_count;
    }
    xSemaphoreGive(s_ref_mutex);
}

static void publish_event(voice_event_t event, voice_command_t command,
                          voice_language_t language, float confidence)
{
    voice_result_t result = {event, command, language, confidence};
    if (xQueueSend(s_result_queue, &result, 0) != pdTRUE && event == VOICE_EVENT_COMMAND) {
        voice_result_t stale;
        (void)xQueueReceive(s_result_queue, &stale, 0);
        (void)xQueueSend(s_result_queue, &result, 0);
    }
}

static void voice_feed_task(void *arg)
{
    (void)arg;
    size_t pending = 0;
    unsigned dropped = 0;
    unsigned consecutive_failures = 0;
    for (;;) {
        if (esp_codec_dev_read(s_mic_dev, s_feed.mic_44k,
                               VOICE_IO_FRAMES * 4) != ESP_CODEC_DEV_OK) {
            if (++consecutive_failures >= 100) {
                fail_start("Microphone capture stopped");
                publish_event(VOICE_EVENT_ERROR, VOICE_COMMAND_NONE, VOICE_LANGUAGE_ENGLISH, 0);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        read_reference(s_feed.ref_stereo_44k, VOICE_IO_FRAMES);
        for (size_t i = 0; i < VOICE_IO_FRAMES; ++i) {
            s_feed.ref_44k[i] = (int16_t)(((int32_t)s_feed.ref_stereo_44k[i * 2] +
                                           s_feed.ref_stereo_44k[i * 2 + 1]) / 2);
        }
        uint32_t mic_frames = s_feed.mic_out_bytes / sizeof(*s_feed.mic_16k);
        uint32_t ref_frames = s_feed.ref_out_bytes / sizeof(*s_feed.ref_16k);
        if (esp_asrc_process(s_mic_asrc, (uint8_t *)s_feed.mic_44k, VOICE_IO_FRAMES,
                             (uint8_t *)s_feed.mic_16k, &mic_frames) != ESP_ASRC_ERR_OK ||
            esp_asrc_process(s_ref_asrc, (uint8_t *)s_feed.ref_44k, VOICE_IO_FRAMES,
                             (uint8_t *)s_feed.ref_16k, &ref_frames) != ESP_ASRC_ERR_OK ||
            mic_frames != ref_frames) {
            if (++dropped % 100 == 1) {
                ESP_LOGW(TAG, "Dropped ASRC frame (%u total)", dropped);
            }
            if (++consecutive_failures >= 100) {
                fail_start("Audio resampler stopped");
                publish_event(VOICE_EVENT_ERROR, VOICE_COMMAND_NONE, VOICE_LANGUAGE_ENGLISH, 0);
                break;
            }
            continue;
        }
        consecutive_failures = 0;
        for (uint32_t i = 0; i < mic_frames; ++i) {
            s_feed.afe_frame[pending * 2] = s_feed.mic_16k[i];
            s_feed.afe_frame[pending * 2 + 1] = s_feed.ref_16k[i];
            if (++pending == (size_t)s_feed.afe_frames) {
                (void)s_afe->feed(s_afe_data, s_feed.afe_frame);
                pending = 0;
            }
        }
    }
    if (s_fetch_task != NULL) {
        vTaskDelete(s_fetch_task);
        s_fetch_task = NULL;
    }
    free_feed_buffers();
    s_feed_task = NULL;
    vTaskDelete(NULL);
}

static void voice_fetch_task(void *arg)
{
    (void)arg;
    bool command_window = false;
    voice_mode_t active_mode = VOICE_MODE_GLOBAL_WAKE;
    for (;;) {
        afe_fetch_result_t *fetch = s_afe->fetch_with_delay(s_afe_data, pdMS_TO_TICKS(200));
        if (fetch == NULL || fetch->ret_value != ESP_OK) {
            continue;
        }
        voice_mode_t mode = s_mode;
        if (mode != active_mode) {
            active_mode = mode;
            command_window = mode == VOICE_MODE_CONTINUOUS;
            s_mn->clean(s_mn_data);
            if (mode == VOICE_MODE_CONTINUOUS) {
                s_afe->disable_wakenet(s_afe_data);
                publish_event(VOICE_EVENT_LISTENING, VOICE_COMMAND_NONE,
                              VOICE_LANGUAGE_ENGLISH, 0);
            } else {
                s_afe->enable_wakenet(s_afe_data);
            }
        }
        if (mode == VOICE_MODE_CONTINUOUS) {
            command_window = true;
        } else if (fetch->wakeup_state == WAKENET_DETECTED) {
            command_window = true;
            ESP_LOGI(TAG, "[WAKE] 'Hi ESP' detected! Audio energy: %.1f dBFS. Listening...", fetch->data_volume);
            publish_event(VOICE_EVENT_WAKE, VOICE_COMMAND_NONE, VOICE_LANGUAGE_JAPANESE, 1);
            s_mn->clean(s_mn_data);
        }
        if (!command_window || fetch->data == NULL) {
            continue;
        }
        esp_mn_state_t state = s_mn->detect(s_mn_data, fetch->data);
        if (state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *results = s_mn->get_results(s_mn_data);
            if (results != NULL && results->num > 0) {
                voice_language_t language;
                voice_command_t command = voice_service_normalize_multinet_id(
                    results->command_id[0], &language);
                ESP_LOGI(TAG, "[MN DETECTED] id=%d, command=%d, lang=%s, prob=%.3f, matched='%s', raw='%s'",
                         results->command_id[0], command,
                         language == VOICE_LANGUAGE_JAPANESE ? "JA" : "EN",
                         results->prob[0], results->string, results->raw_string);
                if (command != VOICE_COMMAND_NONE) {
                    publish_event(VOICE_EVENT_COMMAND, command, language, results->prob[0]);
                }
            }
            s_mn->clean(s_mn_data);
            command_window = mode == VOICE_MODE_CONTINUOUS;
        } else if (state == ESP_MN_STATE_TIMEOUT) {
            esp_mn_results_t *results = s_mn->get_results(s_mn_data);
            ESP_LOGW(TAG, "[MN TIMEOUT] Speech window expired without confident match. raw_heard='%s'",
                     (results && results->raw_string[0]) ? results->raw_string : "none");
            s_mn->clean(s_mn_data);
            publish_event(VOICE_EVENT_IDLE, VOICE_COMMAND_NONE, VOICE_LANGUAGE_ENGLISH, 0);
            command_window = (mode == VOICE_MODE_CONTINUOUS);
        }
    }
}

bool voice_service_start(void)
{
    s_result_queue = xQueueCreate(8, sizeof(voice_result_t));
    s_ref_mutex = xSemaphoreCreateMutex();
    s_ref_ring = heap_caps_calloc(VOICE_REF_RING_FRAMES * 2, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_result_queue || !s_ref_mutex || !s_ref_ring) return fail_start("Voice buffers unavailable");
    if (!open_microphone()) return fail_start("Microphone open failed");
    if (!open_asrc()) return fail_start("S31 ASRC open failed");
    if (!open_speech_models()) return fail_start("Speech models unavailable");
    if (!register_commands()) return fail_start("Command registration failed");
    if (!allocate_feed_buffers()) return fail_start("Speech frame buffers unavailable");
    if (xTaskCreatePinnedToCore(voice_feed_task, "voice_feed", 6144, NULL, 9,
                                &s_feed_task, 0) != pdPASS) {
        free_feed_buffers();
        return fail_start("Voice feed task failed");
    }
    if (xTaskCreatePinnedToCore(voice_fetch_task, "voice_fetch", 6144, NULL, 7,
                                &s_fetch_task, 0) != pdPASS) {
        vTaskDelete(s_feed_task);
        s_feed_task = NULL;
        free_feed_buffers();
        return fail_start("Voice fetch task failed");
    }
    s_ready = true;
    s_error[0] = '\0';
    ESP_LOGI(TAG, "Voice service ready: global Japanese wake + bilingual commands");
    return true;
}

bool voice_service_is_ready(void) { return s_ready; }
const char *voice_service_error(void) { return s_error; }
void voice_service_set_mode(voice_mode_t mode) { s_mode = mode; }
voice_mode_t voice_service_get_mode(void) { return s_mode; }

bool voice_service_receive(voice_result_t *result)
{
    return result != NULL && s_result_queue != NULL &&
           xQueueReceive(s_result_queue, result, 0) == pdTRUE;
}

#endif
