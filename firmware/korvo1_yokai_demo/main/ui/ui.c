#include "ui/ui.h"
#include "ui/ui_theme.h"
#include "ui/ui_home.h"
#include "ui/ui_weather.h"
#include "ui/ui_synth.h"
#include "ui/ui_wifi.h"
#include "ui/ui_bluetooth.h"
#include "ui/ui_apps.h"
#include "ui/ui_fireworks.h"
#include "ui/ui_clock.h"
#include "ui/ui_calculator.h"
#include "ui/ui_drawer.h"
#include "ui/ui_image_loader.h"
#include "app_health.h"
#include "board_ui.h"
#include "weather_service.h"
#include "synth_service.h"
#include "voice_service.h"
#include "vision_service.h"
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
static int s_voice_saved_volume = 80;
static lv_obj_t *s_voice_toast = NULL;
static lv_obj_t *s_voice_toast_label = NULL;
static lv_timer_t *s_voice_toast_timer = NULL;

static void voice_toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_obj_add_flag(s_voice_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(s_voice_toast_timer);
}

static void show_voice_toast(const char *text)
{
    lv_label_set_text(s_voice_toast_label, text);
    lv_obj_remove_flag(s_voice_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(s_voice_toast_timer);
    lv_timer_resume(s_voice_toast_timer);
}

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
    ui_screen_t target = s_return_screen;
    if (target == UI_SCREEN_WIFI || target == UI_SCREEN_BLUETOOTH) {
        target = UI_SCREEN_HOME;
    }
    s_return_screen = UI_SCREEN_HOME;
    ui_switch_screen(target);
    if (s_reopen_drawer_on_return) {
        ui_drawer_set_visible(true);
        s_reopen_drawer_on_return = false;
    }
}

static void on_wifi_details_requested(void)
{
    ESP_LOGI(TAG, "Opening Wi-Fi settings");
    if (s_current_screen != UI_SCREEN_WIFI) {
        s_return_screen = s_current_screen;
    }
    s_reopen_drawer_on_return = false;
    ui_switch_screen(UI_SCREEN_WIFI);
    (void)board_ui_wifi_scan_async();
}

static void on_return_from_bt(void)
{
    ui_screen_t target = s_return_screen;
    if (target == UI_SCREEN_BLUETOOTH) {
        target = UI_SCREEN_HOME;
    }
    s_return_screen = UI_SCREEN_HOME;
    ui_switch_screen(target);
}

