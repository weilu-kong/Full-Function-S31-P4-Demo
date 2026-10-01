#pragma once

#include "lvgl.h"
#include "board_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_wifi_back_cb_t)(void);

/** Create the Japanese Yokai styled full-screen Wi-Fi management screen. */
lv_obj_t *ui_wifi_screen_create(ui_wifi_back_cb_t on_back_cb);

/** Update the Wi-Fi screen with latest board Wi-Fi state snapshot. */
void ui_wifi_screen_update(const board_wifi_info_t *info);

#ifdef __cplusplus
}
#endif
