#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_APP_SYNTH = 0,
    UI_APP_WEATHER,
    UI_APP_VOICE,
    UI_APP_VISION,
    UI_APP_FIREWORKS,
    UI_APP_CLOCK,
    UI_APP_CALCULATOR,
    UI_APP_FOOD,
} ui_app_id_t;

typedef void (*ui_app_launch_cb_t)(ui_app_id_t app);
typedef void (*ui_quick_settings_toggle_cb_t)(void);

/** Create the 2-Page Desktop Launcher Screen. */
lv_obj_t *ui_home_screen_create(ui_app_launch_cb_t app_cb, ui_quick_settings_toggle_cb_t drawer_cb);

/** Update home screen status header (clock, Wi-Fi icon). */
void ui_home_screen_update_status(const char *time_str, bool wifi_connected);

#ifdef __cplusplus
}
#endif
