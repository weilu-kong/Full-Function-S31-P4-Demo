#include "board_ui.h"

#include <stdio.h>

#include "esp_check.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/display.h"
#include "bsp/esp32_s31_korvo_1.h"
#include "bsp/touch.h"

#define GSP_BUNDLE_ENABLE_RAW_IDS 1
#include "bundle_gsp.h"

static const char *TAG = "board_ui";
static bool s_wifi_ready;
static bool s_wifi_scan_running;

static void wifi_set_rows(esp_gsp_handle_t ui, const char *status,
                          const wifi_ap_record_t *aps, uint16_t count)
{
    const uint16_t binds[] = {
        GSP_KORVO_WIFI_BIND_WIFI_AP_0, GSP_KORVO_WIFI_BIND_WIFI_AP_1,
        GSP_KORVO_WIFI_BIND_WIFI_AP_2, GSP_KORVO_WIFI_BIND_WIFI_AP_3,
        GSP_KORVO_WIFI_BIND_WIFI_AP_4,
    };
    char row[72];
    (void)esp_gsp_set_text(ui, GSP_KORVO_WIFI_BIND_WIFI_SCAN_STATUS, status);
    for (size_t i = 0; i < 5; ++i) {
        if (i < count) {
            snprintf(row, sizeof(row), "%s    %d dBm",
                     (const char *)aps[i].ssid, aps[i].rssi);
            (void)esp_gsp_set_text(ui, binds[i], row);
        } else {
            (void)esp_gsp_set_text(ui, binds[i], "--");
        }
    }
}

static esp_err_t wifi_start_once(void)
{
    if (s_wifi_ready) {
        return ESP_OK;
    }
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&config)) != ESP_OK ||
        (err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (err = esp_wifi_start()) != ESP_OK) {
        return err;
    }
    s_wifi_ready = true;
    return ESP_OK;
}

