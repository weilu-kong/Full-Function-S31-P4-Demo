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
#define GSP_BUNDLE_ENABLE_LEGACY_NAMES 1
#include "bundle_gsp.h"

static const char *TAG = "board_ui";
static bool s_wifi_ready;
static bool s_wifi_scan_running;
static volatile uint32_t s_wifi_scan_generation;
static app_state_t *s_wifi_scan_state;
static esp_gsp_handle_t s_wifi_scan_ui;
static esp_gsp_list_t s_wifi_list = ESP_GSP_LIST_NONE;
static esp_gsp_list_t s_bt_list = ESP_GSP_LIST_NONE;

#define MAX_WIFI_APS 10
static wifi_ap_record_t s_wifi_aps[MAX_WIFI_APS];
static uint16_t s_wifi_ap_count;

#define MAX_BT_DEVS 10
static const char *const s_bt_devs[MAX_BT_DEVS] = {
    "   周辺機器",
    "   接続済み　なし",
    "   新しい機器を確認中…",
    "   雷神太鼓",
    "   妖怪通信",
    "   狸屋道具",
    "   河童音具",
    "   言霊マイク",
    "   --",
    "   --",
};

static gsp_err_t wifi_list_bind_cb(esp_gsp_handle_t gsp, esp_gsp_row_t row,
                                   uint32_t index, void *ctx)
{
    (void)ctx;
    if (index < s_wifi_ap_count) {
        char buf[72];
        snprintf(buf, sizeof(buf), "   %s    %d dBm",
                 (const char *)s_wifi_aps[index].ssid, s_wifi_aps[index].rssi);
        (void)esp_gsp_row_text(gsp, row, buf);
    } else {
        (void)esp_gsp_row_text(gsp, row, "   --");
    }
    return GSP_OK;
}

static gsp_err_t bt_list_bind_cb(esp_gsp_handle_t gsp, esp_gsp_row_t row,
                                 uint32_t index, void *ctx)
{
    (void)ctx;
    if (index < MAX_BT_DEVS) {
        (void)esp_gsp_row_text(gsp, row, s_bt_devs[index]);
    } else {
        (void)esp_gsp_row_text(gsp, row, "   --");
    }
    return GSP_OK;
}

static void wifi_set_status(esp_gsp_handle_t ui, const char *status)
{
    (void)esp_gsp_set_text(ui, GSP_KORVO_WIFI_BIND_WIFI_SCAN_STATUS, status);
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
    ESP_LOGI(TAG, "Wi-Fi STA started successfully");
    return ESP_OK;
}

static void wifi_scan_task(void *arg)
{
    const uint32_t generation = (uint32_t)(uintptr_t)arg;
    esp_gsp_handle_t ui = s_wifi_scan_ui;
    app_state_t *state = s_wifi_scan_state;

    vTaskDelay(pdMS_TO_TICKS(200));
    if (generation != s_wifi_scan_generation || state->screen != APP_SCREEN_WIFI_SETTINGS) {
        s_wifi_scan_running = false;
        vTaskDelete(NULL);
        return;
    }

    if (s_wifi_ap_count == 0) {
        wifi_set_status(ui, "Wi-Fi 確認中…");
    }

    esp_err_t err = wifi_start_once();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_start_once failed: %s", esp_err_to_name(err));
        if (generation == s_wifi_scan_generation && state->screen == APP_SCREEN_WIFI_SETTINGS) {
            wifi_set_status(ui, "Wi-Fi ERROR");
        }
        s_wifi_scan_running = false;
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    uint16_t ap_count = 0;
    int retries = 3;
    wifi_scan_config_t scan_cfg = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 150,
    };
    while (retries > 0) {
        if (generation != s_wifi_scan_generation || state->screen != APP_SCREEN_WIFI_SETTINGS) {
            s_wifi_scan_running = false;
            vTaskDelete(NULL);
            return;
        }
        err = esp_wifi_scan_start(&scan_cfg, true);
        if (err == ESP_OK) {
            esp_wifi_scan_get_ap_num(&ap_count);
            if (ap_count > 0 || retries == 1) {
                break;
            }
        }
        ESP_LOGW(TAG, "Wi-Fi scan retry (err=%s, ap_count=%u)", esp_err_to_name(err), (unsigned)ap_count);
        vTaskDelay(pdMS_TO_TICKS(200));
        retries--;
    }

    if (generation != s_wifi_scan_generation || state->screen != APP_SCREEN_WIFI_SETTINGS) {
        s_wifi_scan_running = false;
        vTaskDelete(NULL);
        return;
    }

    if (err == ESP_OK) {
        uint16_t fetch_count = ap_count > MAX_WIFI_APS ? MAX_WIFI_APS : ap_count;
        if (fetch_count > 0) {
            memset(s_wifi_aps, 0, sizeof(s_wifi_aps));
            esp_wifi_scan_get_ap_records(&fetch_count, s_wifi_aps);
        }
        s_wifi_ap_count = fetch_count;

        char status[48];
        snprintf(status, sizeof(status), "Wi-Fi %u", (unsigned)fetch_count);
        wifi_set_status(ui, status);

        if (s_wifi_list == ESP_GSP_LIST_NONE) {
            s_wifi_list = esp_gsp_list_bind_component(
                ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_LIST, wifi_list_bind_cb, NULL);
            ESP_LOGI(TAG, "Bound s_wifi_list in scan task: %u", (unsigned)s_wifi_list);
        }
        if (s_wifi_list != ESP_GSP_LIST_NONE) {
            (void)esp_gsp_list_set_total(ui, s_wifi_list, MAX_WIFI_APS);
            (void)esp_gsp_list_refresh(ui, s_wifi_list);
        }
    } else {
        ESP_LOGE(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
        wifi_set_status(ui, "Wi-Fi ERROR");
    }

    s_wifi_scan_running = false;
    vTaskDelete(NULL);
}

