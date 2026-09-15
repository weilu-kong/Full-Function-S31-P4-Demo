#include "ui/ui_synth.h"
#include "ui/ui_theme.h"
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

static float s_last_active_freq = 0.0f;
static uint32_t s_wave_phase = 0;

static const struct {
    const char *name;
    float freq;
} s_synth_keys[20] = {
    {"C3", 130.81f}, {"C#3", 138.59f}, {"D3", 146.83f}, {"D#3", 155.56f},
    {"E3", 164.81f}, {"F3", 174.61f}, {"F#3", 185.00f}, {"G3", 196.00f},
    {"G#3", 207.65f}, {"A3", 220.00f},
    {"A#3", 233.08f}, {"B3", 246.94f}, {"C4", 261.63f}, {"C#4", 277.18f},
    {"D4", 293.66f}, {"D#4", 311.13f}, {"E4", 329.63f}, {"F4", 349.23f},
    {"G4", 392.00f}, {"A4", 440.00f},
};

static void home_click_event_cb(lv_event_t *e)
{
    (void)e;
    synth_service_set_active(false);
    if (s_home_cb) {
        s_home_cb();
    }
}

static void key_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    uintptr_t key_idx = (uintptr_t)lv_event_get_user_data(e);
    if (key_idx >= 20) return;

    if (code == LV_EVENT_PRESSED) {
        s_last_active_freq = s_synth_keys[key_idx].freq;
        synth_service_note_on(s_last_active_freq, 1.0f);
        ESP_LOGD(TAG, "Key %s pressed (%.2f Hz)", s_synth_keys[key_idx].name, s_last_active_freq);
    } else if (code == LV_EVENT_RELEASED) {
        synth_service_note_on(0.0f, 0.0f);
        s_last_active_freq = 0.0f;
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

    const char *names[] = {"SIN 波", "SQR 矩形", "SAW 锯齿", "DRUM 太鼓"};
    if (s_lbl_wave && next < SYNTH_WAVE_MAX) {
        lv_label_set_text(s_lbl_wave, names[next]);
    }
}