static void wifi_scan_task(void *arg)
{
    esp_gsp_handle_t ui = arg;
    wifi_ap_record_t aps[5] = {0};
    uint16_t count = 5;
    vTaskDelay(pdMS_TO_TICKS(250));
    wifi_set_rows(ui, "Wi-Fi...", NULL, 0);
    esp_err_t err = wifi_start_once();
    if (err == ESP_OK) {
        err = esp_wifi_scan_start(NULL, true);
    }
    if (err == ESP_OK) {
        err = esp_wifi_scan_get_ap_records(&count, aps);
    }
    if (err == ESP_OK) {
        char status[48];
        snprintf(status, sizeof(status), "Wi-Fi %u", (unsigned)count);
        wifi_set_rows(ui, status, aps, count);
    } else {
        ESP_LOGE(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
        wifi_set_rows(ui, "Wi-Fi ERROR", NULL, 0);
    }
    s_wifi_scan_running = false;
    vTaskDelete(NULL);
}

static void wifi_scan_async(esp_gsp_handle_t ui)
{
    if (s_wifi_scan_running) {
        return;
    }
    s_wifi_scan_running = true;
    if (xTaskCreate(wifi_scan_task, "wifi_scan", 6144, ui, 5, NULL) != pdPASS) {
        s_wifi_scan_running = false;
        wifi_set_rows(ui, "Wi-Fi ERROR", NULL, 0);
    }
}

static bool is_wifi_details_event(const esp_gsp_event_t *event)
{
    switch (event->scene_id) {
    case GSP_BUNDLE_SCENE_KORVO_HOME:
        return event->action_id == GSP_KORVO_HOME_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_SYNTH:
        return event->action_id == GSP_KORVO_SYNTH_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_WEATHER:
        return event->action_id == GSP_KORVO_WEATHER_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_VOICE:
        return event->action_id == GSP_KORVO_VOICE_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_OBJECT:
        return event->action_id == GSP_KORVO_OBJECT_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_LIGHTING:
        return event->action_id == GSP_KORVO_LIGHTING_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_CLOCK_TIMER:
        return event->action_id == GSP_KORVO_CLOCK_TIMER_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_CALCULATOR:
        return event->action_id == GSP_KORVO_CALCULATOR_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_FOOD:
        return event->action_id == GSP_KORVO_FOOD_ACT_ID_WIFI_DETAILS;
    default:
        return false;
    }
}

static bool is_bluetooth_details_event(const esp_gsp_event_t *event)
{
    switch (event->scene_id) {
    case GSP_BUNDLE_SCENE_KORVO_HOME:
        return event->action_id == GSP_KORVO_HOME_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_SYNTH:
        return event->action_id == GSP_KORVO_SYNTH_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_WEATHER:
        return event->action_id == GSP_KORVO_WEATHER_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_VOICE:
        return event->action_id == GSP_KORVO_VOICE_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_OBJECT:
        return event->action_id == GSP_KORVO_OBJECT_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_LIGHTING:
        return event->action_id == GSP_KORVO_LIGHTING_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_CLOCK_TIMER:
        return event->action_id == GSP_KORVO_CLOCK_TIMER_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_CALCULATOR:
        return event->action_id == GSP_KORVO_CALCULATOR_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_FOOD:
        return event->action_id == GSP_KORVO_FOOD_ACT_ID_BLUETOOTH_DETAILS;
    default:
        return false;
    }
}

static void board_ui_open_scene(esp_gsp_handle_t ui, app_state_t *state,
                                app_event_t app_event, uint16_t scene_id)
{
    app_state_dispatch(state, app_event);
    esp_gsp_err_t err = esp_gsp_goto_scene(ui, scene_id,
                                           ESP_GSP_FADE_THROUGH_BLACK);
    if (err != ESP_GSP_OK) {
        ESP_LOGE(TAG, "change scene %u failed: %d", (unsigned)scene_id, (int)err);
    }
}

static void board_ui_event(esp_gsp_handle_t ui, const esp_gsp_event_t *event,
                           void *user_ctx)
{
    app_state_t *state = user_ctx;
    if (event == NULL || state == NULL || event->type != ESP_GSP_EVENT_CALL) {
        return;
    }

    if (is_wifi_details_event(event)) {
        board_ui_open_scene(ui, state, APP_EVENT_OPEN_WIFI_SETTINGS,
                            GSP_BUNDLE_SCENE_KORVO_WIFI);
        wifi_scan_async(ui);
        return;
    }

    if (is_bluetooth_details_event(event)) {
        board_ui_open_scene(ui, state, APP_EVENT_OPEN_BLUETOOTH_SETTINGS,
                            GSP_BUNDLE_SCENE_KORVO_BLUETOOTH);
        return;
    }

    if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_WIFI) {
        if (event->action_id == GSP_KORVO_WIFI_ACT_ID_RESCAN) {
            wifi_scan_async(ui);
        } else if (event->action_id == GSP_KORVO_WIFI_ACT_ID_HOME) {
            board_ui_open_scene(ui, state, APP_EVENT_HOME,
                                GSP_BUNDLE_SCENE_KORVO_HOME);
        }
        return;
    }


    if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_BLUETOOTH) {
        if (event->action_id == GSP_KORVO_BLUETOOTH_ACT_ID_HOME) {
            board_ui_open_scene(ui, state, APP_EVENT_HOME,
                                GSP_BUNDLE_SCENE_KORVO_HOME);
        }
        return;
    }

    if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_HOME) {
        switch (event->action_id) {
        case GSP_KORVO_HOME_ACTION_OPEN_SYNTH:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_SYNTH,
                                GSP_BUNDLE_SCENE_KORVO_SYNTH);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_WEATHER:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_WEATHER,
                                GSP_BUNDLE_SCENE_KORVO_WEATHER);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_VOICE:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_VOICE,
                                GSP_BUNDLE_SCENE_KORVO_VOICE);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_OBJECT_RECOGNITION:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_OBJECT_RECOGNITION,
                                GSP_BUNDLE_SCENE_KORVO_OBJECT);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_LIGHTING:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_LIGHTING,
                                GSP_BUNDLE_SCENE_KORVO_LIGHTING);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_CLOCK_TIMER:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_CLOCK_TIMER,
                                GSP_BUNDLE_SCENE_KORVO_CLOCK_TIMER);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_CALCULATOR:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_CALCULATOR,
                                GSP_BUNDLE_SCENE_KORVO_CALCULATOR);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_FOOD:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_FOOD,
                                GSP_BUNDLE_SCENE_KORVO_FOOD);
            break;
        default:
            break;
        }
        return;
    }

    if (event->action_id == GSP_KORVO_SYNTH_ACTION_HOME ||
        event->action_id == GSP_KORVO_WEATHER_ACTION_HOME ||
        event->action_id == GSP_KORVO_VOICE_ACTION_HOME ||
        event->action_id == GSP_KORVO_OBJECT_ACTION_HOME ||
        event->action_id == GSP_KORVO_LIGHTING_ACTION_HOME ||
        event->action_id == GSP_KORVO_CLOCK_TIMER_ACTION_HOME ||
        event->action_id == GSP_KORVO_CALCULATOR_ACTION_HOME ||
        event->action_id == GSP_KORVO_FOOD_ACTION_HOME) {
        board_ui_open_scene(ui, state, APP_EVENT_HOME, GSP_BUNDLE_SCENE_KORVO_HOME);
    }
}

esp_err_t board_ui_start(app_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    bsp_display_config_t display_config = {0};
    ESP_RETURN_ON_ERROR(bsp_display_new(&display_config, &panel, &io), TAG,
                        "create Korvo-1 RGB panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG,
                        "turn on Korvo-1 RGB panel");

    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t touch_error = bsp_touch_new(NULL, &touch);
    if (touch_error != ESP_OK) {
        ESP_LOGW(TAG, "GT1151 touch unavailable: %s", esp_err_to_name(touch_error));
        touch = NULL;
    }

    esp_display_present_target_config_t display = {
        .hw = {
            .panel = panel,
            .panel_type = ESP_DISPLAY_PRESENT_PANEL_RGB,
            .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
        },
        .fb = {
            .mode = ESP_DISPLAY_PRESENT_MODE_AUTO,
        },
    };

    esp_gsp_config_t app_config = gsp_bundle_config();
    esp_gsp_esp_lcd_config_t lcd_config = ESP_GSP_ESP_LCD_CONFIG_INIT();
    lcd_config.display = display;
    lcd_config.touch = touch;
    lcd_config.perf_log = true;

    esp_gsp_handle_t ui = NULL;
    ESP_RETURN_ON_ERROR(esp_gsp_esp_lcd_start(&app_config, &lcd_config, &ui), TAG,
                        "start ESP-GSP bundle");
    ESP_RETURN_ON_ERROR(esp_gsp_on_event(ui, board_ui_event, state), TAG,
                        "register GSP UI events");
    ESP_LOGI(TAG, "Korvo-1 GSP UI started: %dx%d, touch=%s",
             BSP_LCD_H_RES, BSP_LCD_V_RES, touch ? "ready" : "unavailable");
    return ESP_OK;
}
