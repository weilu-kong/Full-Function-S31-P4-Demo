#include "ui/ui_weather.h"
#include "ui/ui_theme.h"
#include "ui/ui_lottie_assets.h"
#include "ui/ui_drawer.h"
#include "esp_lv_lottie.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_weather";

static lv_obj_t *s_scr_weather = NULL;
static lv_obj_t *s_img_bg = NULL;
static lv_obj_t *s_lbl_city = NULL;
static lv_obj_t *s_lbl_badge = NULL;
static lv_obj_t *s_lbl_temp = NULL;
static lv_obj_t *s_lbl_cond = NULL;
static lv_obj_t *s_lbl_time = NULL;
static lv_obj_t *s_lbl_lore = NULL;
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

static void drawer_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ui_drawer_set_visible(true);
}

lv_obj_t *ui_weather_screen_create(ui_home_btn_cb_t home_cb)
{
    s_home_cb = home_cb;

    s_scr_weather = lv_obj_create(NULL);
    lv_obj_set_size(s_scr_weather, 800, 480);
    lv_obj_set_style_bg_color(s_scr_weather, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(s_scr_weather, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. Full-screen Pixel Art Background (Yukionna Standing in Snow) */
    s_img_bg = lv_image_create(s_scr_weather);
    lv_obj_set_size(s_img_bg, 800, 480);
    lv_obj_set_pos(s_img_bg, 0, 0);
    lv_image_set_src(s_img_bg, &ui_img_weather_day);
    lv_obj_remove_flag(s_img_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* 2. Full-Screen Transparent Lottie Snowfall Overlay */
    s_lottie_snow = lv_lottie_create(s_scr_weather);
    if (s_lottie_snow) {
        lv_lottie_set_size(s_lottie_snow, 800, 480);
        lv_obj_set_pos(s_lottie_snow, 0, 0);
        lv_obj_remove_flag(s_lottie_snow, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        if (!s_lottie_json_buf) {
            s_lottie_json_buf = (char *)heap_caps_malloc(sizeof(UI_LOTTIE_SNOW_JSON), MALLOC_CAP_SPIRAM);
            if (s_lottie_json_buf) {
                memcpy(s_lottie_json_buf, UI_LOTTIE_SNOW_JSON, sizeof(UI_LOTTIE_SNOW_JSON));
            }
        }
        if (s_lottie_json_buf) {
            lv_lottie_set_src_data(s_lottie_snow, s_lottie_json_buf, sizeof(UI_LOTTIE_SNOW_JSON) - 1);
            lv_lottie_set_loop_enabled(s_lottie_snow, true);
            lv_lottie_play(s_lottie_snow);
        }
    }

    /* 3. Top Navigation: Home, Refresh, and Drawer Buttons */
    lv_obj_t *btn_home = lv_button_create(s_scr_weather);
    lv_obj_add_style(btn_home, &ui_style_btn_home, 0);
    lv_obj_set_size(btn_home, 106, 36);
    lv_obj_set_pos(btn_home, 16, 12);
    lv_obj_add_event_cb(btn_home, home_click_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_label_set_text(lbl_home, "ホーム");
    lv_obj_set_style_text_font(lbl_home, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_home);

    lv_obj_t *btn_refresh = lv_button_create(s_scr_weather);
    lv_obj_add_style(btn_refresh, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_refresh, 100, 36);
    lv_obj_set_pos(btn_refresh, 564, 12);
    lv_obj_add_event_cb(btn_refresh, refresh_click_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_refresh = lv_label_create(btn_refresh);
    lv_label_set_text(lbl_refresh, "更新");
    lv_obj_set_style_text_font(lbl_refresh, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_refresh);

    lv_obj_t *btn_drawer = lv_button_create(s_scr_weather);
    lv_obj_add_style(btn_drawer, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_drawer, 108, 36);
    lv_obj_set_pos(btn_drawer, 676, 12);
    lv_obj_add_event_cb(btn_drawer, drawer_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_drawer = lv_label_create(btn_drawer);
    lv_label_set_text(lbl_drawer, "設定 ▼");
    lv_obj_set_style_text_font(lbl_drawer, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_drawer);

    /* 4. Bare Text Overlay on Left Smooth Art Region (NO OPAQUE FRAMES!) */

    /* Location Title */
    s_lbl_city = lv_label_create(s_scr_weather);
    lv_label_set_text(s_lbl_city, "妖怪村（東京）");
    lv_obj_set_style_text_color(s_lbl_city, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_city, UI_FONT_TITLE, 0);
    lv_obj_set_pos(s_lbl_city, 28, 68);

    /* DEMO / LIVE Status Badge */
    s_lbl_badge = lv_label_create(s_scr_weather);
    lv_label_set_text(s_lbl_badge, "● DEMO");
    lv_obj_set_style_text_color(s_lbl_badge, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_badge, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_badge, 200, 72);

    /* Temperature */
    s_lbl_temp = lv_label_create(s_scr_weather);
    lv_label_set_text(s_lbl_temp, "26℃");
    lv_obj_set_style_text_color(s_lbl_temp, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_lbl_temp, UI_FONT_LARGE, 0);
    lv_obj_set_pos(s_lbl_temp, 28, 108);

    /* Weather Condition (晴れ, 雲, 雨, 雪, 雷) */
    s_lbl_cond = lv_label_create(s_scr_weather);
    lv_label_set_text(s_lbl_cond, "晴れ");
    lv_obj_set_style_text_color(s_lbl_cond, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_cond, UI_FONT_TITLE, 0);
    lv_obj_set_pos(s_lbl_cond, 28, 156);

    /* Update Time */
    s_lbl_time = lv_label_create(s_scr_weather);
    lv_label_set_text(s_lbl_time, "更新 14:30");
    lv_obj_set_style_text_color(s_lbl_time, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_time, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_time, 28, 194);

    /* Lore / Story description text (Wrapped neatly within left negative area) */
    s_lbl_lore = lv_label_create(s_scr_weather);
    lv_label_set_text(s_lbl_lore, "夜の妖怪村、提灯が灯り、静かに雪が舞い降ります。");
    lv_obj_set_style_text_color(s_lbl_lore, lv_color_hex(0xEDF2F7), 0);
    lv_obj_set_style_text_font(s_lbl_lore, UI_FONT_REGULAR, 0);
    lv_obj_set_width(s_lbl_lore, 220);
    lv_label_set_long_mode(s_lbl_lore, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_lbl_lore, 28, 236);

    return s_scr_weather;
}

void ui_weather_screen_update(const weather_info_t *info)
{
    if (!info || !s_scr_weather) {
        return;
    }

    /* 1. Dynamic Background Image Switch (Day vs Night) */
    if (s_img_bg) {
        if (!info->is_day) {
            lv_image_set_src(s_img_bg, &ui_img_weather_night);
        } else {
            lv_image_set_src(s_img_bg, &ui_img_weather_day);
        }
    }

    /* 2. Temperature */
    if (s_lbl_temp) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d℃", info->temp_c);
        lv_label_set_text(s_lbl_temp, buf);
    }

    /* 3. Weather Condition Name */
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
            cond_name = "雷";
            break;
        default:
            cond_name = "晴れ";
            break;
        }
        lv_label_set_text(s_lbl_cond, cond_name);
    }

    /* 4. Live vs Demo Status Badge */
    if (s_lbl_badge) {
        if (info->is_live) {
            lv_label_set_text(s_lbl_badge, "● LIVE");
            lv_obj_set_style_text_color(s_lbl_badge, UI_COLOR_CYAN_ACCENT, 0);
        } else {
            lv_label_set_text(s_lbl_badge, "○ DEMO");
            lv_obj_set_style_text_color(s_lbl_badge, UI_COLOR_GOLD_ACCENT, 0);
        }
    }

    /* 5. Update Time */
    if (s_lbl_time) {
        char buf[64];
        snprintf(buf, sizeof(buf), "更新 %s",
                 (info->update_time[0] != '\0') ? info->update_time : "14:30");
        lv_label_set_text(s_lbl_time, buf);
    }

    /* 6. Lore Text */
    if (s_lbl_lore && info->lore_text[0] != '\0') {
        lv_label_set_text(s_lbl_lore, info->lore_text);
    }
}