static void wifi_bg_warmup_task(void *arg)
{
    esp_gsp_handle_t ui = (esp_gsp_handle_t)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));
    if (s_wifi_scan_running) {
        vTaskDelete(NULL);
        return;
    }
    s_wifi_scan_running = true;
    ESP_LOGI(TAG, "Wi-Fi background warm-up started");
    esp_err_t err = wifi_start_once();
    if (err == ESP_OK) {
        wifi_scan_config_t scan_cfg = {
            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
            .scan_time.active.min = 100,
            .scan_time.active.max = 150,
        };
        err = esp_wifi_scan_start(&scan_cfg, true);
        if (err == ESP_OK) {
            uint16_t ap_count = 0;
            esp_wifi_scan_get_ap_num(&ap_count);
            uint16_t fetch = ap_count > MAX_WIFI_APS ? MAX_WIFI_APS : ap_count;
            if (fetch > 0) {
                memset(s_wifi_aps, 0, sizeof(s_wifi_aps));
                esp_wifi_scan_get_ap_records(&fetch, s_wifi_aps);
            }
            s_wifi_ap_count = fetch;
            ESP_LOGI(TAG, "Wi-Fi background warm-up finished: found %u APs", (unsigned)fetch);
            if (s_wifi_list != ESP_GSP_LIST_NONE) {
                char status[48];
                snprintf(status, sizeof(status), "Wi-Fi %u", (unsigned)fetch);
                wifi_set_status(ui, status);
                (void)esp_gsp_list_refresh(ui, s_wifi_list);
            }
        } else {
            ESP_LOGW(TAG, "Wi-Fi background scan returned %s", esp_err_to_name(err));
        }
    }
    s_wifi_scan_running = false;
    vTaskDelete(NULL);
}

static void wifi_scan_async(esp_gsp_handle_t ui, app_state_t *state)
{
    if (s_wifi_scan_running) {
        return;
    }
    s_wifi_scan_running = true;
    s_wifi_scan_ui = ui;
    s_wifi_scan_state = state;
    uint32_t generation = ++s_wifi_scan_generation;
    if (xTaskCreate(wifi_scan_task, "wifi_scan", 6144,
                    (void *)(uintptr_t)generation, 5, NULL) != pdPASS) {
        s_wifi_scan_running = false;
        wifi_set_status(ui, "Wi-Fi ERROR");
    }
}

static bool s_wifi_enabled = true;
static bool s_bluetooth_enabled = false;

