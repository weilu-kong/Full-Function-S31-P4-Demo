#include "ui/ui_apps.h"
#include "ui/ui_theme.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


/* -------------------------------------------------------------
 * Common Helper: Header Bar
 * ------------------------------------------------------------- */
static lv_obj_t *create_screen_header(lv_obj_t *parent, const char *title, ui_home_btn_cb_t home_cb)
{
    lv_obj_t *btn_home = lv_button_create(parent);
    lv_obj_add_style(btn_home, &ui_style_btn_home, 0);
    lv_obj_set_size(btn_home, 106, 36);
    lv_obj_set_pos(btn_home, 16, 12);
    if (home_cb) {
        lv_obj_add_event_cb(btn_home, (lv_event_cb_t)home_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_label_set_text(lbl_home, "⌂ ホーム");
    lv_obj_set_style_text_font(lbl_home, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_home);

    lv_obj_t *lbl_title = lv_label_create(parent);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_title, 134, 15);

    return btn_home;
}

/* -------------------------------------------------------------
 * 1. Voice Shrine Screen (言霊の神社)
 * ------------------------------------------------------------- */
static lv_obj_t *s_lbl_voice_status = NULL;
static bool s_voice_listening = false;

static void voice_btn_cb(lv_event_t *e)
{
    (void)e;
    s_voice_listening = !s_voice_listening;
    if (s_lbl_voice_status) {
        if (s_voice_listening) {
            lv_label_set_text(s_lbl_voice_status, "音声認識中… 「妖怪」「雪女」を待機しています");
            lv_obj_set_style_text_color(s_lbl_voice_status, UI_COLOR_CYAN_ACCENT, 0);
        } else {
            lv_label_set_text(s_lbl_voice_status, "待機中 (ESP-SR 準備完了)");
            lv_obj_set_style_text_color(s_lbl_voice_status, UI_COLOR_TEXT_SUB, 0);
        }
    }
}

lv_obj_t *ui_voice_screen_create(ui_home_btn_cb_t home_cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_screen_header(scr, "言霊の神社 (Voice Shrine)", home_cb);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_add_style(card, &ui_style_glass_card, 0);
    lv_obj_set_size(card, 768, 412);
    lv_obj_set_pos(card, 16, 56);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Altar Orb Motif */
    lv_obj_t *orb = lv_obj_create(card);
    lv_obj_set_size(orb, 140, 140);
    lv_obj_set_pos(orb, 314, 40);
    lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_bg_opa(orb, LV_OPA_30, 0);
    lv_obj_set_style_border_color(orb, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_border_width(orb, 2, 0);

    lv_obj_t *lbl_shrine = lv_label_create(card);
    lv_label_set_text(lbl_shrine, "言霊奉納祭壇");
    lv_obj_set_style_text_color(lbl_shrine, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_shrine, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_shrine, 324, 195);

    s_lbl_voice_status = lv_label_create(card);
    lv_label_set_text(s_lbl_voice_status, "待機中 (ESP-SR 準備完了)");
    lv_obj_set_style_text_color(s_lbl_voice_status, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_voice_status, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(s_lbl_voice_status, 240, 240);

    lv_obj_t *btn_rec = lv_button_create(card);
    lv_obj_add_style(btn_rec, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_rec, 200, 50);
    lv_obj_set_pos(btn_rec, 284, 290);
    lv_obj_set_style_bg_color(btn_rec, UI_COLOR_RED_ACCENT, 0);
    lv_obj_add_event_cb(btn_rec, voice_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_rec = lv_label_create(btn_rec);
    lv_label_set_text(lbl_rec, "言霊を唱える 🎙");
    lv_obj_set_style_text_font(lbl_rec, UI_FONT_REGULAR, 0);
    lv_obj_center(lbl_rec);

    return scr;
}

/* -------------------------------------------------------------
 * 2. Vision AI Screen (目目連の眼)
 * ------------------------------------------------------------- */
static lv_obj_t *s_lbl_vision_det = NULL;
static int s_vision_step = 0;

static void vision_btn_cb(lv_event_t *e)
{
    (void)e;
    s_vision_step = (s_vision_step + 1) % 3;
    const char *dets[] = {
        "[物体検出] 雪女の気配を検知 (信頼度 98%)",
        "[物体検出] 狸の置物を識別 (信頼度 94%)",
        "[物体検出] 障子に目目連が現れました (信頼度 99%)"
    };
    if (s_lbl_vision_det) {
        lv_label_set_text(s_lbl_vision_det, dets[s_vision_step]);
    }
}

lv_obj_t *ui_vision_screen_create(ui_home_btn_cb_t home_cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_screen_header(scr, "目目連の眼 (Vision AI)", home_cb);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_add_style(card, &ui_style_glass_card, 0);
    lv_obj_set_size(card, 768, 412);
    lv_obj_set_pos(card, 16, 56);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Viewfinder Area */
    lv_obj_t *vf = lv_obj_create(card);
    lv_obj_set_size(vf, 520, 310);
    lv_obj_set_pos(vf, 20, 20);
    lv_obj_set_style_bg_color(vf, lv_color_hex(0x06090E), 0);
    lv_obj_set_style_border_color(vf, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_border_width(vf, 2, 0);
    lv_obj_set_style_radius(vf, 10, 0);

    lv_obj_t *lbl_target = lv_label_create(vf);
    lv_label_set_text(lbl_target, "[ 霊視ビューファインダー / ESP-DL ]");
    lv_obj_set_style_text_color(lbl_target, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_target, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_target);

    /* Right Control Area */
    lv_obj_t *lbl_info = lv_label_create(card);
    lv_label_set_text(lbl_info, "目目連・画像認識");
    lv_obj_set_style_text_color(lbl_info, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_info, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_info, 560, 24);

    s_lbl_vision_det = lv_label_create(card);
    lv_label_set_text(s_lbl_vision_det, "[物体検出] 妖怪探索中…");
    lv_obj_set_style_text_color(s_lbl_vision_det, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_vision_det, UI_FONT_SMALL, 0);
    lv_obj_set_width(s_lbl_vision_det, 180);
    lv_label_set_long_mode(s_lbl_vision_det, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_lbl_vision_det, 560, 70);

    lv_obj_t *btn_scan = lv_button_create(card);
    lv_obj_add_style(btn_scan, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_scan, 170, 48);
    lv_obj_set_pos(btn_scan, 560, 270);
    lv_obj_set_style_bg_color(btn_scan, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_add_event_cb(btn_scan, vision_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_scan = lv_label_create(btn_scan);
    lv_label_set_text(lbl_scan, "霊視スキャン 👁");
    lv_obj_set_style_text_font(lbl_scan, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(lbl_scan, lv_color_hex(0x0C0F17), 0);
    lv_obj_center(lbl_scan);

    return scr;
}

/* -------------------------------------------------------------
 * 3. Fireworks Screen (夜空の花火)
 * ------------------------------------------------------------- */
static lv_obj_t *s_fireworks_area = NULL;
static lv_obj_t *s_lbl_fw_count = NULL;
static int s_fw_count = 0;

static void fireworks_touch_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    s_fw_count++;
    if (s_lbl_fw_count) {
        char buf[32];
        snprintf(buf, sizeof(buf), "打上数: %d 発", s_fw_count);
        lv_label_set_text(s_lbl_fw_count, buf);
    }

    /* Spawn a momentary particle flower */
    lv_obj_t *p = lv_obj_create(s_fireworks_area);
    lv_obj_set_size(p, 40, 40);
    lv_obj_set_pos(p, pt.x - 20 - 16, pt.y - 20 - 56);
    lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
    lv_color_t colors[3] = {UI_COLOR_RED_ACCENT, UI_COLOR_GOLD_ACCENT, UI_COLOR_CYAN_ACCENT};
    lv_obj_set_style_bg_color(p, colors[s_fw_count % 3], 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *ui_fireworks_screen_create(ui_home_btn_cb_t home_cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_screen_header(scr, "夜空の花火 (Night Sky Fireworks)", home_cb);

    s_lbl_fw_count = lv_label_create(scr);
    lv_label_set_text(s_lbl_fw_count, "打上数: 0 発");
    lv_obj_set_style_text_color(s_lbl_fw_count, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_fw_count, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_fw_count, 660, 18);

    s_fireworks_area = lv_obj_create(scr);
    lv_obj_add_style(s_fireworks_area, &ui_style_glass_card, 0);
    lv_obj_set_size(s_fireworks_area, 768, 412);
    lv_obj_set_pos(s_fireworks_area, 16, 56);
    lv_obj_add_flag(s_fireworks_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_fireworks_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_fireworks_area, fireworks_touch_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_tip = lv_label_create(s_fireworks_area);
    lv_label_set_text(lbl_tip, "夜空をタップして花火を打ち上げてください ★");
    lv_obj_set_style_text_color(lbl_tip, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_tip, UI_FONT_REGULAR, 0);
    lv_obj_align(lbl_tip, LV_ALIGN_BOTTOM_MID, 0, -16);

    return scr;
}

/* -------------------------------------------------------------
 * 4. Clock & Timer Screen (狸屋の時計)
 * ------------------------------------------------------------- */
static lv_obj_t *s_lbl_clock_big = NULL;
static lv_obj_t *s_lbl_timer = NULL;
static int s_timer_seconds = 180; /* 3 minutes */
static bool s_timer_running = false;

static void timer_start_cb(lv_event_t *e)
{
    (void)e;
    s_timer_running = !s_timer_running;
}

static void timer_reset_cb(lv_event_t *e)
{
    (void)e;
    s_timer_running = false;
    s_timer_seconds = 180;
    if (s_lbl_timer) {
        lv_label_set_text(s_lbl_timer, "03:00");
    }
}

lv_obj_t *ui_clock_screen_create(ui_home_btn_cb_t home_cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_screen_header(scr, "狸屋の時計 (Tanuki Clock & Timer)", home_cb);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_add_style(card, &ui_style_glass_card, 0);
    lv_obj_set_size(card, 768, 412);
    lv_obj_set_pos(card, 16, 56);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Big Clock */
    s_lbl_clock_big = lv_label_create(card);
    lv_label_set_text(s_lbl_clock_big, "12:00:00");
    lv_obj_set_style_text_color(s_lbl_clock_big, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_clock_big, UI_FONT_LARGE, 0);
    lv_obj_set_pos(s_lbl_clock_big, 260, 40);

    /* Timer Section */
    lv_obj_t *lbl_t_title = lv_label_create(card);
    lv_label_set_text(lbl_t_title, "茶道・妖怪タイマー (Timer)");
    lv_obj_set_style_text_color(lbl_t_title, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(lbl_t_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_t_title, 250, 160);

    s_lbl_timer = lv_label_create(card);
    lv_label_set_text(s_lbl_timer, "03:00");
    lv_obj_set_style_text_color(s_lbl_timer, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_timer, UI_FONT_LARGE, 0);
    lv_obj_set_pos(s_lbl_timer, 310, 210);

    lv_obj_t *btn_start = lv_button_create(card);
    lv_obj_add_style(btn_start, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_start, 120, 44);
    lv_obj_set_pos(btn_start, 230, 290);
    lv_obj_set_style_bg_color(btn_start, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_add_event_cb(btn_start, timer_start_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_st = lv_label_create(btn_start);
    lv_label_set_text(lbl_st, "開始 / 停止");
    lv_obj_set_style_text_font(lbl_st, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_st, lv_color_hex(0x0C0F17), 0);
    lv_obj_center(lbl_st);

    lv_obj_t *btn_rst = lv_button_create(card);
    lv_obj_add_style(btn_rst, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_rst, 120, 44);
    lv_obj_set_pos(btn_rst, 380, 290);
    lv_obj_add_event_cb(btn_rst, timer_reset_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_rt = lv_label_create(btn_rst);
    lv_label_set_text(lbl_rt, "リセット");
    lv_obj_set_style_text_font(lbl_rt, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_rt);

    return scr;
}

/* -------------------------------------------------------------
 * 5. Japanese Soroban Calculator Screen (和風の算盤)
 * ------------------------------------------------------------- */
static lv_obj_t *s_lbl_calc_screen = NULL;
static char s_calc_buf[64] = "0";
static double s_calc_val = 0.0;
static char s_calc_op = '\0';
static bool s_calc_new_num = true;

static void calc_btn_cb(lv_event_t *e)
{
    const char *key = (const char *)lv_event_get_user_data(e);
    if (!key) return;

    if (strcmp(key, "C") == 0) {
        strcpy(s_calc_buf, "0");
        s_calc_val = 0.0;
        s_calc_op = '\0';
        s_calc_new_num = true;
    } else if (key[0] >= '0' && key[0] <= '9') {
        if (s_calc_new_num || strcmp(s_calc_buf, "0") == 0) {
            snprintf(s_calc_buf, sizeof(s_calc_buf), "%s", key);
            s_calc_new_num = false;
        } else {
            if (strlen(s_calc_buf) < 14) {
                strcat(s_calc_buf, key);
            }
        }
    } else if (key[0] == '+' || key[0] == '-' || key[0] == '*' || key[0] == '/') {
        s_calc_val = atof(s_calc_buf);
        s_calc_op = key[0];
        s_calc_new_num = true;
    } else if (strcmp(key, "=") == 0) {
        double cur = atof(s_calc_buf);
        double res = cur;
        if (s_calc_op == '+') res = s_calc_val + cur;
        else if (s_calc_op == '-') res = s_calc_val - cur;
        else if (s_calc_op == '*') res = s_calc_val * cur;
        else if (s_calc_op == '/' && cur != 0.0) res = s_calc_val / cur;
        snprintf(s_calc_buf, sizeof(s_calc_buf), "%.2f", res);
        s_calc_new_num = true;
        s_calc_op = '\0';
    }

    if (s_lbl_calc_screen) {
        lv_label_set_text(s_lbl_calc_screen, s_calc_buf);
    }
}

lv_obj_t *ui_calculator_screen_create(ui_home_btn_cb_t home_cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_screen_header(scr, "和風の算盤 (Japanese Abacus Calculator)", home_cb);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_add_style(card, &ui_style_glass_card, 0);
    lv_obj_set_size(card, 520, 412);
    lv_obj_set_pos(card, 140, 56);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Calculator LCD Display */
    lv_obj_t *screen_box = lv_obj_create(card);
    lv_obj_set_size(screen_box, 480, 60);
    lv_obj_set_pos(screen_box, 10, 10);
    lv_obj_set_style_bg_color(screen_box, lv_color_hex(0x06090E), 0);
    lv_obj_set_style_border_color(screen_box, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_border_width(screen_box, 1, 0);
    lv_obj_set_style_radius(screen_box, 8, 0);

    s_lbl_calc_screen = lv_label_create(screen_box);
    lv_label_set_text(s_lbl_calc_screen, "0");
    lv_obj_set_style_text_color(s_lbl_calc_screen, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_calc_screen, UI_FONT_TITLE, 0);
    lv_obj_align(s_lbl_calc_screen, LV_ALIGN_RIGHT_MID, -12, 0);

    /* 4x4 Keypad */
    const char *keys[16] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "C", "0", "=", "+"
    };

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int idx = r * 4 + c;
            lv_obj_t *btn = lv_button_create(card);
            lv_obj_set_size(btn, 110, 60);
            lv_obj_set_pos(btn, 10 + c * 124, 85 + r * 72);
            lv_obj_set_style_radius(btn, 8, 0);
            lv_obj_set_style_bg_color(btn, UI_COLOR_KEY_WHITE, 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x2D3748), 0);
            lv_obj_add_event_cb(btn, calc_btn_cb, LV_EVENT_CLICKED, (void *)keys[idx]);

            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, keys[idx]);
            lv_obj_set_style_text_font(lbl, UI_FONT_TITLE, 0);
            lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_TITLE, 0);
            lv_obj_center(lbl);
        }
    }

    return scr;
}

/* -------------------------------------------------------------
 * 6. Food Expiration Tracker Screen (百鬼の台所)
 * ------------------------------------------------------------- */
lv_obj_t *ui_food_screen_create(ui_home_btn_cb_t home_cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_screen_header(scr, "百鬼の台所 (Food Freshness Tracker)", home_cb);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_add_style(card, &ui_style_glass_card, 0);
    lv_obj_set_size(card, 768, 412);
    lv_obj_set_pos(card, 16, 56);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 8, 0);

    const struct {
        const char *name;
        const char *days;
        lv_color_t color;
    } foods[5] = {
        {"妖怪豆腐 (Yokai Tofu)", "残り 2 日 [要注意]", UI_COLOR_RED_ACCENT},
        {"清流山女魚 (Mountain Trout)", "残り 4 日 [新鮮]", UI_COLOR_CYAN_ACCENT},
        {"笹団子 (Sasa Dango)", "残り 7 日 [良好]", UI_COLOR_GOLD_ACCENT},
        {"天狗の生姜 (Tengu Ginger)", "残り 12 日 [新鮮]", UI_COLOR_CYAN_ACCENT},
        {"秘伝鬼味噌 (Demon Miso)", "残り 30 日 [熟成]", UI_COLOR_GOLD_ACCENT},
    };

    for (int i = 0; i < 5; i++) {
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, 740, 52);
        lv_obj_set_style_bg_color(row, UI_COLOR_KEY_WHITE, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x2D3748), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_name = lv_label_create(row);
        lv_label_set_text(lbl_name, foods[i].name);
        lv_obj_set_style_text_color(lbl_name, UI_COLOR_TEXT_TITLE, 0);
        lv_obj_set_style_text_font(lbl_name, UI_FONT_REGULAR, 0);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 12, 0);

        lv_obj_t *lbl_days = lv_label_create(row);
        lv_label_set_text(lbl_days, foods[i].days);
        lv_obj_set_style_text_color(lbl_days, foods[i].color, 0);
        lv_obj_set_style_text_font(lbl_days, UI_FONT_SMALL, 0);
        lv_obj_align(lbl_days, LV_ALIGN_RIGHT_MID, -12, 0);
    }

    return scr;
}

void ui_apps_tick_periodic(void)
{
    /* Update big clock */
    if (s_lbl_clock_big) {
        time_t now = time(NULL);
        struct tm local = {0};
        char time_buf[32] = "12:00:00";
        if (localtime_r(&now, &local) != NULL && local.tm_year >= 120) {
            snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
                     local.tm_hour, local.tm_min, local.tm_sec);
        }
        lv_label_set_text(s_lbl_clock_big, time_buf);
    }

    /* Update timer */
    if (s_timer_running && s_timer_seconds > 0) {
        static uint32_t s_last_timer_tick = 0;
        uint32_t now_tick = lv_tick_get();
        if (now_tick - s_last_timer_tick >= 1000) {
            s_last_timer_tick = now_tick;
            s_timer_seconds--;
            if (s_lbl_timer) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%02d:%02d", s_timer_seconds / 60, s_timer_seconds % 60);
                lv_label_set_text(s_lbl_timer, buf);
            }
        }
    }
}
