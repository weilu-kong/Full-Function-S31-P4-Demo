#include "voice_service.h"

#include <stddef.h>

#define VOICE_MN_JAPANESE_BASE 100

static const voice_command_info_t s_commands[] = {
    {VOICE_COMMAND_SYNTH,       VOICE_TARGET_SYNTH,      "雷神合成器", "OPEN THE SYNTHESIZER",   "音楽",           "OWN GAH COO",      "bN Gn Ko"},
    {VOICE_COMMAND_WEATHER,     VOICE_TARGET_WEATHER,    "雪女天气",   "SHOW THE WEATHER",        "天気",           "TEN KEY",          "TfN Km"},
    {VOICE_COMMAND_VOICE,       VOICE_TARGET_VOICE,      "言灵神社",   "OPEN VOICE CONTROL",      "言霊",           "COAT OH DAH MAH",  "KbT b Dn Mn"},
    {VOICE_COMMAND_VISION,      VOICE_TARGET_VISION,     "目目连视觉", "OPEN THE CAMERA",         "カメラ",         "CAMERA",           "KaMRc"},
    {VOICE_COMMAND_FIREWORKS,   VOICE_TARGET_FIREWORKS,  "烟火应用",   "SHOW THE FIREWORKS",      "花火",           "HAH NAH BEE",      "hn Nn Bm"},
    {VOICE_COMMAND_CLOCK,       VOICE_TARGET_CLOCK,      "时钟",       "OPEN THE CLOCK",          "時計",           "TOE KAY",          "Tb Kd"},
    {VOICE_COMMAND_CALCULATOR,  VOICE_TARGET_CALCULATOR, "阴阳计算器", "OPEN THE CALCULATOR",     "計算",           "KAY SAHN",         "Kd SnN"},
    {VOICE_COMMAND_FOOD,        VOICE_TARGET_FOOD,       "食物应用",   "OPEN THE FOOD APP",       "料理",           "REE OH REE",       "Rm b Rm"},
    {VOICE_COMMAND_WIFI,        VOICE_TARGET_WIFI,       "Wi-Fi 设置", "OPEN WI FI SETTINGS",     "ワイファイ",     "WHY FLY",          "Wi FLi"},
    {VOICE_COMMAND_BLUETOOTH,   VOICE_TARGET_BLUETOOTH,  "蓝牙设置",   "OPEN BLUETOOTH SETTINGS", "ブルートゥース", "BLUE TOOTH",        "BLo Tov"},
    {VOICE_COMMAND_HOME,        VOICE_TARGET_HOME,       "返回桌面",   "GO BACK HOME",            "ホーム",         "HOME",              "hbM"},
    {VOICE_COMMAND_VOLUME_UP,   VOICE_TARGET_VOLUME,     "音量增加",   "TURN THE VOLUME UP",       "上げて",         "AH GET EH",         "c GfT f"},
    {VOICE_COMMAND_VOLUME_DOWN, VOICE_TARGET_VOLUME,     "音量降低",   "TURN THE VOLUME DOWN",     "下げて",         "SAH GET EH",        "Sn GfT f"},
    {VOICE_COMMAND_MUTE,        VOICE_TARGET_VOLUME,     "静音",       "MUTE THE SOUND",           "ミュート",       "MEW TOE",           "MYo Tb"},
    {VOICE_COMMAND_UNMUTE,      VOICE_TARGET_VOLUME,     "恢复音量",   "RESTORE THE VOLUME",       "戻して",         "MOE DOUGH SHE TEH", "Mb Db sm Tf"},
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

bool voice_service_start(void)
{
    return false;
}

#endif
