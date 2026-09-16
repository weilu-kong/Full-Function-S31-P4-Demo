#include "ui/ui_drawer.h"
#include "ui/ui_theme.h"
#include <stdio.h>

static lv_obj_t *s_drawer_modal = NULL;
static lv_obj_t *s_drawer_panel = NULL;
static bool s_is_animating_close = false;

#define DRAWER_PANEL_W       800
#define DRAWER_PANEL_H       480
#define DRAWER_PANEL_REST_X  0
#define DRAWER_PANEL_OPEN_Y  0
#define DRAWER_PANEL_CLOSE_Y -480

static void drawer_anim_set_y(void *var, int32_t val)
{
    lv_obj_set_y((lv_obj_t *)var, val);
}

static void drawer_close_anim_ready_cb(lv_anim_t *a)
{
    (void)a;
    s_is_animating_close = false;
    if (s_drawer_modal) {
        lv_obj_add_flag(s_drawer_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *s_sw_wifi = NULL;
static lv_obj_t *s_sw_bt = NULL;
static lv_obj_t *s_card_wifi = NULL;
static lv_obj_t *s_card_bt = NULL;
static lv_obj_t *s_slider_vol = NULL;
static lv_obj_t *s_lbl_vol_val = NULL;
static lv_obj_t *s_lbl_wf_desc = NULL;
static lv_obj_t *s_lbl_bt_desc = NULL;

static ui_drawer_wifi_toggle_cb_t s_wifi_cb = NULL;
static ui_drawer_wifi_details_cb_t s_wifi_details_cb = NULL;
static ui_drawer_bt_toggle_cb_t s_bt_cb = NULL;
static ui_drawer_bt_details_cb_t s_bt_details_cb = NULL;
static ui_drawer_volume_cb_t s_vol_cb = NULL;

static void wifi_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool state = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (s_card_wifi) {
        if (state) {
            lv_obj_set_style_opa(s_card_wifi, LV_OPA_COVER, 0);
            lv_obj_add_flag(s_card_wifi, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_set_style_opa(s_card_wifi, LV_OPA_40, 0);
            lv_obj_remove_flag(s_card_wifi, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    if (s_wifi_cb) {
        s_wifi_cb(state);
    }
}

static void wifi_card_click_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    if (target == s_sw_wifi) {
        return;
    }
    bool enabled = lv_obj_has_state(s_sw_wifi, LV_STATE_CHECKED);
    if (enabled && s_wifi_details_cb) {
        ui_drawer_set_visible(false);
        s_wifi_details_cb();
    }
}

static void bt_card_click_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    if (target == s_sw_bt) {
        return;
    }
    bool enabled = lv_obj_has_state(s_sw_bt, LV_STATE_CHECKED);
    if (enabled && s_bt_details_cb) {
        ui_drawer_set_visible(false);
        s_bt_details_cb();
    }
}

static void bt_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool state = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (s_card_bt) {
        if (state) {
            lv_obj_set_style_opa(s_card_bt, LV_OPA_COVER, 0);
            lv_obj_add_flag(s_card_bt, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_set_style_opa(s_card_bt, LV_OPA_40, 0);
            lv_obj_remove_flag(s_card_bt, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    if (s_bt_cb) {
        s_bt_cb(state);
    }
}

static void vol_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", (int)val);
    lv_label_set_text(s_lbl_vol_val, buf);
    if (s_vol_cb) {
        s_vol_cb(val);
    }
}

static void close_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ui_drawer_set_visible(false);
}

static void backdrop_click_event_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    if (target == s_drawer_modal) {
        ui_drawer_set_visible(false);
    }
}

lv_obj_t *ui_drawer_create(lv_obj_t *parent,
                           ui_drawer_wifi_toggle_cb_t wifi_cb,
                           ui_drawer_wifi_details_cb_t wifi_details_cb,
                           ui_drawer_bt_toggle_cb_t bt_cb,
                           ui_drawer_bt_details_cb_t bt_details_cb,
                           ui_drawer_volume_cb_t vol_cb,
                           ui_drawer_brightness_cb_t bright_cb)
{
    s_wifi_cb = wifi_cb;
    s_wifi_details_cb = wifi_details_cb;
    s_bt_cb = bt_cb;
    s_bt_details_cb = bt_details_cb;
    s_vol_cb = vol_cb;
    (void)bright_cb;

    /* Full-Screen Dim Modal Backdrop */
    s_drawer_modal = lv_obj_create(parent);
    lv_obj_set_size(s_drawer_modal, 800, 480);
    lv_obj_set_pos(s_drawer_modal, 0, 0);
    lv_obj_set_style_pad_all(s_drawer_modal, 0, 0);
    lv_obj_set_style_bg_color(s_drawer_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_drawer_modal, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_drawer_modal, 0, 0);
    lv_obj_remove_flag(s_drawer_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_drawer_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_drawer_modal, backdrop_click_event_cb, LV_EVENT_CLICKED, NULL);

    /* Full-Screen Glassmorphic Drawer Panel (slides from above screen) */
    lv_obj_t *panel = lv_obj_create(s_drawer_modal);
    s_drawer_panel = panel;
    lv_obj_set_size(panel, DRAWER_PANEL_W, DRAWER_PANEL_H);
    lv_obj_set_pos(panel, DRAWER_PANEL_REST_X, DRAWER_PANEL_CLOSE_Y);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0C121E), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Header: Title */
    lv_obj_t *lbl_title = lv_label_create(panel);
    lv_label_set_text(lbl_title, "クイック設定 (Quick Settings)");
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_title, 30, 16);

    /* Top Subtle Drag Pill */
    lv_obj_t *pill_top = lv_obj_create(panel);
    lv_obj_set_size(pill_top, 50, 4);
    lv_obj_set_pos(pill_top, 375, 8);
    lv_obj_set_style_radius(pill_top, 2, 0);
    lv_obj_set_style_bg_color(pill_top, lv_color_hex(0x3B4860), 0);
    lv_obj_set_style_border_width(pill_top, 0, 0);
    lv_obj_remove_flag(pill_top, LV_OBJ_FLAG_SCROLLABLE);

    /* Close button (30px margin from right: 800 - 30 - 100 = 670) */
    lv_obj_t *btn_close = lv_button_create(panel);
    lv_obj_add_style(btn_close, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_close, 100, 34);
    lv_obj_set_pos(btn_close, 670, 14);
    lv_obj_add_event_cb(btn_close, close_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "閉じる");
    lv_obj_set_style_text_font(lbl_close, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_close);

    /* 1. Wi-Fi Card (358px wide, 30px from left) */
    s_card_wifi = lv_obj_create(panel);
    lv_obj_add_style(s_card_wifi, &ui_style_glass_card, 0);
    lv_obj_set_size(s_card_wifi, 358, 135);
    lv_obj_set_pos(s_card_wifi, 30, 70);
    lv_obj_remove_flag(s_card_wifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_card_wifi, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_card_wifi, wifi_card_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_wf = lv_label_create(s_card_wifi);
    lv_label_set_text(lbl_wf, "Wi-Fi (STA)");
    lv_obj_set_style_text_color(lbl_wf, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(lbl_wf, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(lbl_wf, 18, 18);

    s_lbl_wf_desc = lv_label_create(s_card_wifi);
    lv_label_set_text(s_lbl_wf_desc, "未接続 (タップして設定)");
    lv_obj_set_style_text_color(s_lbl_wf_desc, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_wf_desc, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_wf_desc, 18, 56);

    s_sw_wifi = lv_switch_create(s_card_wifi);
    lv_obj_set_pos(s_sw_wifi, 276, 24);
    lv_obj_set_style_bg_color(s_sw_wifi, UI_COLOR_CYAN_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_state(s_sw_wifi, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sw_wifi, wifi_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 2. Bluetooth Card (358px wide, gap 24px -> x = 412, right margin = 30px) */
    s_card_bt = lv_obj_create(panel);
    lv_obj_add_style(s_card_bt, &ui_style_glass_card, 0);
    lv_obj_set_size(s_card_bt, 358, 135);
    lv_obj_set_pos(s_card_bt, 412, 70);
    lv_obj_remove_flag(s_card_bt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_card_bt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_card_bt, bt_card_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_bt = lv_label_create(s_card_bt);
    lv_label_set_text(lbl_bt, "Bluetooth (A2DP)");
    lv_obj_set_style_text_color(lbl_bt, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(lbl_bt, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(lbl_bt, 18, 18);

    s_lbl_bt_desc = lv_label_create(s_card_bt);
    lv_label_set_text(s_lbl_bt_desc, "Yokai-Groovebox (タップして設定)");
    lv_obj_set_style_text_color(s_lbl_bt_desc, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_bt_desc, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_bt_desc, 18, 56);

    s_sw_bt = lv_switch_create(s_card_bt);
    lv_obj_set_pos(s_sw_bt, 276, 24);
    lv_obj_set_style_bg_color(s_sw_bt, UI_COLOR_GOLD_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_state(s_sw_bt, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sw_bt, bt_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 3. Master Volume Slider Card (740px wide, left margin 30, right margin 30) */
    lv_obj_t *card_vol = lv_obj_create(panel);
    lv_obj_add_style(card_vol, &ui_style_glass_card, 0);
    lv_obj_set_size(card_vol, 740, 135);
    lv_obj_set_pos(card_vol, 30, 230);
    lv_obj_remove_flag(card_vol, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_v = lv_label_create(card_vol);
    lv_label_set_text(lbl_v, "マスター音量 (Master Volume)");
    lv_obj_set_style_text_color(lbl_v, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(lbl_v, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(lbl_v, 20, 20);

    s_slider_vol = lv_slider_create(card_vol);
    lv_obj_set_size(s_slider_vol, 610, 20);
    lv_obj_set_pos(s_slider_vol, 20, 72);
    lv_slider_set_range(s_slider_vol, 0, 100);
    lv_slider_set_value(s_slider_vol, 70, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider_vol, UI_COLOR_CYAN_ACCENT, LV_PART_INDICATOR);
    lv_obj_add_event_cb(s_slider_vol, vol_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_lbl_vol_val = lv_label_create(card_vol);
    lv_label_set_text(s_lbl_vol_val, "70%");
    lv_obj_set_style_text_color(s_lbl_vol_val, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_vol_val, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(s_lbl_vol_val, 650, 66);

    /* Bottom: Swipe up hint and Drag Pill */
    lv_obj_t *lbl_hint = lv_label_create(panel);
    lv_label_set_text(lbl_hint, "▲ 上にスワイプして閉じる");
    lv_obj_set_style_text_color(lbl_hint, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_hint, UI_FONT_SMALL, 0);
    lv_obj_set_align(lbl_hint, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(lbl_hint, -24);

    lv_obj_t *pill_bot = lv_obj_create(panel);
    lv_obj_set_size(pill_bot, 60, 4);
    lv_obj_set_align(pill_bot, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(pill_bot, -10);
    lv_obj_set_style_radius(pill_bot, 2, 0);
    lv_obj_set_style_bg_color(pill_bot, lv_color_hex(0x45526C), 0);
    lv_obj_set_style_border_width(pill_bot, 0, 0);
    lv_obj_remove_flag(pill_bot, LV_OBJ_FLAG_SCROLLABLE);

    /* Initially Hidden and transparent */
    lv_obj_set_style_bg_opa(s_drawer_modal, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_drawer_modal, LV_OBJ_FLAG_HIDDEN);
    return s_drawer_modal;
}

void ui_drawer_set_visible(bool visible)
{
    if (!s_drawer_modal || !s_drawer_panel) return;

    if (visible) {
        if (!lv_obj_has_flag(s_drawer_modal, LV_OBJ_FLAG_HIDDEN) && !s_is_animating_close) {
            return;
        }
        s_is_animating_close = false;

        /* Show modal container */
        lv_obj_remove_flag(s_drawer_modal, LV_OBJ_FLAG_HIDDEN);

        /* Delete active animations */
        lv_anim_delete(s_drawer_panel, drawer_anim_set_y);

        /* Fast, buttery-smooth Panel Slide Down from top */
        int32_t cur_y = lv_obj_get_y(s_drawer_panel);
        if (cur_y >= DRAWER_PANEL_OPEN_Y || cur_y < DRAWER_PANEL_CLOSE_Y) {
            cur_y = DRAWER_PANEL_CLOSE_Y;
        }
        lv_anim_t a_panel;
        lv_anim_init(&a_panel);
        lv_anim_set_var(&a_panel, s_drawer_panel);
        lv_anim_set_values(&a_panel, cur_y, DRAWER_PANEL_OPEN_Y);
        lv_anim_set_duration(&a_panel, 180);
        lv_anim_set_path_cb(&a_panel, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a_panel, drawer_anim_set_y);
        lv_anim_start(&a_panel);
    } else {
        if (lv_obj_has_flag(s_drawer_modal, LV_OBJ_FLAG_HIDDEN) || s_is_animating_close) {
            return;
        }
        s_is_animating_close = true;

        /* Delete active animations */
        lv_anim_delete(s_drawer_panel, drawer_anim_set_y);

        /* Fast, crisp Panel Slide Up to top */
        lv_anim_t a_panel;
        lv_anim_init(&a_panel);
        lv_anim_set_var(&a_panel, s_drawer_panel);
        lv_anim_set_values(&a_panel, lv_obj_get_y(s_drawer_panel), DRAWER_PANEL_CLOSE_Y);
        lv_anim_set_duration(&a_panel, 150);
        lv_anim_set_path_cb(&a_panel, lv_anim_path_ease_in);
        lv_anim_set_exec_cb(&a_panel, drawer_anim_set_y);
        lv_anim_set_completed_cb(&a_panel, drawer_close_anim_ready_cb);
        lv_anim_start(&a_panel);
    }
}

void ui_drawer_toggle(void)
{
    if (ui_drawer_is_visible()) {
        ui_drawer_set_visible(false);
    } else {
        ui_drawer_set_visible(true);
    }
}

bool ui_drawer_is_visible(void)
{
    if (!s_drawer_modal) return false;
    return !lv_obj_has_flag(s_drawer_modal, LV_OBJ_FLAG_HIDDEN) && !s_is_animating_close;
}

void ui_drawer_update_status(const board_wifi_info_t *wifi_info)
{
    if (!s_lbl_wf_desc || !wifi_info) return;

    if (wifi_info->state == BOARD_WIFI_CONNECTED) {
        char buf[64];
        snprintf(buf, sizeof(buf), "接続済み: %s", wifi_info->connected_ssid);
        lv_label_set_text(s_lbl_wf_desc, buf);
        lv_obj_set_style_text_color(s_lbl_wf_desc, UI_COLOR_CYAN_ACCENT, 0);
    } else if (wifi_info->state == BOARD_WIFI_CONNECTING) {
        lv_label_set_text(s_lbl_wf_desc, "接続中…");
        lv_obj_set_style_text_color(s_lbl_wf_desc, UI_COLOR_CYAN_ACCENT, 0);
    } else {
        lv_label_set_text(s_lbl_wf_desc, "未接続 (タップして設定)");
        lv_obj_set_style_text_color(s_lbl_wf_desc, UI_COLOR_TEXT_SUB, 0);
    }
}

void ui_drawer_update_bt_status(bool enabled, bool connected, const char *dev_name)
{
    if (!s_sw_bt || !s_card_bt || !s_lbl_bt_desc) return;

    if (enabled) {
        lv_obj_add_state(s_sw_bt, LV_STATE_CHECKED);
        lv_obj_set_style_opa(s_card_bt, LV_OPA_COVER, 0);
        lv_obj_add_flag(s_card_bt, LV_OBJ_FLAG_CLICKABLE);
        if (connected) {
            char buf[48];
            snprintf(buf, sizeof(buf), "接続中: %s", dev_name ? dev_name : "端末");
            lv_label_set_text(s_lbl_bt_desc, buf);
            lv_obj_set_style_text_color(s_lbl_bt_desc, UI_COLOR_CYAN_ACCENT, 0);
        } else {
            lv_label_set_text(s_lbl_bt_desc, "待機中 (タップして設定)");
            lv_obj_set_style_text_color(s_lbl_bt_desc, UI_COLOR_TEXT_SUB, 0);
        }
    } else {
        lv_obj_remove_state(s_sw_bt, LV_STATE_CHECKED);
        lv_obj_set_style_opa(s_card_bt, LV_OPA_40, 0);
        lv_obj_remove_flag(s_card_bt, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(s_lbl_bt_desc, "無効 (スイッチでON)");
        lv_obj_set_style_text_color(s_lbl_bt_desc, UI_COLOR_TEXT_SUB, 0);
    }
}
