#include "ui/ui_home.h"
#include "ui/ui_theme.h"
#include <stdio.h>

static lv_obj_t *s_scr_home = NULL;
static lv_obj_t *s_tileview = NULL;
static lv_obj_t *s_lbl_clock = NULL;
static lv_obj_t *s_lbl_wifi_status = NULL;
static ui_app_launch_cb_t s_launch_cb = NULL;
static ui_quick_settings_toggle_cb_t s_drawer_cb = NULL;

static void app_card_click_event_cb(lv_event_t *e)
{
    uintptr_t app_id = (uintptr_t)lv_event_get_user_data(e);
    if (s_launch_cb) {
        s_launch_cb((ui_app_id_t)app_id);
    }
}

static void drawer_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_drawer_cb) {
        s_drawer_cb();
    }
}

static lv_obj_t *create_app_card(lv_obj_t *parent, int x, int y, int w, int h,
                                 const char *title, const char *sub,
                                 lv_color_t accent, ui_app_id_t app_id)
{
    lv_obj_t *card = lv_button_create(parent);
    lv_obj_add_style(card, &ui_style_glass_card, 0);
    lv_obj_add_style(card, &ui_style_glass_card_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);

    /* Decorative Accent Bar on Left */
    lv_obj_t *bar = lv_obj_create(card);
    lv_obj_set_size(bar, 6, h - 28);
    lv_obj_set_pos(bar, 4, 14);
    lv_obj_set_style_bg_color(bar, accent, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_border_width(bar, 0, 0);

    /* App Title */
    lv_obj_t *lbl_t = lv_label_create(card);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_color(lbl_t, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(lbl_t, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_t, 24, 18);

    /* Subtitle / Description */
    lv_obj_t *lbl_s = lv_label_create(card);
    lv_label_set_text(lbl_s, sub);
    lv_obj_set_style_text_color(lbl_s, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_s, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_s, 24, 48);

    lv_obj_add_event_cb(card, app_card_click_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)app_id);
    return card;
}

lv_obj_t *ui_home_screen_create(ui_app_launch_cb_t app_cb, ui_quick_settings_toggle_cb_t drawer_cb)
{
    s_launch_cb = app_cb;
    s_drawer_cb = drawer_cb;

    s_scr_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_home, UI_COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(s_scr_home, LV_OPA_COVER, 0);

    /* Atmospheric Deep Background */
    lv_obj_t *bg = lv_obj_create(s_scr_home);
    lv_obj_set_size(bg, 800, 480);
    lv_obj_center(bg);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x0A0D14), 0);
    lv_obj_set_style_bg_grad_color(bg, lv_color_hex(0x131A29), 0);
    lv_obj_set_style_bg_grad_dir(bg, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Top Navigation Bar */
    lv_obj_t *top_bar = lv_obj_create(s_scr_home);
    lv_obj_set_size(top_bar, 800, 50);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x0E131E), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_border_side(top_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(top_bar, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_border_opa(top_bar, LV_OPA_30, 0);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_logo = lv_label_create(top_bar);
    lv_label_set_text(lbl_logo, "妖怪中控 (Yokai OS)");
    lv_obj_set_style_text_color(lbl_logo, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_logo, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(lbl_logo, 16, 12);

    s_lbl_wifi_status = lv_label_create(top_bar);
    lv_label_set_text(s_lbl_wifi_status, "Wi-Fi: ○ 切断");
    lv_obj_set_style_text_color(s_lbl_wifi_status, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_wifi_status, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_wifi_status, 280, 14);

    s_lbl_clock = lv_label_create(top_bar);
    lv_label_set_text(s_lbl_clock, "--:--");
    lv_obj_set_style_text_color(s_lbl_clock, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(s_lbl_clock, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(s_lbl_clock, 450, 13);

    /* Quick Settings Dropdown Toggle Button */
    lv_obj_t *btn_qs = lv_button_create(top_bar);
    lv_obj_add_style(btn_qs, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_qs, 140, 34);
    lv_obj_set_pos(btn_qs, 640, 8);
    lv_obj_add_event_cb(btn_qs, drawer_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_qs = lv_label_create(btn_qs);
    lv_label_set_text(lbl_qs, "設定 / Drawer ▼");
    lv_obj_set_style_text_font(lbl_qs, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_qs);

    /* 2-Page Horizontal TileView */
    s_tileview = lv_tileview_create(s_scr_home);
    lv_obj_set_size(s_tileview, 800, 420);
    lv_obj_set_pos(s_tileview, 0, 50);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tileview, 0, 0);

    /* --- Tile 0: Page 1 (Primary Core Apps) --- */
    lv_obj_t *tile1 = lv_tileview_add_tile(s_tileview, 0, 0, LV_DIR_HOR);
    create_app_card(tile1, 30, 20, 350, 160,
                    "妖怪シンセサイザー", "DSP 合成器 / 20鍵 Groovebox / 示波器",
                    UI_COLOR_RED_ACCENT, UI_APP_SYNTH);
    create_app_card(tile1, 420, 20, 350, 160,
                    "雪女天気", "和風立絵画巻 / Open-Meteo / Lottie 降雪動效",
                    UI_COLOR_CYAN_ACCENT, UI_APP_WEATHER);
    create_app_card(tile1, 30, 210, 350, 160,
                    "音声認識 (ESP-SR)", "AFE AEC + WakeNet 唤醒 + 命令詞",
                    UI_COLOR_GOLD_ACCENT, UI_APP_VOICE);
    create_app_card(tile1, 420, 210, 350, 160,
                    "物体認識 (ESP-DL)", "USB/DVP 相機画面 + 軽量 AI 検出",
                    lv_color_hex(0x2EC4B6), UI_APP_VISION);

    /* --- Tile 1: Page 2 (Secondary Utility Apps) --- */
    lv_obj_t *tile2 = lv_tileview_add_tile(s_tileview, 1, 0, LV_DIR_HOR);
    create_app_card(tile2, 30, 20, 350, 160,
                    "画面花火 (Fireworks)", "夏の夜空を彩る粒子演出",
                    lv_color_hex(0xFF9F1C), UI_APP_FIREWORKS);
    create_app_card(tile2, 420, 20, 350, 160,
                    "時計・タイマー", "SNTP 高精度世界時計とタイマー",
                    UI_COLOR_CYAN_ACCENT, UI_APP_CLOCK);
    create_app_card(tile2, 30, 210, 350, 160,
                    "電卓 (Calculator)", "和風算盤モチーフ計算ツール",
                    UI_COLOR_GOLD_ACCENT, UI_APP_CALCULATOR);
    create_app_card(tile2, 420, 210, 350, 160,
                    "食材管理 (Food)", "賞味期限管理とスマートリスト",
                    lv_color_hex(0x80ED99), UI_APP_FOOD);

    return s_scr_home;
}

void ui_home_screen_update_status(const char *time_str, bool wifi_connected)
{
    if (s_lbl_clock && time_str) {
        lv_label_set_text(s_lbl_clock, time_str);
    }
    if (s_lbl_wifi_status) {
        if (wifi_connected) {
            lv_label_set_text(s_lbl_wifi_status, "Wi-Fi: ● 接続中");
            lv_obj_set_style_text_color(s_lbl_wifi_status, UI_COLOR_CYAN_ACCENT, 0);
        } else {
            lv_label_set_text(s_lbl_wifi_status, "Wi-Fi: ○ 切断");
            lv_obj_set_style_text_color(s_lbl_wifi_status, UI_COLOR_TEXT_SUB, 0);
        }
    }
}
