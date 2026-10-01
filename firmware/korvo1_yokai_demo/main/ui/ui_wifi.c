#include "ui/ui_wifi.h"
#include "ui/ui_theme.h"
#include "ui/ui_wifi_signal.h"
#include "ui/ui_img_key_icon.h"
#include "board_ui.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_wifi";

static lv_obj_t *s_screen = NULL;
static ui_wifi_back_cb_t s_back_cb = NULL;

static lv_obj_t *s_lbl_status = NULL;
static lv_obj_t *s_ap_list = NULL;
static lv_obj_t *s_lbl_empty = NULL;

/* 14 AP Row Items */
typedef struct {
    lv_obj_t *btn;
    lv_obj_t *img_lock;
    lv_obj_t *lbl_ssid;
    lv_obj_t *lbl_badge;
    ui_wifi_signal_t rssi_icon;
    wifi_ap_record_t ap;
    bool in_use;
} ap_row_t;

static ap_row_t s_rows[MAX_WIFI_APS];

/* Target SSID for dialogs */
static char s_target_ssid[33] = {0};

/* Password Modal */
static lv_obj_t *s_pass_modal = NULL;
static lv_obj_t *s_lbl_pass_title = NULL;
static lv_obj_t *s_ta_pass = NULL;
static lv_obj_t *s_kb = NULL;

/* Saved Network Modal */
static lv_obj_t *s_saved_modal = NULL;
static lv_obj_t *s_lbl_saved_title = NULL;
static lv_obj_t *s_lbl_saved_sub = NULL;
static lv_obj_t *s_btn_saved_conn = NULL;
static lv_obj_t *s_btn_saved_disconn = NULL;
static lv_obj_t *s_lbl_s_conn = NULL;

static void home_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_back_cb) {
        s_back_cb();
    }
}

static void rescan_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Rescan button tapped");
    (void)board_ui_wifi_scan_async();
}

static void pass_connect_action(void)
{
    const char *pass = lv_textarea_get_text(s_ta_pass);
    ESP_LOGI(TAG, "Connecting to '%s' with password length %d", s_target_ssid, (int)strlen(pass));
    board_ui_wifi_connect(s_target_ssid, pass);
    lv_obj_add_flag(s_pass_modal, LV_OBJ_FLAG_HIDDEN);
}

static void pass_cancel_action(void)
{
    lv_obj_add_flag(s_pass_modal, LV_OBJ_FLAG_HIDDEN);
}

static void pass_connect_btn_cb(lv_event_t *e)
{
    (void)e;
    pass_connect_action();
}

static void pass_cancel_btn_cb(lv_event_t *e)
{
    (void)e;
    pass_cancel_action();
}

static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        pass_connect_action();
    } else if (code == LV_EVENT_CANCEL) {
        pass_cancel_action();
    }
}

static void saved_reconnect_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Connecting to saved SSID: %s", s_target_ssid);
    if (!board_ui_wifi_is_saved(s_target_ssid)) {
        /* Saved credentials missing or password empty; prompt user for password */
        lv_obj_add_flag(s_saved_modal, LV_OBJ_FLAG_HIDDEN);
        char title[64];
        snprintf(title, sizeof(title), "「%s」に接続", s_target_ssid);
        lv_label_set_text(s_lbl_pass_title, title);
        lv_textarea_set_text(s_ta_pass, "");
        lv_obj_remove_flag(s_pass_modal, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    board_ui_wifi_connect(s_target_ssid, NULL);
    lv_obj_add_flag(s_saved_modal, LV_OBJ_FLAG_HIDDEN);
}

static void saved_disconnect_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Explicit disconnect requested for SSID: %s", s_target_ssid);
    board_ui_wifi_disconnect();
    lv_obj_add_flag(s_saved_modal, LV_OBJ_FLAG_HIDDEN);
}

static void saved_forget_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Forgetting saved SSID: %s", s_target_ssid);
    board_ui_wifi_forget_saved();
    lv_obj_add_flag(s_saved_modal, LV_OBJ_FLAG_HIDDEN);
    (void)board_ui_wifi_scan_async();
}

