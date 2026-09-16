#include "ui/ui_home.h"
#include "ui/ui_theme.h"
#include "ui/ui_wifi_signal.h"
#include <stdio.h>

static lv_obj_t *s_scr_home = NULL;
static lv_obj_t *s_tileview = NULL;
static lv_obj_t *s_tile1 = NULL;
static lv_obj_t *s_tile2 = NULL;
static lv_obj_t *s_lbl_clock = NULL;
static lv_obj_t *s_lbl_wifi_status = NULL;
static ui_wifi_signal_t s_home_wifi_sig;
static ui_app_launch_cb_t s_launch_cb = NULL;
static ui_quick_settings_toggle_cb_t s_drawer_cb = NULL;

static void app_card_click_event_cb(lv_event_t *e)
{
    uintptr_t app_id = (uintptr_t)lv_event_get_user_data(e);
    if (s_launch_cb) {
        s_launch_cb((ui_app_id_t)app_id);
    }
}

static void nav_to_page1_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_tileview && s_tile1) {
        lv_obj_set_tile(s_tileview, s_tile1, LV_ANIM_ON);
    }
}

static void nav_to_page2_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_tileview && s_tile2) {
        lv_obj_set_tile(s_tileview, s_tile2, LV_ANIM_ON);
    }
}

static lv_point_t s_topbar_touch_start = {0, 0};
static bool s_topbar_drag_triggered = false;

static void top_bar_touch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();

    if (code == LV_EVENT_PRESSED) {
        if (indev) {
            lv_indev_get_point(indev, &s_topbar_touch_start);
        }
        s_topbar_drag_triggered = false;
    } else if (code == LV_EVENT_PRESSING) {
        if (indev && !s_topbar_drag_triggered) {
            lv_point_t curr;
            lv_indev_get_point(indev, &curr);
            int32_t dy = curr.y - s_topbar_touch_start.y;
            int32_t dx = curr.x - s_topbar_touch_start.x;
            if (dx < 0) dx = -dx;
            if (dy >= 20 && dy > dx) {
                s_topbar_drag_triggered = true;
                if (s_drawer_cb) {
                    s_drawer_cb();
                }
            }
        }
    } else if (code == LV_EVENT_CLICKED) {
        if (!s_topbar_drag_triggered && s_drawer_cb) {
            s_drawer_cb();
        }
    }
}

