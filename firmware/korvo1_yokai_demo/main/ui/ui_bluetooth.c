#include "ui/ui_bluetooth.h"
#include "ui/ui_theme.h"
#include "synth_service.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_bt";

static lv_obj_t *s_scr_bt = NULL;
static lv_obj_t *s_lbl_badge = NULL;
static lv_obj_t *s_sw_radio = NULL;
static lv_obj_t *s_lbl_conn_title = NULL;
static lv_obj_t *s_lbl_conn_desc = NULL;
static lv_obj_t *s_lbl_stream_badge = NULL;
static lv_obj_t *s_btn_disconnect = NULL;
static lv_obj_t *s_slider_bt_vol = NULL;
static lv_obj_t *s_lbl_bt_vol_val = NULL;

static ui_bt_home_cb_t s_home_cb = NULL;

static void home_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_home_cb) {
        s_home_cb();
    }
}

static void radio_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool enable = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ESP_LOGI(TAG, "Bluetooth radio switch toggled: %d", enable);
    synth_service_set_bt_enabled(enable);
}

static void disconnect_click_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Bluetooth disconnect button clicked");
    synth_service_bt_disconnect();
}

static void bt_vol_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    if (s_lbl_bt_vol_val) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", (int)val);
        lv_label_set_text(s_lbl_bt_vol_val, buf);
    }
    synth_service_set_bt_volume((float)val / 100.0f);
}