static void saved_cancel_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_saved_modal, LV_OBJ_FLAG_HIDDEN);
}

static void ap_row_click_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= MAX_WIFI_APS || !s_rows[idx].in_use) {
        return;
    }
    const wifi_ap_record_t *ap = &s_rows[idx].ap;
    strncpy(s_target_ssid, (const char *)ap->ssid, sizeof(s_target_ssid) - 1);
    s_target_ssid[sizeof(s_target_ssid) - 1] = '\0';

    ESP_LOGI(TAG, "Selected AP [%d]: %s (auth=%d)", idx, s_target_ssid, ap->authmode);

    board_wifi_info_t cur_info;
    board_ui_wifi_get_info(&cur_info);

    wifi_ap_record_t live_ap;
    bool is_live_connected = (esp_wifi_sta_get_ap_info(&live_ap) == ESP_OK &&
                             strncmp(s_target_ssid, (const char *)live_ap.ssid, sizeof(s_target_ssid)) == 0);
    bool is_connected = ((cur_info.state == BOARD_WIFI_CONNECTED &&
                          strcmp(s_target_ssid, cur_info.connected_ssid) == 0) || is_live_connected);

    if (is_connected) {
        char title[64];
        snprintf(title, sizeof(title), "接続中: 「%s」", s_target_ssid);
        lv_label_set_text(s_lbl_saved_title, title);
        lv_label_set_text(s_lbl_saved_sub, "接続中のネットワークです。\nWi-Fi を切断するか、保存された設定を削除できます。");
        lv_obj_add_flag(s_btn_saved_conn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_btn_saved_disconn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_saved_modal, LV_OBJ_FLAG_HIDDEN);
    } else if (board_ui_wifi_is_saved(s_target_ssid)) {
        char title[64];
        snprintf(title, sizeof(title), "設定済み: 「%s」", s_target_ssid);
        lv_label_set_text(s_lbl_saved_title, title);
        lv_label_set_text(s_lbl_saved_sub, "以前に接続したことのあるネットワークです。\n接続するか、保存された設定を削除できます。");
        lv_label_set_text(s_lbl_s_conn, "接続");
        lv_obj_set_style_bg_color(s_btn_saved_conn, UI_COLOR_CYAN_ACCENT, 0);
        lv_obj_remove_flag(s_btn_saved_conn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_btn_saved_disconn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_saved_modal, LV_OBJ_FLAG_HIDDEN);
    } else if (ap->authmode == WIFI_AUTH_OPEN) {
        board_ui_wifi_connect(s_target_ssid, NULL);
    } else {
        char title[64];
        snprintf(title, sizeof(title), "「%s」に接続", s_target_ssid);
        lv_label_set_text(s_lbl_pass_title, title);
        lv_textarea_set_text(s_ta_pass, "");
        lv_obj_remove_flag(s_pass_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *ui_wifi_screen_create(ui_wifi_back_cb_t on_back_cb)
{
    s_back_cb = on_back_cb;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, 800, 480);
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. Header Bar */
    lv_obj_t *btn_home = lv_button_create(s_screen);
    lv_obj_add_style(btn_home, &ui_style_btn_home, 0);
    lv_obj_set_size(btn_home, 106, 36);
    lv_obj_set_pos(btn_home, 16, 12);
    lv_obj_add_event_cb(btn_home, home_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_label_set_text(lbl_home, "ホーム");
    lv_obj_set_style_text_font(lbl_home, UI_FONT_REGULAR, 0);
    lv_obj_center(lbl_home);

    lv_obj_t *lbl_title = lv_label_create(s_screen);
    lv_label_set_text(lbl_title, "Wi-Fi 接続設定");
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_title, 134, 15);

    s_lbl_status = lv_label_create(s_screen);
    lv_label_set_text(s_lbl_status, "スキャン中…");
    lv_obj_set_style_text_color(s_lbl_status, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_status, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_status, 300, 18);

    lv_obj_t *btn_rescan = lv_button_create(s_screen);
    lv_obj_add_style(btn_rescan, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_rescan, 108, 36);
    lv_obj_set_pos(btn_rescan, 676, 12);
    lv_obj_add_event_cb(btn_rescan, rescan_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_rescan = lv_label_create(btn_rescan);
    lv_label_set_text(lbl_rescan, "再検索");
    lv_obj_set_style_text_font(lbl_rescan, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_rescan);

    /* 2. AP List Container */
    s_ap_list = lv_obj_create(s_screen);
    lv_obj_add_style(s_ap_list, &ui_style_glass_card, 0);
    lv_obj_set_size(s_ap_list, 768, 412);
    lv_obj_set_pos(s_ap_list, 16, 56);
    lv_obj_set_flex_flow(s_ap_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_ap_list, 8, 0);
    lv_obj_set_style_pad_row(s_ap_list, 6, 0);

    s_lbl_empty = lv_label_create(s_ap_list);
    lv_label_set_text(s_lbl_empty, "アクセスポイントを探索中…");
    lv_obj_set_style_text_color(s_lbl_empty, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_empty, UI_FONT_REGULAR, 0);
    lv_obj_set_style_pad_all(s_lbl_empty, 20, 0);

    /* Pre-create 14 row items inside the list container */
    for (int i = 0; i < MAX_WIFI_APS; i++) {
        lv_obj_t *btn = lv_button_create(s_ap_list);
        lv_obj_set_size(btn, 736, 48);
        lv_obj_set_style_bg_color(btn, UI_COLOR_KEY_WHITE, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x2D3748), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_pad_hor(btn, 16, 0);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, ap_row_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        /* Graphic Key Icon (Crisp white icon for secured APs) */
        lv_obj_t *img_lock = lv_image_create(btn);
        lv_image_set_src(img_lock, &ui_img_key_icon);
        lv_obj_set_style_image_recolor(img_lock, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_image_recolor_opa(img_lock, LV_OPA_COVER, 0);
        lv_obj_align(img_lock, LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_add_flag(img_lock, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *lbl_ssid = lv_label_create(btn);
        lv_label_set_text(lbl_ssid, "");
        lv_obj_set_style_text_color(lbl_ssid, UI_COLOR_TEXT_TITLE, 0);
        lv_obj_set_style_text_font(lbl_ssid, UI_FONT_REGULAR, 0);
        lv_obj_align(lbl_ssid, LV_ALIGN_LEFT_MID, 28, 0);

        lv_obj_t *lbl_badge = lv_label_create(btn);
        lv_label_set_text(lbl_badge, "");
        lv_obj_set_style_text_font(lbl_badge, UI_FONT_SMALL, 0);
        lv_obj_align(lbl_badge, LV_ALIGN_RIGHT_MID, -50, 0);

        /* 4-bar smartphone-style signal widget */
        ui_wifi_signal_create(&s_rows[i].rssi_icon, btn, 698, 16);

        lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);

        s_rows[i].btn = btn;
        s_rows[i].img_lock = img_lock;
        s_rows[i].lbl_ssid = lbl_ssid;
        s_rows[i].lbl_badge = lbl_badge;
        s_rows[i].in_use = false;
    }

    /* 3. Password Input & Virtual Keyboard Drawer Modal */
    s_pass_modal = lv_obj_create(s_screen);
    lv_obj_set_size(s_pass_modal, 800, 480);
    lv_obj_set_pos(s_pass_modal, 0, 0);
    lv_obj_set_style_bg_color(s_pass_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_pass_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_pass_modal, 0, 0);
    lv_obj_remove_flag(s_pass_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *pass_panel = lv_obj_create(s_pass_modal);
    lv_obj_add_style(pass_panel, &ui_style_glass_card, 0);
    lv_obj_set_size(pass_panel, 780, 460);
    lv_obj_set_pos(pass_panel, 10, 10);
    lv_obj_set_style_pad_all(pass_panel, 6, 0);
    lv_obj_remove_flag(pass_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_pass_title = lv_label_create(pass_panel);
    lv_label_set_text(s_lbl_pass_title, "パスワード入力");
    lv_obj_set_style_text_color(s_lbl_pass_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_pass_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(s_lbl_pass_title, 14, 8);

    s_ta_pass = lv_textarea_create(pass_panel);
    lv_obj_set_size(s_ta_pass, 480, 40);
    lv_obj_set_pos(s_ta_pass, 14, 38);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_textarea_set_placeholder_text(s_ta_pass, "パスワードを入力 (8文字以上)...");
    lv_obj_set_style_bg_color(s_ta_pass, UI_COLOR_KEY_WHITE, 0);
    lv_obj_set_style_text_color(s_ta_pass, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(s_ta_pass, UI_FONT_REGULAR, 0);

    lv_obj_t *btn_pass_conn = lv_button_create(pass_panel);
    lv_obj_add_style(btn_pass_conn, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_pass_conn, 110, 40);
    lv_obj_set_pos(btn_pass_conn, 510, 38);
    lv_obj_set_style_bg_color(btn_pass_conn, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_add_event_cb(btn_pass_conn, pass_connect_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_conn = lv_label_create(btn_pass_conn);
    lv_label_set_text(lbl_conn, "接続");
    lv_obj_set_style_text_font(lbl_conn, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(lbl_conn, lv_color_hex(0x0C0F17), 0);
    lv_obj_center(lbl_conn);

    lv_obj_t *btn_pass_cancel = lv_button_create(pass_panel);
    lv_obj_add_style(btn_pass_cancel, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_pass_cancel, 120, 40);
    lv_obj_set_pos(btn_pass_cancel, 634, 38);
    lv_obj_add_event_cb(btn_pass_cancel, pass_cancel_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_cancel = lv_label_create(btn_pass_cancel);
    lv_label_set_text(lbl_cancel, "キャンセル");
    lv_obj_set_style_text_font(lbl_cancel, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_cancel);

    s_kb = lv_keyboard_create(pass_panel);
    lv_obj_set_size(s_kb, 764, 350);
    lv_obj_set_pos(s_kb, 4, 88);
    lv_keyboard_set_textarea(s_kb, s_ta_pass);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_add_flag(s_pass_modal, LV_OBJ_FLAG_HIDDEN);

    /* 4. Saved Network Action Modal */
    s_saved_modal = lv_obj_create(s_screen);
    lv_obj_set_size(s_saved_modal, 800, 480);
    lv_obj_set_pos(s_saved_modal, 0, 0);
    lv_obj_set_style_bg_color(s_saved_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_saved_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_saved_modal, 0, 0);
    lv_obj_remove_flag(s_saved_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *saved_panel = lv_obj_create(s_saved_modal);
    lv_obj_add_style(saved_panel, &ui_style_glass_card, 0);
    lv_obj_set_size(saved_panel, 620, 240);
    lv_obj_center(saved_panel);
    lv_obj_remove_flag(saved_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_saved_title = lv_label_create(saved_panel);
    lv_label_set_text(s_lbl_saved_title, "設定済みネットワーク");
    lv_obj_set_style_text_color(s_lbl_saved_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_saved_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(s_lbl_saved_title, 20, 16);

    s_lbl_saved_sub = lv_label_create(saved_panel);
    lv_label_set_text(s_lbl_saved_sub, "以前に接続したことのあるネットワークです。\n接続するか、保存された設定を削除できます。");
    lv_obj_set_style_text_color(s_lbl_saved_sub, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_saved_sub, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_saved_sub, 20, 54);

    /* 1. Connect / Reconnect Button */
    s_btn_saved_conn = lv_button_create(saved_panel);
    lv_obj_add_style(s_btn_saved_conn, &ui_style_pill_badge, 0);
    lv_obj_set_size(s_btn_saved_conn, 160, 44);
    lv_obj_set_pos(s_btn_saved_conn, 25, 150);
    lv_obj_set_style_bg_color(s_btn_saved_conn, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_add_event_cb(s_btn_saved_conn, saved_reconnect_btn_cb, LV_EVENT_CLICKED, NULL);

    s_lbl_s_conn = lv_label_create(s_btn_saved_conn);
    lv_label_set_text(s_lbl_s_conn, "接続");
    lv_obj_set_style_text_font(s_lbl_s_conn, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(s_lbl_s_conn, lv_color_hex(0x0C0F17), 0);
    lv_obj_center(s_lbl_s_conn);

    /* 2. Explicit Disconnect Button */
    s_btn_saved_disconn = lv_button_create(saved_panel);
    lv_obj_add_style(s_btn_saved_disconn, &ui_style_pill_badge, 0);
    lv_obj_set_size(s_btn_saved_disconn, 160, 44);
    lv_obj_set_pos(s_btn_saved_disconn, 25, 150);
    lv_obj_set_style_bg_color(s_btn_saved_disconn, UI_COLOR_RED_ACCENT, 0);
    lv_obj_add_event_cb(s_btn_saved_disconn, saved_disconnect_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_s_disconn = lv_label_create(s_btn_saved_disconn);
    lv_label_set_text(lbl_s_disconn, "Wi-Fi を切断");
    lv_obj_set_style_text_font(lbl_s_disconn, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(lbl_s_disconn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_s_disconn);

    /* 3. Forget / Delete Saved Config Button */
    lv_obj_t *btn_saved_forget = lv_button_create(saved_panel);
    lv_obj_add_style(btn_saved_forget, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_saved_forget, 170, 44);
    lv_obj_set_pos(btn_saved_forget, 215, 150);
    lv_obj_set_style_bg_color(btn_saved_forget, lv_color_hex(0x2D3748), 0);
    lv_obj_add_event_cb(btn_saved_forget, saved_forget_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_s_forget = lv_label_create(btn_saved_forget);
    lv_label_set_text(lbl_s_forget, "設定を削除");
    lv_obj_set_style_text_font(lbl_s_forget, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(lbl_s_forget, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_s_forget);

    /* 4. Cancel / Close Button */
    lv_obj_t *btn_saved_cancel = lv_button_create(saved_panel);
    lv_obj_add_style(btn_saved_cancel, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_saved_cancel, 140, 44);
    lv_obj_set_pos(btn_saved_cancel, 415, 150);
    lv_obj_add_event_cb(btn_saved_cancel, saved_cancel_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_s_cancel = lv_label_create(btn_saved_cancel);
    lv_label_set_text(lbl_s_cancel, "閉じる");
    lv_obj_set_style_text_font(lbl_s_cancel, UI_FONT_REGULAR, 0);
    lv_obj_center(lbl_s_cancel);

    lv_obj_add_flag(s_saved_modal, LV_OBJ_FLAG_HIDDEN);

    return s_screen;
}

void ui_wifi_screen_update(const board_wifi_info_t *info)
{
    if (!s_screen || !info) return;

    /* 1. Header Status Update */
    char status_text[128] = {0};
    if (info->state == BOARD_WIFI_CONNECTED) {
        if (info->ip_str[0] != '\0') {
            snprintf(status_text, sizeof(status_text), "接続済み: %s (%s)", info->connected_ssid, info->ip_str);
        } else {
            snprintf(status_text, sizeof(status_text), "接続済み: %s", info->connected_ssid);
        }
        lv_obj_set_style_text_color(s_lbl_status, UI_COLOR_CYAN_ACCENT, 0);
    } else if (info->state == BOARD_WIFI_CONNECTING) {
        snprintf(status_text, sizeof(status_text), "接続中… %s", info->connected_ssid);
        lv_obj_set_style_text_color(s_lbl_status, UI_COLOR_CYAN_ACCENT, 0);
    } else if (info->state == BOARD_WIFI_FAILED) {
        if (info->last_disconnect_reason == 15 || info->last_disconnect_reason == 202 || info->last_disconnect_reason == 204) {
            snprintf(status_text, sizeof(status_text), "接続失敗 (パスワード誤り)");
        } else if (info->last_disconnect_reason == 201) {
            snprintf(status_text, sizeof(status_text), "接続失敗 (APが見つかりません)");
        } else if (info->last_disconnect_reason == 210) {
            snprintf(status_text, sizeof(status_text), "接続失敗 (パスワード要再入力)");
        } else {
            snprintf(status_text, sizeof(status_text), "接続失敗 (エラー: %u)", (unsigned)info->last_disconnect_reason);
        }
        lv_obj_set_style_text_color(s_lbl_status, UI_COLOR_RED_ACCENT, 0);
    } else if (info->scan_running) {
        snprintf(status_text, sizeof(status_text), "スキャン中…");
        lv_obj_set_style_text_color(s_lbl_status, UI_COLOR_TEXT_SUB, 0);
    } else if (info->scan_failed) {
        snprintf(status_text, sizeof(status_text), "スキャン失敗");
        lv_obj_set_style_text_color(s_lbl_status, UI_COLOR_RED_ACCENT, 0);
    } else if (info->ap_count > 0) {
        snprintf(status_text, sizeof(status_text), "アクセスポイント: %u 件", (unsigned)info->ap_count);
        lv_obj_set_style_text_color(s_lbl_status, UI_COLOR_TEXT_SUB, 0);
    } else {
        snprintf(status_text, sizeof(status_text), "未接続 (0 件)");
        lv_obj_set_style_text_color(s_lbl_status, UI_COLOR_TEXT_SUB, 0);
    }
    lv_label_set_text(s_lbl_status, status_text);

    /* 2. Empty prompt visibility */
    if (info->ap_count > 0) {
        lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
        if (info->scan_running) {
            lv_label_set_text(s_lbl_empty, "アクセスポイントを探索中…");
        } else {
            lv_label_set_text(s_lbl_empty, "利用可能なネットワークがありません。「再検索」してください。");
        }
    }

    /* 3. Populate up to MAX_WIFI_APS rows */
    for (int i = 0; i < MAX_WIFI_APS; i++) {
        if (i < info->ap_count && strlen((const char *)info->aps[i].ssid) > 0) {
            s_rows[i].ap = info->aps[i];
            s_rows[i].in_use = true;
            const char *ssid = (const char *)info->aps[i].ssid;

            /* Golden Key Icon */
            if (info->aps[i].authmode != WIFI_AUTH_OPEN) {
                lv_obj_remove_flag(s_rows[i].img_lock, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_rows[i].img_lock, LV_OBJ_FLAG_HIDDEN);
            }

            /* SSID */
            lv_label_set_text(s_rows[i].lbl_ssid, ssid);

            /* Badges: Connected / Saved */
            bool is_connected = (info->state == BOARD_WIFI_CONNECTED &&
                                 strcmp(ssid, info->connected_ssid) == 0);
            if (is_connected) {
                lv_label_set_text(s_rows[i].lbl_badge, "[接続中]");
                lv_obj_set_style_text_color(s_rows[i].lbl_badge, UI_COLOR_CYAN_ACCENT, 0);
            } else if (board_ui_wifi_is_saved(ssid)) {
                lv_label_set_text(s_rows[i].lbl_badge, "[設定済み]");
                lv_obj_set_style_text_color(s_rows[i].lbl_badge, UI_COLOR_GOLD_ACCENT, 0);
            } else {
                lv_label_set_text(s_rows[i].lbl_badge, "");
            }

            /* 4-bar smartphone-style signal icon */
            uint8_t lvl = ui_wifi_rssi_to_level(info->aps[i].rssi);
            ui_wifi_signal_set_level(&s_rows[i].rssi_icon, lvl);

            lv_obj_remove_flag(s_rows[i].btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            s_rows[i].in_use = false;
            lv_obj_add_flag(s_rows[i].btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