static lv_obj_t *create_touch_hotspot(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                                      lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 6, 0);

    /* Tactile pressed feedback */
    lv_obj_set_style_bg_color(btn, UI_COLOR_GOLD_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 2, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, UI_COLOR_GOLD_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(btn, LV_OPA_70, LV_STATE_PRESSED);

    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

lv_obj_t *ui_home_screen_create(ui_app_launch_cb_t app_cb, ui_quick_settings_toggle_cb_t drawer_cb)
{
    s_launch_cb = app_cb;
    s_drawer_cb = drawer_cb;

    s_scr_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_home, lv_color_hex(0x141D33), 0);
    lv_obj_set_style_bg_opa(s_scr_home, LV_OPA_COVER, 0);

    /* 2-Page Horizontal TileView */
    s_tileview = lv_tileview_create(s_scr_home);
    lv_obj_set_size(s_tileview, 800, 480);
    lv_obj_set_pos(s_tileview, 0, 0);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tileview, 0, 0);
    lv_obj_set_style_pad_all(s_tileview, 0, 0);
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_anim_duration(s_tileview, 180, 0);

    /* --- Tile 1: Page 1 (Raijin Synth, Yukionna Weather, Voice, Vision) --- */
    s_tile1 = lv_tileview_add_tile(s_tileview, 0, 0, LV_DIR_HOR);
    lv_obj_remove_flag(s_tile1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_tile1, 0, 0);
    lv_obj_set_style_border_width(s_tile1, 0, 0);

    lv_obj_t *img_p1 = lv_image_create(s_tile1);
    lv_image_set_src(img_p1, &ui_img_home_p1);
    lv_obj_set_pos(img_p1, 0, 0);
    lv_obj_set_size(img_p1, 800, 480);
    lv_obj_remove_flag(img_p1, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Page 1 App Hotspots (Top-Left, Top-Right, Bottom-Left, Bottom-Right) */
    create_touch_hotspot(s_tile1, 48, 39, 346, 188, app_card_click_event_cb, (void *)(uintptr_t)UI_APP_SYNTH);
    create_touch_hotspot(s_tile1, 406, 39, 346, 188, app_card_click_event_cb, (void *)(uintptr_t)UI_APP_WEATHER);
    create_touch_hotspot(s_tile1, 48, 232, 346, 189, app_card_click_event_cb, (void *)(uintptr_t)UI_APP_VOICE);
    create_touch_hotspot(s_tile1, 406, 232, 346, 189, app_card_click_event_cb, (void *)(uintptr_t)UI_APP_VISION);

    /* Page 1 Navigation Button (Right Arrow -> Page 2) */
    create_touch_hotspot(s_tile1, 700, 425, 90, 50, nav_to_page2_event_cb, NULL);

    /* --- Tile 2: Page 2 (Fireworks, Clock, Calculator, Food) --- */
    s_tile2 = lv_tileview_add_tile(s_tileview, 1, 0, LV_DIR_HOR);
    lv_obj_remove_flag(s_tile2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_tile2, 0, 0);
    lv_obj_set_style_border_width(s_tile2, 0, 0);

    lv_obj_t *img_p2 = lv_image_create(s_tile2);
    lv_image_set_src(img_p2, &ui_img_home_p2);
    lv_obj_set_pos(img_p2, 0, 0);
    lv_obj_set_size(img_p2, 800, 480);
    lv_obj_remove_flag(img_p2, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Page 2 App Hotspots (Top-Left, Top-Right, Bottom-Left, Bottom-Right) */
    create_touch_hotspot(s_tile2, 48, 39, 346, 188, app_card_click_event_cb, (void *)(uintptr_t)UI_APP_FIREWORKS);
    create_touch_hotspot(s_tile2, 406, 39, 346, 188, app_card_click_event_cb, (void *)(uintptr_t)UI_APP_CLOCK);
    create_touch_hotspot(s_tile2, 48, 232, 346, 189, app_card_click_event_cb, (void *)(uintptr_t)UI_APP_CALCULATOR);
    create_touch_hotspot(s_tile2, 406, 232, 346, 189, app_card_click_event_cb, (void *)(uintptr_t)UI_APP_FOOD);

    /* Page 2 Navigation Buttons (Left Arrow -> Page 1, Home Icon -> Page 1) */
    create_touch_hotspot(s_tile2, 10, 425, 90, 50, nav_to_page1_event_cb, NULL);
    create_touch_hotspot(s_tile2, 350, 425, 100, 50, nav_to_page1_event_cb, NULL);

    /* --- Fixed Top Status Bar (Layered above TileView) --- */
    lv_obj_t *top_bar = lv_obj_create(s_scr_home);
    lv_obj_set_size(top_bar, 800, 38);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x141D33), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_border_side(top_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(top_bar, lv_color_hex(0x2A3756), 0);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(top_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(top_bar, 16);
    lv_obj_add_event_cb(top_bar, top_bar_touch_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_logo = lv_label_create(top_bar);
    lv_label_set_text(lbl_logo, "妖怪端末 (Yokai OS)");
    lv_obj_set_style_text_color(lbl_logo, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_logo, UI_FONT_SMALL, 0);
    lv_obj_align(lbl_logo, LV_ALIGN_LEFT_MID, 20, 0);

    /* Wi-Fi 4-Bar Signal Indicator + Status Text */
    ui_wifi_signal_create(&s_home_wifi_sig, top_bar, 305, 11);

    s_lbl_wifi_status = lv_label_create(top_bar);
    lv_label_set_text(s_lbl_wifi_status, "Wi-Fi: ○ 未接続");
    lv_obj_set_style_text_color(s_lbl_wifi_status, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_wifi_status, UI_FONT_SMALL, 0);
    lv_obj_align(s_lbl_wifi_status, LV_ALIGN_LEFT_MID, 334, 0);

    s_lbl_clock = lv_label_create(top_bar);
    lv_label_set_text(s_lbl_clock, "--:--");
    lv_obj_set_style_text_color(s_lbl_clock, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(s_lbl_clock, UI_FONT_SMALL, 0);
    lv_obj_align(s_lbl_clock, LV_ALIGN_RIGHT_MID, -20, 0);

    return s_scr_home;
}

void ui_home_screen_update_status(const char *time_str, bool wifi_connected, int rssi)
{
    if (s_lbl_clock && time_str) {
        lv_label_set_text(s_lbl_clock, time_str);
    }
    if (s_lbl_wifi_status) {
        if (wifi_connected) {
            lv_label_set_text(s_lbl_wifi_status, "Wi-Fi: ● 接続中");
            lv_obj_set_style_text_color(s_lbl_wifi_status, UI_COLOR_CYAN_ACCENT, 0);
            int level = ui_wifi_rssi_to_level(rssi);
            ui_wifi_signal_set_level(&s_home_wifi_sig, level);
        } else {
            lv_label_set_text(s_lbl_wifi_status, "Wi-Fi: ○ 未接続");
            lv_obj_set_style_text_color(s_lbl_wifi_status, UI_COLOR_TEXT_SUB, 0);
            ui_wifi_signal_set_level(&s_home_wifi_sig, 0);
        }
    }
}
