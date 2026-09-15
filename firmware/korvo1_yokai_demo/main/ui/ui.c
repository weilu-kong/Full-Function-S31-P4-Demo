#include "ui/ui.h"
#include "ui/ui_theme.h"
#include "ui/ui_home.h"
#include "ui/ui_weather.h"
#include "ui/ui_synth.h"
#include "ui/ui_wifi.h"
#include "ui/ui_bluetooth.h"
#include "ui/ui_apps.h"
#include "ui/ui_drawer.h"
#include "board_ui.h"
#include "weather_service.h"
#include "synth_service.h"
#include "esp_log.h"
#include <time.h>
#include <stdio.h>

static const char *TAG = "ui";

static ui_screen_t s_current_screen = UI_SCREEN_HOME;
static ui_screen_t s_return_screen = UI_SCREEN_HOME;
static bool s_reopen_drawer_on_return = false;
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
    case UI_APP_VOICE:
        ui_switch_screen(UI_SCREEN_VOICE);
        break;
    case UI_APP_VISION:
        ui_switch_screen(UI_SCREEN_VISION);
        break;
    case UI_APP_FIREWORKS:
        ui_switch_screen(UI_SCREEN_FIREWORKS);
        break;
    case UI_APP_CLOCK:
        ui_switch_screen(UI_SCREEN_CLOCK);
        break;
    case UI_APP_CALCULATOR:
        ui_switch_screen(UI_SCREEN_CALCULATOR);
        break;
    case UI_APP_FOOD:
        ui_switch_screen(UI_SCREEN_FOOD);
        break;
    default:
        ESP_LOGI(TAG, "Unknown app %d", app);
        break;
    }
}

static void on_return_home(void)
{
    ui_switch_screen(UI_SCREEN_HOME);
}

static void on_return_from_wifi(void)
{
    ui_switch_screen(s_return_screen);
    if (s_reopen_drawer_on_return) {
        ui_drawer_set_visible(true);
        s_reopen_drawer_on_return = false;
    }
}

static void on_wifi_details_requested(void)
{
    ESP_LOGI(TAG, "Opening Wi-Fi settings from drawer");
    s_return_screen = s_current_screen;
    s_reopen_drawer_on_return = true;
    ui_switch_screen(UI_SCREEN_WIFI);
    (void)board_ui_wifi_scan_async();
}

static void on_return_from_bt(void)
{
    ui_switch_screen(s_return_screen);
    if (s_reopen_drawer_on_return) {
        ui_drawer_set_visible(true);
        s_reopen_drawer_on_return = false;
    }
}

static void on_bt_details_requested(void)
{
    ESP_LOGI(TAG, "Opening Bluetooth settings from drawer");
    s_return_screen = s_current_screen;
    s_reopen_drawer_on_return = true;
    ui_switch_screen(UI_SCREEN_BLUETOOTH);
}

static void on_drawer_toggle(void)
{
    ui_drawer_toggle();
}

static void on_wifi_toggle(bool enable)
{
    ESP_LOGI(TAG, "Drawer Wi-Fi toggled: %d", enable);
    board_ui_wifi_set_enabled(enable);
    if (enable) {
        board_ui_wifi_reconnect_saved();
    } else {
        board_ui_wifi_disconnect();
    }
}

static void on_bt_toggle(bool enable)
{
    ESP_LOGI(TAG, "Drawer BT toggled: %d", enable);
    synth_service_set_bt_enabled(enable);
}

