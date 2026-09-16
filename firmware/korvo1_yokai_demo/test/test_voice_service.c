#include <assert.h>
#include <stdio.h>

#include "voice_service.h"

static void test_command_table(void)
{
    assert(voice_service_command_count() == 15);
    const voice_command_info_t *music = voice_service_command_info(VOICE_COMMAND_SYNTH);
    assert(music != NULL);
    assert(music->target == VOICE_TARGET_SYNTH);
    assert(music->english != NULL);
    assert(music->japanese != NULL);
}

static void test_multinet_id_normalization(void)
{
    voice_language_t language = VOICE_LANGUAGE_ENGLISH;
    assert(voice_service_normalize_multinet_id(VOICE_COMMAND_WEATHER, &language) == VOICE_COMMAND_WEATHER);
    assert(language == VOICE_LANGUAGE_ENGLISH);
    assert(voice_service_normalize_multinet_id(100 + VOICE_COMMAND_WEATHER, &language) == VOICE_COMMAND_WEATHER);
    assert(language == VOICE_LANGUAGE_JAPANESE);
    assert(voice_service_normalize_multinet_id(999, &language) == VOICE_COMMAND_NONE);
}

static void test_volume_semantics(void)
{
    int saved = 80;
    assert(voice_service_apply_volume(VOICE_COMMAND_VOLUME_UP, 95, &saved) == 100);
    assert(voice_service_apply_volume(VOICE_COMMAND_VOLUME_DOWN, 5, &saved) == 0);
    assert(voice_service_apply_volume(VOICE_COMMAND_MUTE, 60, &saved) == 0);
    assert(saved == 60);
    assert(voice_service_apply_volume(VOICE_COMMAND_MUTE, 0, &saved) == 0);
    assert(saved == 60);
    assert(voice_service_apply_volume(VOICE_COMMAND_UNMUTE, 0, &saved) == 60);
}

int main(void)
{
    test_command_table();
    test_multinet_id_normalization();
    test_volume_semantics();
    puts("Voice command checks passed.");
    return 0;
}
