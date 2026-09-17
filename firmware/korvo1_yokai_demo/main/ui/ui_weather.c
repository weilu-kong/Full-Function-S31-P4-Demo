#include "ui/ui_weather.h"
#include "ui/ui_theme.h"
#include "ui/ui_lottie_assets.h"
#include "ui/ui_drawer.h"
#include "ui/ui_wifi_signal.h"
#include "esp_lv_lottie.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_weather";

static lv_obj_t *s_scr_weather = NULL;
static lv_obj_t *s_img_bg = NULL;

/* Top Status Bar Handles */
static lv_obj_t *s_lbl_weather_clock = NULL;
static ui_wifi_signal_t s_weather_wifi_sig;
static lv_obj_t *s_lbl_weather_vol = NULL;

/* Left Card Weather Info Handles */
static lv_obj_t *s_lbl_city = NULL;
static lv_obj_t *s_lbl_badge = NULL;
static lv_obj_t *s_lbl_temp = NULL;
static lv_obj_t *s_lbl_cond = NULL;
static lv_obj_t *s_lbl_time = NULL;
static lv_obj_t *s_lbl_lore = NULL;

/* Precipitation Lottie Overlays */
static lv_obj_t *s_lottie_snow = NULL;
static lv_obj_t *s_lottie_rain = NULL;
static char *s_lottie_snow_buf = NULL;
static char *s_lottie_rain_buf = NULL;

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
    lv_obj_set_size(s_scr_weather, 800, 480);
    lv_obj_set_style_bg_color(s_scr_weather, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(s_scr_weather, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. Full-screen Clean Pixel Art Background */
    s_img_bg = lv_image_create(s_scr_weather);
    lv_obj_set_size(s_img_bg, 800, 480);
    lv_obj_set_pos(s_img_bg, 0, 0);
    lv_image_set_src(s_img_bg, &ui_img_weather_sunny);
    lv_obj_remove_flag(s_img_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* 2a. Full-Screen Transparent Lottie Snowfall Overlay */
    s_lottie_snow = lv_lottie_create(s_scr_weather);
    if (s_lottie_snow) {
        lv_lottie_set_size(s_lottie_snow, 800, 480);
        lv_obj_set_pos(s_lottie_snow, 0, 0);
        lv_obj_remove_flag(s_lottie_snow, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        if (!s_lottie_snow_buf) {
            s_lottie_snow_buf = (char *)heap_caps_malloc(sizeof(UI_LOTTIE_SNOW_JSON), MALLOC_CAP_SPIRAM);
            if (s_lottie_snow_buf) {
                memcpy(s_lottie_snow_buf, UI_LOTTIE_SNOW_JSON, sizeof(UI_LOTTIE_SNOW_JSON));
            }
        }
        if (s_lottie_snow_buf) {
            lv_lottie_set_src_data(s_lottie_snow, s_lottie_snow_buf, sizeof(UI_LOTTIE_SNOW_JSON) - 1);
            lv_lottie_set_loop_enabled(s_lottie_snow, true);
        }
        lv_obj_add_flag(s_lottie_snow, LV_OBJ_FLAG_HIDDEN);
    }

    /* 2b. Full-Screen Transparent Lottie Rainfall Overlay */
    s_lottie_rain = lv_lottie_create(s_scr_weather);
    if (s_lottie_rain) {
        lv_lottie_set_size(s_lottie_rain, 800, 480);
        lv_obj_set_pos(s_lottie_rain, 0, 0);
        lv_obj_remove_flag(s_lottie_rain, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        if (!s_lottie_rain_buf) {
            s_lottie_rain_buf = (char *)heap_caps_malloc(sizeof(UI_LOTTIE_RAIN_JSON), MALLOC_CAP_SPIRAM);
            if (s_lottie_rain_buf) {
                memcpy(s_lottie_rain_buf, UI_LOTTIE_RAIN_JSON, sizeof(UI_LOTTIE_RAIN_JSON));
            }
        }
        if (s_lottie_rain_buf) {
            lv_lottie_set_src_data(s_lottie_rain, s_lottie_rain_buf, sizeof(UI_LOTTIE_RAIN_JSON) - 1);
            lv_lottie_set_loop_enabled(s_lottie_rain, true);
        }
        lv_obj_add_flag(s_lottie_rain, LV_OBJ_FLAG_HIDDEN);
    }

    /* 3. Top Navigation & Status Bar (x: 0..800, y: 0..38) */
    lv_obj_t *top_bar = lv_obj_create(s_scr_weather);
    lv_obj_set_size(top_bar, 800, 38);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Left: Home Return Button */
    lv_obj_t *btn_home = lv_button_create(top_bar);
    lv_obj_add_style(btn_home, &ui_style_btn_home, 0);
    lv_obj_set_size(btn_home, 96, 30);
    lv_obj_set_pos(btn_home, 16, 4);
    lv_obj_add_event_cb(btn_home, home_click_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_label_set_text(lbl_home, "< ホーム");
    lv_obj_set_style_text_font(lbl_home, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_home);

    /* Left-Mid: Screen Title */
    lv_obj_t *lbl_title = lv_label_create(top_bar);
    lv_label_set_text(lbl_title, "天気");
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_title, 122, 6);

    /* Center: Digital Clock */
    s_lbl_weather_clock = lv_label_create(top_bar);
    lv_label_set_text(s_lbl_weather_clock, "--:--");
    lv_obj_set_style_text_color(s_lbl_weather_clock, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(s_lbl_weather_clock, UI_FONT_SMALL, 0);
    lv_obj_align(s_lbl_weather_clock, LV_ALIGN_TOP_MID, 0, 8);

    /* Right: Styled Manual Refresh Button */
    lv_obj_t *btn_refresh = lv_button_create(top_bar);
    lv_obj_add_style(btn_refresh, &ui_style_btn_home, 0);
    lv_obj_set_size(btn_refresh, 76, 30);
    lv_obj_set_pos(btn_refresh, 580, 4);
    lv_obj_add_event_cb(btn_refresh, refresh_click_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_refresh = lv_label_create(btn_refresh);
    lv_label_set_text(lbl_refresh, "更新");
    lv_obj_set_style_text_font(lbl_refresh, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_refresh);

    /* Right: Dynamic Wi-Fi 4-Bar Signal */
    ui_wifi_signal_create(&s_weather_wifi_sig, top_bar, 672, 11);

    /* Right: Dynamic Master Volume */
    s_lbl_weather_vol = lv_label_create(top_bar);
    lv_label_set_text(s_lbl_weather_vol, "♩ 80%");
    lv_obj_set_style_text_color(s_lbl_weather_vol, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_weather_vol, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_weather_vol, 708, 8);

    /* 3b. Bottom Navigation: Touch Hotspot over Artwork's Home Glyph (center bottom) */
    lv_obj_t *btn_bottom_home = lv_button_create(s_scr_weather);
    lv_obj_remove_style_all(btn_bottom_home);
    lv_obj_set_size(btn_bottom_home, 140, 50);
    lv_obj_set_pos(btn_bottom_home, 330, 425);
    lv_obj_add_event_cb(btn_bottom_home, home_click_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_radius(btn_bottom_home, 8, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn_bottom_home, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn_bottom_home, LV_OPA_20, LV_STATE_PRESSED);

    /* 4. Dynamic Text Fitted Neatly Inside Left Pixel Art Scroll Frame (x: 27..199, y: 56..394) */

    /* Location Title (Single line, 165px wide) */
    s_lbl_city = lv_label_create(s_scr_weather);
    lv_label_set_text(s_lbl_city, "妖怪の里（東京）");
    lv_obj_set_style_text_color(s_lbl_city, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_city, UI_FONT_TITLE, 0);
    lv_obj_set_width(s_lbl_city, 165);
    lv_obj_set_pos(s_lbl_city, 34, 56);

    /* DEMO / LIVE Status Badge */
    s_lbl_badge = lv_label_create(s_scr_weather);
    lv_label_set_text(s_lbl_badge, "● DEMO");
    lv_obj_set_style_text_color(s_lbl_badge, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_badge, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_badge, 34, 82);

    /* Large Temperature Display */
    s_lbl_temp = lv_label_create(s_scr_weather);
    lv_label_set_text(s_lbl_temp, "20℃");
    lv_obj_set_style_text_color(s_lbl_temp, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_lbl_temp, UI_FONT_LARGE, 0);
    lv_obj_set_pos(s_lbl_temp, 34, 106);

    /* Weather Condition (Beside temperature: x=120, y=114) */
    s_lbl_cond = lv_label_create(s_scr_weather);
    lv_label_set_text(s_lbl_cond, "晴れ");
    lv_obj_set_style_text_color(s_lbl_cond, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_cond, UI_FONT_TITLE, 0);
    lv_obj_set_pos(s_lbl_cond, 120, 114);

    /* Lore / Story Description Text (Wrapped within 160px) */
    s_lbl_lore = lv_label_create(s_scr_weather);
    lv_label_set_text(s_lbl_lore, "村の上には、澄み渡る空と富士の嶺。");
    lv_obj_set_style_text_color(s_lbl_lore, lv_color_hex(0xEDF2F7), 0);
    lv_obj_set_style_text_font(s_lbl_lore, UI_FONT_SMALL, 0);
    lv_obj_set_width(s_lbl_lore, 160);
    lv_label_set_long_mode(s_lbl_lore, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_lbl_lore, 34, 156);

    /* Update Time Text at Bottom of Left Scroll (to the right of stone lantern, high contrast) */
    s_lbl_time = lv_label_create(s_scr_weather);
    lv_label_set_text(s_lbl_time, "更新 14:30");
    lv_obj_set_style_text_color(s_lbl_time, lv_color_hex(0xEDF2F7), 0);
    lv_obj_set_style_text_font(s_lbl_time, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_time, 75, 352);

    return s_scr_weather;
}

void ui_weather_screen_update_status(const char *time_str, bool wifi_connected, int rssi, int volume)
{
    if (s_lbl_weather_clock && time_str) {
        lv_label_set_text(s_lbl_weather_clock, time_str);
    }
    if (wifi_connected) {
        int level = ui_wifi_rssi_to_level(rssi);
        ui_wifi_signal_set_level(&s_weather_wifi_sig, level);
    } else {
        ui_wifi_signal_set_level(&s_weather_wifi_sig, 0);
    }
    if (s_lbl_weather_vol) {
        char buf[16];
        if (volume <= 0) {
            snprintf(buf, sizeof(buf), "♩ 消音");
            lv_obj_set_style_text_color(s_lbl_weather_vol, UI_COLOR_TEXT_SUB, 0);
        } else {
            snprintf(buf, sizeof(buf), "♩ %d%%", volume);
            lv_obj_set_style_text_color(s_lbl_weather_vol, UI_COLOR_CYAN_ACCENT, 0);
        }
        lv_label_set_text(s_lbl_weather_vol, buf);
    }
}

void ui_weather_screen_update(const weather_info_t *info)
{
    if (!info || !s_scr_weather) {
        return;
    }

    /* 1. Dynamic Background Image Switch based on weather condition & day/night */
    const lv_image_dsc_t *bg_img = &ui_img_weather_sunny;
    bool is_rain = (info->condition == WEATHER_COND_RAINY || info->condition == WEATHER_COND_THUNDER);
    bool is_snow = (info->condition == WEATHER_COND_SNOWY);

    if (is_rain) {
        bg_img = &ui_img_weather_rain;
    } else if (is_snow || !info->is_day) {
        bg_img = &ui_img_weather_night;
    } else if (info->condition == WEATHER_COND_CLOUDY) {
        bg_img = &ui_img_weather_cloudy;
    } else {
        bg_img = &ui_img_weather_sunny;
    }

    if (s_img_bg) {
        lv_image_set_src(s_img_bg, bg_img);
    }

    /* 2. Dynamic Precipitation Effect: Rain vs Snow vs Clear */
    if (is_rain) {
        if (s_lottie_rain) {
            lv_obj_remove_flag(s_lottie_rain, LV_OBJ_FLAG_HIDDEN);
            lv_lottie_play(s_lottie_rain);
        }
        if (s_lottie_snow) {
            lv_obj_add_flag(s_lottie_snow, LV_OBJ_FLAG_HIDDEN);
            lv_lottie_pause(s_lottie_snow);
        }
    } else if (is_snow) {
        if (s_lottie_snow) {
            lv_obj_remove_flag(s_lottie_snow, LV_OBJ_FLAG_HIDDEN);
            lv_lottie_play(s_lottie_snow);
        }
        if (s_lottie_rain) {
            lv_obj_add_flag(s_lottie_rain, LV_OBJ_FLAG_HIDDEN);
            lv_lottie_pause(s_lottie_rain);
        }
    } else {
        /* Clear / Cloudy: hide both precipitation animations */
        if (s_lottie_rain) {
            lv_obj_add_flag(s_lottie_rain, LV_OBJ_FLAG_HIDDEN);
            lv_lottie_pause(s_lottie_rain);
        }
        if (s_lottie_snow) {
            lv_obj_add_flag(s_lottie_snow, LV_OBJ_FLAG_HIDDEN);
            lv_lottie_pause(s_lottie_snow);
        }
    }

    /* 3. Temperature */
    if (s_lbl_temp) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d℃", info->temp_c);
        lv_label_set_text(s_lbl_temp, buf);
    }

    /* 4. Weather Condition Name */
    if (s_lbl_cond) {
        const char *cond_name = "晴れ";
        switch (info->condition) {
        case WEATHER_COND_CLOUDY:
            cond_name = "曇り";
            break;
        case WEATHER_COND_RAINY:
            cond_name = "雨";
            break;
        case WEATHER_COND_SNOWY:
            cond_name = "雪";
            break;
        case WEATHER_COND_THUNDER:
            cond_name = "雷雨";
            break;
        default:
            cond_name = "晴れ";
            break;
        }
        lv_label_set_text(s_lbl_cond, cond_name);
    }

    /* 5. Live vs Demo Status Badge */
    if (s_lbl_badge) {
        if (info->is_live) {
            lv_label_set_text(s_lbl_badge, "● LIVE");
            lv_obj_set_style_text_color(s_lbl_badge, UI_COLOR_CYAN_ACCENT, 0);
        } else {
            lv_label_set_text(s_lbl_badge, "○ DEMO");
            lv_obj_set_style_text_color(s_lbl_badge, UI_COLOR_GOLD_ACCENT, 0);
        }
    }

    /* 6. Update Time */
    if (s_lbl_time) {
        char buf[48];
        snprintf(buf, sizeof(buf), "更新 %s",
                 (info->update_time[0] != '\0') ? info->update_time : "14:30");
        lv_label_set_text(s_lbl_time, buf);
    }

    /* 7. Lore Text */
    if (s_lbl_lore && info->lore_text[0] != '\0') {
        lv_label_set_text(s_lbl_lore, info->lore_text);
    }
}

void ui_weather_set_active(bool active)
{
    if (active) {
        if (s_lottie_snow && !lv_obj_has_flag(s_lottie_snow, LV_OBJ_FLAG_HIDDEN)) {
            lv_lottie_play(s_lottie_snow);
        }
        if (s_lottie_rain && !lv_obj_has_flag(s_lottie_rain, LV_OBJ_FLAG_HIDDEN)) {
            lv_lottie_play(s_lottie_rain);
        }
    } else {
        if (s_lottie_snow) {
            lv_lottie_pause(s_lottie_snow);
        }
        if (s_lottie_rain) {
            lv_lottie_pause(s_lottie_rain);
        }
    }
}