static bool is_wifi_details_event(const esp_gsp_event_t *event)
{
    if (!s_wifi_enabled) {
        return false;
    }
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
    if (!s_bluetooth_enabled) {
        return false;
    }
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

static bool is_wifi_toggle_event(const esp_gsp_event_t *event)
{
    switch (event->scene_id) {
    case GSP_BUNDLE_SCENE_KORVO_HOME:
        return event->action_id == GSP_KORVO_HOME_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_SYNTH:
        return event->action_id == GSP_KORVO_SYNTH_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_WEATHER:
        return event->action_id == GSP_KORVO_WEATHER_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_VOICE:
        return event->action_id == GSP_KORVO_VOICE_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_OBJECT:
        return event->action_id == GSP_KORVO_OBJECT_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_LIGHTING:
        return event->action_id == GSP_KORVO_LIGHTING_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_CLOCK_TIMER:
        return event->action_id == GSP_KORVO_CLOCK_TIMER_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_CALCULATOR:
        return event->action_id == GSP_KORVO_CALCULATOR_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_FOOD:
        return event->action_id == GSP_KORVO_FOOD_ACT_ID_WIFI_TOGGLE;
    default:
        return false;
    }
}

static bool is_bluetooth_toggle_event(const esp_gsp_event_t *event)
{
    switch (event->scene_id) {
    case GSP_BUNDLE_SCENE_KORVO_HOME:
        return event->action_id == GSP_KORVO_HOME_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_SYNTH:
        return event->action_id == GSP_KORVO_SYNTH_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_WEATHER:
        return event->action_id == GSP_KORVO_WEATHER_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_VOICE:
        return event->action_id == GSP_KORVO_VOICE_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_OBJECT:
        return event->action_id == GSP_KORVO_OBJECT_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_LIGHTING:
        return event->action_id == GSP_KORVO_LIGHTING_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_CLOCK_TIMER:
        return event->action_id == GSP_KORVO_CLOCK_TIMER_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_CALCULATOR:
        return event->action_id == GSP_KORVO_CALCULATOR_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_FOOD:
        return event->action_id == GSP_KORVO_FOOD_ACT_ID_BLUETOOTH_TOGGLE;
    default:
        return false;
    }
}

static void update_drawer_quick_controls(esp_gsp_handle_t ui)
{
    (void)esp_gsp_component_set_enabled(ui, GSP_KORVO_HOME_OBJ_KEY_WIFI_CARD, s_wifi_enabled);
    (void)esp_gsp_component_set_text(ui, GSP_KORVO_HOME_OBJ_KEY_WIFI_ENABLED,
                                    s_wifi_enabled ? "ON" : "OFF");
    (void)esp_gsp_component_set_text(ui, GSP_KORVO_HOME_OBJ_KEY_WIFI_CARD,
                                    s_wifi_enabled ? "Wi-Fi　通信" : "Wi-Fi (OFF)");
    (void)esp_gsp_component_set_enabled(ui, GSP_KORVO_HOME_OBJ_KEY_BLUETOOTH_CARD, s_bluetooth_enabled);
    (void)esp_gsp_component_set_text(ui, GSP_KORVO_HOME_OBJ_KEY_BLUETOOTH_ENABLED,
                                    s_bluetooth_enabled ? "ON" : "OFF");
    (void)esp_gsp_component_set_text(ui, GSP_KORVO_HOME_OBJ_KEY_BLUETOOTH_CARD,
                                    s_bluetooth_enabled ? "Bluetooth" : "Bluetooth (OFF)");
}

static void board_ui_open_scene(esp_gsp_handle_t ui, app_state_t *state,
                                app_event_t app_event, uint16_t scene_id)
{
    if (scene_id != GSP_BUNDLE_SCENE_KORVO_WIFI) {
        ++s_wifi_scan_generation;
    }
    app_state_dispatch(state, app_event);
    esp_gsp_err_t err = esp_gsp_goto_scene(ui, scene_id,
                                           ESP_GSP_FADE_THROUGH_BLACK);
    if (err != ESP_GSP_OK) {
        ESP_LOGE(TAG, "change scene %u failed: %d", (unsigned)scene_id, (int)err);
    }
}

static void board_ui_event(esp_gsp_handle_t ui, const esp_gsp_event_t *event,
                           void *ctx)
{
    app_state_t *state = (app_state_t *)ctx;
    if (event == NULL || state == NULL) {
        return;
    }
    ESP_LOGI(TAG, "board_ui_event: scene=%d, action=%d, type=%d",
             event->scene_id, event->action_id, event->type);

    if (event->type == ESP_GSP_EVENT_SCENE_CHANGED) {
        ESP_LOGI(TAG, "Scene changed settled to %d", event->scene_id);
        if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_WIFI) {
            if (s_wifi_list == ESP_GSP_LIST_NONE) {
                s_wifi_list = esp_gsp_list_bind_component(
                    ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_LIST, wifi_list_bind_cb, NULL);
                ESP_LOGI(TAG, "Bound s_wifi_list in SCENE_CHANGED: %u", (unsigned)s_wifi_list);
            }
            if (s_wifi_list != ESP_GSP_LIST_NONE) {
                (void)esp_gsp_list_set_total(ui, s_wifi_list, MAX_WIFI_APS);
                (void)esp_gsp_list_refresh(ui, s_wifi_list);
            }
            if (s_wifi_ap_count > 0) {
                char status[48];
                snprintf(status, sizeof(status), "Wi-Fi %u", (unsigned)s_wifi_ap_count);
                wifi_set_status(ui, status);
            }
        } else if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_BLUETOOTH) {
            if (s_bt_list == ESP_GSP_LIST_NONE) {
                s_bt_list = esp_gsp_list_bind_component(
                    ui, GSP_KORVO_BLUETOOTH_OBJ_KEY_BLUETOOTH_LIST, bt_list_bind_cb, NULL);
                ESP_LOGI(TAG, "Bound s_bt_list in SCENE_CHANGED: %u", (unsigned)s_bt_list);
            }
            if (s_bt_list != ESP_GSP_LIST_NONE) {
                (void)esp_gsp_list_set_total(ui, s_bt_list, MAX_BT_DEVS);
                (void)esp_gsp_list_refresh(ui, s_bt_list);
            }
        } else {
            update_drawer_quick_controls(ui);
        }
        return;
    }

    if (event->type != ESP_GSP_EVENT_CALL) {
        return;
    }

    if (is_wifi_toggle_event(event)) {
        s_wifi_enabled = !s_wifi_enabled;
        ESP_LOGI(TAG, "Wi-Fi toggled: %s", s_wifi_enabled ? "ON" : "OFF");
        update_drawer_quick_controls(ui);
        return;
    }

    if (is_bluetooth_toggle_event(event)) {
        s_bluetooth_enabled = !s_bluetooth_enabled;
        ESP_LOGI(TAG, "Bluetooth toggled: %s", s_bluetooth_enabled ? "ON" : "OFF");
        update_drawer_quick_controls(ui);
        return;
    }

    if (is_wifi_details_event(event)) {
        board_ui_open_scene(ui, state, APP_EVENT_OPEN_WIFI_SETTINGS,
                            GSP_BUNDLE_SCENE_KORVO_WIFI);
        wifi_scan_async(ui, state);
        return;
    }

    if (is_bluetooth_details_event(event)) {
        board_ui_open_scene(ui, state, APP_EVENT_OPEN_BLUETOOTH_SETTINGS,
                            GSP_BUNDLE_SCENE_KORVO_BLUETOOTH);
        return;
    }

    if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_WIFI) {
        if (event->action_id == GSP_KORVO_WIFI_ACT_ID_RESCAN) {
            wifi_scan_async(ui, state);
        } else if (event->action_id == GSP_KORVO_WIFI_ACT_ID_HOME) {
            board_ui_open_scene(ui, state, APP_EVENT_HOME,
                                GSP_BUNDLE_SCENE_KORVO_HOME);
        }
        return;
    }

    if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_BLUETOOTH) {
        if (event->action_id == GSP_KORVO_BLUETOOTH_ACT_ID_REFRESH) {
            if (s_bt_list != ESP_GSP_LIST_NONE) {
                (void)esp_gsp_list_refresh(ui, s_bt_list);
            }
        } else if (event->action_id == GSP_KORVO_BLUETOOTH_ACT_ID_HOME) {
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

    if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_SYNTH) {
        if (event->action_id == GSP_KORVO_SYNTH_ACTION_HOME) {
            board_ui_open_scene(ui, state, APP_EVENT_HOME, GSP_BUNDLE_SCENE_KORVO_HOME);
        } else {
            ESP_LOGI(TAG, "Synth action: %d", (int)event->action_id);
        }
        return;
    }

    if (event->action_id == GSP_KORVO_WEATHER_ACTION_HOME ||
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

    xTaskCreate(wifi_bg_warmup_task, "wifi_warmup", 6144, ui, 2, NULL);

    ESP_LOGI(TAG, "Korvo-1 GSP UI started: %dx%d, touch=%s",
             BSP_LCD_H_RES, BSP_LCD_V_RES, touch ? "ready" : "unavailable");
    return ESP_OK;
}
