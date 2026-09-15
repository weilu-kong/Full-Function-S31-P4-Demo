#include "ui/ui_drawer.h"
#include "ui/ui_theme.h"
#include <stdio.h>

static lv_obj_t *s_drawer_modal = NULL;
static lv_obj_t *s_sw_wifi = NULL;
static lv_obj_t *s_sw_bt = NULL;
static lv_obj_t *s_slider_vol = NULL;
static lv_obj_t *s_slider_bright = NULL;
static lv_obj_t *s_lbl_vol_val = NULL;
static lv_obj_t *s_lbl_bright_val = NULL;

static ui_drawer_wifi_toggle_cb_t s_wifi_cb = NULL;
static ui_drawer_bt_toggle_cb_t s_bt_cb = NULL;
static ui_drawer_volume_cb_t s_vol_cb = NULL;
static ui_drawer_brightness_cb_t s_bright_cb = NULL;

static void wifi_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool en = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (s_wifi_cb) {
        s_wifi_cb(en);
    }
}

static void bt_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool en = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (s_bt_cb) {
        s_bt_cb(en);
    }
}

static void vol_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    if (s_lbl_vol_val) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(s_lbl_vol_val, buf);
    }
    if (s_vol_cb) {
        s_vol_cb(val);
    }
}

static void bright_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    if (s_lbl_bright_val) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(s_lbl_bright_val, buf);
    }
    if (s_bright_cb) {
        s_bright_cb(val);
    }
}

static void close_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ui_drawer_set_visible(false);
}

