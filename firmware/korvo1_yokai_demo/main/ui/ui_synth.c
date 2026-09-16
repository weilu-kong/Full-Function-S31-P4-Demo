#include "ui/ui_synth.h"
#include "ui/ui_theme.h"
#include "ui/ui_drawer.h"
#include "synth_service.h"
#include "esp_log.h"
#include <math.h>
#include <stdio.h>

static const char *TAG = "ui_synth";

static lv_obj_t *s_scr_synth = NULL;
static lv_obj_t *s_chart_wave = NULL;
static lv_chart_series_t *s_ser_wave = NULL;
static lv_obj_t *s_arc_cutoff = NULL;
static lv_obj_t *s_arc_res = NULL;
static lv_obj_t *s_lbl_cutoff_val = NULL;
static lv_obj_t *s_lbl_res_val = NULL;
static lv_obj_t *s_btn_wave = NULL;
static lv_obj_t *s_lbl_wave = NULL;
static ui_synth_home_cb_t s_home_cb = NULL;

static lv_obj_t *s_btn_modes[3] = {0};

static float s_last_active_freq = 0.0f;
static uint32_t s_wave_phase = 0;

/* 12 White Keys */
typedef struct {
    const char *name;
    float freq;
} synth_key_info_t;

static const synth_key_info_t s_white_keys[12] = {
    {"C3", 130.81f},
    {"D3", 146.83f},
    {"E3", 164.81f},
    {"F3", 174.61f},
    {"G3", 196.00f},
    {"A3", 220.00f},
    {"B3", 246.94f},
    {"C4", 261.63f},
    {"D4", 293.66f},
    {"E4", 329.63f},
    {"F4", 349.23f},
    {"G4", 392.00f},
};

/* 8 Black Keys (placed between specific white keys) */
typedef struct {
    const char *name;
    float freq;
    int after_white_idx;
} synth_black_key_info_t;

static const synth_black_key_info_t s_black_keys[8] = {
    {"C#3", 138.59f, 0},
    {"D#3", 155.56f, 1},
    {"F#3", 185.00f, 3},
    {"G#3", 207.65f, 4},
    {"A#3", 233.08f, 5},
    {"C#4", 277.18f, 7},
    {"D#4", 311.13f, 8},
    {"F#4", 369.99f, 10},
};

static void home_click_event_cb(lv_event_t *e)
{
    (void)e;
    synth_service_set_active(false);
    if (s_home_cb) {
        s_home_cb();
    }
}

static void key_play_note(float freq, bool on)
{
    if (on) {
        s_last_active_freq = freq;
        synth_service_note_on(freq, 1.0f);
        ESP_LOGD(TAG, "Note ON: %.2f Hz", freq);
    } else {
        if (s_last_active_freq == freq) {
            synth_service_note_on(0.0f, 0.0f);
            s_last_active_freq = 0.0f;
        }
    }
}

static void white_key_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx >= 12) return;

    if (code == LV_EVENT_PRESSED) {
        key_play_note(s_white_keys[idx].freq, true);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        key_play_note(s_white_keys[idx].freq, false);
    }
}

static void black_key_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx >= 8) return;

    if (code == LV_EVENT_PRESSED) {
        key_play_note(s_black_keys[idx].freq, true);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        key_play_note(s_black_keys[idx].freq, false);
    }
}

static void arc_cutoff_event_cb(lv_event_t *e)
{
    lv_obj_t *arc = lv_event_get_target(e);
    int32_t val = lv_arc_get_value(arc);
    float hz = 200.0f + (float)val * 78.0f; /* 200Hz .. 8000Hz */

    synth_fx_params_t fx;
    synth_service_get_fx(&fx);
    fx.cutoff_hz = hz;
    synth_service_set_fx(&fx);

    if (s_lbl_cutoff_val) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%dHz", (int)hz);
        lv_label_set_text(s_lbl_cutoff_val, buf);
    }
}

