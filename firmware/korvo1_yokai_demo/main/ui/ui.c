#include "ui/ui.h"
#include "ui/ui_theme.h"
#include "ui/ui_home.h"
#include "ui/ui_weather.h"
#include "ui/ui_synth.h"
#include "ui/ui_drawer.h"
#include "weather_service.h"
#include "synth_service.h"
#include "esp_log.h"
#include <time.h>
#include <stdio.h>

static const char *TAG = "ui";

static ui_screen_t s_current_screen = UI_SCREEN_HOME;
static lv_obj_t *s_screen_objs[UI_SCREEN_MAX] = {0};
static lv_obj_t *s_drawer = NULL;
static app_state_t *s_app_state = NULL;

static void on_app_launch(ui_app_id_t app)
{
    ESP_LOGI(TAG, "Launch app: %d", app);
    switch (app) {
    case UI_APP_SYNTH:
        ui_switch_screen(UI_SCREEN_SYNTH);
        break;
    case UI_APP_WEATHER:
        ui_switch_screen(UI_SCREEN_WEATHER);
        break;
    default:
        ESP_LOGI(TAG, "App %d selected (placeholder)", app);
        break;
    }
}

static void on_return_home(void)
{
    ui_switch_screen(UI_SCREEN_HOME);
}

static void on_drawer_toggle(void)
{
    ui_drawer_toggle();
}

static void on_wifi_toggle(bool enable)
{
    ESP_LOGI(TAG, "Drawer Wi-Fi toggled: %d", enable);
    /* Wi-Fi state handling */
}

static void on_bt_toggle(bool enable)
{
    ESP_LOGI(TAG, "Drawer BT toggled: %d", enable);
}

static void on_volume_change(int volume)
{
    ESP_LOGD(TAG, "Master volume changed: %d", volume);
}

static void on_bright_change(int brightness)
{
    ESP_LOGD(TAG, "Brightness changed: %d", brightness);
}

void ui_init(lv_display_t *disp, app_state_t *state)
{
    (void)disp;
    s_app_state = state;

    ui_theme_init();

    /* Create Screens */
    s_screen_objs[UI_SCREEN_HOME] = ui_home_screen_create(on_app_launch, on_drawer_toggle);
    s_screen_objs[UI_SCREEN_WEATHER] = ui_weather_screen_create(on_return_home);
    s_screen_objs[UI_SCREEN_SYNTH] = ui_synth_screen_create(on_return_home);

    /* Create Quick Settings Drawer on the persistent Top Layer */
    lv_obj_t *top_layer = lv_layer_top();
    s_drawer = ui_drawer_create(top_layer, on_wifi_toggle, on_bt_toggle, on_volume_change, on_bright_change);

    /* Start at Home Desktop */
    s_current_screen = UI_SCREEN_HOME;
    lv_screen_load(s_screen_objs[UI_SCREEN_HOME]);
    ESP_LOGI(TAG, "Yokai UI initialized with Home screen active");
}

void ui_switch_screen(ui_screen_t target)
{
    if (target >= UI_SCREEN_MAX || !s_screen_objs[target]) {
        return;
    }

    if (s_current_screen == UI_SCREEN_SYNTH && target != UI_SCREEN_SYNTH) {
        synth_service_set_active(false);
    } else if (target == UI_SCREEN_SYNTH) {
        synth_service_set_active(true);
    }

    s_current_screen = target;
    lv_screen_load_anim(s_screen_objs[target], LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

ui_screen_t ui_get_current_screen(void)
{
    return s_current_screen;
}

void ui_tick_periodic(void)
{
    /* 1. Update clock */
    time_t now = time(NULL);
    struct tm local = {0};
    char time_buf[16] = "--:--";
    if (localtime_r(&now, &local) != NULL && local.tm_year >= 120) {
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d", local.tm_hour, local.tm_min);
    }
    ui_home_screen_update_status(time_buf, false);

    /* 2. Update Weather Screen */
    if (s_current_screen == UI_SCREEN_WEATHER && weather_service_is_dirty()) {
        weather_info_t info;
        weather_service_get_info(&info);
        ui_weather_screen_update(&info);
        weather_service_clear_dirty();
    }

    /* 3. Update Synth Oscilloscope */
    if (s_current_screen == UI_SCREEN_SYNTH) {
        ui_synth_update_waveform();
    }
}
