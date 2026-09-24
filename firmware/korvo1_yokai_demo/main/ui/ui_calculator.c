#include "ui/ui_calculator.h"
#include "ui/ui_image_loader.h"
#include "ui/ui_theme.h"
#include "calculator_engine.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    CK_0 = 0, CK_1, CK_2, CK_3, CK_4, CK_5, CK_6, CK_7, CK_8, CK_9,
    CK_DOT, CK_ADD, CK_SUB, CK_MUL, CK_DIV, CK_EQ, CK_CLEAR, CK_SIGN, CK_PERCENT
} calc_key_t;

static ui_home_btn_cb_t s_home_cb;
static calculator_engine_t s_calc;
static lv_obj_t *s_display;
static lv_obj_t *s_clear_label;
static lv_obj_t *s_hist_panel;
static lv_obj_t *s_hist_rows[8];

static void home_evt(lv_event_t *e)
{
    (void)e;
    if (s_home_cb) s_home_cb();
}

static void refresh_history(void)
{
    for (int i = 0; i < 8; ++i) {
        if (i < s_calc.history_count) {
            int src = s_calc.history_count - 1 - i;
            char b[88];
            snprintf(b, sizeof(b), "%s = %s",
                     s_calc.history[src].expression,
                     s_calc.history[src].result);
            lv_label_set_text(s_hist_rows[i], b);
        } else {
            lv_label_set_text(s_hist_rows[i], "");
        }
    }
}

static void refresh_display(void)
{
    const char *v = calculator_display(&s_calc);
    lv_label_set_text(s_display, v);
    lv_label_set_text(s_clear_label,
                      calculator_is_all_clear(&s_calc) ? "AC" : "C");

    size_t n = strlen(v);
    lv_obj_set_style_text_font(s_display,
        n > 13 ? UI_FONT_REGULAR : UI_FONT_LARGE, 0);

    refresh_history();
}

static void key_evt(lv_event_t *e)
{
    calc_key_t k = (calc_key_t)(intptr_t)lv_event_get_user_data(e);
    if (k <= CK_9) {
        calculator_press_digit(&s_calc, (int)k);
    } else {
        switch (k) {
        case CK_DOT: calculator_press_decimal(&s_calc); break;
        case CK_ADD: calculator_press_operator(&s_calc, CALC_OP_ADD); break;
        case CK_SUB: calculator_press_operator(&s_calc, CALC_OP_SUB); break;
        case CK_MUL: calculator_press_operator(&s_calc, CALC_OP_MUL); break;
        case CK_DIV: calculator_press_operator(&s_calc, CALC_OP_DIV); break;
        case CK_EQ: calculator_press_equals(&s_calc); break;
        case CK_CLEAR: calculator_press_clear(&s_calc); break;
        case CK_SIGN: calculator_press_sign(&s_calc); break;
        case CK_PERCENT: calculator_press_percent(&s_calc); break;
        default: break;
        }
    }
    refresh_display();
}

static void history_toggle_evt(lv_event_t *e)
{
    (void)e;
    if (lv_obj_has_flag(s_hist_panel, LV_OBJ_FLAG_HIDDEN))
        lv_obj_remove_flag(s_hist_panel, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_hist_panel, LV_OBJ_FLAG_HIDDEN);
}

static void history_clear_evt(lv_event_t *e)
{
    (void)e;
    calculator_clear_history(&s_calc);
    refresh_history();
}

static lv_obj_t *make_key(lv_obj_t *parent, int x, int y, int w, int h,
                          const char *text, calc_key_t key, int kind)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_border_width(b, 1, 0);

    if (kind == 1) { /* operator (amber gold) */
        lv_obj_set_style_bg_color(b, lv_color_hex(0x7A5623), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0xD4A753), 0);
    } else if (kind == 2) { /* clear (crimson lacquer) */
        lv_obj_set_style_bg_color(b, lv_color_hex(0xB02828), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0xEF5350), 0);
    } else if (kind == 3) { /* equals (cyan accent) */
        lv_obj_set_style_bg_color(b, lv_color_hex(0x0C7D91), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0x26C6DA), 0);
    } else if (kind == 4) { /* sign/percent (slate lacquer) */
        lv_obj_set_style_bg_color(b, lv_color_hex(0x202B38), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0x566C82), 0);
    } else { /* digits (deep charcoal lacquer) */
        lv_obj_set_style_bg_color(b, lv_color_hex(0x131D27), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0x384B5D), 0);
    }
    lv_obj_set_style_bg_opa(b, LV_OPA_90, 0);
    lv_obj_set_style_border_opa(b, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x293C4E), LV_STATE_PRESSED);

    lv_obj_add_event_cb(b, key_evt, LV_EVENT_CLICKED, (void *)(intptr_t)key);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, UI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(l, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_center(l);

    if (key == CK_CLEAR) s_clear_label = l;
    return b;
}

