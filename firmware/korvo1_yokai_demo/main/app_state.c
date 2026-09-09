#include "app_state.h"

void app_state_init(app_state_t *state)
{
    if (!state) {
        return;
    }
    state->screen = APP_SCREEN_HOME;
    state->voice = APP_VOICE_IDLE;
    state->home_page = 0;
    state->quick_settings_open = false;
}

void app_state_dispatch(app_state_t *state, app_event_t event)
{
    if (!state) {
        return;
    }

    switch (event) {
    case APP_EVENT_HOME:
        state->screen = APP_SCREEN_HOME;
        state->home_page = 0;
        state->quick_settings_open = false;
        state->voice = APP_VOICE_IDLE;
        break;
    case APP_EVENT_NEXT_PAGE:
        if (state->screen == APP_SCREEN_HOME) {
            state->home_page = 1;
        }
        break;
    case APP_EVENT_PREVIOUS_PAGE:
        if (state->screen == APP_SCREEN_HOME) {
            state->home_page = 0;
        }
        break;
    case APP_EVENT_TOGGLE_QUICK_SETTINGS:
        state->quick_settings_open = !state->quick_settings_open;
        break;
    case APP_EVENT_OPEN_SYNTH:
        state->screen = APP_SCREEN_SYNTH;
        state->quick_settings_open = false;
        break;
    case APP_EVENT_OPEN_WEATHER:
        state->screen = APP_SCREEN_WEATHER;
        state->quick_settings_open = false;
        break;
    case APP_EVENT_OPEN_VOICE:
        state->screen = APP_SCREEN_VOICE;
        state->voice = APP_VOICE_IDLE;
        state->quick_settings_open = false;
        break;
    case APP_EVENT_OPEN_OBJECT_RECOGNITION:
        state->screen = APP_SCREEN_OBJECT_RECOGNITION;
        state->quick_settings_open = false;
        break;
    case APP_EVENT_OPEN_LIGHTING:
        state->screen = APP_SCREEN_LIGHTING;
        state->quick_settings_open = false;
        break;
    case APP_EVENT_OPEN_CLOCK_TIMER:
        state->screen = APP_SCREEN_CLOCK_TIMER;
        state->quick_settings_open = false;
        break;
    case APP_EVENT_OPEN_CALCULATOR:
        state->screen = APP_SCREEN_CALCULATOR;
        state->quick_settings_open = false;
        break;
    case APP_EVENT_OPEN_FOOD:
        state->screen = APP_SCREEN_FOOD;
        state->quick_settings_open = false;
        break;
    case APP_EVENT_WAKE_DETECTED:
        state->screen = APP_SCREEN_VOICE;
        state->quick_settings_open = false;
        state->voice = APP_VOICE_LISTENING;
        break;
    case APP_EVENT_COMMAND_ACCEPTED:
        if (state->screen == APP_SCREEN_VOICE) {
            state->voice = APP_VOICE_SUCCESS;
        }
        break;
    case APP_EVENT_COMMAND_REJECTED:
        if (state->screen == APP_SCREEN_VOICE) {
            state->voice = APP_VOICE_FAILURE;
        }
        break;
    }
}
