#pragma once

#include "lvgl.h"
#include "weather_service.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_image_dsc_t ui_img_weather_sunny;
extern lv_image_dsc_t ui_img_weather_cloudy;
extern lv_image_dsc_t ui_img_weather_rain;
extern lv_image_dsc_t ui_img_weather_night;
#define ui_img_weather_day ui_img_weather_sunny


typedef void (*ui_home_btn_cb_t)(void);

/** Create the Yokai Yukionna Weather Screen. */
lv_obj_t *ui_weather_screen_create(ui_home_btn_cb_t home_cb);

/** Update weather screen with the latest weather info. */
void ui_weather_screen_update(const weather_info_t *info);

/** Update weather screen top bar status (clock, Wi-Fi signal, volume). */
void ui_weather_screen_update_status(const char *time_str, bool wifi_connected, int rssi, int volume);

/** Pause or resume Lottie snow animation based on screen active state. */
void ui_weather_set_active(bool active);

#ifdef __cplusplus
}
#endif
