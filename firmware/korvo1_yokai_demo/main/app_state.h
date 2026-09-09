#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_SCREEN_HOME,
    APP_SCREEN_SYNTH,
    APP_SCREEN_WEATHER,
    APP_SCREEN_VOICE,
    APP_SCREEN_OBJECT_RECOGNITION,
    APP_SCREEN_LIGHTING,
    APP_SCREEN_CLOCK_TIMER,
    APP_SCREEN_CALCULATOR,
    APP_SCREEN_FOOD,
} app_screen_t;

typedef enum {
    APP_VOICE_IDLE,
    APP_VOICE_LISTENING,
    APP_VOICE_PROCESSING,
    APP_VOICE_SUCCESS,
    APP_VOICE_FAILURE,
} app_voice_state_t;

typedef enum {
    APP_EVENT_HOME,
    APP_EVENT_NEXT_PAGE,
    APP_EVENT_PREVIOUS_PAGE,
    APP_EVENT_TOGGLE_QUICK_SETTINGS,
    APP_EVENT_OPEN_SYNTH,
    APP_EVENT_OPEN_WEATHER,
    APP_EVENT_OPEN_VOICE,
    APP_EVENT_OPEN_OBJECT_RECOGNITION,
    APP_EVENT_OPEN_LIGHTING,
    APP_EVENT_OPEN_CLOCK_TIMER,
    APP_EVENT_OPEN_CALCULATOR,
    APP_EVENT_OPEN_FOOD,
    APP_EVENT_WAKE_DETECTED,
    APP_EVENT_COMMAND_ACCEPTED,
    APP_EVENT_COMMAND_REJECTED,
} app_event_t;

typedef struct {
    app_screen_t screen;
    app_voice_state_t voice;
    uint8_t home_page;
    bool quick_settings_open;
} app_state_t;

void app_state_init(app_state_t *state);
void app_state_dispatch(app_state_t *state, app_event_t event);
