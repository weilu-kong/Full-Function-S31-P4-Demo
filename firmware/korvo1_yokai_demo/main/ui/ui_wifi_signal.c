#include "ui/ui_wifi_signal.h"
#include "ui/ui_theme.h"

int ui_wifi_rssi_to_level(int rssi)
{
    if (rssi <= -95 || rssi == 0) {
        return 0;
    }
    if (rssi >= -55) {
        return 4;
    }
    if (rssi >= -70) {
        return 3;
    }
    if (rssi >= -80) {
        return 2;
    }
    return 1;
}

void ui_wifi_signal_create(ui_wifi_signal_t *sig, lv_obj_t *parent, int x, int y)
{
    if (!sig || !parent) {
        return;
    }

    sig->container = lv_obj_create(parent);
    lv_obj_remove_style_all(sig->container);
    lv_obj_set_size(sig->container, 22, 16);
    lv_obj_set_pos(sig->container, x, y);
    lv_obj_remove_flag(sig->container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    const int bar_h[4] = {4, 7, 11, 15};
    const int bar_w = 3;
    const int bar_gap = 2;

    for (int i = 0; i < 4; i++) {
        lv_obj_t *bar = lv_obj_create(sig->container);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, bar_w, bar_h[i]);
        lv_obj_set_pos(bar, i * (bar_w + bar_gap), 16 - bar_h[i]);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x4A5568), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_40, 0);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        sig->bars[i] = bar;
    }
}

void ui_wifi_signal_set_level(ui_wifi_signal_t *sig, int level)
{
    if (!sig || !sig->container) {
        return;
    }
    if (level < 0) {
        level = 0;
    }
    if (level > 4) {
        level = 4;
    }

    for (int i = 0; i < 4; i++) {
        if (!sig->bars[i]) {
            continue;
        }
        if (i < level) {
            lv_obj_set_style_bg_color(sig->bars[i], UI_COLOR_CYAN_ACCENT, 0);
            lv_obj_set_style_bg_opa(sig->bars[i], LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_color(sig->bars[i], lv_color_hex(0x4A5568), 0);
            lv_obj_set_style_bg_opa(sig->bars[i], LV_OPA_40, 0);
        }
    }
}