static void on_volume_change(int volume)
{
    ESP_LOGI(TAG, "Master volume changed: %d", volume);
    synth_service_set_master_volume(volume);
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

    /* Create All Screens */
    s_screen_objs[UI_SCREEN_HOME] = ui_home_screen_create(on_app_launch, on_drawer_toggle);
    s_screen_objs[UI_SCREEN_WEATHER] = ui_weather_screen_create(on_return_home);
    s_screen_objs[UI_SCREEN_SYNTH] = ui_synth_screen_create(on_return_home);
    s_screen_objs[UI_SCREEN_WIFI] = ui_wifi_screen_create(on_return_from_wifi);
    s_screen_objs[UI_SCREEN_BLUETOOTH] = ui_bluetooth_screen_create(on_return_from_bt);
    s_screen_objs[UI_SCREEN_VOICE] = ui_voice_screen_create(on_return_home);
    s_screen_objs[UI_SCREEN_VISION] = ui_vision_screen_create(on_return_home);
    s_screen_objs[UI_SCREEN_FIREWORKS] = ui_fireworks_screen_create(on_return_home);
    s_screen_objs[UI_SCREEN_CLOCK] = ui_clock_screen_create(on_return_home);
    s_screen_objs[UI_SCREEN_CALCULATOR] = ui_calculator_screen_create(on_return_home);
    s_screen_objs[UI_SCREEN_FOOD] = ui_food_screen_create(on_return_home);

    /* Create Quick Settings Drawer on the persistent Top Layer */
    lv_obj_t *top_layer = lv_layer_top();
    s_drawer = ui_drawer_create(top_layer, on_wifi_toggle, on_wifi_details_requested,
                                on_bt_toggle, on_bt_details_requested,
                                on_volume_change, on_bright_change);

    /* Start at Home Desktop */
    s_current_screen = UI_SCREEN_HOME;
    lv_screen_load(s_screen_objs[UI_SCREEN_HOME]);
    ESP_LOGI(TAG, "Yokai UI initialized with 8 Apps + Wi-Fi & BT screens active");
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
    /* 1. Wi-Fi Status Check & Update */
    board_wifi_info_t wifi_info;
    board_ui_wifi_get_info(&wifi_info);

    if (board_ui_wifi_is_dirty()) {
        board_ui_wifi_clear_dirty();
        if (s_current_screen == UI_SCREEN_WIFI) {
            ui_wifi_screen_update(&wifi_info);
        }
        ui_drawer_update_status(&wifi_info);
    } else if (s_current_screen == UI_SCREEN_WIFI) {
        ui_wifi_screen_update(&wifi_info);
    }

    /* 2. Update Home Clock and Status Header */
    time_t now = time(NULL);
    struct tm local = {0};
    char time_buf[16] = "--:--";
    if (localtime_r(&now, &local) != NULL && local.tm_year >= 120) {
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d", local.tm_hour, local.tm_min);
    }
    ui_home_screen_update_status(time_buf, wifi_info.state == BOARD_WIFI_CONNECTED);

    /* 3. Update Weather Screen */
    if (s_current_screen == UI_SCREEN_WEATHER && weather_service_is_dirty()) {
        weather_info_t info;
        weather_service_get_info(&info);
        ui_weather_screen_update(&info);
        weather_service_clear_dirty();
    }

    /* 4. Update Bluetooth Status */
    bool bt_enabled = synth_service_is_bt_enabled();
    bool bt_connected = synth_service_is_bt_connected();
    bool bt_streaming = synth_service_is_bt_streaming();
    char dev_name[32] = "";
    char bda_str[24] = "";
    if (bt_connected) {
        synth_service_get_bt_device_info(dev_name, sizeof(dev_name), bda_str, sizeof(bda_str));
    }
    ui_drawer_update_bt_status(bt_enabled, bt_connected, dev_name);
    if (s_current_screen == UI_SCREEN_BLUETOOTH) {
        ui_bluetooth_screen_update(bt_enabled, bt_connected, bt_streaming, dev_name, bda_str);
    }

    /* 5. Update Synth Oscilloscope */
    if (s_current_screen == UI_SCREEN_SYNTH) {
        ui_synth_update_waveform();
    }

    /* 6. Update Remaining Apps (Clock / Timer, etc.) */
    ui_apps_tick_periodic();
}
