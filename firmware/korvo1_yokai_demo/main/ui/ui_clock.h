#pragma once
#include "lvgl.h"
#include "ui/ui_weather.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_clock_screen_create(ui_home_btn_cb_t home_cb);
void ui_clock_set_active(bool active);
void ui_clock_tick(void);

#ifdef __cplusplus
}
#endif
