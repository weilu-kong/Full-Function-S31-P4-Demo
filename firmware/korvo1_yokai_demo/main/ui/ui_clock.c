#include "ui/ui_clock.h"
#include "ui/ui_yokai_art.h"
#include "ui/ui_theme.h"
#include "clock_service.h"
#include "weather_service.h"
#include <stdint.h>
#include <stdio.h>
#include <time.h>

typedef enum {
    CLOCK_TAB_CLOCK = 0,
    CLOCK_TAB_TIMER,
    CLOCK_TAB_STOPWATCH,
} clock_tab_t;

static ui_home_btn_cb_t s_home_cb;
static bool s_active;
static clock_tab_t s_tab;

static lv_obj_t *s_page[3];
static lv_obj_t *s_tab_btn[3];

static lv_obj_t *s_clock_time;
static lv_obj_t *s_clock_date;
static lv_obj_t *s_clock_sync;
static lv_obj_t *s_clock_weather;

static lv_obj_t *s_timer_value;
static lv_obj_t *s_timer_start;
static lv_obj_t *s_timer_start_label;
static lv_obj_t *s_timer_h;
static lv_obj_t *s_timer_m;
static lv_obj_t *s_timer_s;
static int s_set_h = 0, s_set_m = 5, s_set_s = 0;

static lv_obj_t *s_sw_value;
static lv_obj_t *s_sw_start_label;
static lv_obj_t *s_sw_laps[8];

static void home_evt(lv_event_t *e)
{
    (void)e;
    if (s_home_cb) s_home_cb();
}

static void style_tab(int idx, bool selected)
{
    lv_obj_t *b = s_tab_btn[idx];
    if (!b) return;
    lv_obj_set_style_bg_color(b, selected ? lv_color_hex(0x0E657A)
                                          : lv_color_hex(0x0B1520), 0);
    lv_obj_set_style_bg_opa(b, selected ? LV_OPA_90 : LV_OPA_50, 0);
    lv_obj_set_style_border_color(b, selected ? UI_COLOR_CYAN_ACCENT
                                              : lv_color_hex(0x587082), 0);
    lv_obj_set_style_border_opa(b, selected ? LV_OPA_COVER : LV_OPA_50, 0);
}