lv_obj_t *ui_bluetooth_screen_create(ui_bt_home_cb_t home_cb)
{
    s_home_cb = home_cb;

    s_scr_bt = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_bt, UI_COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(s_scr_bt, LV_OPA_COVER, 0);

    /* Atmospheric Gradient Background */
    lv_obj_t *bg = lv_obj_create(s_scr_bt);
    lv_obj_set_size(bg, 800, 480);
    lv_obj_center(bg);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x090D15), 0);
    lv_obj_set_style_bg_grad_color(bg, lv_color_hex(0x111724), 0);
    lv_obj_set_style_bg_grad_dir(bg, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Top Navigation Bar */
    lv_obj_t *top_bar = lv_obj_create(s_scr_bt);
    lv_obj_set_size(top_bar, 800, 52);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x0E131E), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_90, 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_border_side(top_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(top_bar, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_border_opa(top_bar, LV_OPA_30, 0);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Home Return Button */
    lv_obj_t *btn_home = lv_button_create(top_bar);
    lv_obj_add_style(btn_home, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_home, 106, 34);
    lv_obj_set_pos(btn_home, 16, 9);
    lv_obj_add_event_cb(btn_home, home_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_label_set_text(lbl_home, "ホーム");
    lv_obj_set_style_text_font(lbl_home, UI_FONT_REGULAR, 0);
    lv_obj_center(lbl_home);

    /* Screen Title */
    lv_obj_t *lbl_title = lv_label_create(top_bar);
    lv_label_set_text(lbl_title, "Bluetooth 設定 (A2DP 伴奏連携)");
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_title, 136, 14);

    /* Status Badge */
    s_lbl_badge = lv_label_create(top_bar);
    lv_label_set_text(s_lbl_badge, "● ペアリング待機中");
    lv_obj_set_style_text_color(s_lbl_badge, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_badge, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_badge, 620, 16);

    /* --- Left Column: Device Info Card (360x400px) --- */
    lv_obj_t *card_dev = lv_obj_create(s_scr_bt);
    lv_obj_add_style(card_dev, &ui_style_glass_card, 0);
    lv_obj_set_size(card_dev, 360, 400);
    lv_obj_set_pos(card_dev, 20, 64);
    lv_obj_set_style_pad_all(card_dev, 0, 0);
    lv_obj_remove_flag(card_dev, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_d1 = lv_label_create(card_dev);
    lv_label_set_text(lbl_d1, "受信機情報 (A2DP Sink)");
    lv_obj_set_style_text_color(lbl_d1, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_d1, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(lbl_d1, 18, 14);

    /* Radio Switch */
    lv_obj_t *lbl_sw = lv_label_create(card_dev);
    lv_label_set_text(lbl_sw, "Bluetooth 機能");
    lv_obj_set_style_text_color(lbl_sw, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(lbl_sw, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(lbl_sw, 18, 52);

    s_sw_radio = lv_switch_create(card_dev);
    lv_obj_set_pos(s_sw_radio, 276, 46);
    lv_obj_set_style_bg_color(s_sw_radio, UI_COLOR_GOLD_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_state(s_sw_radio, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sw_radio, radio_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Device Name Row */
    lv_obj_t *lbl_name_tag = lv_label_create(card_dev);
    lv_label_set_text(lbl_name_tag, "デバイス名 (SSID / Name):");
    lv_obj_set_style_text_color(lbl_name_tag, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_name_tag, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_name_tag, 18, 96);

    lv_obj_t *lbl_dev_name = lv_label_create(card_dev);
    lv_label_set_text(lbl_dev_name, "Yokai-Groovebox");
    lv_obj_set_style_text_color(lbl_dev_name, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_dev_name, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_dev_name, 18, 118);

    /* Discovery Mode */
    lv_obj_t *lbl_disc = lv_label_create(card_dev);
    lv_label_set_text(lbl_disc, "動作モード: 全端末から検出可能 (可接続)");
    lv_obj_set_style_text_color(lbl_disc, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_disc, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_disc, 18, 156);

    /* Instructions Box */
    lv_obj_t *box_inst = lv_obj_create(card_dev);
    lv_obj_set_size(box_inst, 324, 196);
    lv_obj_set_pos(box_inst, 18, 188);
    lv_obj_set_style_pad_all(box_inst, 0, 0);
    lv_obj_set_style_bg_color(box_inst, lv_color_hex(0x0D121B), 0);
    lv_obj_set_style_bg_opa(box_inst, LV_OPA_80, 0);
    lv_obj_set_style_border_width(box_inst, 1, 0);
    lv_obj_set_style_border_color(box_inst, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_border_opa(box_inst, LV_OPA_20, 0);
    lv_obj_set_style_radius(box_inst, 8, 0);
    lv_obj_remove_flag(box_inst, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_inst_t = lv_label_create(box_inst);
    lv_label_set_text(lbl_inst_t, "【接続の手引き】");
    lv_obj_set_style_text_color(lbl_inst_t, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_inst_t, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_inst_t, 12, 10);

    lv_obj_t *lbl_inst_c = lv_label_create(box_inst);
    lv_label_set_text(lbl_inst_c,
                      "1. スマホの「設定」→「Bluetooth」を開く\n"
                      "2. 一覧から「Yokai-Groovebox」をタップ\n"
                      "3. 接続完了後、音楽アプリを再生すると\n"
                      "   Korvo-1 のスピーカーから伴奏が流れます\n"
                      "4. シンセサイザーの鍵盤でリアルタイムに\n"
                      "   和音やメロディを重ねて演奏できます！");
    lv_obj_set_style_text_color(lbl_inst_c, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_inst_c, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_inst_c, 12, 34);

    /* --- Right Top: Current Connection Card (380x194px) --- */
    lv_obj_t *card_conn = lv_obj_create(s_scr_bt);
    lv_obj_add_style(card_conn, &ui_style_glass_card, 0);
    lv_obj_set_size(card_conn, 380, 194);
    lv_obj_set_pos(card_conn, 400, 64);
    lv_obj_set_style_pad_all(card_conn, 0, 0);
    lv_obj_remove_flag(card_conn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_c_tag = lv_label_create(card_conn);
    lv_label_set_text(lbl_c_tag, "接続状況 (Connection Status)");
    lv_obj_set_style_text_color(lbl_c_tag, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_c_tag, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(lbl_c_tag, 18, 14);

    s_lbl_conn_title = lv_label_create(card_conn);
    lv_label_set_text(s_lbl_conn_title, "未接続 (スマホから接続)");
    lv_obj_set_style_text_color(s_lbl_conn_title, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(s_lbl_conn_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(s_lbl_conn_title, 18, 48);

    s_lbl_conn_desc = lv_label_create(card_conn);
    lv_label_set_text(s_lbl_conn_desc, "周囲のBluetooth端末から本機を検索できます");
    lv_obj_set_style_text_color(s_lbl_conn_desc, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_conn_desc, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_conn_desc, 18, 86);

    s_lbl_stream_badge = lv_label_create(card_conn);
    lv_label_set_text(s_lbl_stream_badge, "待機中");
    lv_obj_set_style_text_color(s_lbl_stream_badge, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_stream_badge, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_stream_badge, 18, 118);

    s_btn_disconnect = lv_button_create(card_conn);
    lv_obj_add_style(s_btn_disconnect, &ui_style_pill_badge, 0);
    lv_obj_set_size(s_btn_disconnect, 110, 36);
    lv_obj_set_pos(s_btn_disconnect, 252, 140);
    lv_obj_set_style_bg_color(s_btn_disconnect, UI_COLOR_RED_ACCENT, 0);
    lv_obj_add_event_cb(s_btn_disconnect, disconnect_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_btn_disconnect, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *lbl_disc_btn = lv_label_create(s_btn_disconnect);
    lv_label_set_text(lbl_disc_btn, "切断する");
    lv_obj_set_style_text_font(lbl_disc_btn, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_disc_btn);

    /* --- Right Bottom: Volume & Mix Level Card (380x194px) --- */
    lv_obj_t *card_vol = lv_obj_create(s_scr_bt);
    lv_obj_add_style(card_vol, &ui_style_glass_card, 0);
    lv_obj_set_size(card_vol, 380, 194);
    lv_obj_set_pos(card_vol, 400, 270);
    lv_obj_set_style_pad_all(card_vol, 0, 0);
    lv_obj_remove_flag(card_vol, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_v_tag = lv_label_create(card_vol);
    lv_label_set_text(lbl_v_tag, "伴奏音量バランス (Mix Level)");
    lv_obj_set_style_text_color(lbl_v_tag, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_v_tag, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(lbl_v_tag, 18, 14);

    lv_obj_t *lbl_v_desc = lv_label_create(card_vol);
    lv_label_set_text(lbl_v_desc, "Bluetooth 伴奏のミックス音量を調整します");
    lv_obj_set_style_text_color(lbl_v_desc, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_v_desc, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_v_desc, 18, 46);

    s_slider_bt_vol = lv_slider_create(card_vol);
    lv_obj_set_size(s_slider_bt_vol, 260, 14);
    lv_obj_set_pos(s_slider_bt_vol, 18, 88);
    lv_slider_set_range(s_slider_bt_vol, 0, 100);
    lv_slider_set_value(s_slider_bt_vol, 75, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider_bt_vol, UI_COLOR_CYAN_ACCENT, LV_PART_INDICATOR);
    lv_obj_add_event_cb(s_slider_bt_vol, bt_vol_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_lbl_bt_vol_val = lv_label_create(card_vol);
    lv_label_set_text(s_lbl_bt_vol_val, "75%");
    lv_obj_set_style_text_color(s_lbl_bt_vol_val, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(s_lbl_bt_vol_val, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(s_lbl_bt_vol_val, 296, 84);

    lv_obj_t *lbl_v_hint = lv_label_create(card_vol);
    lv_label_set_text(lbl_v_hint, "※ 鍵盤音と伴奏は ALC 自動リミッターで調和されます");
    lv_obj_set_style_text_color(lbl_v_hint, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_v_hint, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_v_hint, 18, 134);

    return s_scr_bt;
}

void ui_bluetooth_screen_update(bool enabled, bool connected, bool streaming,
                                const char *dev_name, const char *bda_str)
{
    if (!s_scr_bt) return;

    if (s_sw_radio) {
        if (enabled) {
            lv_obj_add_state(s_sw_radio, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_sw_radio, LV_STATE_CHECKED);
        }
    }

    if (!enabled) {
        if (s_lbl_badge) {
            lv_label_set_text(s_lbl_badge, "○ 無効 (OFF)");
            lv_obj_set_style_text_color(s_lbl_badge, UI_COLOR_TEXT_SUB, 0);
        }
        if (s_lbl_conn_title) {
            lv_label_set_text(s_lbl_conn_title, "Bluetooth は無効です");
            lv_obj_set_style_text_color(s_lbl_conn_title, UI_COLOR_TEXT_SUB, 0);
        }
        if (s_lbl_conn_desc) {
            lv_label_set_text(s_lbl_conn_desc, "スイッチをONにしてペアリングを開始してください");
        }
        if (s_lbl_stream_badge) {
            lv_label_set_text(s_lbl_stream_badge, "停止中");
        }
        if (s_btn_disconnect) {
            lv_obj_add_flag(s_btn_disconnect, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (connected) {
        if (s_lbl_badge) {
            lv_label_set_text(s_lbl_badge, "● 接続済み");
            lv_obj_set_style_text_color(s_lbl_badge, UI_COLOR_CYAN_ACCENT, 0);
        }
        if (s_lbl_conn_title) {
            char tbuf[64];
            snprintf(tbuf, sizeof(tbuf), "接続中: %s", dev_name ? dev_name : "端末");
            lv_label_set_text(s_lbl_conn_title, tbuf);
            lv_obj_set_style_text_color(s_lbl_conn_title, UI_COLOR_CYAN_ACCENT, 0);
        }
        if (s_lbl_conn_desc) {
            char dbuf[64];
            snprintf(dbuf, sizeof(dbuf), "端末アドレス: %s", bda_str ? bda_str : "--:--:--:--:--:--");
            lv_label_set_text(s_lbl_conn_desc, dbuf);
        }
        if (s_lbl_stream_badge) {
            if (streaming) {
                lv_label_set_text(s_lbl_stream_badge, "♫ 伴奏再生中 (Streaming)");
                lv_obj_set_style_text_color(s_lbl_stream_badge, UI_COLOR_GOLD_ACCENT, 0);
            } else {
                lv_label_set_text(s_lbl_stream_badge, "待機中 (音声ストリームなし)");
                lv_obj_set_style_text_color(s_lbl_stream_badge, UI_COLOR_TEXT_SUB, 0);
            }
        }
        if (s_btn_disconnect) {
            lv_obj_remove_flag(s_btn_disconnect, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        if (s_lbl_badge) {
            lv_label_set_text(s_lbl_badge, "● ペアリング待機中");
            lv_obj_set_style_text_color(s_lbl_badge, UI_COLOR_GOLD_ACCENT, 0);
        }
        if (s_lbl_conn_title) {
            lv_label_set_text(s_lbl_conn_title, "未接続 (スマホから接続)");
            lv_obj_set_style_text_color(s_lbl_conn_title, UI_COLOR_TEXT_TITLE, 0);
        }
        if (s_lbl_conn_desc) {
            lv_label_set_text(s_lbl_conn_desc, "周囲のBluetooth端末から本機を検索できます");
        }
        if (s_lbl_stream_badge) {
            lv_label_set_text(s_lbl_stream_badge, "未接続");
            lv_obj_set_style_text_color(s_lbl_stream_badge, UI_COLOR_TEXT_SUB, 0);
        }
        if (s_btn_disconnect) {
            lv_obj_add_flag(s_btn_disconnect, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