static void on_bt_details_requested(void)
{
    ESP_LOGI(TAG, "Opening Bluetooth settings");
    if (s_current_screen != UI_SCREEN_BLUETOOTH) {
        s_return_screen = s_current_screen;
    }
    s_reopen_drawer_on_return = false;
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

static void execute_voice_command(const voice_result_t *result)
{
    const voice_command_info_t *info = voice_service_command_info(result->command);
    if (!info) return;

    switch (info->target) {
    case VOICE_TARGET_SYNTH:      ui_switch_screen(UI_SCREEN_SYNTH); break;
    case VOICE_TARGET_WEATHER:    ui_switch_screen(UI_SCREEN_WEATHER); break;
    case VOICE_TARGET_VOICE:      ui_switch_screen(UI_SCREEN_VOICE); break;
    case VOICE_TARGET_VISION:     ui_switch_screen(UI_SCREEN_VISION); break;
    case VOICE_TARGET_FIREWORKS:  ui_switch_screen(UI_SCREEN_FIREWORKS); break;
    case VOICE_TARGET_CLOCK:      ui_switch_screen(UI_SCREEN_CLOCK); break;
    case VOICE_TARGET_CALCULATOR: ui_switch_screen(UI_SCREEN_CALCULATOR); break;
    case VOICE_TARGET_FOOD:       ui_switch_screen(UI_SCREEN_FOOD); break;
    case VOICE_TARGET_WIFI:       on_wifi_details_requested(); break;
    case VOICE_TARGET_BLUETOOTH:  on_bt_details_requested(); break;
    case VOICE_TARGET_HOME:       ui_switch_screen(UI_SCREEN_HOME); break;
    case VOICE_TARGET_VOLUME: {
        int volume = voice_service_apply_volume(result->command,
                                                synth_service_get_master_volume(),
                                                &s_voice_saved_volume);
        synth_service_set_master_volume(volume);
        ui_drawer_set_volume(volume);
        break;
    }
    default:
        break;
    }
}

void ui_init(lv_display_t *disp, app_state_t *state)
{
    (void)disp;
    s_app_state = state;

    /* Decompress Yokai background JPEGs into PSRAM via hardware acceleration */
    ui_images_init();

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
    lv_obj_set_style_pad_all(top_layer, 0, 0);
    lv_obj_set_style_border_width(top_layer, 0, 0);
    lv_obj_remove_flag(top_layer, LV_OBJ_FLAG_SCROLLABLE);
    s_drawer = ui_drawer_create(top_layer, on_wifi_toggle, on_wifi_details_requested,
                                on_bt_toggle, on_bt_details_requested,
                                on_volume_change, on_bright_change);
    ui_drawer_set_volume(synth_service_get_master_volume());

    s_voice_toast = lv_obj_create(top_layer);
    lv_obj_set_size(s_voice_toast, 420, 58);
    lv_obj_align(s_voice_toast, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_style_radius(s_voice_toast, 16, 0);
    lv_obj_set_style_bg_color(s_voice_toast, lv_color_hex(0x090D14), 0);
    lv_obj_set_style_bg_opa(s_voice_toast, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_voice_toast, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_border_width(s_voice_toast, 1, 0);
    lv_obj_remove_flag(s_voice_toast, LV_OBJ_FLAG_SCROLLABLE);
    s_voice_toast_label = lv_label_create(s_voice_toast);
    lv_obj_set_style_text_font(s_voice_toast_label, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(s_voice_toast_label, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_center(s_voice_toast_label);
    lv_obj_add_flag(s_voice_toast, LV_OBJ_FLAG_HIDDEN);
    s_voice_toast_timer = lv_timer_create(voice_toast_timer_cb, 1500, NULL);
    lv_timer_pause(s_voice_toast_timer);

    /* Start at Home Desktop */
    s_current_screen = UI_SCREEN_HOME;
    voice_service_set_mode(VOICE_MODE_GLOBAL_WAKE);
    lv_screen_load(s_screen_objs[UI_SCREEN_HOME]);
    ESP_LOGI(TAG, "Yokai UI initialized with 8 Apps + Wi-Fi & BT screens active");
}

/* -------------------------------------------------------------
 * iPhone-style Pure Black Snappy Center Transition (95ms ease-out)
 * ------------------------------------------------------------- */
static lv_obj_t *s_trans_card = NULL;
static bool s_trans_active = false;
static ui_screen_t s_pending_target = UI_SCREEN_HOME;

static void trans_geom_apply(lv_obj_t *card, int32_t v)
{
    /* Proportional 5:3 center expansion from 60x36 to 800x480 */
    int32_t w = 60 + ((800 - 60) * v) / 1000;
    int32_t h = 36 + ((480 - 36) * v) / 1000;
    int32_t x = 400 - w / 2;
    int32_t y = 240 - h / 2;
    int32_t r = (20 * (1000 - v)) / 1000;

    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_radius(card, r, 0);
}

static void trans_expand_exec_cb(lv_anim_t *a, int32_t v)
{
    lv_obj_t *card = (lv_obj_t *)a->var;
    if (card) {
        trans_geom_apply(card, v);
    }
}

static void trans_expand_completed_cb(lv_anim_t *a)
{
    lv_obj_t *card = (lv_obj_t *)a->var;
    if (s_pending_target < UI_SCREEN_MAX && s_screen_objs[s_pending_target]) {
        lv_screen_load(s_screen_objs[s_pending_target]);
        if (s_pending_target == UI_SCREEN_SYNTH) {
            synth_service_set_active(true);
        } else if (s_pending_target == UI_SCREEN_WEATHER) {
            ui_weather_set_active(true);
            time_t now = time(NULL);
            struct tm local = {0};
            char time_buf[16] = "--:--";
            if (localtime_r(&now, &local) != NULL && local.tm_year >= 120) {
                snprintf(time_buf, sizeof(time_buf), "%02d:%02d", local.tm_hour, local.tm_min);
            }
            board_wifi_info_t winfo;
            board_ui_wifi_get_info(&winfo);
            ui_weather_screen_update_status(time_buf, winfo.state == BOARD_WIFI_CONNECTED, winfo.connected_rssi, synth_service_get_master_volume());
            weather_info_t info;
            weather_service_get_info(&info);
            ui_weather_screen_update(&info);
        } else if (s_pending_target == UI_SCREEN_VISION) {
            ui_vision_set_active(true);
            vision_service_start();
        } else if (s_pending_target == UI_SCREEN_FIREWORKS) {
            ui_app_background_load(UI_APP_BG_FIREWORKS);
            ui_fireworks_set_active(true);
            app_health_log_heap("enter fireworks");
        } else if (s_pending_target == UI_SCREEN_CLOCK) {
            ui_app_background_load(UI_APP_BG_CLOCK);
            ui_clock_set_active(true);
            app_health_log_heap("enter clock");
        } else if (s_pending_target == UI_SCREEN_CALCULATOR) {
            ui_app_background_load(UI_APP_BG_CALCULATOR);
            app_health_log_heap("enter calculator");
        }
    }
    if (card) {
        lv_obj_delete(card);
    }
    s_trans_card = NULL;
    s_trans_active = false;
}

void ui_switch_screen(ui_screen_t target)
{
    if (target >= UI_SCREEN_MAX || !s_screen_objs[target]) {
        return;
    }
    if (target == s_current_screen) {
        return;
    }
    if (s_trans_active) {
        return;
    }

    voice_service_set_mode(target == UI_SCREEN_VOICE
                           ? VOICE_MODE_CONTINUOUS : VOICE_MODE_GLOBAL_WAKE);

    ui_screen_t prev = s_current_screen;
    s_current_screen = target;
    s_pending_target = target;

    if (prev == UI_SCREEN_SYNTH && target != UI_SCREEN_SYNTH) {
        synth_service_set_active(false);
    }
    if (prev == UI_SCREEN_WEATHER && target != UI_SCREEN_WEATHER) {
        ui_weather_set_active(false);
    }
    if (prev == UI_SCREEN_VISION && target != UI_SCREEN_VISION) {
        vision_service_stop();
        ui_vision_set_active(false);
    }
    if (prev == UI_SCREEN_FIREWORKS && target != UI_SCREEN_FIREWORKS) {
        ui_fireworks_set_active(false);
        app_health_log_heap("exit fireworks");
    }
    if (prev == UI_SCREEN_CLOCK && target != UI_SCREEN_CLOCK) {
        ui_clock_set_active(false);
        app_health_log_heap("exit clock");
    }
    if (prev == UI_SCREEN_CALCULATOR && target != UI_SCREEN_CALCULATOR) {
        app_health_log_heap("exit calculator");
    }
    if ((prev == UI_SCREEN_FIREWORKS || prev == UI_SCREEN_CLOCK || prev == UI_SCREEN_CALCULATOR) &&
        (target != UI_SCREEN_FIREWORKS && target != UI_SCREEN_CLOCK && target != UI_SCREEN_CALCULATOR)) {
        ui_app_background_free();
    }

    /*
     * iPhone-style Pure Black Snappy Center Transition:
     * Clean pure black surface rapidly blooms outward from center (400, 240) in 95ms,
     * seamlessly occluding the outgoing view and revealing the incoming dark screen
     * without any abrupt 1-frame blackout.
     */
    lv_obj_t *top = lv_layer_top();
    s_trans_active = true;
    s_trans_card = lv_obj_create(top);
    lv_obj_remove_flag(s_trans_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_trans_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_trans_card, 60, 36);
    lv_obj_set_pos(s_trans_card, 400 - 30, 240 - 18);
    lv_obj_set_style_radius(s_trans_card, 20, 0);
    lv_obj_set_style_bg_color(s_trans_card, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_trans_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_trans_card, 0, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_trans_card);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 95);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_custom_exec_cb(&a, trans_expand_exec_cb);
    lv_anim_set_completed_cb(&a, trans_expand_completed_cb);
    lv_anim_start(&a);
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
    ui_home_screen_update_status(time_buf, wifi_info.state == BOARD_WIFI_CONNECTED, wifi_info.connected_rssi);

    /* 3. Update Weather Screen */
    if (s_current_screen == UI_SCREEN_WEATHER) {
        ui_weather_screen_update_status(time_buf,
                                        wifi_info.state == BOARD_WIFI_CONNECTED,
                                        wifi_info.connected_rssi,
                                        synth_service_get_master_volume());
        if (weather_service_is_dirty()) {
            weather_info_t info;
            weather_service_get_info(&info);
            ui_weather_screen_update(&info);
            weather_service_clear_dirty();
        }
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

    /* 6. Update Clock / Timer and Fireworks */
    ui_clock_tick();
    if (s_current_screen == UI_SCREEN_FIREWORKS) {
        ui_fireworks_tick();
    }

    /* 7. Update Vision Screen */
    if (s_current_screen == UI_SCREEN_VISION) {
        board_ui_health_set_stage(UI_HEALTH_STAGE_VISION);
        ui_vision_screen_update();
        board_ui_health_set_stage(UI_HEALTH_STAGE_UI_PERIODIC);
    }

    /* 8. Consume speech results only on the LVGL thread. */
    board_ui_health_set_stage(UI_HEALTH_STAGE_VOICE);
    voice_result_t result;
    while (voice_service_receive(&result)) {
        if (result.event == VOICE_EVENT_WAKE) {
            synth_service_play_feedback_tone();
            show_voice_toast("御用でしょうか");
        } else if (result.event == VOICE_EVENT_RETRY) {
            show_voice_toast("もう一度");
        } else if (result.event == VOICE_EVENT_COMMAND) {
            const voice_command_info_t *info = voice_service_command_info(result.command);
            execute_voice_command(&result);
            if (info) {
                char text[96];
                if (info->target == VOICE_TARGET_VOLUME) {
                    snprintf(text, sizeof(text), "%s  %d%%", info->feature,
                             synth_service_get_master_volume());
                    show_voice_toast(text);
                } else {
                    show_voice_toast(info->feature);
                }
            }
        }
        ui_voice_screen_update(&result, synth_service_get_master_volume(),
                               voice_service_is_ready(), voice_service_error());
    }
    board_ui_health_set_stage(UI_HEALTH_STAGE_UI_PERIODIC);
}