static void set_tab(clock_tab_t tab)
{
    s_tab = tab;
    for (int i = 0; i < 3; ++i) {
        if (s_page[i]) {
            if (i == tab) lv_obj_remove_flag(s_page[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(s_page[i], LV_OBJ_FLAG_HIDDEN);
        }
        style_tab(i, i == tab);
    }
}

static void tab_evt(lv_event_t *e)
{
    set_tab((clock_tab_t)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *make_text_btn(lv_obj_t *parent, int x, int y, int w, int h,
                               const char *txt, lv_color_t border)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x0C1824), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, border, 0);
    lv_obj_set_style_border_opa(b, LV_OPA_60, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(l, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_center(l);
    return b;
}

static void update_set_labels(void)
{
    char b[8];
    snprintf(b, sizeof(b), "%02d", s_set_h); lv_label_set_text(s_timer_h, b);
    snprintf(b, sizeof(b), "%02d", s_set_m); lv_label_set_text(s_timer_m, b);
    snprintf(b, sizeof(b), "%02d", s_set_s); lv_label_set_text(s_timer_s, b);
}

static void preset_evt(lv_event_t *e)
{
    int min = (int)(intptr_t)lv_event_get_user_data(e);
    s_set_h = 0; s_set_m = min; s_set_s = 0;
    update_set_labels();
    clock_timer_set_duration(s_set_h, s_set_m, s_set_s);
}

typedef enum { ADJ_H_UP, ADJ_H_DN, ADJ_M_UP, ADJ_M_DN, ADJ_S_UP, ADJ_S_DN } adj_t;

static void adjust_evt(lv_event_t *e)
{
    adj_t a = (adj_t)(intptr_t)lv_event_get_user_data(e);
    switch (a) {
    case ADJ_H_UP: s_set_h = (s_set_h + 1) % 100; break;
    case ADJ_H_DN: s_set_h = (s_set_h + 99) % 100; break;
    case ADJ_M_UP: s_set_m = (s_set_m + 1) % 60; break;
    case ADJ_M_DN: s_set_m = (s_set_m + 59) % 60; break;
    case ADJ_S_UP: s_set_s = (s_set_s + 1) % 60; break;
    case ADJ_S_DN: s_set_s = (s_set_s + 59) % 60; break;
    }
    update_set_labels();
    clock_timer_set_duration(s_set_h, s_set_m, s_set_s);
}

static void timer_start_evt(lv_event_t *e)
{
    (void)e;
    clock_timer_snapshot_t st;
    clock_timer_get(&st);
    if (st.state == CLOCK_TIMER_RUNNING) clock_timer_pause();
    else if (st.state == CLOCK_TIMER_PAUSED) clock_timer_resume();
    else {
        clock_timer_set_duration(s_set_h, s_set_m, s_set_s);
        clock_timer_start();
    }
}

static void timer_cancel_evt(lv_event_t *e)
{
    (void)e;
    clock_timer_cancel();
}

static void sw_start_evt(lv_event_t *e)
{
    (void)e;
    clock_stopwatch_snapshot_t sw;
    clock_stopwatch_get(&sw);
    if (sw.state == CLOCK_SW_RUNNING) clock_stopwatch_pause();
    else clock_stopwatch_start();
}

static void sw_lap_reset_evt(lv_event_t *e)
{
    (void)e;
    clock_stopwatch_snapshot_t sw;
    clock_stopwatch_get(&sw);
    if (sw.state == CLOCK_SW_RUNNING) clock_stopwatch_lap();
    else clock_stopwatch_reset();
}

static lv_obj_t *make_digit_box(lv_obj_t *parent, int x, lv_obj_t **out_label)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_pos(box, x, 177);
    lv_obj_set_size(box, 76, 72);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x0B1621), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_80, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x4C7183), 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    *out_label = lv_label_create(box);
    lv_label_set_text(*out_label, "00");
    lv_obj_set_style_text_font(*out_label, UI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(*out_label, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_center(*out_label);
    return box;
}

lv_obj_t *ui_clock_screen_create(ui_home_btn_cb_t home_cb)
{
    s_home_cb = home_cb;
    clock_service_init();
    clock_timer_set_duration(0, 5, 0);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *art = lv_obj_create(scr);
    lv_obj_set_size(art, 800, 480);
    lv_obj_set_pos(art, 0, 0);
    ui_yokai_art_attach(art, UI_YOKAI_ART_CLOCK_B);

    lv_obj_t *home = lv_button_create(scr);
    lv_obj_add_style(home, &ui_style_btn_home, 0);
    lv_obj_set_pos(home, 16, 14);
    lv_obj_set_size(home, 112, 44);
    lv_obj_add_event_cb(home, home_evt, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hl = lv_label_create(home);
    lv_label_set_text(hl, "< ホーム");
    lv_obj_set_style_text_font(hl, UI_FONT_SMALL, 0);
    lv_obj_center(hl);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "狸屋の時計");
    lv_obj_set_style_text_font(title, UI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_pos(title, 154, 18);

    static const char *tabs[3] = {"時計", "タイマー", "ストップウォッチ"};
    const int tx[3] = {393, 514, 635};
    const int tw[3] = {116, 116, 150};
    for (int i = 0; i < 3; ++i) {
        s_tab_btn[i] = lv_button_create(scr);
        lv_obj_set_pos(s_tab_btn[i], tx[i], 14);
        lv_obj_set_size(s_tab_btn[i], tw[i], 42);
        lv_obj_set_style_radius(s_tab_btn[i], 10, 0);
        lv_obj_set_style_border_width(s_tab_btn[i], 1, 0);
        lv_obj_add_event_cb(s_tab_btn[i], tab_evt, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(s_tab_btn[i]);
        lv_label_set_text(l, tabs[i]);
        lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
        lv_obj_center(l);
    }

    for (int i = 0; i < 3; ++i) {
        s_page[i] = lv_obj_create(scr);
        lv_obj_set_pos(s_page[i], 0, 64);
        lv_obj_set_size(s_page[i], 800, 416);
        lv_obj_set_style_bg_opa(s_page[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_page[i], 0, 0);
        lv_obj_remove_flag(s_page[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    /* CLOCK PAGE */
    s_clock_date = lv_label_create(s_page[CLOCK_TAB_CLOCK]);
    lv_obj_set_pos(s_clock_date, 360, 68);
    lv_obj_set_style_text_font(s_clock_date, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(s_clock_date, UI_COLOR_TEXT_TITLE, 0);

    s_clock_time = lv_label_create(s_page[CLOCK_TAB_CLOCK]);
    lv_obj_set_pos(s_clock_time, 355, 115);
    lv_obj_set_style_text_font(s_clock_time, UI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(s_clock_time, lv_color_hex(0xF2DDA1), 0);

    s_clock_weather = lv_label_create(s_page[CLOCK_TAB_CLOCK]);
    lv_obj_set_pos(s_clock_weather, 360, 185);
    lv_obj_set_style_text_font(s_clock_weather, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(s_clock_weather, UI_COLOR_TEXT_TITLE, 0);

    s_clock_sync = lv_label_create(s_page[CLOCK_TAB_CLOCK]);
    lv_obj_set_pos(s_clock_sync, 360, 240);
    lv_obj_set_style_text_font(s_clock_sync, UI_FONT_REGULAR, 0);

    /* TIMER PAGE */
    lv_obj_t *t_sub = lv_label_create(s_page[CLOCK_TAB_TIMER]);
    lv_label_set_text(t_sub, "残り時間");
    lv_obj_set_pos(t_sub, 350, 105);
    lv_obj_set_style_text_font(t_sub, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(t_sub, UI_COLOR_TEXT_SUB, 0);

    s_timer_value = lv_label_create(s_page[CLOCK_TAB_TIMER]);
    lv_obj_set_pos(s_timer_value, 345, 145);
    lv_obj_set_style_text_font(s_timer_value, UI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(s_timer_value, lv_color_hex(0xF2DDA1), 0);

    /* Divider ONLY on Countdown Timer page */
    lv_obj_t *timer_sep = lv_obj_create(s_page[CLOCK_TAB_TIMER]);
    lv_obj_set_pos(timer_sep, 474, 52);
    lv_obj_set_size(timer_sep, 2, 330);
    lv_obj_set_style_bg_color(timer_sep, lv_color_hex(0x2E5066), 0);
    lv_obj_set_style_bg_opa(timer_sep, LV_OPA_40, 0);
    lv_obj_set_style_border_width(timer_sep, 0, 0);

    lv_obj_t *timer_title = lv_label_create(s_page[CLOCK_TAB_TIMER]);
    lv_label_set_text(timer_title, "カウントダウン");
    lv_obj_set_pos(timer_title, 500, 30);
    lv_obj_set_style_text_font(timer_title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(timer_title, UI_COLOR_TEXT_TITLE, 0);

    int presets[4] = {1, 3, 5, 10};
    for (int i = 0; i < 4; ++i) {
        char txt[12];
        snprintf(txt, sizeof(txt), "%d分", presets[i]);
        lv_obj_t *b = make_text_btn(s_page[CLOCK_TAB_TIMER],
                                    485 + i*72, 72, 64, 38, txt,
                                    UI_COLOR_CYAN_ACCENT);
        lv_obj_add_event_cb(b, preset_evt, LV_EVENT_CLICKED,
                            (void *)(intptr_t)presets[i]);
    }

    make_digit_box(s_page[CLOCK_TAB_TIMER], 490, &s_timer_h);
    make_digit_box(s_page[CLOCK_TAB_TIMER], 584, &s_timer_m);
    make_digit_box(s_page[CLOCK_TAB_TIMER], 678, &s_timer_s);

    const int bx[3] = {490, 584, 678};
    const adj_t up[3] = {ADJ_H_UP, ADJ_M_UP, ADJ_S_UP};
    const adj_t dn[3] = {ADJ_H_DN, ADJ_M_DN, ADJ_S_DN};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *u = make_text_btn(s_page[CLOCK_TAB_TIMER], bx[i]+13, 132, 50, 34, "^",
                                    lv_color_hex(0x4C7183));
        lv_obj_add_event_cb(u, adjust_evt, LV_EVENT_CLICKED, (void *)(intptr_t)up[i]);
        lv_obj_t *d = make_text_btn(s_page[CLOCK_TAB_TIMER], bx[i]+13, 260, 50, 34, "v",
                                    lv_color_hex(0x4C7183));
        lv_obj_add_event_cb(d, adjust_evt, LV_EVENT_CLICKED, (void *)(intptr_t)dn[i]);
    }

    s_timer_start = make_text_btn(s_page[CLOCK_TAB_TIMER],
                                  500, 318, 180, 54, "開始",
                                  UI_COLOR_CYAN_ACCENT);
    s_timer_start_label = lv_obj_get_child(s_timer_start, 0);
    lv_obj_add_event_cb(s_timer_start, timer_start_evt, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel = make_text_btn(s_page[CLOCK_TAB_TIMER],
                                     690, 318, 92, 54, "取消",
                                     UI_COLOR_RED_ACCENT);
    lv_obj_add_event_cb(cancel, timer_cancel_evt, LV_EVENT_CLICKED, NULL);

    update_set_labels();

    /* STOPWATCH PAGE */
    s_sw_value = lv_label_create(s_page[CLOCK_TAB_STOPWATCH]);
    lv_label_set_text(s_sw_value, "00:00:00.00");
    lv_obj_set_pos(s_sw_value, 345, 58);
    lv_obj_set_size(s_sw_value, 430, 48);
    lv_obj_set_style_text_align(s_sw_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_sw_value, UI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(s_sw_value, lv_color_hex(0xF2DDA1), 0);

    lv_obj_t *sw_start = make_text_btn(s_page[CLOCK_TAB_STOPWATCH],
                                       355, 122, 195, 48, "開始",
                                       UI_COLOR_RED_ACCENT);
    s_sw_start_label = lv_obj_get_child(sw_start, 0);
    lv_obj_add_event_cb(sw_start, sw_start_evt, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lap = make_text_btn(s_page[CLOCK_TAB_STOPWATCH],
                                 570, 122, 195, 48, "ラップ",
                                 lv_color_hex(0x5B7185));
    lv_obj_add_event_cb(lap, sw_lap_reset_evt, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lap_box = lv_obj_create(s_page[CLOCK_TAB_STOPWATCH]);
    lv_obj_set_pos(lap_box, 355, 185);
    lv_obj_set_size(lap_box, 410, 218);
    lv_obj_set_style_bg_color(lap_box, lv_color_hex(0x07131D), 0);
    lv_obj_set_style_bg_opa(lap_box, LV_OPA_70, 0);
    lv_obj_set_style_border_color(lap_box, lv_color_hex(0x244256), 0);
    lv_obj_set_style_border_width(lap_box, 1, 0);
    lv_obj_set_style_radius(lap_box, 10, 0);
    lv_obj_remove_flag(lap_box, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 8; ++i) {
        s_sw_laps[i] = lv_label_create(lap_box);
        int col = i / 4;
        int row = i % 4;
        lv_obj_set_pos(s_sw_laps[i], 16 + col * 200, 12 + row * 46);
        lv_obj_set_style_text_font(s_sw_laps[i], UI_FONT_SMALL, 0);
        lv_obj_set_style_text_color(s_sw_laps[i], UI_COLOR_TEXT_TITLE, 0);
        lv_label_set_text(s_sw_laps[i], "");
    }

    set_tab(CLOCK_TAB_TIMER);
    return scr;
}

static void update_wall_clock(void)
{
    time_t now = time(NULL);
    struct tm tmv;
    static const char *wd[] = {"日", "月", "火", "水", "木", "金", "土"};

    if (localtime_r(&now, &tmv) && tmv.tm_year >= 120) {
        char b[64];
        snprintf(b, sizeof(b), "%04d年%02d月%02d日 %s曜日",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 wd[tmv.tm_wday]);
        lv_label_set_text(s_clock_date, b);

        snprintf(b, sizeof(b), "%02d:%02d:%02d",
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        lv_label_set_text(s_clock_time, b);

        lv_label_set_text(s_clock_sync, "● 時刻同期済み (NTP)");
        lv_obj_set_style_text_color(s_clock_sync, UI_COLOR_GREEN_ACCENT, 0);
    } else {
        lv_label_set_text(s_clock_date, "----年--月--日");
        lv_label_set_text(s_clock_time, "--:--:--");
        lv_label_set_text(s_clock_sync, "時刻未設定");
        lv_obj_set_style_text_color(s_clock_sync, UI_COLOR_RED_ACCENT, 0);
    }

    weather_info_t w;
    weather_service_get_info(&w);
    lv_label_set_text(s_clock_weather, w.main_text);
}

void ui_clock_set_active(bool active)
{
    s_active = active;
}

void ui_clock_tick(void)
{
    if (!s_active) {
        /* Still refresh service state so finish event can be observed elsewhere later. */
        clock_timer_snapshot_t tmp;
        clock_timer_get(&tmp);
        return;
    }

    update_wall_clock();

    clock_timer_snapshot_t t;
    clock_timer_get(&t);
    char b[64];
    clock_format_hms(t.remaining_us, b, sizeof(b));
    lv_label_set_text(s_timer_value, b);

    if (t.state == CLOCK_TIMER_RUNNING) {
        lv_label_set_text(s_timer_start_label, "一時停止");
    } else if (t.state == CLOCK_TIMER_PAUSED) {
        lv_label_set_text(s_timer_start_label, "再開");
    } else if (t.state == CLOCK_TIMER_FINISHED) {
        lv_label_set_text(s_timer_start_label, "時間です");
    } else {
        lv_label_set_text(s_timer_start_label, "開始");
    }

    clock_stopwatch_snapshot_t sw;
    clock_stopwatch_get(&sw);
    clock_format_stopwatch(sw.elapsed_us, b, sizeof(b));
    lv_label_set_text(s_sw_value, b);
    lv_label_set_text(s_sw_start_label,
                      sw.state == CLOCK_SW_RUNNING ? "停止" : "開始");

    for (int i = 0; i < 8; ++i) {
        if (i < sw.lap_count) {
            char tbuf[32];
            clock_format_stopwatch(sw.laps_us[sw.lap_count - 1 - i],
                                   tbuf, sizeof(tbuf));
            snprintf(b, sizeof(b), "Lap %d   %s",
                     sw.lap_count - i, tbuf);
            lv_label_set_text(s_sw_laps[i], b);
        } else {
            lv_label_set_text(s_sw_laps[i], "");
        }
    }
}
