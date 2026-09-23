#pragma once

#include "lvgl.h"
#include "ui/ui_weather.h" /* for ui_home_btn_cb_t */
#include "voice_service.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_voice_screen_create(ui_home_btn_cb_t home_cb);
lv_obj_t *ui_vision_screen_create(ui_home_btn_cb_t home_cb);
lv_obj_t *ui_food_screen_create(ui_home_btn_cb_t home_cb);

void ui_voice_screen_update(const voice_result_t *result, int volume,
                            bool service_ready, const char *error_text);

void ui_vision_set_active(bool active);
void ui_vision_screen_update(void);

/** Periodic hook for clock and timer updates */
void ui_apps_tick_periodic(void);

#ifdef __cplusplus
}
#endif