static void arc_res_event_cb(lv_event_t *e)
{
    lv_obj_t *arc = lv_event_get_target(e);
    int32_t val = lv_arc_get_value(arc);
    float q = 0.5f + ((float)val / 100.0f) * 4.5f; /* 0.5 .. 5.0 */

    synth_fx_params_t fx;
    synth_service_get_fx(&fx);
    fx.resonance_q = q;
    synth_service_set_fx(&fx);

    if (s_lbl_res_val) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", q);
        lv_label_set_text(s_lbl_res_val, buf);
    }
}

static void wave_switch_event_cb(lv_event_t *e)
{
    (void)e;
    synth_wave_t cur = synth_service_get_waveform();
    synth_wave_t next = (cur + 1) % SYNTH_WAVE_MAX;
    synth_service_set_waveform(next);

    const char *names[] = {"正弦波 (SIN)", "矩形波 (SQR)", "ノコギリ (SAW)", "和太鼓 (Drum)"};
    if (s_lbl_wave && next < SYNTH_WAVE_MAX) {
        lv_label_set_text(s_lbl_wave, names[next]);
    }
}

static void mode_select_event_cb(lv_event_t *e)
{
    synth_mode_t mode = (synth_mode_t)(uintptr_t)lv_event_get_user_data(e);
    synth_service_set_mode(mode);
    for (int i = 0; i < 3; i++) {
        if (s_btn_modes[i]) {
            if (i == (int)mode) {
                lv_obj_set_style_bg_color(s_btn_modes[i], UI_COLOR_GOLD_ACCENT, 0);
                lv_obj_set_style_text_color(lv_obj_get_child(s_btn_modes[i], 0), lv_color_hex(0x0C0F17), 0);
            } else {
                lv_obj_set_style_bg_color(s_btn_modes[i], UI_COLOR_KEY_WHITE, 0);
                lv_obj_set_style_text_color(lv_obj_get_child(s_btn_modes[i], 0), UI_COLOR_TEXT_SUB, 0);
            }
        }
    }
    ESP_LOGI(TAG, "Switched Synth Mode: %d", (int)mode);
}

