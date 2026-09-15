#pragma once

#include "lvgl.h"
#include "board_ui.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_drawer_wifi_toggle_cb_t)(bool enable);
typedef void (*ui_drawer_wifi_details_cb_t)(void);
typedef void (*ui_drawer_bt_toggle_cb_t)(bool enable);
typedef void (*ui_drawer_bt_details_cb_t)(void);
typedef void (*ui_drawer_volume_cb_t)(int volume);
typedef void (*ui_drawer_brightness_cb_t)(int brightness);

/** Create the Quick Settings Drawer modal overlay. */
lv_obj_t *ui_drawer_create(lv_obj_t *parent,
                           ui_drawer_wifi_toggle_cb_t wifi_cb,
                           ui_drawer_wifi_details_cb_t wifi_details_cb,
                           ui_drawer_bt_toggle_cb_t bt_cb,
                           ui_drawer_bt_details_cb_t bt_details_cb,
                           ui_drawer_volume_cb_t vol_cb,
                           ui_drawer_brightness_cb_t bright_cb);

/** Open or close the drawer with animation. */
void ui_drawer_set_visible(bool visible);

/** Toggle drawer visibility. */
void ui_drawer_toggle(void);

/** Check if drawer is currently open. */
bool ui_drawer_is_visible(void);

/** Update drawer Wi-Fi status display and sync switches. */
void ui_drawer_update_status(const board_wifi_info_t *wifi_info);

/** Update drawer Bluetooth status display and sync switch. */
void ui_drawer_update_bt_status(bool enabled, bool connected, const char *dev_name);

#ifdef __cplusplus
}
#endif
