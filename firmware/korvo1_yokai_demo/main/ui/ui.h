#pragma once

#include "lvgl.h"
#include "app_state.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SCREEN_HOME = 0,
    UI_SCREEN_SYNTH,
    UI_SCREEN_WEATHER,
    UI_SCREEN_MAX,
} ui_screen_t;

/** Initialize all Yokai LVGL UI screens and drawer overlay. */
void ui_init(lv_display_t *disp, app_state_t *state);

/** Navigate to a specific screen with animation. */
void ui_switch_screen(ui_screen_t target);

/** Get currently active screen. */
ui_screen_t ui_get_current_screen(void);

/** Periodic tick hook for updating waveform, weather info, and clock. */
void ui_tick_periodic(void);

#ifdef __cplusplus
}
#endif
