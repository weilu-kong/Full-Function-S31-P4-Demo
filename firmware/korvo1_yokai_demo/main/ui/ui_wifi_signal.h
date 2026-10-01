#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *container;
    lv_obj_t *bars[4];
} ui_wifi_signal_t;

/** Convert RSSI in dBm to 4-bar level (0: no signal/disconnected, 1: weak, 2: fair, 3: good, 4: strong). */
int ui_wifi_rssi_to_level(int rssi);

/** Create a 4-bar signal widget (width ~20px, height ~16px) inside parent at (x, y). */
void ui_wifi_signal_create(ui_wifi_signal_t *sig, lv_obj_t *parent, int x, int y);

/** Set the active signal level (0 to 4). */
void ui_wifi_signal_set_level(ui_wifi_signal_t *sig, int level);

#ifdef __cplusplus
}
#endif