lv_obj_t *ui_synth_screen_create(ui_synth_home_cb_t home_cb)
{
    s_home_cb = home_cb;

    s_scr_synth = lv_obj_create(NULL);
    lv_obj_set_size(s_scr_synth, 800, 480);
    lv_obj_set_style_bg_color(s_scr_synth, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(s_scr_synth, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. Header Bar: Home Button, Title, and Mode Buttons */
    lv_obj_t *btn_home = lv_button_create(s_scr_synth);
    lv_obj_add_style(btn_home, &ui_style_btn_home, 0);
    lv_obj_set_size(btn_home, 106, 36);
    lv_obj_set_pos(btn_home, 16, 12);
    lv_obj_add_event_cb(btn_home, home_click_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_label_set_text(lbl_home, "ホーム");
    lv_obj_set_style_text_font(lbl_home, UI_FONT_REGULAR, 0);
    lv_obj_center(lbl_home);

    lv_obj_t *lbl_title = lv_label_create(s_scr_synth);
    lv_label_set_text(lbl_title, "妖怪シンセサイザー (Yokai)");
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_title, 128, 15);

    /* Mode Buttons: KEY / BT / WEB (Right-aligned in header) */
    synth_mode_t cur_mode = synth_service_get_mode();
    const char *mode_names[3] = {"鍵盤演奏", "BT 伴奏", "WEB 連動"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn_m = lv_button_create(s_scr_synth);
        lv_obj_set_size(btn_m, 86, 36);
        lv_obj_set_pos(btn_m, 508 + i * 92, 12);
        lv_obj_set_style_radius(btn_m, 18, 0);
        lv_obj_set_style_border_width(btn_m, 1, 0);
        lv_obj_set_style_border_color(btn_m, UI_COLOR_GOLD_ACCENT, 0);

        if (i == (int)cur_mode) {
            lv_obj_set_style_bg_color(btn_m, UI_COLOR_GOLD_ACCENT, 0);
        } else {
            lv_obj_set_style_bg_color(btn_m, UI_COLOR_KEY_WHITE, 0);
        }

        lv_obj_t *lbl_m = lv_label_create(btn_m);
        lv_label_set_text(lbl_m, mode_names[i]);
        lv_obj_set_style_text_font(lbl_m, UI_FONT_SMALL, 0);
        if (i == (int)cur_mode) {
            lv_obj_set_style_text_color(lbl_m, lv_color_hex(0x0C0F17), 0);
        } else {
            lv_obj_set_style_text_color(lbl_m, UI_COLOR_TEXT_SUB, 0);
        }
        lv_obj_center(lbl_m);

        lv_obj_add_event_cb(btn_m, mode_select_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        s_btn_modes[i] = btn_m;
    }

    /* 2. Oscilloscope Display Card (Upper Left) */
    lv_obj_t *osc_card = lv_obj_create(s_scr_synth);
    lv_obj_add_style(osc_card, &ui_style_glass_card, 0);
    lv_obj_set_size(osc_card, 376, 196);
    lv_obj_set_pos(osc_card, 16, 56);
    lv_obj_set_style_pad_all(osc_card, 0, 0);
    lv_obj_remove_flag(osc_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_osc_title = lv_label_create(osc_card);
    lv_label_set_text(lbl_osc_title, "オシロスコープ (OSCILLOSCOPE)");
    lv_obj_set_style_text_color(lbl_osc_title, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_osc_title, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_osc_title, 16, 10);

    s_chart_wave = lv_chart_create(osc_card);
    lv_obj_set_size(s_chart_wave, 344, 142);
    lv_obj_set_pos(s_chart_wave, 16, 36);
    lv_chart_set_type(s_chart_wave, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart_wave, 32);
    lv_chart_set_range(s_chart_wave, LV_CHART_AXIS_PRIMARY_Y, -100, 100);
    lv_obj_set_style_bg_opa(s_chart_wave, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chart_wave, 0, 0);
    lv_obj_set_style_line_width(s_chart_wave, 2, LV_PART_ITEMS);

    s_ser_wave = lv_chart_add_series(s_chart_wave, UI_COLOR_CYAN_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < 32; i++) {
        lv_chart_set_next_value(s_chart_wave, s_ser_wave, 0);
    }

    /* 3. Knobs and Controls Card (Upper Right) */
    lv_obj_t *ctrl_card = lv_obj_create(s_scr_synth);
    lv_obj_add_style(ctrl_card, &ui_style_glass_card, 0);
    lv_obj_set_size(ctrl_card, 376, 196);
    lv_obj_set_pos(ctrl_card, 408, 56);
    lv_obj_set_style_pad_all(ctrl_card, 0, 0);
    lv_obj_remove_flag(ctrl_card, LV_OBJ_FLAG_SCROLLABLE);

    /* Knob 1: Cutoff Frequency */
    s_arc_cutoff = lv_arc_create(ctrl_card);
    lv_obj_set_size(s_arc_cutoff, 90, 90);
    lv_obj_set_pos(s_arc_cutoff, 16, 16);
    lv_arc_set_rotation(s_arc_cutoff, 135);
    lv_arc_set_bg_angles(s_arc_cutoff, 0, 270);
    lv_arc_set_range(s_arc_cutoff, 0, 100);
    lv_arc_set_value(s_arc_cutoff, 50);
    lv_obj_set_style_arc_color(s_arc_cutoff, UI_COLOR_GOLD_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc_cutoff, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc_cutoff, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(s_arc_cutoff, arc_cutoff_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_cut = lv_label_create(ctrl_card);
    lv_label_set_text(lbl_cut, "カットオフ");
    lv_obj_set_style_text_color(lbl_cut, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_cut, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_cut, 20, 116);

    s_lbl_cutoff_val = lv_label_create(ctrl_card);
    lv_label_set_text(s_lbl_cutoff_val, "4100Hz");
    lv_obj_set_style_text_color(s_lbl_cutoff_val, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_cutoff_val, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(s_lbl_cutoff_val, 24, 138);

    /* Knob 2: Resonance */
    s_arc_res = lv_arc_create(ctrl_card);
    lv_obj_set_size(s_arc_res, 90, 90);
    lv_obj_set_pos(s_arc_res, 134, 16);
    lv_arc_set_rotation(s_arc_res, 135);
    lv_arc_set_bg_angles(s_arc_res, 0, 270);
    lv_arc_set_range(s_arc_res, 0, 100);
    lv_arc_set_value(s_arc_res, 30);
    lv_obj_set_style_arc_color(s_arc_res, UI_COLOR_RED_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc_res, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc_res, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(s_arc_res, arc_res_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_res = lv_label_create(ctrl_card);
    lv_label_set_text(lbl_res, "レゾナンス (Q)");
    lv_obj_set_style_text_color(lbl_res, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_res, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_res, 142, 116);

    s_lbl_res_val = lv_label_create(ctrl_card);
    lv_label_set_text(s_lbl_res_val, "1.8");
    lv_obj_set_style_text_color(s_lbl_res_val, UI_COLOR_RED_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_res_val, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(s_lbl_res_val, 160, 138);

    /* Waveform Button */
    s_btn_wave = lv_button_create(ctrl_card);
    lv_obj_add_style(s_btn_wave, &ui_style_pill_badge, 0);
    lv_obj_set_size(s_btn_wave, 138, 46);
    lv_obj_set_pos(s_btn_wave, 226, 40);
    lv_obj_set_style_pad_hor(s_btn_wave, 6, 0);
    lv_obj_add_event_cb(s_btn_wave, wave_switch_event_cb, LV_EVENT_CLICKED, NULL);

    s_lbl_wave = lv_label_create(s_btn_wave);
    synth_wave_t init_wave = synth_service_get_waveform();
    const char *init_names[] = {"正弦波 (SIN)", "矩形波 (SQR)", "ノコギリ (SAW)", "和太鼓 (Drum)"};
    if (init_wave < SYNTH_WAVE_MAX) {
        lv_label_set_text(s_lbl_wave, init_names[init_wave]);
    } else {
        lv_label_set_text(s_lbl_wave, "正弦波 (SIN)");
    }
    lv_obj_set_style_text_font(s_lbl_wave, UI_FONT_SMALL, 0);
    lv_obj_center(s_lbl_wave);

    lv_obj_t *lbl_wave_title = lv_label_create(ctrl_card);
    lv_label_set_text(lbl_wave_title, "波形切り替え");
    lv_obj_set_style_text_color(lbl_wave_title, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_wave_title, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_wave_title, 247, 96);

    /* 4. Real Piano Keyboard Surface (12 White Keys + 8 Floating Black Keys) */
    lv_obj_t *keys_panel = lv_obj_create(s_scr_synth);
    lv_obj_set_size(keys_panel, 768, 206);
    lv_obj_set_pos(keys_panel, 16, 262);
    lv_obj_set_style_pad_all(keys_panel, 0, 0);
    lv_obj_set_style_bg_color(keys_panel, lv_color_hex(0x141822), 0);
    lv_obj_set_style_radius(keys_panel, 12, 0);
    lv_obj_set_style_border_width(keys_panel, 1, 0);
    lv_obj_set_style_border_color(keys_panel, lv_color_hex(0x2D3748), 0);
    lv_obj_remove_flag(keys_panel, LV_OBJ_FLAG_SCROLLABLE);

    const int white_w = 58;
    const int white_gap = 5;
    const int white_h = 192;
    const int start_x = 9;
    const int start_y = 7;

    /* A. Create 12 White Keys Side by Side */
    for (int i = 0; i < 12; i++) {
        int x = start_x + i * (white_w + white_gap);
        lv_obj_t *btn_w = lv_button_create(keys_panel);
        lv_obj_set_size(btn_w, white_w, white_h);
        lv_obj_set_pos(btn_w, x, start_y);
        lv_obj_set_style_bg_color(btn_w, UI_COLOR_KEY_WHITE, 0);
        lv_obj_set_style_radius(btn_w, 8, 0);
        lv_obj_set_style_border_width(btn_w, 1, 0);
        lv_obj_set_style_border_color(btn_w, lv_color_hex(0x4A5568), 0);

        /* Pressed style */
        lv_obj_set_style_bg_color(btn_w, UI_COLOR_CYAN_ACCENT, LV_STATE_PRESSED);

        lv_obj_t *lbl = lv_label_create(btn_w);
        lv_label_set_text(lbl, s_white_keys[i].name);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_TITLE, 0);
        lv_obj_set_style_text_font(lbl, UI_FONT_REGULAR, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -8);

        lv_obj_add_event_cb(btn_w, white_key_event_cb, LV_EVENT_ALL, (void *)(uintptr_t)i);
    }

    /* B. Create 8 Black Keys Floating on Top */
    const int black_w = 36;
    const int black_h = 118;
    for (int j = 0; j < 8; j++) {
        int after_idx = s_black_keys[j].after_white_idx;
        /* Position black key centered over the seam between white keys */
        int white_x = start_x + after_idx * (white_w + white_gap);
        int black_x = white_x + white_w + (white_gap / 2) - (black_w / 2);

        lv_obj_t *btn_b = lv_button_create(keys_panel);
        lv_obj_set_size(btn_b, black_w, black_h);
        lv_obj_set_pos(btn_b, black_x, start_y);
        lv_obj_set_style_bg_color(btn_b, UI_COLOR_KEY_BLACK, 0);
        lv_obj_set_style_radius(btn_b, 6, 0);
        lv_obj_set_style_border_width(btn_b, 1, 0);
        lv_obj_set_style_border_color(btn_b, UI_COLOR_GOLD_ACCENT, 0);

        /* Pressed style */
        lv_obj_set_style_bg_color(btn_b, UI_COLOR_RED_ACCENT, LV_STATE_PRESSED);

        lv_obj_t *lbl = lv_label_create(btn_b);
        lv_label_set_text(lbl, s_black_keys[j].name);
        lv_obj_set_style_text_color(lbl, UI_COLOR_GOLD_ACCENT, 0);
        lv_obj_set_style_text_font(lbl, UI_FONT_SMALL, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -6);

        lv_obj_add_event_cb(btn_b, black_key_event_cb, LV_EVENT_ALL, (void *)(uintptr_t)j);
    }

    return s_scr_synth;
}

void ui_synth_update_waveform(void)
{
    if (!s_chart_wave || !s_ser_wave) {
        return;
    }

    static bool s_was_active = false;
    if (s_last_active_freq <= 0.0f) {
        if (!s_was_active) {
            return; /* Idle: no chart refresh needed */
        }
        s_was_active = false;
        for (int i = 0; i < 32; i++) {
            lv_chart_set_value_by_id(s_chart_wave, s_ser_wave, i, 0);
        }
        lv_chart_refresh(s_chart_wave);
        return;
    }

    s_was_active = true;
    s_wave_phase += 3;
    synth_wave_t wave = synth_service_get_waveform();

    for (int i = 0; i < 32; i++) {
        float t = (float)(i + s_wave_phase) * 0.3f;
        float val = 0.0f;
        switch (wave) {
        case SYNTH_WAVE_SIN:
            val = sinf(t) * 75.0f;
            break;
        case SYNTH_WAVE_SQR:
            val = (sinf(t) >= 0.0f ? 60.0f : -60.0f);
            break;
        case SYNTH_WAVE_SAW:
            val = (fmodf(t, 3.14159f) / 3.14159f * 140.0f) - 70.0f;
            break;
        case SYNTH_WAVE_DRUM:
            val = sinf(t * 1.5f) * expf(-fmodf((float)s_wave_phase * 0.05f, 2.0f)) * 80.0f;
            break;
        default:
            val = 0.0f;
            break;
        }
        lv_chart_set_value_by_id(s_chart_wave, s_ser_wave, i, (int32_t)val);
    }
    lv_chart_refresh(s_chart_wave);
}
