#include "ui/ui_weather.h"
#include "ui/ui_theme.h"
#include "ui/ui_lottie_assets.h"
#include "esp_lv_lottie.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "ui_weather";

static lv_obj_t *s_scr_weather = NULL;
static lv_obj_t *s_lbl_temp = NULL;
static lv_obj_t *s_lbl_cond = NULL;
static lv_obj_t *s_lbl_time = NULL;
static lv_obj_t *s_lbl_badge = NULL;
static lv_obj_t *s_lottie_snow = NULL;
static char *s_lottie_json_buf = NULL;
static ui_home_btn_cb_t s_home_cb = NULL;

static void home_click_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_home_cb) {
        s_home_cb();
    }
}

static void refresh_click_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Manual weather refresh requested");
    weather_service_trigger_refresh();
}

lv_obj_t *ui_weather_screen_create(ui_home_btn_cb_t home_cb)
{
    s_home_cb = home_cb;

    s_scr_weather = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_weather, UI_COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(s_scr_weather, LV_OPA_COVER, 0);

    /* Atmospheric Japanese Mythological Gradient Background */
    lv_obj_t *bg_gradient = lv_obj_create(s_scr_weather);
    lv_obj_set_size(bg_gradient, 800, 480);
    lv_obj_center(bg_gradient);
    lv_obj_set_style_bg_color(bg_gradient, lv_color_hex(0x0E1424), 0);
    lv_obj_set_style_bg_grad_color(bg_gradient, lv_color_hex(0x1B0E29), 0);
    lv_obj_set_style_bg_grad_dir(bg_gradient, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(bg_gradient, 0, 0);
    lv_obj_set_style_radius(bg_gradient, 0, 0);
    lv_obj_remove_flag(bg_gradient, LV_OBJ_FLAG_SCROLLABLE);

    /* Decorative Shrine / Moon Circle */
    lv_obj_t *moon = lv_obj_create(bg_gradient);
    lv_obj_set_size(moon, 260, 260);
    lv_obj_set_pos(moon, 500, 60);
    lv_obj_set_style_radius(moon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(moon, lv_color_hex(0xFAEDCD), 0);
    lv_obj_set_style_bg_opa(moon, LV_OPA_20, 0);
    lv_obj_set_style_border_width(moon, 1, 0);
    lv_obj_set_style_border_color(moon, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_border_opa(moon, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(moon, 50, 0);
    lv_obj_set_style_shadow_color(moon, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_shadow_opa(moon, LV_OPA_30, 0);
    lv_obj_remove_flag(moon, LV_OBJ_FLAG_SCROLLABLE);

    /* Glassmorphic Yukionna Weather Card (Left / Center) */
    lv_obj_t *card = lv_obj_create(s_scr_weather);
    lv_obj_add_style(card, &ui_style_glass_card, 0);
    lv_obj_set_size(card, 440, 360);
    lv_obj_set_pos(card, 40, 60);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* City Badge */
    lv_obj_t *badge_city = lv_obj_create(card);
    lv_obj_add_style(badge_city, &ui_style_pill_badge, 0);
    lv_obj_set_size(badge_city, LV_SIZE_CONTENT, 32);
    lv_obj_set_pos(badge_city, 16, 16);
    lv_obj_t *lbl_city = lv_label_create(badge_city);
    lv_label_set_text(lbl_city, "東京 / TOKYO");
    lv_obj_set_style_text_color(lbl_city, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_city, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_city);

    /* Status Badge (LIVE / DEMO) */
    lv_obj_t *badge_status = lv_obj_create(card);
    lv_obj_add_style(badge_status, &ui_style_pill_badge, 0);
    lv_obj_set_size(badge_status, LV_SIZE_CONTENT, 32);
    lv_obj_set_pos(badge_status, 170, 16);
    s_lbl_badge = lv_label_create(badge_status);
    lv_label_set_text(s_lbl_badge, "● DEMO");
    lv_obj_set_style_text_color(s_lbl_badge, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_badge, UI_FONT_SMALL, 0);
    lv_obj_center(s_lbl_badge);

    /* Large Temperature Display */
    s_lbl_temp = lv_label_create(card);
    lv_label_set_text(s_lbl_temp, "26°");
    lv_obj_set_style_text_color(s_lbl_temp, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(s_lbl_temp, UI_FONT_LARGE, 0);
    lv_obj_set_pos(s_lbl_temp, 20, 75);

    lv_obj_t *lbl_c = lv_label_create(card);
    lv_label_set_text(lbl_c, "C");
    lv_obj_set_style_text_color(lbl_c, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_c, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_c, 90, 80);

    /* Condition Label (e.g. 晴れ, 降雪, 小雨) */
    s_lbl_cond = lv_label_create(card);
    lv_label_set_text(s_lbl_cond, "晴れ / Clear");
    lv_obj_set_style_text_color(s_lbl_cond, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_cond, UI_FONT_TITLE, 0);
    lv_obj_set_pos(s_lbl_cond, 20, 140);

    /* Metrics: Humidity & Wind */
    lv_obj_t *lbl_metrics = lv_label_create(card);
    lv_label_set_text(lbl_metrics, "湿度: 65%   風速: 2.4 m/s");
    lv_obj_set_style_text_color(lbl_metrics, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_metrics, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_metrics, 20, 185);

    /* Time & Attribution */
    s_lbl_time = lv_label_create(card);
    lv_label_set_text(s_lbl_time, "更新: 14:30  (Open-Meteo)");
    lv_obj_set_style_text_color(s_lbl_time, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_time, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_time, 20, 230);

    /*
     * ThorVG Lottie Vector Snow / Atmospheric Overlay
     * Rendered inside the card overlaying the weather scene
     */
    s_lottie_snow = lv_lottie_create(card);
    if (s_lottie_snow) {
        lv_lottie_set_size(s_lottie_snow, 300, 200);
        lv_obj_set_pos(s_lottie_snow, 110, 120);

        if (!s_lottie_json_buf) {
            s_lottie_json_buf = heap_caps_malloc(sizeof(UI_LOTTIE_SNOW_JSON), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!s_lottie_json_buf) {
                s_lottie_json_buf = malloc(sizeof(UI_LOTTIE_SNOW_JSON));
            }
        }
        if (s_lottie_json_buf) {
            memcpy(s_lottie_json_buf, UI_LOTTIE_SNOW_JSON, sizeof(UI_LOTTIE_SNOW_JSON));
            lv_lottie_set_src_data(s_lottie_snow, s_lottie_json_buf, sizeof(UI_LOTTIE_SNOW_JSON) - 1);
            lv_lottie_set_loop_enabled(s_lottie_snow, true);
            lv_lottie_play(s_lottie_snow);
            ESP_LOGI(TAG, "ThorVG Lottie snow vector animation loaded & playing");
        }
    }

    /* Right Side Japanese Mythological Calligraphy / Yokai Label */
    lv_obj_t *yokai_card = lv_obj_create(s_scr_weather);
    lv_obj_add_style(yokai_card, &ui_style_glass_card, 0);
    lv_obj_set_size(yokai_card, 240, 360);
    lv_obj_set_pos(yokai_card, 510, 60);
    lv_obj_remove_flag(yokai_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_yokai_title = lv_label_create(yokai_card);
    lv_label_set_text(lbl_yokai_title, "雪 女 伝 説");
    lv_obj_set_style_text_color(lbl_yokai_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_yokai_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_yokai_title, 20, 20);

    lv_obj_t *lbl_yokai_desc = lv_label_create(yokai_card);
    lv_label_set_text(lbl_yokai_desc,
        "Yukionna Weather\n\n"
        "吹雪の夜に現れる\n"
        "純白の妖精。\n"
        "山嶺を巡る冷気と\n"
        "都会の天候を宿す。\n\n"
        "ESP32-S31 Korvo-1\n"
        "LVGL v9 + ThorVG");
    lv_obj_set_style_text_color(lbl_yokai_desc, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_yokai_desc, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_yokai_desc, 20, 65);

    /* Manual Refresh Button */
    lv_obj_t *btn_refresh = lv_button_create(yokai_card);
    lv_obj_add_style(btn_refresh, &ui_style_btn_home, 0);
    lv_obj_set_size(btn_refresh, 180, 42);
    lv_obj_set_pos(btn_refresh, 20, 280);
    lv_obj_add_event_cb(btn_refresh, refresh_click_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_refresh = lv_label_create(btn_refresh);
    lv_label_set_text(lbl_refresh, "天候更新 (Refresh)");
    lv_obj_set_style_text_font(lbl_refresh, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_refresh);

    /* Top Bar Navigation: Home Button */
    lv_obj_t *btn_home = lv_button_create(s_scr_weather);
    lv_obj_add_style(btn_home, &ui_style_btn_home, 0);
    lv_obj_set_size(btn_home, 110, 38);
    lv_obj_set_pos(btn_home, 20, 12);
    lv_obj_add_event_cb(btn_home, home_click_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_label_set_text(lbl_home, "< ホーム");
    lv_obj_set_style_text_font(lbl_home, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_home);

    return s_scr_weather;
}

void ui_weather_screen_update(const weather_info_t *info)
{
    if (!info || !s_scr_weather) {
        return;
    }

    if (s_lbl_temp) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d°", info->temp_c);
        lv_label_set_text(s_lbl_temp, buf);
    }

    if (s_lbl_cond) {
        lv_label_set_text(s_lbl_cond, (info->main_text[0] != '\0') ? info->main_text : "晴れ");
    }

    if (s_lbl_badge) {
        if (info->is_live) {
            lv_label_set_text(s_lbl_badge, "● LIVE");
            lv_obj_set_style_text_color(s_lbl_badge, UI_COLOR_CYAN_ACCENT, 0);
        } else {
            lv_label_set_text(s_lbl_badge, "○ DEMO");
            lv_obj_set_style_text_color(s_lbl_badge, UI_COLOR_GOLD_ACCENT, 0);
        }
    }

    if (s_lbl_time) {
        char buf[64];
        snprintf(buf, sizeof(buf), "更新: %s  (Open-Meteo)",
                 (info->update_time[0] != '\0') ? info->update_time : "14:30");
        lv_label_set_text(s_lbl_time, buf);
    }
}