lv_obj_t *ui_synth_screen_create(ui_synth_home_cb_t home_cb)
{
    s_home_cb = home_cb;

    s_scr_synth = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_synth, UI_COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(s_scr_synth, LV_OPA_COVER, 0);

    /* Background Subtle Grid / Carbon texture */
    lv_obj_t *bg_overlay = lv_obj_create(s_scr_synth);
    lv_obj_set_size(bg_overlay, 800, 480);
    lv_obj_center(bg_overlay);
    lv_obj_set_style_bg_color(bg_overlay, lv_color_hex(0x10141D), 0);
    lv_obj_set_style_border_width(bg_overlay, 0, 0);
    lv_obj_set_style_radius(bg_overlay, 0, 0);
    lv_obj_remove_flag(bg_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Top Bar: Title & Home Button */
    lv_obj_t *btn_home = lv_button_create(s_scr_synth);
    lv_obj_add_style(btn_home, &ui_style_btn_home, 0);
    lv_obj_set_size(btn_home, 110, 38);
    lv_obj_set_pos(btn_home, 20, 12);
    lv_obj_add_event_cb(btn_home, home_click_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_label_set_text(lbl_home, "< ホーム");
    lv_obj_set_style_text_font(lbl_home, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_home);

    lv_obj_t *lbl_title = lv_label_create(s_scr_synth);
    lv_label_set_text(lbl_title, "妖怪シンセサイザー (Yokai Groovebox)");
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_title, 150, 18);

    /* Upper Left: Real-time Oscilloscope Display Card */
    lv_obj_t *osc_card = lv_obj_create(s_scr_synth);
    lv_obj_add_style(osc_card, &ui_style_glass_card, 0);
    lv_obj_set_size(osc_card, 380, 200);
    lv_obj_set_pos(osc_card, 20, 60);
    lv_obj_remove_flag(osc_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_osc_title = lv_label_create(osc_card);
    lv_label_set_text(lbl_osc_title, "OSCILLOSCOPE  /  示波器");
    lv_obj_set_style_text_color(lbl_osc_title, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_osc_title, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_osc_title, 10, 8);

    s_chart_wave = lv_chart_create(osc_card);
    lv_obj_set_size(s_chart_wave, 340, 140);
    lv_obj_set_pos(s_chart_wave, 10, 30);
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

    /* Upper Right: Knobs and Control Card */
    lv_obj_t *ctrl_card = lv_obj_create(s_scr_synth);
    lv_obj_add_style(ctrl_card, &ui_style_glass_card, 0);
    lv_obj_set_size(ctrl_card, 360, 200);
    lv_obj_set_pos(ctrl_card, 420, 60);
    lv_obj_remove_flag(ctrl_card, LV_OBJ_FLAG_SCROLLABLE);

    /* Knob 1: Cutoff Frequency Arc */
    s_arc_cutoff = lv_arc_create(ctrl_card);
    lv_obj_set_size(s_arc_cutoff, 95, 95);
    lv_obj_set_pos(s_arc_cutoff, 15, 20);
    lv_arc_set_rotation(s_arc_cutoff, 135);
    lv_arc_set_bg_angles(s_arc_cutoff, 0, 270);
    lv_arc_set_range(s_arc_cutoff, 0, 100);
    lv_arc_set_value(s_arc_cutoff, 50);
    lv_obj_set_style_arc_color(s_arc_cutoff, UI_COLOR_GOLD_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc_cutoff, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc_cutoff, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(s_arc_cutoff, arc_cutoff_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_cut = lv_label_create(ctrl_card);
    lv_label_set_text(lbl_cut, "CUTOFF");
    lv_obj_set_style_text_color(lbl_cut, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_cut, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_cut, 35, 120);

    s_lbl_cutoff_val = lv_label_create(ctrl_card);
    lv_label_set_text(s_lbl_cutoff_val, "4100Hz");
    lv_obj_set_style_text_color(s_lbl_cutoff_val, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_cutoff_val, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_cutoff_val, 32, 140);

    /* Knob 2: Resonance Arc */
    s_arc_res = lv_arc_create(ctrl_card);
    lv_obj_set_size(s_arc_res, 95, 95);
    lv_obj_set_pos(s_arc_res, 135, 20);
    lv_arc_set_rotation(s_arc_res, 135);
    lv_arc_set_bg_angles(s_arc_res, 0, 270);
    lv_arc_set_range(s_arc_res, 0, 100);
    lv_arc_set_value(s_arc_res, 30);
    lv_obj_set_style_arc_color(s_arc_res, UI_COLOR_RED_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc_cutoff, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc_cutoff, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(s_arc_res, arc_res_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_res = lv_label_create(ctrl_card);
    lv_label_set_text(lbl_res, "RESONANCE");
    lv_obj_set_style_text_color(lbl_res, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_res, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_res, 145, 120);

    s_lbl_res_val = lv_label_create(ctrl_card);
    lv_label_set_text(s_lbl_res_val, "1.8");
    lv_obj_set_style_text_color(s_lbl_res_val, UI_COLOR_RED_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_res_val, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_res_val, 170, 140);

    /* Waveform Switch Button */
    s_btn_wave = lv_button_create(ctrl_card);
    lv_obj_add_style(s_btn_wave, &ui_style_pill_badge, 0);
    lv_obj_set_size(s_btn_wave, 95, 45);
    lv_obj_set_pos(s_btn_wave, 245, 45);
    lv_obj_add_event_cb(s_btn_wave, wave_switch_event_cb, LV_EVENT_CLICKED, NULL);

    s_lbl_wave = lv_label_create(s_btn_wave);
    lv_label_set_text(s_lbl_wave, "SIN 波");
    lv_obj_set_style_text_font(s_lbl_wave, UI_FONT_SMALL, 0);
    lv_obj_center(s_lbl_wave);

    lv_obj_t *lbl_wave_title = lv_label_create(ctrl_card);
    lv_label_set_text(lbl_wave_title, "WAVEFORM");
    lv_obj_set_style_text_color(lbl_wave_title, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_wave_title, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_wave_title, 255, 105);

    /* Lower Area: 20-Key Groovebox Keyboard Surface (2 rows x 10 keys) */
    lv_obj_t *keys_container = lv_obj_create(s_scr_synth);
    lv_obj_set_size(keys_container, 760, 195);
    lv_obj_set_pos(keys_container, 20, 270);
    lv_obj_set_style_bg_opa(keys_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(keys_container, 0, 0);
    lv_obj_remove_flag(keys_container, LV_OBJ_FLAG_SCROLLABLE);

    const int key_w = 70;
    const int key_h = 85;
    const int gap = 6;

    for (int i = 0; i < 20; i++) {
        int row = i / 10;
        int col = i % 10;
        int x = col * (key_w + gap);
        int y = row * (key_h + gap);

        lv_obj_t *btn_k = lv_button_create(keys_container);
        lv_obj_set_size(btn_k, key_w, key_h);
        lv_obj_set_pos(btn_k, x, y);

        /* Japanese Lacquer Key Styling */
        bool is_sharp = (s_synth_keys[i].name[1] == '#');
        lv_color_t bg_col = is_sharp ? UI_COLOR_KEY_BLACK : UI_COLOR_KEY_WHITE;
        lv_obj_set_style_bg_color(btn_k, bg_col, 0);
        lv_obj_set_style_bg_opa(btn_k, LV_OPA_90, 0);
        lv_obj_set_style_radius(btn_k, 10, 0);
        lv_obj_set_style_border_width(btn_k, 1, 0);
        lv_obj_set_style_border_color(btn_k, UI_COLOR_GOLD_ACCENT, 0);
        lv_obj_set_style_border_opa(btn_k, is_sharp ? LV_OPA_30 : LV_OPA_60, 0);

        /* Key label */
        lv_obj_t *lbl_k = lv_label_create(btn_k);
        lv_label_set_text(lbl_k, s_synth_keys[i].name);
        lv_obj_set_style_text_color(lbl_k, is_sharp ? UI_COLOR_GOLD_ACCENT : UI_COLOR_TEXT_TITLE, 0);
        lv_obj_set_style_text_font(lbl_k, UI_FONT_SMALL, 0);
        lv_obj_align(lbl_k, LV_ALIGN_BOTTOM_MID, 0, -6);

        lv_obj_add_event_cb(btn_k, key_event_cb, LV_EVENT_ALL, (void *)(uintptr_t)i);
    }

    return s_scr_synth;
}

void ui_synth_update_waveform(void)
{
    if (!s_chart_wave || !s_ser_wave) {
        return;
    }

    s_wave_phase += 3;
    synth_wave_t wave = synth_service_get_waveform();

    for (int i = 0; i < 32; i++) {
        float val = 0.0f;
        if (s_last_active_freq > 0.0f) {
            float t = (float)(i + s_wave_phase) * 0.3f;
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
        }
        lv_chart_set_value_by_id(s_chart_wave, s_ser_wave, i, (int32_t)val);
    }
    lv_chart_refresh(s_chart_wave);
}