lv_obj_t *ui_drawer_create(lv_obj_t *parent,
                           ui_drawer_wifi_toggle_cb_t wifi_cb,
                           ui_drawer_bt_toggle_cb_t bt_cb,
                           ui_drawer_volume_cb_t vol_cb,
                           ui_drawer_brightness_cb_t bright_cb)
{
    s_wifi_cb = wifi_cb;
    s_bt_cb = bt_cb;
    s_vol_cb = vol_cb;
    s_bright_cb = bright_cb;

    /* Full-screen semi-transparent backdrop */
    s_drawer_modal = lv_obj_create(parent);
    lv_obj_set_size(s_drawer_modal, 800, 480);
    lv_obj_set_pos(s_drawer_modal, 0, 0);
    lv_obj_set_style_bg_color(s_drawer_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_drawer_modal, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_drawer_modal, 0, 0);
    lv_obj_remove_flag(s_drawer_modal, LV_OBJ_FLAG_SCROLLABLE);

    /* Centered Glassmorphic Drawer Panel */
    lv_obj_t *panel = lv_obj_create(s_drawer_modal);
    lv_obj_add_style(panel, &ui_style_glass_card, 0);
    lv_obj_set_size(panel, 660, 360);
    lv_obj_center(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Header & Title */
    lv_obj_t *lbl_title = lv_label_create(panel);
    lv_label_set_text(lbl_title, "クイック設定 (Quick Settings)");
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(lbl_title, 20, 16);

    /* Close button */
    lv_obj_t *btn_close = lv_button_create(panel);
    lv_obj_add_style(btn_close, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_close, 100, 34);
    lv_obj_set_pos(btn_close, 520, 12);
    lv_obj_add_event_cb(btn_close, close_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "閉じる ✕");
    lv_obj_center(lbl_close);

    /* 1. Wi-Fi Card */
    lv_obj_t *card_wifi = lv_obj_create(panel);
    lv_obj_add_style(card_wifi, &ui_style_glass_card, 0);
    lv_obj_set_size(card_wifi, 280, 110);
    lv_obj_set_pos(card_wifi, 20, 65);
    lv_obj_remove_flag(card_wifi, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_wf = lv_label_create(card_wifi);
    lv_label_set_text(lbl_wf, "Wi-Fi (STA)");
    lv_obj_set_style_text_color(lbl_wf, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(lbl_wf, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(lbl_wf, 12, 12);

    lv_obj_t *lbl_wf_desc = lv_label_create(card_wifi);
    lv_label_set_text(lbl_wf_desc, "Open-Meteo 天気接続");
    lv_obj_set_style_text_color(lbl_wf_desc, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_wf_desc, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_wf_desc, 12, 40);

    s_sw_wifi = lv_switch_create(card_wifi);
    lv_obj_set_pos(s_sw_wifi, 195, 20);
    lv_obj_set_style_bg_color(s_sw_wifi, UI_COLOR_CYAN_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sw_wifi, wifi_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 2. Bluetooth Card */
    lv_obj_t *card_bt = lv_obj_create(panel);
    lv_obj_add_style(card_bt, &ui_style_glass_card, 0);
    lv_obj_set_size(card_bt, 280, 110);
    lv_obj_set_pos(card_bt, 320, 65);
    lv_obj_remove_flag(card_bt, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_bt = lv_label_create(card_bt);
    lv_label_set_text(lbl_bt, "Bluetooth (A2DP)");
    lv_obj_set_style_text_color(lbl_bt, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(lbl_bt, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(lbl_bt, 12, 12);

    lv_obj_t *lbl_bt_desc = lv_label_create(card_bt);
    lv_label_set_text(lbl_bt_desc, "Yokai-Groovebox");
    lv_obj_set_style_text_color(lbl_bt_desc, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_bt_desc, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_bt_desc, 12, 40);

    s_sw_bt = lv_switch_create(card_bt);
    lv_obj_set_pos(s_sw_bt, 195, 20);
    lv_obj_set_style_bg_color(s_sw_bt, UI_COLOR_GOLD_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sw_bt, bt_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 3. Master Volume Slider */
    lv_obj_t *card_vol = lv_obj_create(panel);
    lv_obj_add_style(card_vol, &ui_style_glass_card, 0);
    lv_obj_set_size(card_vol, 280, 110);
    lv_obj_set_pos(card_vol, 20, 195);
    lv_obj_remove_flag(card_vol, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_v = lv_label_create(card_vol);
    lv_label_set_text(lbl_v, "マスター音量 (Volume)");
    lv_obj_set_style_text_color(lbl_v, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(lbl_v, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_v, 12, 10);

    s_slider_vol = lv_slider_create(card_vol);
    lv_obj_set_size(s_slider_vol, 180, 12);
    lv_obj_set_pos(s_slider_vol, 12, 50);
    lv_slider_set_range(s_slider_vol, 0, 100);
    lv_slider_set_value(s_slider_vol, 70, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider_vol, UI_COLOR_CYAN_ACCENT, LV_PART_INDICATOR);
    lv_obj_add_event_cb(s_slider_vol, vol_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_lbl_vol_val = lv_label_create(card_vol);
    lv_label_set_text(s_lbl_vol_val, "70%");
    lv_obj_set_style_text_color(s_lbl_vol_val, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_vol_val, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_lbl_vol_val, 210, 46);

    /* 4. Display Brightness Slider */
    lv_obj_t *card_bright = lv_obj_create(panel);
    lv_obj_add_style(card_bright, &ui_style_glass_card, 0);
    lv_obj_set_size(card_bright, 280, 110);
    lv_obj_set_pos(card_bright, 320, 195);
    lv_obj_remove_flag(card_bright, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_b = lv_label_create(card_bright);
    lv_label_set_text(lbl_b, "画面の明るさ (Brightness)");
    lv_obj_set_style_text_color(lbl_b, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(lbl_b, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_b, 12, 10);

    s_slider_bright = lv_slider_create(card_bright);
    lv_obj_set_size(s_slider_bright, 180, 12);
    lv_obj_set_pos(s_slider_bright, 12, 50);
    lv_slider_set_range(s_slider_bright, 10, 100);
    lv_slider_set_value(s_slider_bright, 80, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider_bright, UI_COLOR_GOLD_ACCENT, LV_PART_INDICATOR);
    lv_obj_add_event_cb(s_slider_bright, bright_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_lbl_bright_val = lv_label_create(card_bright);
    lv_label_set_text(s_lbl_bright_val, "80%");
    lv_obj_set_style_text_color(s_lbl_bright_val, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_bright_val, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_lbl_bright_val, 210, 46);

    /* Initially Hidden */
    lv_obj_add_flag(s_drawer_modal, LV_OBJ_FLAG_HIDDEN);
    return s_drawer_modal;
}

void ui_drawer_set_visible(bool visible)
{
    if (!s_drawer_modal) return;
    if (visible) {
        lv_obj_remove_flag(s_drawer_modal, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_drawer_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_drawer_toggle(void)
{
    if (!s_drawer_modal) return;
    bool is_hidden = lv_obj_has_flag(s_drawer_modal, LV_OBJ_FLAG_HIDDEN);
    ui_drawer_set_visible(is_hidden);
}

bool ui_drawer_is_visible(void)
{
    if (!s_drawer_modal) return false;
    return !lv_obj_has_flag(s_drawer_modal, LV_OBJ_FLAG_HIDDEN);
}