lv_obj_t *ui_calculator_screen_create(ui_home_btn_cb_t home_cb)
{
    s_home_cb = home_cb;
    calculator_init(&s_calc);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Background: Real Yokai Modern Soroban Glass artwork */
    lv_obj_t *bg = lv_image_create(scr);
    lv_image_set_src(bg, &ui_app_shared_bg);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_size(bg, 800, 480);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *home = lv_button_create(scr);
    lv_obj_add_style(home, &ui_style_btn_home, 0);
    lv_obj_set_pos(home, 16, 14);
    lv_obj_set_size(home, 112, 44);
    lv_obj_add_event_cb(home, home_evt, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hl = lv_label_create(home);
    lv_label_set_text(hl, "< ホーム");
    lv_obj_set_style_text_font(hl, UI_FONT_SMALL, 0);
    lv_obj_center(hl);

    /* History button top-right: Japanese text only (fixes [x] mojibake) */
    lv_obj_t *hist = lv_button_create(scr);
    lv_obj_set_pos(hist, 674, 14);
    lv_obj_set_size(hist, 110, 42);
    lv_obj_set_style_radius(hist, 10, 0);
    lv_obj_set_style_bg_color(hist, lv_color_hex(0x101A24), 0);
    lv_obj_set_style_bg_opa(hist, LV_OPA_80, 0);
    lv_obj_set_style_border_width(hist, 1, 0);
    lv_obj_set_style_border_color(hist, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_add_event_cb(hist, history_toggle_evt, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hist_l = lv_label_create(hist);
    lv_label_set_text(hist_l, "履歴");
    lv_obj_set_style_text_font(hist_l, UI_FONT_REGULAR, 0);
    lv_obj_center(hist_l);

    /* Display box positioned over the background art glass frame */
    lv_obj_t *disp_box = lv_obj_create(scr);
    lv_obj_set_pos(disp_box, 296, 79);
    lv_obj_set_size(disp_box, 488, 70);
    lv_obj_set_style_bg_opa(disp_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(disp_box, 0, 0);
    lv_obj_set_style_pad_all(disp_box, 0, 0);
    lv_obj_remove_flag(disp_box, LV_OBJ_FLAG_SCROLLABLE);

    s_display = lv_label_create(disp_box);
    lv_label_set_text(s_display, "0");
    lv_obj_set_style_text_font(s_display, UI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(s_display, lv_color_hex(0xF3DDA1), 0);
    lv_obj_align(s_display, LV_ALIGN_RIGHT_MID, -24, 0);

    /* 4 columns × 5 rows matching concept art layout */
    const int x[4] = {299, 418, 537, 658};
    const int y[5] = {160, 220, 280, 340, 400};
    const int w = 112, h = 50;

    make_key(scr, x[0], y[0], w, h, "AC", CK_CLEAR, 2);
    make_key(scr, x[1], y[0], w, h, "±", CK_SIGN, 4);
    make_key(scr, x[2], y[0], w, h, "%", CK_PERCENT, 4);
    make_key(scr, x[3], y[0], 122, h, "÷", CK_DIV, 1);

    make_key(scr, x[0], y[1], w, h, "7", CK_7, 0);
    make_key(scr, x[1], y[1], w, h, "8", CK_8, 0);
    make_key(scr, x[2], y[1], w, h, "9", CK_9, 0);
    make_key(scr, x[3], y[1], 122, h, "×", CK_MUL, 1);

    make_key(scr, x[0], y[2], w, h, "4", CK_4, 0);
    make_key(scr, x[1], y[2], w, h, "5", CK_5, 0);
    make_key(scr, x[2], y[2], w, h, "6", CK_6, 0);
    make_key(scr, x[3], y[2], 122, h, "-", CK_SUB, 1);

    make_key(scr, x[0], y[3], w, h, "1", CK_1, 0);
    make_key(scr, x[1], y[3], w, h, "2", CK_2, 0);
    make_key(scr, x[2], y[3], w, h, "3", CK_3, 0);
    make_key(scr, x[3], y[3], 122, h, "+", CK_ADD, 1);

    make_key(scr, x[0], y[4], 231, h, "0", CK_0, 0);
    make_key(scr, x[2], y[4], w, h, ".", CK_DOT, 0);
    make_key(scr, x[3], y[4], 122, h, "=", CK_EQ, 3);

    /* History overlay */
    s_hist_panel = lv_obj_create(scr);
    lv_obj_set_pos(s_hist_panel, 432, 62);
    lv_obj_set_size(s_hist_panel, 352, 402);
    lv_obj_set_style_radius(s_hist_panel, 14, 0);
    lv_obj_set_style_bg_color(s_hist_panel, lv_color_hex(0x08131E), 0);
    lv_obj_set_style_bg_opa(s_hist_panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_hist_panel, 1, 0);
    lv_obj_set_style_border_color(s_hist_panel, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_remove_flag(s_hist_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ht = lv_label_create(s_hist_panel);
    lv_label_set_text(ht, "計算履歴");
    lv_obj_set_style_text_font(ht, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(ht, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_pos(ht, 14, 10);

    for (int i = 0; i < 8; ++i) {
        s_hist_rows[i] = lv_label_create(s_hist_panel);
        lv_obj_set_pos(s_hist_rows[i], 16, 50 + i*36);
        lv_obj_set_style_text_font(s_hist_rows[i], UI_FONT_SMALL, 0);
        lv_obj_set_style_text_color(s_hist_rows[i], UI_COLOR_TEXT_TITLE, 0);
        lv_label_set_text(s_hist_rows[i], "");
    }

    lv_obj_t *hc = lv_button_create(s_hist_panel);
    lv_obj_set_pos(hc, 190, 342);
    lv_obj_set_size(hc, 140, 40);
    lv_obj_set_style_radius(hc, 10, 0);
    lv_obj_set_style_bg_color(hc, lv_color_hex(0x572325), 0);
    lv_obj_add_event_cb(hc, history_clear_evt, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hcl = lv_label_create(hc);
    lv_label_set_text(hcl, "履歴を消去");
    lv_obj_set_style_text_font(hcl, UI_FONT_SMALL, 0);
    lv_obj_center(hcl);

    lv_obj_add_flag(s_hist_panel, LV_OBJ_FLAG_HIDDEN);
    refresh_display();
    return scr;
}
