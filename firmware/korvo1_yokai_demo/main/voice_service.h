#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    VOICE_COMMAND_NONE = 0,
    VOICE_COMMAND_SYNTH,
    VOICE_COMMAND_WEATHER,
    VOICE_COMMAND_VOICE,
    VOICE_COMMAND_VISION,
    VOICE_COMMAND_FIREWORKS,
    VOICE_COMMAND_CLOCK,
    VOICE_COMMAND_CALCULATOR,
    VOICE_COMMAND_FOOD,
    VOICE_COMMAND_WIFI,
    VOICE_COMMAND_BLUETOOTH,
    VOICE_COMMAND_HOME,
    VOICE_COMMAND_VOLUME_UP,
    VOICE_COMMAND_VOLUME_DOWN,
    VOICE_COMMAND_MUTE,
    VOICE_COMMAND_UNMUTE,
    VOICE_COMMAND_COUNT,
} voice_command_t;

typedef enum {
    VOICE_TARGET_NONE = 0,
    VOICE_TARGET_SYNTH,
    VOICE_TARGET_WEATHER,
    VOICE_TARGET_VOICE,
    VOICE_TARGET_VISION,
    VOICE_TARGET_FIREWORKS,
    VOICE_TARGET_CLOCK,
    VOICE_TARGET_CALCULATOR,
    VOICE_TARGET_FOOD,
    VOICE_TARGET_WIFI,
    VOICE_TARGET_BLUETOOTH,
    VOICE_TARGET_HOME,
    VOICE_TARGET_VOLUME,
} voice_target_t;

typedef enum {
    VOICE_LANGUAGE_ENGLISH = 0,
    VOICE_LANGUAGE_JAPANESE,
} voice_language_t;

typedef enum {
    VOICE_MODE_GLOBAL_WAKE = 0,
    VOICE_MODE_CONTINUOUS,
} voice_mode_t;

typedef enum {
    VOICE_EVENT_IDLE = 0,
    VOICE_EVENT_WAKE,
    VOICE_EVENT_LISTENING,
    VOICE_EVENT_COMMAND,
    VOICE_EVENT_RETRY,
    VOICE_EVENT_ERROR,
} voice_event_t;

typedef struct {
    voice_command_t command;
    voice_target_t target;
    const char *feature;
    const char *english;
    const char *japanese;
    const char *phonetic_alias;
    const char *phonemes;
} voice_command_info_t;

typedef struct {
    voice_event_t event;
    voice_command_t command;
    voice_language_t language;
    float confidence;
} voice_result_t;

size_t voice_service_command_count(void);
const voice_command_info_t *voice_service_command_info(voice_command_t command);
voice_command_t voice_service_normalize_multinet_id(int id, voice_language_t *language);
int voice_service_apply_volume(voice_command_t command, int current, int *saved_nonzero);

bool voice_service_start(void);
bool voice_service_is_ready(void);
const char *voice_service_error(void);
void voice_service_set_mode(voice_mode_t mode);
voice_mode_t voice_service_get_mode(void);
bool voice_service_receive(voice_result_t *result);
bool voice_service_inject_command(voice_command_t command);
void voice_service_feed_playback(const int16_t *stereo, size_t frames);
