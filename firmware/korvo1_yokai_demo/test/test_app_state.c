#include <assert.h>

#include "app_state.h"

static void test_home_resets_navigation(void)
{
    app_state_t state;
    app_state_init(&state);
    app_state_dispatch(&state, APP_EVENT_NEXT_PAGE);
    app_state_dispatch(&state, APP_EVENT_OPEN_SYNTH);
    app_state_dispatch(&state, APP_EVENT_HOME);
    assert(state.screen == APP_SCREEN_HOME);
    assert(state.home_page == 0);
    assert(!state.quick_settings_open);
}

static void test_wake_closes_quick_settings_and_listens(void)
{
    app_state_t state;
    app_state_init(&state);
    app_state_dispatch(&state, APP_EVENT_TOGGLE_QUICK_SETTINGS);
    app_state_dispatch(&state, APP_EVENT_WAKE_DETECTED);
    assert(!state.quick_settings_open);
    assert(state.screen == APP_SCREEN_VOICE);
    assert(state.voice == APP_VOICE_LISTENING);
}

static void test_voice_results_are_visible(void)
{
    app_state_t state;
    app_state_init(&state);
    app_state_dispatch(&state, APP_EVENT_WAKE_DETECTED);
    app_state_dispatch(&state, APP_EVENT_COMMAND_ACCEPTED);
    assert(state.voice == APP_VOICE_SUCCESS);
    app_state_dispatch(&state, APP_EVENT_WAKE_DETECTED);
    app_state_dispatch(&state, APP_EVENT_COMMAND_REJECTED);
    assert(state.voice == APP_VOICE_FAILURE);
}

static void test_app_entries_open_their_scenes(void)
{
    const app_event_t events[] = {
        APP_EVENT_OPEN_SYNTH,
        APP_EVENT_OPEN_WEATHER,
        APP_EVENT_OPEN_VOICE,
        APP_EVENT_OPEN_OBJECT_RECOGNITION,
    };
    const app_screen_t screens[] = {
        APP_SCREEN_SYNTH,
        APP_SCREEN_WEATHER,
        APP_SCREEN_VOICE,
        APP_SCREEN_OBJECT_RECOGNITION,
    };

    for (unsigned int i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
        app_state_t state;
        app_state_init(&state);
        app_state_dispatch(&state, events[i]);
        assert(state.screen == screens[i]);
        app_state_dispatch(&state, APP_EVENT_HOME);
        assert(state.screen == APP_SCREEN_HOME);
        assert(state.home_page == 0);
    }
}

int main(void)
{
    test_home_resets_navigation();
    test_wake_closes_quick_settings_and_listens();
    test_voice_results_are_visible();
    test_app_entries_open_their_scenes();
    return 0;
}
