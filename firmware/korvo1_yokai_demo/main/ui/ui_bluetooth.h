#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_bt_home_cb_t)(void);

/**
 * @brief Create full-screen Bluetooth management screen.
 */
lv_obj_t *ui_bluetooth_screen_create(ui_bt_home_cb_t home_cb);

/**
 * @brief Update Bluetooth screen data and status in real-time.
 */
void ui_bluetooth_screen_update(bool enabled, bool connected, bool streaming,
                                const char *dev_name, const char *bda_str);

#ifdef __cplusplus
}
#endif
